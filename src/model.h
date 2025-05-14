#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "cpu_debug.h"
#include "savestate.h"

typedef enum
{
    FDC_NONE,
    FDC_I8271,
    FDC_ACORN,
    FDC_MASTER,
    FDC_OPUS,
    FDC_STL,
    FDC_WATFORD,
    FDC_MAX
} fdc_type_t;

typedef struct
{
    char name[10];
    void (*func)(void);
} rom_setup_t;

typedef struct
{
    const char *cfgsect;
    const char *name;
    const char *os;
    const char *cmos;
    const char *group;
    rom_setup_t *romsetup;
    fdc_type_t fdc_type;
    uint8_t x65c02:1;
    uint8_t bplus:1;
    uint8_t master:1;
    uint8_t modela:1;
    uint8_t os01:1;
    uint8_t compact:1;
    uint8_t integra:1;
    uint8_t boot_logo;
    uint8_t kbdips;
    int tube;
} MODEL;

extern MODEL *models;
extern int model_count;
extern const char **model_groups;
extern int model_ngroup;

typedef struct
{
    char name[32];
    bool (*init)(void *rom);
    void (*reset)(void);
    cpu_debug_t *debug;
} TUBE_CPU;

typedef struct
{
    const TUBE_CPU *cpu;
    const char *cfgsect;
    const char *name;
    uint_least32_t rom_size;
    const char *bootrom;
    double speed_multiplier;
} TUBE_MODEL;

extern TUBE_MODEL *tubes;
extern int num_tubes;

extern int curmodel, curtube, oldmodel, selecttube;
extern fdc_type_t fdc_type;
extern bool BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos, integra, weramrom;

void model_loadcfg(void);
void model_check(void);
void model_init(void);
void model_savestate(FILE *f);
void model_loadstate(FILE *f);
void model_savecfg(void);

#endif
