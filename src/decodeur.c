/******************************************************************************
 * Laboratoire 3 - decodeur.c (Version Optimisée Pi Zero)
 ******************************************************************************/

#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"

#include "jpgd.h"
#include <time.h>

#define HEADER_SIZE 4
const char header[] = "SETR";

int main(int argc, char* argv[])
{
    // --- CORRECTIF 1 : Verrouiller la mémoire du processus ---
    // Cela empêche le code et le heap d'être envoyés dans le swap,
    // garantissant que malloc() est rapide.
    if(mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("[decodeur] Attention: mlockall a echoué (lancer avec sudo?)");
    }

    // Initialise le profilage
    char signatureProfilage[128] = {0};
    char* nomProgramme = (argv[0][0] == '.') ? argv[0]+2 : argv[0];
    snprintf(signatureProfilage, 128, "profilage-%s-%u.txt", nomProgramme, (unsigned int)getpid());
    InfosProfilage profInfos;
    initProfilage(&profInfos, signatureProfilage);
    
    evenementProfilage(&profInfos, ETAT_INITIALISATION);
    
    if(argc < 3)
    {
        printf("Usage: %s [options] fichier_entree flux_sortie\n", argv[0]);
        return -1;
    }

    const char* fichier_ulv = NULL;
    const char* mem_sortie = NULL;
    int argidx = 1;

    if(strcmp(argv[argidx], "--debug") == 0)
    {
        printf("Mode debug selectionne pour le decodeur\n");
        fichier_ulv = "240p/02_Sintel.ulv";
        mem_sortie = "/mem1";
    } 
    else
    {
        while(argidx < argc-2 && argv[argidx][0] == '-') argidx++;
        fichier_ulv = argv[argidx];
        mem_sortie = argv[argidx+1];
    }

    int fd = open(fichier_ulv, O_RDONLY);
    if(fd < 0) { perror("open ULV"); return -1; }

    off_t taille_fichier = lseek(fd, 0, SEEK_END);
    if(taille_fichier < 24) { fprintf(stderr, "Fichier ULV trop petit\n"); close(fd); return -1; }

    // --- CORRECTIF 2 : MAP_SHARED au lieu de MAP_PRIVATE ---
    // MAP_SHARED permet au kernel de libérer les pages de la vidéo immédiatement
    // s'il a besoin de RAM pour le malloc(), car il sait qu'elles sont sur le disque.
    // On ne met PAS MAP_POPULATE ici pour éviter de saturer la RAM au démarrage.
    void* fichier_map = mmap(NULL, taille_fichier, PROT_READ, MAP_SHARED, fd, 0);
    
    if(fichier_map == MAP_FAILED) { perror("mmap ULV"); close(fd); return -1; }
    
    // --- CORRECTIF 3 : Optimisation de lecture ---
    // On dit au kernel qu'on va lire séquentiellement. Il va précharger les petits bouts
    // nécessaires (read-ahead) sans saturer la RAM.
    madvise(fichier_map, taille_fichier, MADV_SEQUENTIAL);
    madvise(fichier_map, taille_fichier, MADV_WILLNEED);

    close(fd);
    unsigned char* ptr = (unsigned char*)fichier_map;

    if(memcmp(ptr, header, 4) != 0) {
        fprintf(stderr, "Fichier ULV: header invalide\n");
        munmap(fichier_map, taille_fichier);
        return -1;
    }

    struct videoInfos infos;
    memcpy(&infos.largeur, ptr+4, 4);
    memcpy(&infos.hauteur, ptr+8, 4);
    memcpy(&infos.canaux,  ptr+12, 4);
    memcpy(&infos.fps,     ptr+16, 4);
    size_t taille_image = infos.largeur * infos.hauteur * infos.canaux;

    if(prepareMemoire(1, 1) != 0)
    {
        fprintf(stderr, "Erreur prepareMemoire\n");
        munmap(fichier_map, taille_fichier);
        return -1;
    }

    struct memPartage zone = {0};
    if(initMemoirePartageeEcrivain(mem_sortie, &zone, &infos) != 0) {
        fprintf(stderr, "Erreur initMemoirePartageeEcrivain\n");
        munmap(fichier_map, taille_fichier);
        return -1;
    }

    size_t offset = 20; 
    int frame_count = 0;

    while(1)
    {
        if(offset+4 > (size_t)taille_fichier) offset = 20;
        
        uint32_t taille_jpeg = 0;
        memcpy(&taille_jpeg, ptr+offset, 4);
        
        if(taille_jpeg == 0) { offset = 20; continue; }
        
        if(offset+4+taille_jpeg > (size_t)taille_fichier) {
            offset = 20;
            continue;
        }

        evenementProfilage(&profInfos, ETAT_TRAITEMENT);
        
        int w = infos.largeur;
        int h = infos.hauteur;
        int comps = 0;
        
        // Tentative de décompression
        unsigned char* img = jpgd::decompress_jpeg_image_from_memory(ptr+offset+4, taille_jpeg, &w, &h, &comps, infos.canaux);
        
        if(!img)
        {
            // Si on est ici, c'est que la RAM est vraiment pleine.
            // Petite pause pour laisser le système respirer et espérer que kswapd libère de la place.
            usleep(1000); 
            fprintf(stderr, "[decodeur] OOM Frame %d. Saut.\n", frame_count);
            offset += 4 + taille_jpeg;
            continue;
        }

        // Sécurité dimensions
        size_t real_size = w * h * comps;
        if (real_size < taille_image) {
             free(img);
             offset += 4 + taille_jpeg;
             continue;
        }

        evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
        attenteEcrivain(&zone);

        memcpy(zone.data, img, taille_image);
        
        signalEcrivain(&zone);

        free(img);

        evenementProfilage(&profInfos, ETAT_ENPAUSE);

        offset += 4 + taille_jpeg;
        frame_count++;
        
        // Très important : laisse la main au kernel pour gérer la mémoire
        sched_yield();
    }

    munmap(fichier_map, taille_fichier);
    return 0;
}