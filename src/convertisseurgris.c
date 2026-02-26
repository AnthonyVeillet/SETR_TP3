/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier implémentant le programme de conversion en niveaux de gris
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
    
    // Code lisant les options sur la ligne de commande
    char *entree, *sortie;                          // Zones memoires d'entree et de sortie
    struct SchedParams schedParams = {0};           // Paramètres de l'ordonnanceur
    unsigned int runtime, deadline, period;         // Dans le cas de l'ordonnanceur DEADLINE

    if(argc < 2){
        printf("Nombre d'arguments insuffisant\n");
        return -1;
    }

    if(strcmp(argv[1], "--debug") == 0){
        // Mode debug, vous pouvez changer ces valeurs pour ce qui convient dans vos tests
        printf("Mode debug selectionne pour le convertisseur niveau de gris\n");
        entree = (char*)"/mem1";
        sortie = (char*)"/mem2";
        runtime = 10;
        deadline = 20;
        period = 25;
        (void)runtime; (void)deadline; (void)period; // Pour retirer les warning qui dise qu'ils ne sont pas utilisés
    }
    else{
        int c;
        opterr = 0;

        while ((c = getopt (argc, argv, "s:d:")) != -1){
            switch (c) {
                case 's':
                    parseSchedOption(optarg, &schedParams);
                    break;
                case 'd':
                    parseDeadlineParams(optarg, &schedParams);
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
        entree = argv[optind];
        sortie = argv[optind+1];
    }

    printf("Initialisation convertisseur, entree=%s, sortie=%s, mode d'ordonnancement=%i\n", entree, sortie, schedParams.modeOrdonnanceur);
    
    // Changement de mode d'ordonnancement
    if (appliquerOrdonnancement(&schedParams, "convertisseurgris") != 0) {
        printf("Erreur appliquerOrdonnancement\n");
        return -1;
    }
    
    // FAIT PAR ANTHONY VEILLET
    // Code initialisant les zones mémoire partagées (une en entrée, en tant que lecteur, et l'autre en sortie,
    // en tant qu'écrivain).
    // Initialise également l'allocateur mémoire (avec prepareMemoire). S'assure que toute la mémoire utilisée dans la
    // section critique est préallouée ET bloquée

    struct memPartage inZone = {0};
    struct memPartage outZone = {0};

    if (initMemoirePartageeLecteur(entree, &inZone) != 0)
        {
            printf("Erreur d'initialisation memoire partagee lecteur\n");
            return -1;
        }

    // On peut initialiser la memoire out avec un canal de 1, puisque c'est le format de sortie (niveaux de gris)
    // Donc on se crée une struct videoInfos temporaire pour initialiser la memoire partagee de sortie, avec canaux = 1
    struct videoInfos tempInfos = {0};
    tempInfos.canaux = 1;
    tempInfos.largeur = inZone.header->infos.largeur;
    tempInfos.hauteur = inZone.header->infos.hauteur;
    tempInfos.fps = inZone.header->infos.fps;

    if (initMemoirePartageeEcrivain(sortie, &outZone, &tempInfos) != 0)
    {
        printf("Erreur d'initialisation memoire partagee ecrivain\n");
        return -1;
    }

    uint32_t largeurVideo = tempInfos.largeur;
    uint32_t hauteurVideo = tempInfos.hauteur;
    uint32_t canauxVideo = inZone.header->infos.canaux;
    if (canauxVideo != 1 && canauxVideo != 3)
    {
        printf("Format de video non supporte (canaux = %u), seulement les formats en niveaux de gris (1 canal) et BGR (3 canaux) sont supportes\n", canauxVideo);
        return -1;
    }


    // Sans cast : largeur * hauteur est calculé en uint32_t, puis converti en size_t (overflow possible).
    // Avec cast : (size_t)largeur force le calcul en size_t dès le départ.
    // Exemple : uint32_t largeur = 100 000, uint32_t hauteur = 100 000, size_t taille = largeur * hauteur;
    // Sans cast : 100 000 * 100 000 = 10 000 000 000 (overflow car ca dépasse la valeur max d'un uint32_t, 
    // donc résultat incorrect), puis ensuite on converti en size_t (toujours incorrect).
    // Avec cast : (size_t)100 000 * 100 000 = 10 000 000 000 (calcul correct, car on converti en size_t avant de multiplier),
    // donc aucun overflow possible, et résultat correct.
    size_t tailleIn  = (size_t)largeurVideo * hauteurVideo * canauxVideo;
    size_t tailleOut = (size_t)largeurVideo * hauteurVideo; // puisque le résultat est en niveaux de gris (1 canal)

    if (prepareMemoire(tailleIn, tailleOut) != 0)
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

    // Comme que convertToGray lit et écrit des données dans les zones mémoire partagées,
    // on créer une copie locale de ces données pour éviter de bloquer le mutex pendant toute
    // la durée du traitement (ce qui serait le cas si on passait directement les pointeurs de
    // la mémoire partagée à convertToGray).
    unsigned char* tempIn = tempsreel_malloc(tailleIn);
    unsigned char* tempOut = tempsreel_malloc(tailleOut);
    if ((tempOut == NULL) || (tempIn == NULL))
    {
        printf("Erreur d'allocation memoire pour les buffers temporaires\n");
        return -1;
    }



    // Section critique (boucle à l'infini).
    while(1){
        // Code permettant de convertir une image en niveaux de gris, en utilisant la
        // fonction convertToGray de utils.c. Le code lit une image depuis une zone mémoire 
        // partagée et envoyer le résultat sur une autre zone mémoire partagée.

        evenementProfilage(&profInfos, ETAT_TRAITEMENT);
        if (canauxVideo == 1)
        {
            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            attenteLecteur(&inZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(tempOut, inZone.data, tailleOut); // On peut copier directement les données d'entrée vers tempOut, puisque c'est déjà en niveaux de gris
            signalLecteur(&inZone);

            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
            attenteEcrivain(&outZone);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            memcpy(outZone.data, tempOut, tailleOut); // Copie sortie temporaire vers la mémoire partagée de sortie
            signalEcrivain(&outZone);
            continue; // On peut passer directement à la prochaine itération de la boucle
        }
        
        // Evenement de profilage : attente mutex lecture
        evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
        attenteLecteur(&inZone);
        evenementProfilage(&profInfos, ETAT_TRAITEMENT); // Evenement de profilage : traitement
        memcpy(tempIn, inZone.data, tailleIn);
        signalLecteur(&inZone); // libère vite l'entrée
        // Traitement direct dans tempOut pour éviter de bloquer le mutex pendant toute la durée du traitement
        evenementProfilage(&profInfos, ETAT_TRAITEMENT);
        convertToGray(tempIn, hauteurVideo, largeurVideo, canauxVideo, tempOut);
        
        // Evenement de profilage : attente mutex ecriture
        evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXECRITURE);
        attenteEcrivain(&outZone);
        evenementProfilage(&profInfos, ETAT_TRAITEMENT);
        memcpy(outZone.data, tempOut, tailleOut);
        signalEcrivain(&outZone);
    }

    //tempsreel_free(tempIn);
    tempsreel_free(tempOut);

    return 0;
}
