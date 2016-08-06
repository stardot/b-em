#include <allegro.h>
#include "b-em.h"

#include "model.h"
#include "cmos.h"
#include "mem.h"
#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"

int I8271, WD1770, BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;
int curtube;
int oldmodel;

MODEL models[17] =
{
/*       Name                        8271  1770  65c02  B+  Master  SWRAM  A  OS 0.1  Compact  OS      ROM dir   CMOS           ROM setup function         Second processor*/
        {"BBC A w/OS 0.1",           1,    0,    0,     0,  0,      0,     1, 1,      0,       "",     "a01",    "",            mem_romsetup_os01,         -1},
        {"BBC B w/OS 0.1",           1,    0,    0,     0,  0,      0,     0, 1,      0,       "",     "a01",    "",            mem_romsetup_os01,         -1},
        {"BBC A",                    1,    0,    0,     0,  0,      0,     1, 0,      0,       "os",   "a",      "",            NULL,                      -1},
        {"BBC B w/8271 FDC",         1,    0,    0,     0,  0,      0,     0, 0,      0,       "os",   "b",      "",            NULL,                      -1},
        {"BBC B w/8271+SWRAM",       1,    0,    0,     0,  0,      1,     0, 0,      0,       "os",   "b",      "",            NULL,                      -1},
        {"BBC B w/1770 FDC",         0,    1,    0,     0,  0,      1,     0, 0,      0,       "os",   "b1770",  "",            NULL,                      -1},
        {"BBC B US",                 1,    0,    0,     0,  0,      0,     0, 0,      0,       "usmos","us",     "",            NULL,                      -1},
        {"BBC B German",             1,    0,    0,     0,  0,      0,     0, 0,      0,       "deos", "us",     "",            NULL,                      -1},
        {"BBC B+ 64K",               0,    1,    0,     1,  0,      0,     0, 0,      0,       "bpos", "bp",     "",            NULL,                      -1},
        {"BBC B+ 128K",              0,    1,    0,     1,  0,      0,     0, 0,      0,       "bpos", "bp",     "",            mem_romsetup_bplus128,     -1},
        {"BBC Master 128",           0,    1,    1,     0,  1,      0,     0, 0,      0,       "",     "master", "cmos.bin",    mem_romsetup_master128,    -1},
        {"BBC Master 512",           0,    1,    1,     0,  1,      0,     0, 0,      0,       "",     "master", "cmos.bin",    mem_romsetup_master128,     3},
        {"BBC Master Turbo",         0,    1,    1,     0,  1,      0,     0, 0,      0,       "",     "master", "cmos.bin",    mem_romsetup_master128,     0},
        {"BBC Master Compact",       0,    1,    1,     0,  1,      0,     0, 0,      1,       "",     "compact","cmosc.bin",   mem_romsetup_mastercompact,-1},
        {"ARM Evaluation System",    0,    1,    1,     0,  1,      0,     0, 0,      0,       "",     "master", "cmosa.bin",   mem_romsetup_master128,     1},
        {"BBC Master 128 w/MOS 3.5", 0,    1,    1,     0,  1,      0,     0, 0,      0,       "",     "master", "cmos350.bin", mem_romsetup_master128_35, -1},
        {"",0,0,0,0,0,0,0,0,0,"","","",0,0}
};

static int _modelcount = 0;
char *model_get()
{
        return models[_modelcount++].name;
}

TUBE tubes[7]=
{
        {"6502", tube_6502_init,  tube_6502_reset},
        {"ARM",  tube_arm_init,   arm_reset},
        {"Z80",  tube_z80_init,   z80_reset},
        {"80186",tube_x86_init,   x86_reset},
        {"65816",tube_65816_init, w65816_reset},
        {"32016",tube_32016_init, n32016_reset},
        {"",0,0}
};

void model_init()
{
        char t[512],t2[512];
        bem_debugf("Starting emulation as %s\n",models[curmodel].name);
        I8271       = models[curmodel].I8271;
        WD1770      = models[curmodel].WD1770;
        BPLUS       = models[curmodel].bplus;
        x65c02      = models[curmodel].x65c02;
        MASTER      = models[curmodel].master;
        MODELA      = models[curmodel].modela;
        OS01        = models[curmodel].os01;
        compactcmos = models[curmodel].compact;

        curtube = selecttube;
        if (models[curmodel].tube != -1) curtube = models[curmodel].tube;


        getcwd(t, 511);
        append_filename(t2, exedir, "roms", 511);
        chdir(t2);
        mem_clearroms();
        if (models[curmodel].romsetup)
        {
                if (models[curmodel].romsetup())
                        exit(-1);
        }

        mem_loadroms(models[curmodel].os, models[curmodel].romdir);
//        if (ideenable) loadiderom();
        if (curtube!=-1) tubes[curtube].init();
        tube_reset();
        chdir(t);

        cmos_load(models[curmodel]);
        if (models[curmodel].swram) mem_fillswram();
}
