/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier implémentant le programme de filtrage des images
 ******************************************************************************/

// Gestion des ressources et permissions
#include <sys/resource.h>

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/mman.h>


int main(int argc, char* argv[]){
    // On desactive le buffering pour les printf(), pour qu'il soit possible de les voir depuis votre ordinateur
    setbuf(stdout, NULL);
    
    // Initialise le profilage
    char signatureProfilage[128] = {0};
    char* nomProgramme = (argv[0][0] == '.') ? argv[0]+2 : argv[0];
    snprintf(signatureProfilage, 128, "profilage-%s-%u.txt", nomProgramme, (unsigned int)getpid());
    InfosProfilage profInfos;
    initProfilage(&profInfos, signatureProfilage);
    
    // Premier evenement de profilage : l'initialisation du programme
    evenementProfilage(&profInfos, ETAT_INITIALISATION);


    //=================================
    // CODE FAIT PAR ANTHONY VEILLET
    //=================================

    // Code lisant les options sur la ligne de commande
    char *entree, *sortie;                          // Zones memoires d'entree et de sortie
    struct SchedParams schedParams = {0};           // Paramètres de l'ordonnanceur
    unsigned int runtime, deadline, period;         // Dans le cas de l'ordonnanceur DEADLINE
    int f = 2; // Options en entrée

    if(argc < 2){
        printf("Nombre d'arguments insuffisant\n");
        return -1;
    }

    if(strcmp(argv[1], "--debug") == 0){
        // Mode debug, vous pouvez changer ces valeurs pour ce qui convient dans vos tests
        printf("Mode debug selectionne pour le filtreur\n");
        entree = (char*)"/mem1";
        sortie = (char*)"/mem2";
        runtime = 10;
        deadline = 20;
        period = 25;
        f = 0;
        (void)runtime; (void)deadline; (void)period; // Pour retirer les warning qui dise qu'ils ne sont pas utilisés

    }
    else{
        int c;
        opterr = 0;

        while ((c = getopt (argc, argv, "s:d:f:")) != -1){
            switch (c) {
                case 's':
                    parseSchedOption(optarg, &schedParams);
                    break;
                case 'd':
                    parseDeadlineParams(optarg, &schedParams);
                    break;
                case 'f':
                    f = atoi(optarg);
                    break;
                default:
                    continue;
            }
        }

        // Ce qui suit est la description des zones memoires d'entree et de sortie
        if(argc - optind < 2){
            printf("Arguments manquants (fichier_entree flux_sortie)\n");
            return -1;
        }

        if ((f != 0) && (f != 1)) {
            printf("La méthode de filtrage doit être 0 (Low-pass) ou 1 (High-pass)\n");
            return -1;
        }

        entree = argv[optind];
        sortie = argv[optind+1];
    }

    printf("Initialisation redimensionneur filtre=%d, entree=%s, sortie=%s, mode d'ordonnancement=%i\n",
        f, entree, sortie, schedParams.modeOrdonnanceur);
    
    // Changement de mode d'ordonnancement
    if (appliquerOrdonnancement(&schedParams, "filtreur") != 0) {
        printf("Erreur appliquerOrdonnancement\n");
        return -1;
    }

    // Init des mémoires
    struct memPartage inZone = {0};
    struct memPartage outZone = {0};

    if (initMemoirePartageeLecteur(entree, &inZone) != 0)
        {
            printf("Erreur d'initialisation memoire partagee lecteur\n");
            return -1;
        }
    
    // Même si tempInfos n'est pas utile ici, car contrairement à redimensionneur et convertisseurgris
    // la taille entré et sortie est parreil (même nombre de pixels et de canaux, seulement un filtre appliqué),
    // on le garde parreil pour avoir la même structure que les autres, et ainsi pouvoir réutiliser leur code rapidement
    struct videoInfos tempInfos = {0};
    tempInfos.canaux = inZone.header->infos.canaux;
    tempInfos.largeur = inZone.header->infos.largeur;
    tempInfos.hauteur = inZone.header->infos.hauteur;
    tempInfos.fps = inZone.header->infos.fps;

    if (initMemoirePartageeEcrivain(sortie, &outZone, &tempInfos) != 0)
    {
        printf("Erreur d'initialisation memoire partagee ecrivain\n");
        return -1;
    }

    uint32_t largeurVideo = inZone.header->infos.largeur;
    uint32_t hauteurVideo = inZone.header->infos.hauteur;
    uint32_t canauxVideo = inZone.header->infos.canaux;

    // Être certain que faire prepareMemoire(taille, taille) ne soit TROP PETIT à cause des fonctions filtreur
    size_t tailleImg  = (size_t)largeurVideo * (size_t)hauteurVideo * (size_t)canauxVideo;
    size_t tailleFloat = (size_t)largeurVideo * (size_t)hauteurVideo * (size_t)canauxVideo * sizeof(float);

    // Le plus gros "single malloc" potentiel vient des buffers float
    size_t tailleGros = (tailleFloat > tailleImg) ? tailleFloat : tailleImg;

    if (prepareMemoire(tailleGros, tailleGros) != 0)
    {
        printf("Erreur d'allocation memoire\n");
        return -1;
    }
    
    // Permet de retirer la limite de mémoire pouvant être verrouillée
    if (setrlimit(RLIMIT_MEMLOCK, &(struct rlimit){.rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY}) == -1)
    {
        printf("Erreur lors de la suppression de la limite de verrouillage memoire\n");
        return -1;
    }
    
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
    {
        printf("Erreur lors du verrouillage memoire\n");
        return -1;
    }

    // Comme que les fonction de filtrage lisent et écrivent des données dans les zones mémoire partagées,
    // on créer une copie locale de ces données pour éviter de bloquer le mutex pendant toute
    // la durée du traitement (ce qui serait le cas si on passait directement les pointeurs de
    // la mémoire partagée).
    unsigned char* tempIn = tempsreel_malloc(tailleImg);
    unsigned char* tempOut = tempsreel_malloc(tailleImg);
    if (tempOut == NULL)
    {
        printf("Erreur d'allocation memoire pour les buffers temporaires\n");
        return -1;
    }


    // Code permettant de filtrer une image (en utilisant les fonctions précodées
    // dans utils.c). Le code lit une image depuis une zone mémoire partagée et
    // envoyer le résultat sur une autre zone mémoire partagée.
    // Il respect la syntaxe de la ligne de commande présentée dans l'énoncé.
    
    unsigned int kernel_size = 3;
    float sigma = 5.0;

    // Section critique (boucle à l'infini).
    while(1){

        evenementProfilage(&profInfos, ETAT_TRAITEMENT);

        if (f == 0)
        {
            // Evenement de profilage : attente mutex lecture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            attenteLecteur(&inZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT); // Evenement de profilage : traitement
            memcpy(tempIn, inZone.data, tailleImg);
            signalLecteur(&inZone); // libère vite l'entrée
            // Traitement direct dans tempOut pour éviter de bloquer le mutex pendant toute la durée du traitement
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            lowpassFilter(hauteurVideo, largeurVideo, tempIn, tempOut,
                            kernel_size, sigma, canauxVideo);
            
            
            // Evenement de profilage : attente mutex ecriture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleImg);
            signalEcrivain(&outZone);
        }

        else if (f == 1)
        {
            // Evenement de profilage : attente mutex lecture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            attenteLecteur(&inZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT); // Evenement de profilage : traitement
            memcpy(tempIn, inZone.data, tailleImg);
            signalLecteur(&inZone); // libère vite l'entrée
            // Traitement direct dans tempOut pour éviter de bloquer le mutex pendant toute la durée du traitement
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            highpassFilter(hauteurVideo, largeurVideo, tempIn, tempOut,
                            kernel_size, sigma, canauxVideo);
            
            
            // Evenement de profilage : attente mutex ecriture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleImg);
            signalEcrivain(&outZone);
        }

    }
    return 0;
}
