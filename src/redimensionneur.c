/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier implémentant le programme de redimensionnement d'images
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
    uint32_t w = 0, h = 0, r = 0; // Options en entrée

    if(argc < 2){
        printf("Nombre d'arguments insuffisant\n");
        return -1;
    }

    if(strcmp(argv[1], "--debug") == 0){
        // Mode debug, vous pouvez changer ces valeurs pour ce qui convient dans vos tests
        printf("Mode debug selectionne pour le redimensionneur\n");
        entree = (char*)"/mem1";
        sortie = (char*)"/mem2";
        runtime = 10;
        deadline = 20;
        period = 25;
        w = 640;
        h = 480;
        r = 0;
        (void)runtime; (void)deadline; (void)period; // Pour retirer les warning qui dise qu'ils ne sont pas utilisés
    }
    else{
        int c;
        opterr = 0;

        while ((c = getopt (argc, argv, "s:d:w:h:r:")) != -1){
            switch (c) {
                case 's':
                    parseSchedOption(optarg, &schedParams);
                    break;
                case 'd':
                    parseDeadlineParams(optarg, &schedParams);
                    break;
                case 'w':
                    w = atoi(optarg);
                    break;
                case 'h':
                    h = atoi(optarg);
                    break;
                case 'r':
                    r = atoi(optarg);
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

        if ((int32_t)w <= 0 || (int32_t)h <= 0) {
            printf("Largeur/hauteur invalides: -w et -h doivent etre > 0\n");
            return -1;
        }

        if ((r != 0) && (r != 1)) {
            printf("La méthode de redimensionnement doit être 0 (plus proche voisin) ou 1 (interpolation linéaire)\n");
            return -1;
        }

        entree = argv[optind];
        sortie = argv[optind+1];
    }

    printf("Initialisation redimensionneur largeur=%u, hauteur=%u, entree=%s, sortie=%s, mode d'ordonnancement=%i\n",
        w, h, entree, sortie, schedParams.modeOrdonnanceur);
    
    // Changement de mode d'ordonnancement
    if (appliquerOrdonnancement(&schedParams, "redimensionneur") != 0) {
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
    
    // On peut initialiser la memoire out avec la dimension redimensionnée et le bon canal, puisque c'est le format de sortie.
    // Donc on se crée une struct videoInfos temporaire pour initialiser la memoire partagee de sortie.
    struct videoInfos tempInfos = {0};
    tempInfos.canaux = inZone.header->infos.canaux;
    tempInfos.largeur = w;
    tempInfos.hauteur = h;
    tempInfos.fps = inZone.header->infos.fps;

    if (initMemoirePartageeEcrivain(sortie, &outZone, &tempInfos) != 0)
    {
        printf("Erreur d'initialisation memoire partagee ecrivain\n");
        return -1;
    }

    uint32_t largeurVideoIn = inZone.header->infos.largeur;
    uint32_t hauteurVideoIn = inZone.header->infos.hauteur;
    uint32_t canauxVideoIn = inZone.header->infos.canaux;

    uint32_t largeurVideoOut = tempInfos.largeur;
    uint32_t hauteurVideoOut = tempInfos.hauteur;
    uint32_t canauxVideoOut = tempInfos.canaux;

    size_t tailleIn  = (size_t)largeurVideoIn * hauteurVideoIn * canauxVideoIn;
    size_t tailleOut = (size_t)largeurVideoOut * hauteurVideoOut * canauxVideoOut;

    // Être certain que faire prepareMemoire(tailleIn, tailleOut) ne soit TROP PETIT à cause de ResizeGrid
    size_t gridBytes = (size_t)largeurVideoOut * (size_t)hauteurVideoOut * sizeof(unsigned int);
    size_t tailleOutPourAlloc = (tailleOut > gridBytes) ? tailleOut : gridBytes;

    if (prepareMemoire(tailleIn, tailleOutPourAlloc) != 0)
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

    // Comme que les fonction de redimension lisent et écrivent des données dans les zones mémoire partagées,
    // on créer une copie locale de ces données pour éviter de bloquer le mutex pendant toute
    // la durée du traitement (ce qui serait le cas si on passait directement les pointeurs de
    // la mémoire partagée).
    unsigned char* tempIn = tempsreel_malloc(tailleIn);
    unsigned char* tempOut = tempsreel_malloc(tailleOut);
    if (tempOut == NULL)
    {
        printf("Erreur d'allocation memoire pour les buffers temporaires\n");
        return -1;
    }


    // Code permettant de redimensionner une image (en utilisant les fonctions précodées
    // dans utils.c, celles commençant par "resize"). Le code lit une image depuis une zone 
    // mémoire partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // Respect de la syntaxe de la ligne de commande présentée dans l'énoncé.

    ResizeGrid rg; // initialise une ResizeGrid pour le redimensionnement
    if (r == 0)
    {
        rg = resizeNearestNeighborInit(hauteurVideoOut, largeurVideoOut, hauteurVideoIn, largeurVideoIn);
    }

    else if (r == 1)
    {
        rg = resizeBilinearInit(hauteurVideoOut, largeurVideoOut, hauteurVideoIn, largeurVideoIn);
    }
    

    // Section critique (boucle à l'infini).
    while(1){

        evenementProfilage(&profInfos, ETAT_TRAITEMENT);

        if ((w == largeurVideoIn) && (h == hauteurVideoIn)) // Image parreil, donc copie direct
        {
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            attenteLecteur(&inZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(tempOut, inZone.data, tailleOut);
            signalLecteur(&inZone);

            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleOut); // Copie sortie temporaire vers la mémoire partagée de sortie
            signalEcrivain(&outZone);
            continue; // On peut passer directement à la prochaine itération de la boucle
        }

        else if (r == 0)
        {
            // Evenement de profilage : attente mutex lecture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            attenteLecteur(&inZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT); // Evenement de profilage : traitement
            memcpy(tempIn, inZone.data, tailleIn);
            signalLecteur(&inZone); // libère vite l'entrée
            // Traitement direct dans tempOut pour éviter de bloquer le mutex pendant toute la durée du traitement
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            resizeNearestNeighbor(tempIn, hauteurVideoIn, largeurVideoIn, tempOut,
                                    hauteurVideoOut, largeurVideoOut, rg, canauxVideoIn);
            
            
            // Evenement de profilage : attente mutex ecriture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleOut);
            signalEcrivain(&outZone);
        }

        else if (r == 1)
        {
            // Evenement de profilage : attente mutex lecture
            evenementProfilage(&profInfos, ETAT_TRAITEMENT); // Evenement de profilage : traitement
            memcpy(tempIn, inZone.data, tailleIn);
            signalLecteur(&inZone); // libère vite l'entrée
            // Traitement direct dans tempOut pour éviter de bloquer le mutex pendant toute la durée du traitement
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            resizeBilinear(tempIn, hauteurVideoIn, largeurVideoIn, tempOut,
                                    hauteurVideoOut, largeurVideoOut, rg, canauxVideoIn);
            
            
            // Evenement de profilage : attente mutex ecriture
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleOut);
            signalEcrivain(&outZone);
        }

    }

    resizeDestroy(rg);
    return 0;
}
