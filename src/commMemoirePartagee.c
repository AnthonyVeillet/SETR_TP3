/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier implémentant les fonctions de communication inter-processus
 ******************************************************************************/

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

// Initialisation de la mémoire partagée (écrivain)
int initMemoirePartageeEcrivain(const char* identifiant, struct memPartage *zone, struct videoInfos *infos)
{
	if (!identifiant || !zone || !infos) return -1;
	size_t taille_header = sizeof(struct memPartageHeader);
	size_t taille = taille_header + infos->largeur * infos->hauteur * infos->canaux;
	printf("[initMemoirePartageeEcrivain] sizeof(memPartageHeader) = %zu\n", taille_header);
	printf("[initMemoirePartageeEcrivain] taille totale mempartage = %zu\n", taille);
	printf("[initMemoirePartageeEcrivain] shm_open permissions = 0666\n");
	int fd = shm_open(identifiant, O_CREAT | O_RDWR, 0666);
	if (fd < 0) return -1;
	if (ftruncate(fd, taille) < 0)
    {
		close(fd);
		shm_unlink(identifiant);
		return -1;
	}

	void* ptr = mmap(NULL, taille, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED)
    {
		close(fd);
		shm_unlink(identifiant);
		return -1;
	}

	struct memPartageHeader* header = (struct memPartageHeader*)ptr;
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&header->mutex, &mattr);
	pthread_cond_init(&header->condEcrivain, &cattr);
	pthread_cond_init(&header->condLecteur, &cattr);
	header->etat = ETAT_PRET_SANS_DONNEES;
	printf("[initMemoirePartageeEcrivain] etat initial = %u (attendu: 1)\n", header->etat);
	header->infos = *infos;
	pthread_mutexattr_destroy(&mattr);
	pthread_condattr_destroy(&cattr);
	zone->fd = fd;
	zone->header = header;
	zone->tailleDonnees = taille - sizeof(struct memPartageHeader);
	zone->data = (unsigned char*)ptr + sizeof(struct memPartageHeader);
	return 0;
}

// Initialisation de la mémoire partagée (lecteur)
int initMemoirePartageeLecteur(const char* identifiant, struct memPartage *zone) {
	if (!identifiant || !zone) return -1;
	int fd;
	struct stat st;
	void* ptr = NULL;
	while (1)
    {
		fd = shm_open(identifiant, O_RDWR, 0666);
		if (fd < 0)
        {
			if (errno == ENOENT)
            {
				usleep(DELAI_INIT_READER_USEC);
				continue;
			} 
            else
            {
				return -1;
			}
		}

		if (fstat(fd, &st) < 0)
        {
			close(fd);
			return -1;
		}

		if ((size_t)st.st_size < sizeof(struct memPartageHeader))
        {
			close(fd);
			usleep(DELAI_INIT_READER_USEC);
			continue;
		}

		ptr = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr == MAP_FAILED)
        {
			close(fd);
			return -1;
		}

		struct memPartageHeader* header = (struct memPartageHeader*)ptr;
		while (header->etat == ETAT_NON_INITIALISE)
        {
			usleep(DELAI_INIT_READER_USEC);
		}

		zone->fd = fd;
		zone->header = header;
		zone->tailleDonnees = st.st_size - sizeof(struct memPartageHeader);
		zone->data = (unsigned char*)ptr + sizeof(struct memPartageHeader);
		return 0;
	}
}

// Attente du lecteur (Le lecteur attend sur SA condition "condLecteur")
int attenteLecteur(struct memPartage *zone)
{
	if (!zone || !zone->header) return -1;
	pthread_mutex_lock(&zone->header->mutex);
	while (zone->header->etat != ETAT_PRET_AVEC_DONNEES)
    {
        // CORRECTION : Le lecteur attend sur condLecteur (pas condEcrivain)
		pthread_cond_wait(&zone->header->condLecteur, &zone->header->mutex);
	}
	return 0;
}

// Attente du lecteur Async (idem)
int attenteLecteurAsync(struct memPartage *zone)
{
	if (!zone || !zone->header) return -1;
	
	pthread_mutex_lock(&zone->header->mutex);
	if (zone->header->etat == ETAT_PRET_AVEC_DONNEES)
	{
		return 1;
	}

	pthread_mutex_unlock(&zone->header->mutex);
	return 0;
}

// Attente de l'écrivain (L'écrivain attend sur SA condition "condEcrivain")
int attenteEcrivain(struct memPartage *zone)
{
	if (!zone || !zone->header) return -1;
	pthread_mutex_lock(&zone->header->mutex);

	while (zone->header->etat != ETAT_PRET_SANS_DONNEES)
	{
        // CORRECTION : L'écrivain attend sur condEcrivain (pas condLecteur)
		pthread_cond_wait(&zone->header->condEcrivain, &zone->header->mutex);
	}
	return 0;
}

// Signal du lecteur (Le lecteur a fini, il réveille l'écrivain sur "condEcrivain")
void signalLecteur(struct memPartage *zone)
{
	if (!zone || !zone->header) return;

	zone->header->etat = ETAT_PRET_SANS_DONNEES; 

    // CORRECTION : On signale l'écrivain qui dort
	pthread_cond_signal(&zone->header->condEcrivain); 
	pthread_mutex_unlock(&zone->header->mutex);
}

// Signal de l'écrivain (L'écrivain a fini, il réveille le lecteur sur "condLecteur")
void signalEcrivain(struct memPartage *zone)
{
	if (!zone || !zone->header) return;

	zone->header->etat = ETAT_PRET_AVEC_DONNEES;

    // CORRECTION : On signale le lecteur qui dort
	pthread_cond_signal(&zone->header->condLecteur);
	pthread_mutex_unlock(&zone->header->mutex);
}
