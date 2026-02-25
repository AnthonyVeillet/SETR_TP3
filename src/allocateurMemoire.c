#include "allocateurMemoire.h"
#include <string.h>
#include <sys/mman.h>

// Variables globales pour l'état de l'allocateur
static void *gros_blocs[ALLOC_N_GROS_BLOCS] = {0};
static int gros_blocs_libres[ALLOC_N_GROS_BLOCS] = {0};
static size_t taille_gros_bloc = 0;

static void *petits_blocs[ALLOC_N_PETITS_BLOCS] = {0};
static int petits_blocs_libres[ALLOC_N_PETITS_BLOCS] = {0};

// Initialisation des pools de mémoire
int prepareMemoire(size_t tailleImageEntree, size_t tailleImageSortie)
{
	// Libère d'abord toute ancienne allocation (si jamais re-appelé)
	for (int i = 0; i < ALLOC_N_GROS_BLOCS; ++i)
	{
		if (gros_blocs[i])
		{
			munlock(gros_blocs[i], taille_gros_bloc);
			free(gros_blocs[i]);
			gros_blocs[i] = NULL;
		}
	}
	for (int i = 0; i < ALLOC_N_PETITS_BLOCS; ++i)
	{
		if (petits_blocs[i])
		{
			munlock(petits_blocs[i], ALLOC_TAILLE_PETIT);
			free(petits_blocs[i]);
			petits_blocs[i] = NULL;
		}
	}

	// Taille d'un gros bloc = max(tailleImageEntree, tailleImageSortie)
	taille_gros_bloc = (tailleImageEntree > tailleImageSortie) ? tailleImageEntree : tailleImageSortie;
	if (taille_gros_bloc == 0)
		return -1;

	// Alloue et lock les gros blocs
	for (int i = 0; i < ALLOC_N_GROS_BLOCS; ++i)
	{
		gros_blocs[i] = malloc(taille_gros_bloc);
		if (!gros_blocs[i])
		{
			// Libère tout ce qui a déjà été alloué
			for (int j = 0; j < i; ++j)
			{
				munlock(gros_blocs[j], taille_gros_bloc);
				free(gros_blocs[j]);
				gros_blocs[j] = NULL;
			}
			return -1;
		}
		if (mlock(gros_blocs[i], taille_gros_bloc) != 0)
		{
			for (int j = 0; j <= i; ++j)
			{
				free(gros_blocs[j]);
				gros_blocs[j] = NULL;
			}
			return -1;
		}
		gros_blocs_libres[i] = 1;
	}
	// Alloue et lock les petits blocs
	for (int i = 0; i < ALLOC_N_PETITS_BLOCS; ++i)
	{
		petits_blocs[i] = malloc(ALLOC_TAILLE_PETIT);
		if (!petits_blocs[i])
		{
			// Libère tout ce qui a déjà été alloué
			for (int j = 0; j < ALLOC_N_GROS_BLOCS; ++j)
			{
				munlock(gros_blocs[j], taille_gros_bloc);
				free(gros_blocs[j]);
				gros_blocs[j] = NULL;
			}
			for (int j = 0; j < i; ++j)
			{
				munlock(petits_blocs[j], ALLOC_TAILLE_PETIT);
				free(petits_blocs[j]);
				petits_blocs[j] = NULL;
			}
			return -1;
		}
		if (mlock(petits_blocs[i], ALLOC_TAILLE_PETIT) != 0)
		{
			for (int j = 0; j < ALLOC_N_GROS_BLOCS; ++j)
			{
				munlock(gros_blocs[j], taille_gros_bloc);
				free(gros_blocs[j]);
				gros_blocs[j] = NULL;
			}
			for (int j = 0; j <= i; ++j)
			{
				free(petits_blocs[j]);
				petits_blocs[j] = NULL;
			}
			return -1;
		}
		petits_blocs_libres[i] = 1;
	}
	return 0;
}

void *tempsreel_malloc(size_t taille)
{
	// Petit bloc ?
	if (taille <= ALLOC_TAILLE_PETIT)
	{
		for (int i = 0; i < ALLOC_N_PETITS_BLOCS; ++i)
		{
			if (petits_blocs_libres[i])
			{
				petits_blocs_libres[i] = 0;
				return petits_blocs[i];
			}
		}
		return NULL; // Plus de petits blocs
	}
	// Gros bloc ?
	if (taille <= taille_gros_bloc)
	{
		for (int i = 0; i < ALLOC_N_GROS_BLOCS; ++i)
		{
			if (gros_blocs_libres[i])
			{
				gros_blocs_libres[i] = 0;
				return gros_blocs[i];
			}
		}
		return NULL; // Plus de gros blocs
	}
	// Trop gros
	return NULL;
}

void tempsreel_free(void *ptr)
{
	// Cherche dans les petits blocs
	for (int i = 0; i < ALLOC_N_PETITS_BLOCS; ++i)
	{
		if (petits_blocs[i] == ptr)
		{
			petits_blocs_libres[i] = 1;
			return;
		}
	}
	// Cherche dans les gros blocs
	for (int i = 0; i < ALLOC_N_GROS_BLOCS; ++i)
	{
		if (gros_blocs[i] == ptr)
		{
			gros_blocs_libres[i] = 1;
			return;
		}
	}
	// Sinon, ne fait rien (ou pourrait afficher une erreur)
}
