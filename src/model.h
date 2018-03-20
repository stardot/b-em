#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "cpu_debug.h"

void model_check(void);
void model_init(void);
void model_save(ALLEGRO_CONFIG *bem_cfg);
char *model_get(void);

typedef struct
{
        char name[32];
        int I8271,WD1770;
        int x65c02;
        int bplus;
        int master;
        int modela;
        int os01;
        int compact;
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
} TUBE;

#define NUM_TUBES 7
extern TUBE tubes[NUM_TUBES];

extern int curmodel, curtube, oldmodel, selecttube;
extern int I8271, WD1770, BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;

#endif
