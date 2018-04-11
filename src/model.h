#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "cpu_debug.h"
#include "savestate.h"

void model_check(void);
void model_init(void);
void model_save(ALLEGRO_CONFIG *bem_cfg);
char *model_get(void);

typedef enum
{
    FDC_NONE,
    FDC_I8271,
    FDC_ACORN,
    FDC_MASTER,
    FDC_OPUS,
    FDC_STL,
    FDC_WATFORD
} fdc_type_t;

typedef struct
{
    char name[32];
    fdc_type_t fdc_type;
    uint8_t x65c02:1;
    uint8_t bplus:1;
    uint8_t master:1;
    uint8_t modela:1;
    uint8_t os01:1;
    uint8_t compact:1;
    char cfgsect[16];
    char os[16];
    char basic[16];
    char dfs[16];
    char cmos[16];
    void (*romsetup)(void);
    int tube;
} MODEL;

#define NUM_MODELS 22
extern MODEL models[NUM_MODELS];

typedef struct
{
    char name[32];
    void (*init)(FILE *romf);
    void (*reset)(void);
    cpu_debug_t *debug;
    char bootrom[16];
    int  speed_multiplier;
} TUBE;

#define NUM_TUBES 7
extern TUBE tubes[NUM_TUBES];

extern int curmodel, curtube, oldmodel, selecttube;
extern fdc_type_t fdc_type;
extern int BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;

#endif
