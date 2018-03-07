#include "b-em.h"

#include "model.h"
#include "config.h"
#include "cmos.h"
#include "mem.h"
#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "wd1770.h"
#include "x86_tube.h"
#include "z80.h"

int I8271, WD1770, BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;
int curtube;
int oldmodel;

MODEL models[NUM_MODELS] =
{
/*       Name                        8271  1770             65c02  B+  Master  A  OS 0.1  Compact  Config Section    OS         BASIC      DFS ROM     CMOS       ROM setup function    Second processor*/
        {"BBC A w/OS 0.1",            0,    WD1770_NONE,    0,     0,  0,      1, 1,      0,       "bbc_a_os01",     "os01",    "basic1",  "",         "",        mem_romsetup_os01,    -1},
        {"BBC B w/OS 0.1",            0,    WD1770_NONE,    0,     0,  0,      0, 1,      0,       "bbc_b_os01",     "os01",    "basic1",  "",         "",        mem_romsetup_os01,    -1},
        {"BBC A",                     1,    WD1770_NONE,    0,     0,  0,      1, 0,      0,       "bbc_a",          "os12",    "basic2",  "",         "",        mem_romsetup_std,     -1},
        {"BBC B w/8271 FDC",          1,    WD1770_NONE,    0,     0,  0,      0, 0,      0,       "bbc_b_8271",     "os12",    "basic2",  "dfs09",    "",        mem_romsetup_std,     -1},
        {"BBC B w/8271+SWRAM",        1,    WD1770_NONE,    0,     0,  0,      0, 0,      0,       "bbc_b_swram",    "os12",    "basic2",  "dfs09",    "",        mem_romsetup_swram,   -1},
        {"BBC B w/1770 FDC",          0,    WD1770_ACORN,   0,     0,  0,      0, 0,      0,       "bbc_b_1770",     "os12",    "basic2",  "dfs226",   "",        mem_romsetup_swram,   -1},
        {"BBC B US",                  1,    WD1770_NONE,    0,     0,  0,      0, 0,      0,       "bbc_b_us",       "usmos",   "usbasic", "usdnfs",   "",        mem_romsetup_std,     -1},
        {"BBC B German",              1,    WD1770_NONE,    0,     0,  0,      0, 0,      0,       "bbc_b_de",       "deos",    "usbasic", "usdnfs",   "",        mem_romsetup_std,     -1},
        {"BBC B+ 64K",                0,    WD1770_ACORN,   0,     1,  0,      0, 0,      0,       "bbc_b+64",       "bpos",    "basic2",  "dfs226",   "",        mem_romsetup_std,     -1},
        {"BBC B+ 128K",               0,    WD1770_ACORN,   0,     1,  0,      0, 0,      0,       "bbc_b+128",      "bpos",    "basic2",  "dfs226",   "",        mem_romsetup_bp128,   -1},
        {"BBC Master 128",            0,    WD1770_MASTER,  1,     0,  1,      0, 0,      0,       "master_128",     "mos320",  "",        "",         "cmos",    mem_romsetup_master,  -1},
        {"BBC Master 512",            0,    WD1770_MASTER,  1,     0,  1,      0, 0,      0,       "master_512",     "mos320",  "",        "",         "cmos",    mem_romsetup_master,   3},
        {"BBC Master Turbo",          0,    WD1770_MASTER,  1,     0,  1,      0, 0,      0,       "master_turbo",   "mos320",  "",        "",         "cmos",    mem_romsetup_master,   0},
        {"BBC Master Compact",        0,    WD1770_MASTER,  1,     0,  1,      0, 0,      1,       "master_compact", "os51",    "basic48", "adfs210",  "cmosc",   mem_romsetup_compact, -1},
        {"ARM Evaluation System",     0,    WD1770_MASTER,  1,     0,  1,      0, 0,      0,       "master_arm",     "mos320",  "",        "",         "cmosa",   mem_romsetup_master,   1},
        {"BBC Master 128 w/MOS 3.5",  0,    WD1770_MASTER,  1,     0,  1,      0, 0,      0,       "master_os350",   "mos350",  "",        "",         "cmos350", mem_romsetup_master,  -1},
        {"BBC B wo/FDC w/SWRAM",      0,    WD1770_NONE,    0,     0,  0,      0, 0,      0,       "bbc_b_nofdc",    "os12",    "basic2",  "",         "",        mem_romsetup_swram,   -1},
        {"BBC B w/Solidisk 1770 FDC", 0,    WD1770_STL,     0,     0,  0,      0, 0,      0,       "bbc_b_solidisk", "os12",    "basic2",  "stldfs21", "",        mem_romsetup_swram,   -1},
        {"BBC B w/Opus 1770 FDC",     0,    WD1770_OPUS,    0,     0,  0,      0, 0,      0,       "bbc_b_opus",     "os12",    "basic2",  "oddos345", "",        mem_romsetup_swram,   -1},
        {"BBC B w/Watford 1770 FDC",  0,    WD1770_WATFORD, 0,     0,  0,      0, 0,      0,       "bbc_b_watford",  "os12",    "basic2",  "wddfs153", "",        mem_romsetup_swram,   -1},
        {"BBC B with 65C02, no FDC",  0,    WD1770_NONE,    1,     0,  0,      0, 0,      0,       "bbc_b_65c02",    "os12",    "basic4",  "",         "",        mem_romsetup_swram,   -1},
        {"BBC B, 65C02, Acorn 1770",  0,    WD1770_ACORN,   1,     0,  0,      0, 0,      0,       "bbc_b_c1770",    "os12",    "basic4",  "dfs226",   "",        mem_romsetup_swram,   -1}
};

