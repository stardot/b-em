#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "cpu_debug.h"

void model_check(void);
void model_init();
char *model_get();

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
        void (*romsetup)();
        int tube;
} MODEL;

#define NUM_MODELS 20
extern MODEL models[NUM_MODELS];

typedef struct
{
        char name[32];
        void (*init)();
        void (*reset)();
        cpu_debug_t *debug;
} TUBE;

#define NUM_TUBES 7
extern TUBE tubes[NUM_TUBES];

extern int curmodel, curtube, oldmodel, selecttube;
extern int I8271, WD1770, BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;

#endif
