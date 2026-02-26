/*****************************************************************************
*
* Fonctions utilitaires, laboratoire 3, SETR
*
* Fonctions accomplissant diverses tâches nécessaires au traitement des images.
* La plupart de ces fonctions sont déjà implémentées pour vous dans utils.c,
* la seule fonction que VOUS avez à implémenter est appliquerOrdonnancement
* (voir sa description plus bas).
*
* Marc-André Gardner, Hiver 2026
* Merci à Yannick Hold-Geoffroy pour l'implémentation des fonctions de
* redimensionnement et de filtrage.
******************************************************************************/
#ifndef UTILS_H
#define UTILS_H


// Permet de protéger le header lorsqu'il est inclus par un fichier C++
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>	
#include <time.h>
#include <inttypes.h>
#include <sched.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "allocateurMemoire.h"

#define ORDONNANCEMENT_NORT 0
#define ORDONNANCEMENT_RR 1
#define ORDONNANCEMENT_FIFO 2
#define ORDONNANCEMENT_DEADLINE 3

// Structure contenant les paramètres d'ordonnancement
// Attention, le paramètres pour deadline sont conservés en millisecondes (tels
// que reçus sur la ligne de commande), alors que la valeur à utiliser avec 
// sched_setattr doit être en nanosecondes!
struct SchedParams {
    int modeOrdonnanceur;
    unsigned int runtime;   // en millisecondes, tel que reçu sur la ligne de commande
    unsigned int deadline;  // en millisecondes, tel que reçu sur la ligne de commande
    unsigned int period;    // en millisecondes, tel que reçu sur la ligne de commande
};

// Parse l'argument suivant l'option -s (type d'ordonnanceur: NORT, RR, FIFO, DEADLINE)
// et initialise le champ modeOrdonnanceur dans la structure SchedParams.
// Retourne 0 en cas de succès, -1 si le mode n'est pas reconnu (utilise NORT par défaut)
int parseSchedOption(const char* arg, struct SchedParams* params);

// Parse l'argument suivant l'option -d (runtime,deadline,period en millisecondes)
// et initialise les champs correspondants dans la structure SchedParams.
int parseDeadlineParams(char* arg, struct SchedParams* params);

// Applique les paramètres d'ordonnancement au processus courant
// La chaîne de caractères nomProgramme est utilisée pour les messages d'erreur
// Retourne 0 en cas de succès, -1 en cas d'erreur
// TODO : implémenter cette fonction dans utils.c
int appliquerOrdonnancement(const struct SchedParams* params, const char* nomProgramme);

#define ETAT_INDEFINI 0
#define ETAT_INITIALISATION 10
#define ETAT_ATTENTE_MUTEXLECTURE 20
#define ETAT_TRAITEMENT 30
#define ETAT_ATTENTE_MUTEXECRITURE 40
#define ETAT_ENPAUSE 50

// Mettre a zero pour desactiver le profilage
#define PROFILAGE_ACTIF 0
// Assez d'espace pour 30 caracteres par evenement * 5 evenements par boucle * 30 images par seconde
#define PROFILAGE_INTERVALLE_SAUVEGARDE_SEC 4
#define PROFILAGE_TAILLE_INIT 30 * 5 * 30 * PROFILAGE_INTERVALLE_SAUVEGARDE_SEC * 4


/* Data structures */
typedef struct {
    unsigned int width, height;
    float *data;
} Kernel;

typedef struct {
    unsigned int *i, *j;
    float *i_f, *j_f;
} ResizeGrid;


typedef struct{
    char *data;
    size_t length;
    unsigned int pos;
    uint64_t derniere_sauvegarde;
    unsigned int dernier_etat;
    FILE* fd;
} InfosProfilage;

// Les fonctions de redimensionnement requièrent une *ResizeGrid* en entrée. Celle-ci est commune à toutes les
// images et peut être précalculée, ce qui accélère le traitement.

// Cette fonction initialise une ResizeGrid pour l'interpolation au plus proche voisin. Sa valeur de retour peut par
// la suite être utilisée comme paramètre lors d'un redimensionnement.
ResizeGrid resizeNearestNeighborInit(const unsigned int out_height, const unsigned int out_width, const unsigned int in_height, const unsigned int in_width);

// Cette fonction redimensionne une image en utilisant la technique du plus proche voisin. Le buffer de sortie (output)
// DOIT être préalloué en considérant les dimensions de l'image.
void resizeNearestNeighbor(const unsigned char* input, const unsigned int in_height, const unsigned int in_width,
                            unsigned char* output, const unsigned int out_height, const unsigned int out_width,
                            const ResizeGrid rg, const unsigned int n_channels);

// Cette fonction initialise une ResizeGrid pour l'interpolation bilinéaire. Sa valeur de retour peut par
// la suite être utilisée comme paramètre lors d'un redimensionnement.
ResizeGrid resizeBilinearInit(const unsigned int out_height, const unsigned int out_width, const unsigned int in_height, const unsigned int in_width);

// Cette fonction redimensionne une image en utilisant une interpolation bilinéaire. Le buffer de sortie (output)
// DOIT être préalloué en considérant les dimensions de l'image.
void resizeBilinear(const unsigned char* input, const unsigned int in_height, const unsigned int in_width,
                    unsigned char* output, const unsigned int out_height, const unsigned int out_width,
                    const ResizeGrid rg, const unsigned int n_channels);

// Désalloue une ResizeGrid, si besoin est
void resizeDestroy(ResizeGrid rg);

// Effectue un filtrage passe-bas sur une image. Les dimensions de cette dernière restent inchangées.
// Le buffer de sortie (output) DOIT être préalloué en considérant les dimensions de l'image.
// kernel_size et sigma sont les paramètres du filtrage (gaussien). Vous pouvez par exemple utiliser 3 et 5.
void lowpassFilter(const unsigned int height, const unsigned int width, const unsigned char *input, unsigned char *output,
                   const unsigned int kernel_size, float sigma, const unsigned int n_channels);

// Effectue un filtrage passe-haut sur une image. Les dimensions de cette dernière restent inchangées.
// Le buffer de sortie (output) DOIT être préalloué en considérant les dimensions de l'image.
// kernel_size et sigma sont les paramètres du filtrage (gaussien). Vous pouvez par exemple utiliser 3 et 5.
void highpassFilter(const unsigned int height, const unsigned int width, const unsigned char *input, unsigned char *output,
                    const unsigned int kernel_size, float sigma, const unsigned int n_channels);

// Convertit l'image en niveaux de gris
// Le buffer de sortie (output) DOIT être préalloué en considérant les dimensions de l'image.
void convertToGray(const unsigned char* input, const unsigned int in_height, const unsigned int in_width, const unsigned int n_channels,
                    unsigned char* output);


// Enregistre l'image dans un fichier PPM dont le nom est passé en paramètre
void enregistreImage(const unsigned char* input, const unsigned int in_height, const unsigned int in_width, const unsigned int n_channels, const char* nomfichier);


void initProfilage(InfosProfilage *dataprof, const char *chemin_enregistrement);

void evenementProfilage(InfosProfilage *dataprof, unsigned int type);


#ifdef __cplusplus
}
#endif

#endif
