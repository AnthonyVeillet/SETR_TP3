/******************************************************************************
* Laboratoire 3
* GIF-3004 Systèmes embarqués temps réel
* Hiver 2026
* Marc-André Gardner
* 
* Programme compositeur
*
* Récupère plusieurs flux vidéos à partir d'espaces mémoire partagés et les
* affiche directement dans le framebuffer de la carte graphique.
* 
* IMPORTANT : CE CODE ASSUME QUE TOUS LES FLUX QU'IL REÇOIT SONT EN 427x240
* (427 pixels en largeur, 240 en hauteur). TOUTE AUTRE TAILLE ENTRAINERA UN
* COMPORTEMENT INDÉFINI. Les flux peuvent comporter 1 ou 3 canaux. Dans ce
* dernier cas, ils doivent être dans l'ordre BGR et NON RGB.
*
* Le code permettant l'affichage est inspiré de celui présenté sur le blog
* Raspberry Compote (http://raspberrycompote.blogspot.ie/2014/03/low-level-graphics-on-raspberry-pi-part_14.html),
* par J-P Rosti, publié sous la licence CC-BY 3.0.
*
* Merci à Yannick Hold-Geoffroy pour l'aide apportée pour la gestion
* du framebuffer.
******************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <sys/time.h>

#include <sys/types.h>

// Allocation mémoire, mmap et mlock
#include <sys/mman.h>

// Gestion des ressources et permissions
#include <sys/resource.h>

// Mesure du temps
#include <time.h>

// Obtenir la taille des fichiers
#include <sys/stat.h>

// Contrôle de la console
#include <linux/fb.h>
#include <linux/kd.h>

// Gestion des erreurs
#include <err.h>
#include <errno.h>

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"
#include <getopt.h>


// Fonction permettant de récupérer le temps courant sous forme double
double get_time()
{
    struct timeval t;
    //struct timezone tzp; ERREUR AVEC STRUCT timezone NON DEFINIE, CORRECTION PAR CHATGPT
    //gettimeofday(&t, &tzp);
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)(t.tv_usec)*1e-6;
}


// Cette fonction écrit l'image dans le framebuffer, à la position demandée. Elle est déjà codée pour vous,
// mais vous devez l'utiliser correctement. En particulier, n'oubliez pas que cette fonction assume que
// TOUTES LES IMAGES QU'ELLE REÇOIT SONT EN 427x240 (1 ou 3 canaux). Cette fonction peut gérer
// l'affichage de 1, 2, 3 ou 4 images sur le même écran, en utilisant la séparation préconisée dans l'énoncé.
// La position (premier argument) doit être un entier inférieur au nombre total d'images à afficher (second argument).
// Le troisième argument est le descripteur de fichier du framebuffer (nommé fbfb dans la fonction main()).
// Le quatrième argument est un pointeur sur le memory map de ce framebuffer (nommé fbd dans la fonction main()).
// Les cinquième et sixième arguments sont la largeur et la hauteur de ce framebuffer.
// Le septième est une structure contenant l'information sur le framebuffer (nommé vinfo dans la fonction main()).
// Le huitième est la longueur effective d'une ligne du framebuffer (en octets), contenue dans finfo.line_length dans la fonction main().
// Le neuvième argument est le buffer contenant l'image à afficher, et les trois derniers arguments ses dimensions.
void ecrireImage(const int position, const int total,
                    int fbfd, unsigned char* fb, size_t largeurFB, size_t hauteurFB, struct fb_var_screeninfo *vinfoPtr, int fbLineLength,
                    const unsigned char *data, size_t hauteurSource, size_t largeurSource, size_t canauxSource){
    static int currentPage = 0;
    static unsigned char* imageGlobale = NULL;
    if(imageGlobale == NULL)
        imageGlobale = (unsigned char*)calloc(fbLineLength*hauteurFB, 1);

    currentPage = (currentPage+1) % 2;
    unsigned char *currentFramebuffer = fb + currentPage * fbLineLength * hauteurFB;

    if(position >= total){
        return;
    }

    const unsigned char *dataTraite = data;
    unsigned char* d = NULL;
    if(canauxSource == 1){
        d = (unsigned char*)tempsreel_malloc(largeurSource*hauteurSource*3);
        unsigned int pos = 0;
        for(unsigned int i=0; i < hauteurSource; ++i){
            for(unsigned int j=0; j < largeurSource; ++j){
                d[pos++] = data[i*largeurSource + j];
                d[pos++] = data[i*largeurSource + j];
                d[pos++] = data[i*largeurSource + j];
            }
        }
        dataTraite = d;
    }


    if(total == 1){
        // Une seule image en plein écran
        for(unsigned int ligne=0; ligne < hauteurSource; ligne++){
            memcpy(currentFramebuffer + ligne * fbLineLength, dataTraite + ligne * largeurSource * 3, largeurFB * 3);
        }
    }
    else if(total == 2){
        // Deux images
        if(position == 0){
            // Image du haut
            for(unsigned int ligne=0; ligne < hauteurSource; ligne++){
                memcpy(imageGlobale + ligne * fbLineLength, dataTraite + ligne * largeurSource * 3, largeurFB * 3);
            }
        }
        else{
            // Image du bas
            for(unsigned int ligne=hauteurSource; ligne < hauteurSource*2; ligne++){
                memcpy(imageGlobale + ligne * fbLineLength, dataTraite + (ligne-hauteurSource) * largeurSource * 3, largeurFB * 3);
            }
        }
    }
    else if(total == 3 || total == 4){
        // 3 ou 4 images
        off_t offsetLigne = 0;
        off_t offsetColonne = 0;
        switch (position) {
            case 0:
                // En haut, à gauche
                break;
            case 1:
                // En haut, à droite
                offsetColonne = largeurSource;
                break;
            case 2:
                // En bas, à gauche
                offsetLigne = hauteurSource;
                break;
            case 3:
                // En bas, à droite
                offsetLigne = hauteurSource;
                offsetColonne = largeurSource;
                break;
        }
        // On copie les données ligne par ligne
        offsetLigne *= fbLineLength;
        offsetColonne *= 3;
        for(unsigned int ligne=0; ligne < hauteurSource; ligne++){
            memcpy(imageGlobale + offsetLigne + offsetColonne, dataTraite + ligne * largeurSource * 3, largeurSource * 3);
            offsetLigne += fbLineLength;
        }
    }

    if(total > 1)
        memcpy(currentFramebuffer, imageGlobale, fbLineLength*hauteurFB);
        
    if(canauxSource == 1)
        tempsreel_free(d);
        
    vinfoPtr->yoffset = currentPage * vinfoPtr->yres;
    vinfoPtr->activate = FB_ACTIVATE_VBL;
    if (ioctl(fbfd, FBIOPAN_DISPLAY, vinfoPtr)) {
        printf("Erreur lors du changement de buffer (double buffering inactif)!\n");
    }
}

// Fonction pour helper
static inline int lecture_pret(int r) {
    return (r == 1);
}

int main(int argc, char* argv[])
{
    // FAIT PAR ANTHONY VEILLET
    // code d'analyse des arguments du programme et d'initialisation des zones mémoire partagées
    int nbrActifs;      // Après initialisation, cette variable contient le nombre de flux vidéos actifs (de 1 à 4 inclusivement).
    

    char *entree1, *entree2, *entree3, *entree4;
    struct memPartage zone1 = {0}, zone2 = {0}, zone3 = {0}, zone4 = {0};
    uint32_t
        largeurVideo1, hauteurVideo1, canauxVideo1, fpsVideo1,
        largeurVideo2, hauteurVideo2, canauxVideo2, fpsVideo2,
        largeurVideo3, hauteurVideo3, canauxVideo3, fpsVideo3,
        largeurVideo4, hauteurVideo4, canauxVideo4, fpsVideo4;

    largeurVideo1 = hauteurVideo1 = canauxVideo1 = fpsVideo1 = 0;
    largeurVideo2 = hauteurVideo2 = canauxVideo2 = fpsVideo2 = 0;
    largeurVideo3 = hauteurVideo3 = canauxVideo3 = fpsVideo3 = 0;
    largeurVideo4 = hauteurVideo4 = canauxVideo4 = fpsVideo4 = 0;

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
    struct SchedParams schedParams = {0};           // Paramètres de l'ordonnanceur
    unsigned int runtime, deadline, period;         // Dans le cas de l'ordonnanceur DEADLINE

    if(argc < 2){
        printf("Nombre d'arguments insuffisant\n");
        return -1;
    }

    if(strcmp(argv[1], "--debug") == 0){
        // Mode debug, vous pouvez changer ces valeurs pour ce qui convient dans vos tests
        printf("Mode debug selectionne pour le compositeur\n");
        entree1 = (char*)"/mem1";
        entree2 = (char*)"/mem2";
        entree3 = (char*)"/mem3";
        entree4 = (char*)"/mem4";
        runtime = 10;
        deadline = 20;
        period = 25;
        (void)runtime; (void)deadline; (void)period; // Pour retirer les warning qui dise qu'ils ne sont pas utilisés

        nbrActifs = 4;
        printf("Initialisation compositeur, entree1=%s, entree2=%s, entree3=%s, entree4=%s, "
            "mode d'ordonnancement=%i\n",
            entree1, entree2, entree3, entree4, schedParams.modeOrdonnanceur);
        int r1 = initMemoirePartageeLecteur(entree1, &zone1);
        int r2 = initMemoirePartageeLecteur(entree2, &zone2);
        int r3 = initMemoirePartageeLecteur(entree3, &zone3);
        int r4 = initMemoirePartageeLecteur(entree4, &zone4);
        if ((r1 != 0) || (r2 != 0) || (r3 != 0) || (r4 != 0))
            {
                printf("Erreur d'initialisation memoire partagee lecteur\n");
                return -1;
            }
        
        largeurVideo1 = zone1.header->infos.largeur;
        hauteurVideo1 = zone1.header->infos.hauteur;
        canauxVideo1 = zone1.header->infos.canaux;
        fpsVideo1 = zone1.header->infos.fps;
        largeurVideo2 = zone2.header->infos.largeur;
        hauteurVideo2 = zone2.header->infos.hauteur;
        canauxVideo2 = zone2.header->infos.canaux;
        fpsVideo2 = zone2.header->infos.fps;
        largeurVideo3 = zone3.header->infos.largeur;
        hauteurVideo3 = zone3.header->infos.hauteur;
        canauxVideo3 = zone3.header->infos.canaux;
        fpsVideo3 = zone3.header->infos.fps;
        largeurVideo4 = zone4.header->infos.largeur;
        hauteurVideo4 = zone4.header->infos.hauteur;
        canauxVideo4 = zone4.header->infos.canaux;
        fpsVideo4 = zone4.header->infos.fps;

        if ((fpsVideo1 == 0) || (fpsVideo2 == 0) || (fpsVideo3 == 0) || (fpsVideo4 == 0))
        {
            printf("Le nombre de FPS pour chaque vidéo doit être supérieur à 0\n");
            return -1;
        }

        if ((canauxVideo1 != 1 && canauxVideo1 != 3) || (canauxVideo2 != 1 && canauxVideo2 != 3) || (canauxVideo3 != 1 && canauxVideo3 != 3) || (canauxVideo4 != 1 && canauxVideo4 != 3))
        {
            printf("Format de video non supporte, seulement les formats gris ou BGR\n");
            return -1;
        }
        if ((largeurVideo1 != 427 || hauteurVideo1 != 240) || (largeurVideo2 != 427 || hauteurVideo2 != 240) || (largeurVideo3 != 427 || hauteurVideo3 != 240) || (largeurVideo4 != 427 || hauteurVideo4 != 240))
        {
            printf("Format de video non supporte, seulement les videos en 427x240 sont supportees\n");
            return -1;
        }

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

        // Ce qui suit est la description des zones memoires d'entree
        if(argc - optind < 1){
            printf("Arguments manquants (fichier_entree\n");
            return -1;
        }

        else if (argc - optind > 4)
        {
            printf("Trop d'arguments pour les fichiers d'entree (max 4)\n");
            return -1;
        }

        else if (argc - optind == 1)
        {
            nbrActifs = 1;
            entree1 = argv[optind];
            printf("Initialisation compositeur, entree1=%s, "
                "mode d'ordonnancement=%i\n",
                entree1, schedParams.modeOrdonnanceur);
            if (initMemoirePartageeLecteur(entree1, &zone1) != 0)
                {
                    printf("Erreur d'initialisation memoire partagee lecteur\n");
                    return -1;
                }
            
            largeurVideo1 = zone1.header->infos.largeur;
            hauteurVideo1 = zone1.header->infos.hauteur;
            canauxVideo1 = zone1.header->infos.canaux;
            fpsVideo1 = zone1.header->infos.fps;

            if (fpsVideo1 == 0)
            {
                printf("Le nombre de FPS du vidéo doit être supérieur à 0\n");
                return -1;
            }

            if (canauxVideo1 != 1 && canauxVideo1 != 3)
            {
                printf("Format de video non supporte, seulement les formats gris ou BGR\n");
                return -1;
            }
            if (largeurVideo1 != 427 || hauteurVideo1 != 240)
            {
                printf("Format de video non supporte, seulement les videos en 427x240 sont supportees\n");
                return -1;
            }
        }

        else if (argc - optind == 2)
        {
            nbrActifs = 2;
            entree1 = argv[optind];
            entree2 = argv[optind+1];
            printf("Initialisation compositeur, entree1=%s, entree2=%s, "
                "mode d'ordonnancement=%i\n",
                entree1, entree2, schedParams.modeOrdonnanceur);
            int r1 = initMemoirePartageeLecteur(entree1, &zone1);
            int r2 = initMemoirePartageeLecteur(entree2, &zone2);
            if ((r1 != 0) || (r2 != 0))
                {
                    printf("Erreur d'initialisation memoire partagee lecteur\n");
                    return -1;
                }
            
            largeurVideo1 = zone1.header->infos.largeur;
            hauteurVideo1 = zone1.header->infos.hauteur;
            canauxVideo1 = zone1.header->infos.canaux;
            fpsVideo1 = zone1.header->infos.fps;
            largeurVideo2 = zone2.header->infos.largeur;
            hauteurVideo2 = zone2.header->infos.hauteur;
            canauxVideo2 = zone2.header->infos.canaux;
            fpsVideo2 = zone2.header->infos.fps;

            if ((fpsVideo1 == 0) || (fpsVideo2 == 0))
            {
                printf("Le nombre de FPS pour chaque vidéo doit être supérieur à 0\n");
                return -1;
            }
            
            if ((canauxVideo1 != 1 && canauxVideo1 != 3) || (canauxVideo2 != 1 && canauxVideo2 != 3))
            {
                printf("Format de video non supporte, seulement les formats gris ou BGR\n");
                return -1;
            }
            if ((largeurVideo1 != 427 || hauteurVideo1 != 240) || (largeurVideo2 != 427 || hauteurVideo2 != 240))
            {
                printf("Format de video non supporte, seulement les videos en 427x240 sont supportees\n");
                return -1;
            }

        }
        else if (argc - optind == 3)
        {
            nbrActifs = 3;
            entree1 = argv[optind];
            entree2 = argv[optind+1];
            entree3 = argv[optind+2];
            printf("Initialisation compositeur, entree1=%s, entree2=%s, entree3=%s, "
                "mode d'ordonnancement=%i\n",
                entree1, entree2, entree3, schedParams.modeOrdonnanceur);
            int r1 = initMemoirePartageeLecteur(entree1, &zone1);
            int r2 = initMemoirePartageeLecteur(entree2, &zone2);
            int r3 = initMemoirePartageeLecteur(entree3, &zone3);
            if ((r1 != 0) || (r2 != 0) || (r3 != 0))
                {
                    printf("Erreur d'initialisation memoire partagee lecteur\n");
                    return -1;
                }
            
            largeurVideo1 = zone1.header->infos.largeur;
            hauteurVideo1 = zone1.header->infos.hauteur;
            canauxVideo1 = zone1.header->infos.canaux;
            fpsVideo1 = zone1.header->infos.fps;
            largeurVideo2 = zone2.header->infos.largeur;
            hauteurVideo2 = zone2.header->infos.hauteur;
            canauxVideo2 = zone2.header->infos.canaux;
            fpsVideo2 = zone2.header->infos.fps;
            largeurVideo3 = zone3.header->infos.largeur;
            hauteurVideo3 = zone3.header->infos.hauteur;
            canauxVideo3 = zone3.header->infos.canaux;
            fpsVideo3 = zone3.header->infos.fps;

            if ((fpsVideo1 == 0) || (fpsVideo2 == 0) || (fpsVideo3 == 0))
            {
                printf("Le nombre de FPS pour chaque vidéo doit être supérieur à 0\n");
                return -1;
            }

            if ((canauxVideo1 != 1 && canauxVideo1 != 3) || (canauxVideo2 != 1 && canauxVideo2 != 3) || (canauxVideo3 != 1 && canauxVideo3 != 3))
            {
                printf("Format de video non supporte, seulement les formats gris ou BGR\n");
                return -1;
            }
            if ((largeurVideo1 != 427 || hauteurVideo1 != 240) || (largeurVideo2 != 427 || hauteurVideo2 != 240) || (largeurVideo3 != 427 || hauteurVideo3 != 240))
            {
                printf("Format de video non supporte, seulement les videos en 427x240 sont supportees\n");
                return -1;
            }

        }
        else
        {
            nbrActifs = 4;
            entree1 = argv[optind];
            entree2 = argv[optind+1];
            entree3 = argv[optind+2];
            entree4 = argv[optind+3];
            printf("Initialisation compositeur, entree1=%s, entree2=%s, entree3=%s, entree4=%s, "
                "mode d'ordonnancement=%i\n",
                entree1, entree2, entree3, entree4, schedParams.modeOrdonnanceur);
            int r1 = initMemoirePartageeLecteur(entree1, &zone1);
            int r2 = initMemoirePartageeLecteur(entree2, &zone2);
            int r3 = initMemoirePartageeLecteur(entree3, &zone3);
            int r4 = initMemoirePartageeLecteur(entree4, &zone4);
            if ((r1 != 0) || (r2 != 0) || (r3 != 0) || (r4 != 0))
                {
                    printf("Erreur d'initialisation memoire partagee lecteur\n");
                    return -1;
                }
            
            largeurVideo1 = zone1.header->infos.largeur;
            hauteurVideo1 = zone1.header->infos.hauteur;
            canauxVideo1 = zone1.header->infos.canaux;
            fpsVideo1 = zone1.header->infos.fps;
            largeurVideo2 = zone2.header->infos.largeur;
            hauteurVideo2 = zone2.header->infos.hauteur;
            canauxVideo2 = zone2.header->infos.canaux;
            fpsVideo2 = zone2.header->infos.fps;
            largeurVideo3 = zone3.header->infos.largeur;
            hauteurVideo3 = zone3.header->infos.hauteur;
            canauxVideo3 = zone3.header->infos.canaux;
            fpsVideo3 = zone3.header->infos.fps;
            largeurVideo4 = zone4.header->infos.largeur;
            hauteurVideo4 = zone4.header->infos.hauteur;
            canauxVideo4 = zone4.header->infos.canaux;
            fpsVideo4 = zone4.header->infos.fps;

            if ((fpsVideo1 == 0) || (fpsVideo2 == 0) || (fpsVideo3 == 0) || (fpsVideo4 == 0))
            {
                printf("Le nombre de FPS pour chaque vidéo doit être supérieur à 0\n");
                return -1;
            }

            if ((canauxVideo1 != 1 && canauxVideo1 != 3) || (canauxVideo2 != 1 && canauxVideo2 != 3) || (canauxVideo3 != 1 && canauxVideo3 != 3) || (canauxVideo4 != 1 && canauxVideo4 != 3))
            {
                printf("Format de video non supporte, seulement les formats gris ou BGR\n");
                return -1;
            }
            if ((largeurVideo1 != 427 || hauteurVideo1 != 240) || (largeurVideo2 != 427 || hauteurVideo2 != 240) || (largeurVideo3 != 427 || hauteurVideo3 != 240) || (largeurVideo4 != 427 || hauteurVideo4 != 240))
            {
                printf("Format de video non supporte, seulement les videos en 427x240 sont supportees\n");
                return -1;
            }

        }
    }

    // Changement de mode d'ordonnancement
    if (appliquerOrdonnancement(&schedParams, "compositeur") != 0) {
        printf("Erreur appliquerOrdonnancement\n");
        return -1;
    }


    // Initialisation des structures nécessaires à l'affichage
    long int screensize = 0;
    // Ouverture du framebuffer
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        perror("Erreur lors de l'ouverture du framebuffer ");
        return -1;
    }

    // Obtention des informations sur l'affichage et le framebuffer
    struct fb_var_screeninfo vinfo;
    struct fb_var_screeninfo orig_vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Erreur lors de la requete d'informations sur le framebuffer ");
    }

    // On conserve les précédents paramètres
    memcpy(&orig_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));

    // On choisit la bonne résolution
    vinfo.bits_per_pixel = 24;
    switch (nbrActifs) {
        case 1:
            vinfo.xres = 427;
            vinfo.yres = 240;
            break;
        case 2:
            vinfo.xres = 427;
            vinfo.yres = 480;
            break;
        case 3:
        case 4:
            vinfo.xres = 854;
            vinfo.yres = 480;
            break;
        default:
            printf("Nombre de sources invalide!\n");
            return -1;
            break;
    }

    vinfo.xres_virtual = vinfo.xres;
    vinfo.yres_virtual = vinfo.yres * 2;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
        perror("Erreur lors de l'appel a ioctl ");
    }

    // On récupère les "vraies" paramètres du framebuffer
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        perror("Erreur lors de l'appel a ioctl (2) ");
    }

    // On fait un mmap pour avoir directement accès au framebuffer
    screensize = finfo.smem_len;
    unsigned char *fbp = (unsigned char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if (fbp == MAP_FAILED) {
        perror("Erreur lors du mmap de l'affichage ");
        return -1;
    }


    //=====================================
    // Définition des éléments avant boucle
    //=====================================

    FILE *fstats = fopen("stats.txt", "w");
    if (fstats == NULL)
    {
        perror("fopen stats.txt");
        return -1;
    }
    setbuf(fstats, NULL);

    double debutCompositeur = get_time();

    // ------------------------
    // TABLEAUX (1 à 4 flux)
    // ------------------------
    struct memPartage* zones[4] = { &zone1, &zone2, &zone3, &zone4 };

    size_t w[4]  = { largeurVideo1, largeurVideo2, largeurVideo3, largeurVideo4};
    size_t h[4]  = { hauteurVideo1, hauteurVideo2, hauteurVideo3, hauteurVideo4};
    size_t ch[4] = { canauxVideo1, canauxVideo2, canauxVideo3, canauxVideo4};
    size_t fps[4]= { fpsVideo1, fpsVideo2, fpsVideo3, fpsVideo4};

    // Pour éviter le malloc dans ecrireImage quand ch==1 : on stocke toujours une trame BGR (3 canaux)
    size_t pixels[4] = {0};
    unsigned char* lastFrameBGR[4] = {NULL};
    int gotFrame[4] = {0};
    int newFrame[4] = {0};

    // FPS cap par entrée (période)
    double fpsCap[4] = {0.0};
    double framePeriod[4] = {0.0};
    double nextDisplay[4] = {0.0};

    // Stats fenêtre 5 sec
    double winStart[4] = {0.0};
    double lastDisplayed[4] = {0.0};
    double maxDt[4] = {0.0};
    int framesWin[4] = {0};

    double nextStatsWrite = debutCompositeur + 5.0;

    size_t maxFrameIn = 427u * 240u * 3u; // le plus gros bloc alloué (BGR)
    if (prepareMemoire(maxFrameIn, maxFrameIn) != 0) {
        printf("Erreur prepareMemoire\n");
        return -1;
    }

    struct rlimit rl = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY };
    setrlimit(RLIMIT_MEMLOCK, &rl);
    mlockall(MCL_CURRENT | MCL_FUTURE);

    // init par entrée active
    for (int i = 0; i < nbrActifs; i++) {
        pixels[i] = w[i] * h[i];

        // Buffer BGR constant (3 canaux)
        lastFrameBGR[i] = (unsigned char*)tempsreel_malloc(pixels[i] * 3u);
        if (!lastFrameBGR[i]) {
            perror("tempsreel_malloc lastFrameBGR");
            return -1;
        }

        gotFrame[i] = 0;
        newFrame[i] = 0;

        // fallback fps si 0
        fpsCap[i] = (fps[i] == 0) ? 30.0 : (double)fps[i];
        framePeriod[i] = 1.0 / fpsCap[i];
        nextDisplay[i] = debutCompositeur;

        winStart[i] = debutCompositeur;
        lastDisplayed[i] = -1.0;
        maxDt[i] = 0.0;
        framesWin[i] = 0;
    }

    while(1) {
        evenementProfilage(&profInfos, ETAT_TRAITEMENT);
        double now = get_time();

        // ------------------------
        // 1) POLL NON-BLOQUANT : récupérer des nouvelles frames
        // ------------------------
        for (int i = 0; i < nbrActifs; i++) {

            evenementProfilage(&profInfos, ETAT_ATTENTE_MUTEXLECTURE);
            int r = attenteLecteurAsync(zones[i]);
            evenementProfilage(&profInfos, ETAT_TRAITEMENT);
            if (!lecture_pret(r)) {
                continue; // pas prêt -> next source
            }

            // Ici, le mutex lecteur est lock. On copie puis on libère.
            if (ch[i] == 3) {
                // BGR -> BGR
                memcpy(lastFrameBGR[i], zones[i]->data, pixels[i] * 3u);
            } else if (ch[i] == 1) {
                // GRIS -> BGR (sans malloc dans ecrireImage)
                const unsigned char* src = zones[i]->data;
                unsigned char* dst = lastFrameBGR[i];
                for (size_t p = 0; p < pixels[i]; p++) {
                    unsigned char g = src[p];
                    *dst++ = g; *dst++ = g; *dst++ = g;
                }
            } else {
                signalLecteur(zones[i]);
                continue;
            }

            signalLecteur(zones[i]);

            gotFrame[i] = 1;
            newFrame[i] = 1;
        }

        // ------------------------
        // 2) AFFICHAGE avec cap FPS (par entrée)
        // ------------------------
        for (int i = 0; i < nbrActifs; i++) {

            if (!gotFrame[i]) continue;
            if (now < nextDisplay[i]) continue;

            // Affiche la dernière trame connue à la position i
            ecrireImage(
                i, nbrActifs,
                fbfd, fbp,
                vinfo.xres, vinfo.yres,
                &vinfo, finfo.line_length,
                lastFrameBGR[i],
                h[i], w[i],
                3 // toujours BGR
            );

            // Stats, on compte seulement si une *nouvelle* frame est arrivée depuis le dernier affichage
            if (newFrame[i]) {
                framesWin[i]++;

                if (lastDisplayed[i] > 0.0) {
                    double dt = now - lastDisplayed[i];
                    if (dt > maxDt[i]) maxDt[i] = dt;
                }
                lastDisplayed[i] = now;
                newFrame[i] = 0;
            }

            // Prochain affichage (anti-drift)
            do { nextDisplay[i] += framePeriod[i]; } while (nextDisplay[i] <= now);
        }

        // ------------------------
        // 3) stats.txt toutes 5 sec
        // ------------------------
        if (now >= nextStatsWrite) {
            double elapsed = now - debutCompositeur;

            // Formatage
            fprintf(fstats, "[%.1f] ", elapsed);

            for (int i = 0; i < nbrActifs; i++) {
                double winDur = now - winStart[i];
                double moy = (winDur > 0.0) ? ((double)framesWin[i] / winDur) : 0.0;

                fprintf(fstats, "Entree %d: moy=%.1f fps, max=%.1f ms",
                        i + 1, moy, maxDt[i] * 1000.0);

                if (i != nbrActifs - 1) fprintf(fstats, " | ");

                // reset fenêtre 5 sec
                winStart[i] = now;
                framesWin[i] = 0;
                maxDt[i] = 0.0;
            }

            fprintf(fstats, "\n");
            nextStatsWrite += 5.0;
        }

        // ------------------------
        // 4) éviter CPU 100% (sleep jusqu'au prochain event)
        // ------------------------
        double nextEvent = nextStatsWrite;

        for (int i = 0; i < nbrActifs; i++) {
            if (nextDisplay[i] < nextEvent) nextEvent = nextDisplay[i];
        }

        now = get_time();
        if (nextEvent > now) {
            double sleepSec = nextEvent - now;
            if (sleepSec > 0.0005) { // >0.5 ms
                evenementProfilage(&profInfos, ETAT_ENPAUSE);
                usleep((unsigned int)(sleepSec * 1e6));
            } else {
                evenementProfilage(&profInfos, ETAT_ENPAUSE);
                usleep(200); // micro-yield
            }
        } else {
            evenementProfilage(&profInfos, ETAT_ENPAUSE);
            usleep(500); // fallback yield
        }
    }


    // cleanup
    // Retirer le mmap
    munmap(fbp, screensize);


    // reset the display mode
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &orig_vinfo)) {
        printf("Error re-setting variable information.\n");
    }
    // Fermer le framebuffer
    close(fbfd);
    fclose(fstats);

    return 0;

}

