/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier de déclaration des fonctions de l'allocateur mémoire temps réel
 * Ne modifiez pas les prototypes de fonction écrits ici.
 ******************************************************************************/

#ifndef ALLOC_MEM_H
#define ALLOC_MEM_H

// Permet de protéger le header lorsqu'il est inclus par un fichier C++
#ifdef __cplusplus
extern "C" {
#endif
    
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

/******************************************************************************
 * L'idée de cet allocateur mémoire est de pré-allouer plusieurs blocs de mémoire
 * de taille fixe. Lorsque l'on demande un bloc de mémoire, on retourne un des blocs
 * libres. Lorsqu'on libère un bloc, on le remet dans la liste des blocs libres.
 * 
 * Lors de l'initialisation, prepareMemoire() est appelée et alloue les blocs
 * (en utilisant un ou des malloc standards, ce qui correct puisque nous ne
 * sommes pas encore dans la section critique!). Par la suite, les appels à
 * malloc() sont remplacés par des appels à tempsreel_malloc(), qui fonctionne
 * _exactement_ de la même façon du point de vue de la fonction appelante, mais
 * qui utilise les blocs pré-alloués. De même, les appels à free() sont remplacés
 * par des appels à tempsreel_free().
 * 
 * tempsreel_malloc() retourne toujours un bloc contenant _au moins_ la taille 
 * demandée, mais en pratique elle sera souvent supérieure. Par exemple, une 
 * demande de 450 octets pourra être satisfaite par un bloc de 1024 octets.
 * 
 * Dans le cadre de ce travail, il y a deux types d'allocations :
 * - Les "grosses" allocations, qui correspondent aux images (dont la taille
 * peut varier en fonction de la configuration, mais plusieurs centaines de Ko)
 * - Les "petites" allocations, qui correspondent à d'autres allocations (ex. pour
 * des manipulations de chaînes de caractères) et qui sont inférieures à 2 Ko mais
 * potentiellement nombreuses.
 * 
 * Il serait inefficace de retourner un bloc de (par exemple) 1 Mo pour chaque
 * demande, même petite. C'est pourquoi nous gérons deux types d'allocations
 * distinctes, chacune avec son propre pool de mémoire.
 * 
 * Le nombre de "gros" blocs à allouer est défini dans ALLOC_N_GROS_BLOCS. 
 * Similairement, le nombre de "petits" blocs est défini dans ALLOC_N_PETITS_BLOCS, 
 * et la taille de ces petits blocs (en octets) est définie dans ALLOC_TAILLE_PETIT.
 * 
 * La taille des "gros" blocs dépend de la largeur/hauteur/canaux des images
 * d'entrée et de sortie. Pour cette raison, la fonction prepareMemoire() reçoit
 * en argument la taille des images d'entrée et de sortie. À partir de là, vous
 * pouvez calculer la taille des blocs nécessaires pour les "grosses" allocations.
 * 
 ******************************************************************************/


#define ALLOC_N_GROS_BLOCS 16
#define ALLOC_N_PETITS_BLOCS 100
#define ALLOC_TAILLE_PETIT 2048

// Prépare les buffers nécessaires pour une allocation correspondante aux tailles
// d'images passées en paramètre. Retourne 0 en cas de succès, et -1 si un
// problème (par exemple manque de mémoire) est survenu.
int prepareMemoire(size_t tailleImageEntree, size_t tailleImageSortie);

// Ces deux fonctions doivent pouvoir s'utiliser exactement comme malloc() et free()
// (dans la limite de la mémoire disponible, bien sûr)
void* tempsreel_malloc(size_t taille);

void tempsreel_free(void* ptr);

// N'oubliez pas de créer le fichier allocateurMemoire.c et d'y implémenter les fonctions décrites ici!             

#ifdef __cplusplus
}
#endif

#endif                      