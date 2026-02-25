/******************************************************************************
 * Laboratoire 3
 * GIF-3004 Systèmes embarqués temps réel
 * Hiver 2026
 * Marc-André Gardner
 * 
 * Fichier implémentant les fonctions utilitaires telles que déclarées dans utils.h
 ******************************************************************************/
// La ligne ci-dessous indique à la libc que nous voulons les extensions supplémentaires
// permettant, entre autres, l'accès à sched_setattr
#define _GNU_SOURCE
#include "utils.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))


// Applique les paramètres d'ordonnancement au processus courant
int appliquerOrdonnancement(const struct SchedParams* p, const char* nomProg)
{
    if (!p) return -1;

    // NORT -> rien à faire
    if (p->modeOrdonnanceur == ORDONNANCEMENT_NORT) {
        // Optionnel: print pour debug
        // printf("[%s] Scheduler: NORT\n", nomProg ? nomProg : "prog");
        return 0;
    }

    // RR
    if (p->modeOrdonnanceur == ORDONNANCEMENT_RR) {
        struct sched_param sp;
        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = 99;

        if (sched_setscheduler(0, SCHED_RR, &sp) != 0) {
            fprintf(stderr, "[%s] ERREUR sched_setscheduler(RR): %s\n",
                    nomProg ? nomProg : "prog", strerror(errno));
            return -1;
        }

        printf("Mode d'operation du scheduler modifie avec succes pour %s (RR).\n",
               nomProg ? nomProg : "prog");
        return 0;
    }

    // FIFO
    if (p->modeOrdonnanceur == ORDONNANCEMENT_FIFO) {
        struct sched_param sp;
        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = 99;

        if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
            fprintf(stderr, "[%s] ERREUR sched_setscheduler(FIFO): %s\n",
                    nomProg ? nomProg : "prog", strerror(errno));
            return -1;
        }

        printf("Mode d'operation du scheduler modifie avec succes pour %s (FIFO).\n",
               nomProg ? nomProg : "prog");
        return 0;
    }

    // DEADLINE -> laisse ton impl sched_setattr/syscall ici si tu l'as déjà
    // (mais pour régler ton problème de fluidité du scénario 01, pas besoin tout de suite)
    if (p->modeOrdonnanceur == ORDONNANCEMENT_DEADLINE) {
        fprintf(stderr, "[%s] DEADLINE pas géré ici (à implémenter via sched_setattr)\n",
                nomProg ? nomProg : "prog");
        return -1;
    }

    return -1;
}

// Parse l'option -s (type d'ordonnanceur: NORT, RR, FIFO, DEADLINE)
int parseSchedOption(const char* arg, struct SchedParams* params) {
    if (strcmp(arg, "NORT") == 0) {
        params->modeOrdonnanceur = ORDONNANCEMENT_NORT;
    } else if (strcmp(arg, "RR") == 0) {
        params->modeOrdonnanceur = ORDONNANCEMENT_RR;
    } else if (strcmp(arg, "FIFO") == 0) {
        params->modeOrdonnanceur = ORDONNANCEMENT_FIFO;
    } else if (strcmp(arg, "DEADLINE") == 0) {
        params->modeOrdonnanceur = ORDONNANCEMENT_DEADLINE;
    } else {
        params->modeOrdonnanceur = ORDONNANCEMENT_NORT;
        printf("Mode d'ordonnancement %s non valide, defaut sur NORT\n", arg);
        return -1;
    }
    return 0;
}

// Parse l'argument suivant l'option -d (runtime,deadline,period en millisecondes)
int parseDeadlineParams(char* arg, struct SchedParams* params) {
    int paramIndex = 0;
    char* splitString = strtok(arg, ",");
    while (splitString != NULL) {
        unsigned int value = (unsigned int)atoi(splitString);
        if (paramIndex == 0) {
            params->runtime = value;
        } else if (paramIndex == 1) {
            params->deadline = value;
        } else {
            params->period = value;
            break;
        }
        paramIndex++;
        splitString = strtok(NULL, ",");
    }
    return 0;
}


/* Convolution with repeat mode */
void _convolve(const unsigned int height, const unsigned int width, const float* input, const Kernel kern, float* output){
    int i, j;
    unsigned int x, y;
    float coeff, data;
    float sum;

    // Short forms of the image dimensions g
    const int iw = width;
    const int kw = kern.width, kh = kern.height;
    const int ow = width;

    // Kernel half-sizes and number of elements g
    const int kw2   = kw/2,        kh2 = kh/2;

    // Iterate over pixels of image
    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            sum = 0;

            // Iterate over elements of kernel
            for(i=-kh2; i<=kh2; i++){
                for(j=-kw2; j<=kw2; j++){
                    data = input[min(max(y + i, 0), height - 1)*iw + min(max(j + x, 0), width - 1)];
                    coeff = kern.data[(i + kh2)*kw + (j + kw2)];
                    sum += data * coeff;
                }
            }

            output[y*ow + x] = sum;
        }
    }
}