static int _modelcount = 0;
char *model_get()
{
        return models[_modelcount++].name;
}

extern cpu_debug_t n32016_cpu_debug;

TUBE tubes[NUM_TUBES]=
{
        {"6502", tube_6502_init,  tube_6502_reset, &tube6502_cpu_debug  },
        {"ARM",  tube_arm_init,   arm_reset,       &tubearm_cpu_debug   },
        {"Z80",  tube_z80_init,   z80_reset,       &tubez80_cpu_debug   },
        {"80186",tube_x86_init,   x86_reset,       &tubex86_cpu_debug   },
        {"65816",tube_65816_init, w65816_reset,    &tube65816_cpu_debug },
        {"32016",tube_32016_init, n32016_reset,    &n32016_cpu_debug    },
        {"",0,0}
};

void model_check(void) {
    const int defmodel = 3;

    if (curmodel < 0 || curmodel >= NUM_MODELS) {
        log_warn("No model #%d, using #%d (%s) instead", curmodel, defmodel, models[defmodel].name);
        curmodel = defmodel;
    }
    if (models[curmodel].tube != -1)
        curtube = models[curmodel].tube;
    else
        curtube = selecttube;
    if (curtube < -1 || curtube >= NUM_TUBES) {
        log_warn("No tube #%d, running with no tube instead", curtube);
        curtube = -1;
    }
}

void model_init()
{
        model_check();
        log_info("mem: starting emulation as %s", models[curmodel].name);
        set_config_string(models[curmodel].cfgsect, "name", models[curmodel].name);

        I8271       = models[curmodel].I8271;
        WD1770      = models[curmodel].WD1770;
        BPLUS       = models[curmodel].bplus;
        x65c02      = models[curmodel].x65c02;
        MASTER      = models[curmodel].master;
        MODELA      = models[curmodel].modela;
        OS01        = models[curmodel].os01;
        compactcmos = models[curmodel].compact;

        mem_clearroms();
        models[curmodel].romsetup();

        if (curtube!=-1)
            tubes[curtube].init();
        tube_reset();

        cmos_load(models[curmodel]);
}

void model_save(void) {
    const char *sect = models[curmodel].cfgsect;
    
    set_config_string(sect, "name", models[curmodel].name);
    mem_save_romcfg(sect);
}
