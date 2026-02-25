#include "commMemoirePartagee.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <limits.h>

// Initialisation de la mémoire partagée (écrivain)
int initMemoirePartageeEcrivain(const char* identifiant, struct memPartage *zone, struct videoInfos *infos)
{
    if (!identifiant || !zone || !infos) return -1;
    if (infos->largeur == 0 || infos->hauteur == 0 || infos->canaux == 0) return -1;

    // Taille en size_t (évite overflow 32-bit)
    if ((size_t)infos->largeur > SIZE_MAX / (size_t)infos->hauteur) return -1;
    size_t pixels = (size_t)infos->largeur * (size_t)infos->hauteur;
    if (pixels > SIZE_MAX / (size_t)infos->canaux) return -1;
    size_t data_sz = pixels * (size_t)infos->canaux;

    size_t header_sz = sizeof(struct memPartageHeader);
    size_t total_sz  = header_sz + data_sz;

    int fd = shm_open(identifiant, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return -1;

    if (ftruncate(fd, (off_t)total_sz) < 0) {
        close(fd);
        shm_unlink(identifiant);
        return -1;
    }

    void* ptr = mmap(NULL, total_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        shm_unlink(identifiant);
        return -1;
    }

    struct memPartageHeader* header = (struct memPartageHeader*)ptr;

    // IMPORTANT: bloquer les lecteurs pendant l'init
    header->etat = ETAT_NON_INITIALISE;

    pthread_mutexattr_t mattr;
    pthread_condattr_t  cattr;

    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

    // Optionnel: peut échouer selon l'environnement -> on ignore si ça fail
    (void)pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);

    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&header->mutex, &mattr);
    pthread_cond_init(&header->condEcrivain, &cattr);
    pthread_cond_init(&header->condLecteur, &cattr);

    // IMPORTANT: écrire les infos AVANT de rendre l'état "prêt"
    header->infos = *infos;
    header->etat  = ETAT_PRET_SANS_DONNEES;

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    zone->fd = fd;
    zone->header = header;
    zone->tailleDonnees = data_sz;
    zone->data = (unsigned char*)ptr + header_sz;

    return 0;
}

// Initialisation de la mémoire partagée (lecteur)
int initMemoirePartageeLecteur(const char* identifiant, struct memPartage *zone)
{
    if (!identifiant || !zone) return -1;

    int fd;
    struct stat st;
    void* ptr;

    while (1) {
        fd = shm_open(identifiant, O_RDWR, 0666);
        if (fd < 0) {
            if (errno == ENOENT) {
                usleep(DELAI_INIT_READER_USEC);
                continue;
            }
            return -1;
        }

        if (fstat(fd, &st) < 0) {
            close(fd);
            return -1;
        }

        if ((size_t)st.st_size < sizeof(struct memPartageHeader)) {
            close(fd);
            usleep(DELAI_INIT_READER_USEC);
            continue;
        }

        ptr = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            return -1;
        }

        struct memPartageHeader* header = (struct memPartageHeader*)ptr;

        // Attend que l'écrivain finisse d'initialiser mutex/conds/infos
        while (header->etat == ETAT_NON_INITIALISE) {
            usleep(DELAI_INIT_READER_USEC);
        }

        zone->fd = fd;
        zone->header = header;
        zone->tailleDonnees = (size_t)st.st_size - sizeof(struct memPartageHeader);
        zone->data = (unsigned char*)ptr + sizeof(struct memPartageHeader);

        return 0;
    }
}

// Attente du lecteur (bloquant). Quand ça retourne, mutex LOCKÉ.
int attenteLecteur(struct memPartage *zone)
{
    if (!zone || !zone->header) return -1;

    pthread_mutex_lock(&zone->header->mutex);
    while (zone->header->etat != ETAT_PRET_AVEC_DONNEES) {
        pthread_cond_wait(&zone->header->condLecteur, &zone->header->mutex);
    }
    return 1; // prêt à lire, mutex locké
}

// Attente du lecteur Async (non-bloquant). Si prêt, mutex LOCKÉ.
int attenteLecteurAsync(struct memPartage *zone)
{
    if (!zone || !zone->header) return -1;

    pthread_mutex_lock(&zone->header->mutex);
    if (zone->header->etat == ETAT_PRET_AVEC_DONNEES) {
        return 1; // mutex locké
    }

    pthread_mutex_unlock(&zone->header->mutex);
    return 0;
}

// Attente de l'écrivain (bloquant). Quand ça retourne, mutex LOCKÉ.
int attenteEcrivain(struct memPartage *zone)
{
    if (!zone || !zone->header) return -1;

    pthread_mutex_lock(&zone->header->mutex);
    while (zone->header->etat != ETAT_PRET_SANS_DONNEES) {
        pthread_cond_wait(&zone->header->condEcrivain, &zone->header->mutex);
    }
    return 1; // prêt à écrire, mutex locké
}

// Signal du lecteur: finit de lire -> réveille l'écrivain, puis UNLOCK.
void signalLecteur(struct memPartage *zone)
{
    if (!zone || !zone->header) return;

    zone->header->etat = ETAT_PRET_SANS_DONNEES;
    pthread_cond_signal(&zone->header->condEcrivain);
    pthread_mutex_unlock(&zone->header->mutex);
}

// Signal de l'écrivain: finit d'écrire -> réveille le lecteur, puis UNLOCK.
void signalEcrivain(struct memPartage *zone)
{
    if (!zone || !zone->header) return;

    zone->header->etat = ETAT_PRET_AVEC_DONNEES;
    pthread_cond_signal(&zone->header->condLecteur);
    pthread_mutex_unlock(&zone->header->mutex);
}