/* Helpers */

void _permuteRGB(const unsigned int in_height, const unsigned int in_width,
                float *input_cont, const unsigned int n_channels, const unsigned char *input)
{
    for (unsigned int k = 0; k < n_channels; ++k) {
        for (unsigned int i = 0; i < in_height; ++i) {
            for (unsigned int j = 0; j < in_width; ++j) {
                input_cont[(in_height*in_width)*k + i*in_width + j] = (float)(input[(i*in_width+j)*n_channels+k]);
            }
        }
    }
}


void _permuteRGB_char(const unsigned int in_height, const unsigned int in_width,
                    unsigned char *input_cont, const unsigned int n_channels, const unsigned char *input)
{
    for (unsigned int k = 0; k < n_channels; ++k) {
        for (unsigned int i = 0; i < in_height; ++i) {
            for (unsigned int j = 0; j < in_width; ++j) {
                input_cont[(in_height*in_width)*k + i*in_width + j] = input[(i*in_width+j)*n_channels+k];
            }
        }
    }
}


void _unpermuteRGB(const unsigned int out_height, const unsigned int out_width,
                float *output_cont, const unsigned int n_channels,
                unsigned char *output)
{
    for (unsigned int k = 0; k < n_channels; ++k) {
        for (unsigned int i = 0; i < out_height; ++i) {
            for (unsigned int j = 0; j < out_width; ++j) {
                output[(i*out_width + j)*n_channels + k] = (unsigned char)(output_cont[(out_height*out_width)*k + i*out_width + j]);
            }
        }
    }
}

