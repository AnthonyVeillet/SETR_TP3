#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <getopt.h>
#include <limits.h>

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"
#include "jpgd.h"

#define ULV_MAGIC_SIZE 4
static const unsigned char ULV_MAGIC[ULV_MAGIC_SIZE] = {'S','E','T','R'};

static int read_u32(const unsigned char* p, uint32_t* out)
{
    uint32_t v;
    memcpy(&v, p, 4);
    *out = v;
    return 0;
}

int main(int argc, char* argv[])
{
    setbuf(stdout, NULL);

    // Profilage
    char signatureProfilage[128] = {0};
    char* nomProgramme = (argv[0][0] == '.') ? argv[0] + 2 : argv[0];
    snprintf(signatureProfilage, sizeof(signatureProfilage),
             "profilage-%s-%u.txt", nomProgramme, (unsigned int)getpid());
    InfosProfilage profInfos;
    initProfilage(&profInfos, signatureProfilage);
    evenementProfilage(&profInfos, ETAT_INITIALISATION);

    const char* fichier_ulv = NULL;
    const char* mem_sortie = NULL;
    struct SchedParams schedParams = {0};

    if (argc >= 2 && strcmp(argv[1], "--debug") == 0) {
        printf("[decodeur] Mode debug\n");
        fichier_ulv = "240p/02_Sintel.ulv";
        mem_sortie  = "/mem1";
        schedParams.modeOrdonnanceur = ORDONNANCEMENT_NORT;
    } else {
        int c;
        opterr = 0;
        while ((c = getopt(argc, argv, "s:d:")) != -1) {
            switch (c) {
                case 's': parseSchedOption(optarg, &schedParams); break;
                case 'd': parseDeadlineParams(optarg, &schedParams); break;
                default: break;
            }
        }

        if (argc - optind < 2) {
            fprintf(stderr, "Usage: %s [options] fichier_entree flux_sortie\n", argv[0]);
            return -1;
        }
        fichier_ulv = argv[optind];
        mem_sortie  = argv[optind + 1];
    }

    printf("[decodeur] init: entree=%s sortie=%s sched=%d\n",
           fichier_ulv, mem_sortie, schedParams.modeOrdonnanceur);

    appliquerOrdonnancement(&schedParams, "decodeur");

    // --- Ouvrir + mmap (MAP_POPULATE recommandé, avec fallback si ENOMEM) ---
    int fd = open(fichier_ulv, O_RDONLY);
    if (fd < 0) { perror("[decodeur] open"); return -1; }

    off_t taille_fichier = lseek(fd, 0, SEEK_END);
    if (taille_fichier < 20) {
        fprintf(stderr, "[decodeur] ULV trop petit\n");
        close(fd);
        return -1;
    }

    void* fichier_map = mmap(NULL, (size_t)taille_fichier, PROT_READ,
                             MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (fichier_map == MAP_FAILED && errno == ENOMEM) {
        // fallback (si jamais le fichier est trop gros / pression mémoire)
        fichier_map = mmap(NULL, (size_t)taille_fichier, PROT_READ,
                           MAP_PRIVATE, fd, 0);
    }

    if (fichier_map == MAP_FAILED) {
        perror("[decodeur] mmap");
        close(fd);
        return -1;
    }

    close(fd);

    madvise(fichier_map, (size_t)taille_fichier, MADV_SEQUENTIAL);

    unsigned char* ptr = (unsigned char*)fichier_map;

    // --- Vérifier header ULV ---
    if (memcmp(ptr, ULV_MAGIC, ULV_MAGIC_SIZE) != 0) {
        fprintf(stderr, "[decodeur] ULV: magic invalide\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }

    // --- Lire infos vidéo (offsets ULV) ---
    struct videoInfos infos;
    memcpy(&infos.largeur, ptr + 4,  4);
    memcpy(&infos.hauteur, ptr + 8,  4);
    memcpy(&infos.canaux,  ptr + 12, 4);
    memcpy(&infos.fps,     ptr + 16, 4);

    if (infos.largeur == 0 || infos.hauteur == 0 || infos.canaux == 0) {
        fprintf(stderr, "[decodeur] ULV: infos invalides\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }

    // Taille image brute attendue
    if ((size_t)infos.largeur > SIZE_MAX / (size_t)infos.hauteur) {
        fprintf(stderr, "[decodeur] overflow taille image\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }
    size_t pixels = (size_t)infos.largeur * (size_t)infos.hauteur;

    if (pixels > SIZE_MAX / (size_t)infos.canaux) {
        fprintf(stderr, "[decodeur] overflow taille image\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }
    size_t taille_image = pixels * (size_t)infos.canaux;

    // Préparer pool mémoire (pour buffer décodé)
    if (prepareMemoire(taille_image, taille_image) != 0) {
        fprintf(stderr, "[decodeur] Erreur prepareMemoire\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }

    unsigned char* decoded_buf = (unsigned char*)tempsreel_malloc(taille_image);
    if (!decoded_buf) {
        fprintf(stderr, "[decodeur] tempsreel_malloc(taille_image) a échoué\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }

    // Init mémoire partagée (writer)
    struct memPartage zone = {0};
    if (initMemoirePartageeEcrivain(mem_sortie, &zone, &infos) != 0) {
        fprintf(stderr, "[decodeur] Erreur initMemoirePartageeEcrivain\n");
        munmap(fichier_map, (size_t)taille_fichier);
        return -1;
    }

    const size_t base_offset = 20;
    size_t offset = base_offset;
    int frame_count = 0;

    // Sanity cap : un JPEG 240p ne devrait JAMAIS être énorme.
    // On met large pour rester safe sur d'autres tailles.
    const uint32_t MAX_JPEG_SIZE = (uint32_t)((taille_image < (size_t)UINT32_MAX / 4) ? (taille_image * 4) : UINT32_MAX);

    while (1) {
        if (offset + 4 > (size_t)taille_fichier) offset = base_offset;

        uint32_t taille_jpeg = 0;
        read_u32(ptr + offset, &taille_jpeg);

        if (taille_jpeg == 0) { // fin de fichier -> boucle
            offset = base_offset;
            continue;
        }

        // Sanity check pour éviter de partir dans le champ (taille corrompue)
        if (taille_jpeg > MAX_JPEG_SIZE || taille_jpeg > (uint32_t)INT_MAX) {
            fprintf(stderr, "[decodeur] Taille JPEG bizarre (%u) frame=%d -> restart\n",
                    (unsigned)taille_jpeg, frame_count);
            offset = base_offset;
            continue;
        }

        if (offset + 4 + (size_t)taille_jpeg > (size_t)taille_fichier) {
            offset = base_offset;
            continue;
        }

        // Attendre la zone de sortie dispo (mutex LOCKÉ à la sortie)
        evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
        if (attenteEcrivain(&zone) < 0) {
            fprintf(stderr, "[decodeur] attenteEcrivain failed\n");
            break;
        }

        // On libère le mutex pendant le décodage
        pthread_mutex_unlock(&zone.header->mutex);

        evenementProfilage(&profInfos, ETAT_TRAITEMENT);

        // --- Décodage JPEG en scanlines (PAS de grosse alloc par frame) ---
        jpgd::jpeg_decoder_mem_stream stream(ptr + offset + 4, (jpgd::uint)taille_jpeg);
        jpgd::jpeg_decoder decoder(&stream);

        if (decoder.get_error_code() != jpgd::JPGD_SUCCESS) {
            fprintf(stderr, "[decodeur] JPEG decoder ctor error=%d frame=%d -> skip\n",
                    (int)decoder.get_error_code(), frame_count);
            offset += 4 + (size_t)taille_jpeg;
            frame_count++;
            evenementProfilage(&profInfos, ETAT_ENPAUSE);
            usleep(1000);
            continue;
        }

        int st = decoder.begin_decoding();
        if (st != jpgd::JPGD_SUCCESS) {
            fprintf(stderr, "[decodeur] begin_decoding error=%d frame=%d -> skip\n",
                    st, frame_count);
            offset += 4 + (size_t)taille_jpeg;
            frame_count++;
            evenementProfilage(&profInfos, ETAT_ENPAUSE);
            usleep(1000);
            continue;
        }

        const int w = decoder.get_width();
        const int h = decoder.get_height();
        const int bpp = decoder.get_bytes_per_pixel(); // 1 (Y) ou 4 (RGBA)

        if (w != (int)infos.largeur || h != (int)infos.hauteur) {
            fprintf(stderr, "[decodeur] Dimensions JPEG (%d,%d) != ULV (%u,%u) frame=%d -> restart\n",
                    w, h, infos.largeur, infos.hauteur, frame_count);
            offset = base_offset;
            continue;
        }

        if (infos.canaux == 3 && bpp != 4) {
            fprintf(stderr, "[decodeur] bpp inattendu (%d) pour canaux=3 frame=%d -> skip\n", bpp, frame_count);
            offset += 4 + (size_t)taille_jpeg;
            frame_count++;
            evenementProfilage(&profInfos, ETAT_ENPAUSE);
            usleep(1000);
            continue;
        }
        if (infos.canaux == 1 && bpp != 1) {
            fprintf(stderr, "[decodeur] bpp inattendu (%d) pour canaux=1 frame=%d -> skip\n", bpp, frame_count);
            offset += 4 + (size_t)taille_jpeg;
            frame_count++;
            evenementProfilage(&profInfos, ETAT_ENPAUSE);
            usleep(1000);
            continue;
        }

        // Remplir decoded_buf (RGB ou GRAY) ligne par ligne
        for (int y = 0; y < h; y++) {
            const void* pScan = NULL;
            jpgd::uint scan_len = 0;

            int r = decoder.decode(&pScan, &scan_len);
            if (r == jpgd::JPGD_DONE) {
                // terminé plus tôt que prévu (bizarre, mais on sort)
                break;
            }
            if (r != jpgd::JPGD_SUCCESS || !pScan) {
                fprintf(stderr, "[decodeur] decode scanline fail r=%d err=%d frame=%d -> skip\n",
                        r, (int)decoder.get_error_code(), frame_count);
                // skip frame
                goto skip_frame;
            }

            if (infos.canaux == 1) {
                // scan_len devrait être w*1
                memcpy(decoded_buf + (size_t)y * (size_t)w, pScan, (size_t)w);
            } else {
                // bpp == 4 (RGBA), on drop A -> RGB
                const unsigned char* src = (const unsigned char*)pScan;
                unsigned char* dst = decoded_buf + ((size_t)y * (size_t)w * 3);

                for (int x = 0; x < w; x++) {
                    dst[x*3 + 0] = src[x*4 + 0]; // R
                    dst[x*3 + 1] = src[x*4 + 1]; // G
                    dst[x*3 + 2] = src[x*4 + 2]; // B
                }
            }
        }

        // Écrire en mémoire partagée (mutex court)
        pthread_mutex_lock(&zone.header->mutex);
        memcpy(zone.data, decoded_buf, taille_image);
        signalEcrivain(&zone); // unlock + signal lecteur

        offset += 4 + (size_t)taille_jpeg;
        frame_count++;

        evenementProfilage(&profInfos, ETAT_ENPAUSE);
        //sched_yield();
        continue;

skip_frame:
        // On n’a rien écrit -> on NE signale PAS le lecteur.
        // Important: on attend juste un peu et on continue.
        offset += 4 + (size_t)taille_jpeg;
        frame_count++;
        evenementProfilage(&profInfos, ETAT_ENPAUSE);
        usleep(1000);
        continue;
    }

    munmap(fichier_map, (size_t)taille_fichier);
    return 0;
}