Kernel _createGaussianKernel(const unsigned int height, const unsigned int width,
                            const float stdv)
{
    float r, s = 2.0 * stdv * stdv;
    float sum = 0.0;
    Kernel kern;
    kern.width = width;
    kern.height = height;
    kern.data = (float*)tempsreel_malloc(width*height*sizeof(float));
    if(kern.data == NULL){
        fprintf(stderr, "[_createGaussianKernel] Erreur d'allocation memoire avec tempsreel_malloc pour le kernel gaussien (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }

    const int max_x = (height - 1) / 2;
    const int min_x = -(height / 2);
    const int max_y = (width - 1) / 2;
    const int min_y = -(width / 2);

    for (int x = min_x; x <= max_x; x++) {
        for(int y = min_y; y <= max_y; y++) {
            r = sqrt(x*x + y*y);
            kern.data[(x - min_x)*width + (y - min_y)] = (exp(-(r*r)/s))/(M_PI * s);
            sum += kern.data[(x - min_x)*width + (y - min_y)];
        }
    }

    // Normalize the kernel
    for(unsigned int i = 0; i < height*width; ++i) {
        kern.data[i] /= sum;
    }

    return kern;
}

void _destroyKernel(Kernel *kern)
{
    tempsreel_free(kern->data);
}

/* Filtering */

void lowpassFilter(const unsigned int height, const unsigned int width, const unsigned char *input, unsigned char *output,
                const unsigned int kernel_size, float sigma, const unsigned int n_channels)
{
    float *input_cont = (float*)tempsreel_malloc(height * width * n_channels * sizeof(float));
    if(input_cont == NULL){
        fprintf(stderr, "[lowpassFilter] Erreur d'allocation memoire avec tempsreel_malloc pour input_cont (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    float *output_cont = (float*)tempsreel_malloc(height * width * n_channels * sizeof(float));
    if(output_cont == NULL){
        fprintf(stderr, "[lowpassFilter] Erreur d'allocation memoire avec tempsreel_malloc pour output_cont (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }

    Kernel kern = _createGaussianKernel(kernel_size, kernel_size, sigma);

    _permuteRGB(height, width, input_cont, n_channels, input);

    for (unsigned int i = 0; i < n_channels; ++i) {
        _convolve(height, width, input_cont+(height*width)*i, kern, output_cont+(height*width)*i);
    }

    _unpermuteRGB(height, width, output_cont, n_channels, output);

    _destroyKernel(&kern);
    tempsreel_free(input_cont);
    tempsreel_free(output_cont);
}

void highpassFilter(const unsigned int height, const unsigned int width, const unsigned char *input, unsigned char *output,
                const unsigned int kernel_size, float sigma, const unsigned int n_channels)
{
    unsigned char *filtered = (unsigned char*)tempsreel_malloc(height * width * n_channels * sizeof(unsigned char));
    if(filtered == NULL){
        fprintf(stderr, "[highpassFilter] Erreur d'allocation memoire avec tempsreel_malloc pour filtered (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    
    lowpassFilter(height, width, input, filtered, kernel_size, sigma, n_channels);

    for (unsigned int i = 0; i < height * width * n_channels; ++i ) {
        output[i] = min(abs(input[i] - filtered[i])*2, 255);
    }

    tempsreel_free(filtered);
}



/* Resize */

void _ul_nearestneighbors_regulargrid(const unsigned char *ya, const unsigned int stride, const unsigned int *x1,
            const unsigned int *x2, const unsigned int sz, unsigned char *y)
{
    for (unsigned int i = 0; i < sz; ++i) {
        y[i] = ya[x1[i]*stride + x2[i]];
    }
}

void _ul_bilinear_regulargrid(const unsigned char *ya, const unsigned int stride, const float *x1,
            const float *x2, const unsigned int sz, unsigned char *y)
{
    float l, r, t, b;
    float tmp1, tmp2;

    for (unsigned int i = 0; i < sz; ++i) {
        l = floor(x2[i]);
        r = ceil(x2[i]+1e-4);
        t = floor(x1[i]);
        b = ceil(x1[i]+1e-4);

        tmp1 = (r - x2[i])*(float)ya[(int)t*stride + (int)r] + (x2[i] - l)*(float)ya[(int)t*stride + (int)l];
        tmp2 = (r - x2[i])*(float)ya[(int)b*stride + (int)r] + (x2[i] - l)*(float)ya[(int)b*stride + (int)l];
        y[i] = (unsigned char)((x1[i] - t)*tmp1 + (b - x1[i])*tmp2);
    }
}


void _createGrid(const unsigned int height, const unsigned int width, const float target_x, const float target_y, unsigned int *data_i, unsigned int *data_j)
{
    float step_x = target_x / (float)height;
    float step_y = target_y / (float)width;

    for (unsigned int i = 0; i < height; ++i) {
        for (unsigned int j = 0; j < width; ++j) {
            data_i[i*width + j] = (unsigned int)(step_x*i);
            data_j[i*width + j] = (unsigned int)(step_y*j);
        }
    }
}

void _createGridFloat(const unsigned int height, const unsigned int width, const float target_x, const float target_y, float *data_i, float *data_j)
{
    float step_x = target_x / (float)height;
    float step_y = target_y / (float)width;

    for (unsigned int i = 0; i < height; ++i) {
        for (unsigned int j = 0; j < width; ++j) {
            data_i[i*width + j] = step_x*i;
            data_j[i*width + j] = step_y*j;
        }
    }
}

#define XORSWAP_UNSAFE(a, b)	((a)^=(b),(b)^=(a),(a)^=(b))
#define XORSWAP(a, b)   ((&(a) == &(b)) ? (a) : ((a)^=(b),(b)^=(a),(a)^=(b)))


ResizeGrid resizeNearestNeighborInit(const unsigned int out_height, const unsigned int out_width, const unsigned int in_height, const unsigned int in_width)
{
    ResizeGrid retval;
    memset(&retval, 0, sizeof(retval));
    retval.i = (unsigned int*)tempsreel_malloc(out_height * out_width * sizeof(unsigned int));
    if(retval.i == NULL){
        fprintf(stderr, "[resizeNearestNeighborInit] Erreur d'allocation memoire avec tempsreel_malloc pour retval.i (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    retval.j = (unsigned int*)tempsreel_malloc(out_height * out_width * sizeof(unsigned int));
    if(retval.j == NULL){
        fprintf(stderr, "[resizeNearestNeighborInit] Erreur d'allocation memoire avec tempsreel_malloc pour retval.j (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    _createGrid(out_height, out_width, (float)in_height, (float)in_width, retval.i, retval.j);

    return retval;
}


ResizeGrid resizeBilinearInit(const unsigned int out_height, const unsigned int out_width, const unsigned int in_height, const unsigned int in_width)
{
    ResizeGrid retval;
    memset(&retval, 0, sizeof(retval));
    retval.i_f = (float*)tempsreel_malloc(out_height * out_width * sizeof(float));
    if(retval.i_f == NULL){
        fprintf(stderr, "[resizeBilinearInit] Erreur d'allocation memoire avec tempsreel_malloc pour retval.i_f (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    retval.j_f = (float*)tempsreel_malloc(out_height * out_width * sizeof(float));
    if(retval.j_f == NULL){
        fprintf(stderr, "[resizeBilinearInit] Erreur d'allocation memoire avec tempsreel_malloc pour retval.j_f (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }

    _createGridFloat(out_height, out_width, (float)in_height, (float)in_width, retval.i_f, retval.j_f);

    return retval;
}


void resizeDestroy(ResizeGrid rg)
{
    if (rg.i != NULL) { tempsreel_free(rg.i); }
    if (rg.j != NULL) { tempsreel_free(rg.j); }
    if (rg.i_f != NULL) { tempsreel_free(rg.i_f); }
    if (rg.j_f != NULL) { tempsreel_free(rg.j_f); }
}


void resizeNearestNeighbor(const unsigned char* input, const unsigned int in_height, const unsigned int in_width,
            unsigned char* output, const unsigned int out_height, const unsigned int out_width,
            const ResizeGrid rg, const unsigned int n_channels)
{
    if (n_channels > 1) {
        unsigned char *input_cont = (unsigned char*)tempsreel_malloc(in_height * in_width * n_channels * sizeof(unsigned char));
        if(input_cont == NULL){
            fprintf(stderr, "[resizeNearestNeighborInit] Erreur d'allocation memoire avec tempsreel_malloc pour input_cont (pointeur nul)\n");
            exit(EXIT_FAILURE);
        }
        unsigned char *output_cont = (unsigned char*)tempsreel_malloc(out_height * out_width * n_channels * sizeof(unsigned char));
        if(output_cont == NULL){
            fprintf(stderr, "[resizeNearestNeighborInit] Erreur d'allocation memoire avec tempsreel_malloc pour output_cont (pointeur nul)\n");
            exit(EXIT_FAILURE);
        }
        _permuteRGB_char(in_height, in_width, input_cont, n_channels, input);
        for (unsigned int i = 0; i < n_channels; ++i) {
            _ul_nearestneighbors_regulargrid(input_cont + (in_height*in_width)*i, in_width, rg.i, rg.j, out_height*out_width, output_cont + (out_height*out_width)*i);
        }

        for (unsigned int i = 0; i < out_height; ++i) {
            for (unsigned int j = 0; j < out_width; ++j) {
                for (unsigned int k = 0; k < n_channels; ++k) {
                    output[(i*out_width + j)*n_channels + k] = output_cont[(out_height*out_width)*k + i*out_width + j];
                }
            }
        }

        tempsreel_free(input_cont);
        tempsreel_free(output_cont);
    } else {
        _ul_nearestneighbors_regulargrid(input, in_width, rg.i, rg.j, out_height*out_width, output);
    }

}

void resizeBilinear(const unsigned char* input, const unsigned int in_height, const unsigned int in_width,
                    unsigned char* output, const unsigned int out_height, const unsigned int out_width,
                    const ResizeGrid rg, const unsigned int n_channels)
{
    unsigned char *input_cont = (unsigned char*)tempsreel_malloc(in_height * in_width * n_channels * sizeof(unsigned char));
    if(input_cont == NULL){
        fprintf(stderr, "[resizeBilinear] Erreur d'allocation memoire avec tempsreel_malloc pour input_cont (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    unsigned char *output_cont = (unsigned char*)tempsreel_malloc(out_height * out_width * n_channels * sizeof(unsigned char));
    if(output_cont == NULL){
        fprintf(stderr, "[resizeBilinear] Erreur d'allocation memoire avec tempsreel_malloc pour output_cont (pointeur nul)\n");
        exit(EXIT_FAILURE);
    }
    if (n_channels > 1) {
        _permuteRGB_char(in_height, in_width, input_cont, n_channels, input);
        for (unsigned int i = 0; i < n_channels; ++i) {
            _ul_bilinear_regulargrid(input_cont + (in_height*in_width)*i, in_width, rg.i_f, rg.j_f, out_height*out_width, output_cont + (out_height*out_width)*i);
        }

        for (unsigned int i = 0; i < out_height; ++i) {
            for (unsigned int j = 0; j < out_width; ++j) {
                for (unsigned int k = 0; k < n_channels; ++k) {
                    output[(i*out_width + j)*n_channels + k] = output_cont[(out_height*out_width)*k + i*out_width + j];
                }
            }
        }
    } else {
        _ul_bilinear_regulargrid(input, in_width, rg.i_f, rg.j_f, out_height*out_width, output);
    }

    tempsreel_free(input_cont);
    tempsreel_free(output_cont);
}

void convertToGray(const unsigned char* input, const unsigned int in_height, const unsigned int in_width, const unsigned int n_channels,
                    unsigned char* output){
    // =========================================================================
    // OPTIMISATION POUR ARMv6 (Pi Zero W) : Arithmétique entière
    // =========================================================================
    // Le Pi Zero W n'a pas de FPU performante (VFPv2 très lent).
    // On remplace les calculs flottants par de l'arithmétique en point fixe.
    //
    // Formule originale (ITU-R BT.601) :
    //   Y = 0.114*B + 0.587*G + 0.299*R
    //
    // Conversion en point fixe (facteur 256 = 2^8) :
    //   0.114 * 256 ≈ 29
    //   0.587 * 256 ≈ 150  
    //   0.299 * 256 ≈ 77
    //   Total = 256, donc on divise par 256 avec un décalage >> 8
    //
    // Ceci évite toutes les conversions float et opérations FPU.
    // =========================================================================
    
    const unsigned int total_pixels = in_height * in_width;
    const unsigned char *src = input;
    unsigned char *dst = output;
    
    for (unsigned int idx = 0; idx < total_pixels; ++idx) {
        // Calcul en arithmétique entière : (29*B + 150*G + 77*R) >> 8
        // Les coefficients sont pour BGR (ordre utilisé dans le projet)
        *dst++ = (unsigned char)((29 * src[0] + 150 * src[1] + 77 * src[2]) >> 8);
        src += n_channels;
    }
}


void enregistreImage(const unsigned char* input, const unsigned int in_height, const unsigned int in_width, const unsigned int n_channels, const char* nomfichier){
    FILE *f = fopen(nomfichier, "w");
    fprintf(f, "P3\n%d %d\n%d\n", in_width, in_height, 255);
    if(n_channels == 1){
        for (unsigned int i=0; i<in_width*in_height; i++)
            fprintf(f,"%d %d %d ", input[i], input[i], input[i]);
    }
    else{
    for (unsigned int i=0; i<in_width*in_height*3; i+=3)
        fprintf(f,"%d %d %d ", input[i], input[i+1], input[i+2]);
    }
    fclose(f);
}

void initProfilage(InfosProfilage *dataprof, const char *chemin_enregistrement){
    if(PROFILAGE_ACTIF == 0){
        return;
    }
    // Ouverture du fichier
    dataprof->fd = fopen(chemin_enregistrement, "w+");
    dataprof->derniere_sauvegarde = 0;
    dataprof->dernier_etat = ETAT_INDEFINI;

    // Allocation du buffer pour eviter de devoir ecrire systematiquement a chaque appel
    dataprof->data = (char*)calloc(PROFILAGE_TAILLE_INIT, sizeof(char));
    // Force le noyau a allouer reellement la memoire
    memset(dataprof->data, 0, PROFILAGE_TAILLE_INIT * sizeof(char));
    
    dataprof->length = PROFILAGE_TAILLE_INIT;
    dataprof->pos = 0;
}

void evenementProfilage(InfosProfilage *dataprof, unsigned int type){
    if(PROFILAGE_ACTIF == 0){
        return;
    }

    // Obtention du temps courant
    struct timespec temps_courant;
    clock_gettime(CLOCK_MONOTONIC, &temps_courant);
    double multiplier = 1000000000.;
    int c;

    // Si l'etat du programme n'a pas change, on a rien a faire
    if(type == dataprof->dernier_etat){
        return;
    }

    dataprof->dernier_etat = type;

    // Si le buffer n'est pas assez grand (peu probable, mais on ne veut pas causer
    // une erreur de segmentation si ca arrive)
    if(dataprof->pos + 100 > dataprof->length){
        dataprof->data = (char*)realloc(dataprof->data, dataprof->length*2 * sizeof(char));
        dataprof->length *= 2;
    }

    // On cree la ligne indiquant l'evenement et le temps associe
    c = sprintf(dataprof->data + dataprof->pos, "%u,%f\n", type, multiplier * temps_courant.tv_sec + temps_courant.tv_nsec);
    dataprof->pos += c;

    // On ecrit dans le fichier a intervalles reguliers
    if(dataprof->derniere_sauvegarde == 0 || temps_courant.tv_sec - dataprof->derniere_sauvegarde > PROFILAGE_INTERVALLE_SAUVEGARDE_SEC){
        dataprof->derniere_sauvegarde = temps_courant.tv_sec;
        fwrite(dataprof->data, dataprof->pos, 1, dataprof->fd);
        fflush(dataprof->fd);
        dataprof->pos = 0;
    }
}
