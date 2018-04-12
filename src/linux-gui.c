/*B-em v2.2 by Tom Walker
  Linux GUI*/

#ifndef WIN32
#include <allegro.h>
#include "b-em.h"
#include "resources.h"

#include "config.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "ide.h"
#include "keyboard.h"
#include "linux-gui.h"
#include "main.h"
#include "mem.h"
#include "midi-linux.h"
#include "model.h"
#include "mouse.h"
#include "music5000.h"
#include "savestate.h"
#include "scsi.h"
#include "sid_b-em.h"
#include "sound.h"
#include "sn76489.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"

#define ROM_LABEL_LEN 50

#if defined(HAVE_JACK_JACK_H) || defined(HAVE_ALSA_ASOUNDLIB_H)
#define HAVE_MIDI
#endif

#undef printf

int timerspeeds[] = {5, 12, 25, 38, 50, 75, 100, 150, 200, 250};
int frameskips[]  = {0, 0,  0,  0,  0,  0,  1,   2,   3,   4};
int emuspeed = 4;

void setejecttext(int d, const char *s)
{
}

extern int quited;
extern int windx,windy;

MENU filemenu[6];
MENU discmenu[12];
MENU tapespdmenu[3];
MENU tapemenu[5];
MENU modelmenu[NUM_MODELS+1];
MENU tubespdmenu[6];
#ifdef NS32016
MENU tubemenu[7];
#else
MENU tubemenu[6];
#endif
MENU displaymenu[4];
MENU bordersmenu[4];
MENU videomenu[5];
MENU sidtypemenu[15];
MENU methodmenu[3];
MENU residmenu[3];
MENU waveformmenu[6];
MENU ddtypemenu[3];
MENU ddvolmenu[4];
MENU soundmenu[12];
MENU keymenu[3];
#ifdef HAVE_MIDI
static MENU m4000menu[4];
static MENU m2000out1menu[4];
static MENU m2000out2menu[4];
static MENU m2000out3menu[4];
static MENU midimenu[5];
#endif
MENU mousemenu[2];
MENU hdiskmenu[4];
MENU settingsmenu[9];
MENU miscmenu[6];
MENU speedmenu[11];
MENU mainmenu[7];

#ifdef HAVE_MIDI

static void gui_m4000_update(void) {
    int i = 0;
#ifdef HAVE_JACK_JACK_H
    m4000menu[i++].flags = (midi_music4000.jack_enabled) ? D_SELECTED : 0;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    m4000menu[i++].flags = (midi_music4000.alsa_seq_enabled) ? D_SELECTED : 0;
    m4000menu[i++].flags = (midi_music4000.alsa_raw_enabled) ? D_SELECTED : 0;
#endif
}

static void gui_m2000_out1_update(void) {
    int i = 0;
#ifdef HAVE_JACK_JACK_H
    m2000out1menu[i++].flags = (midi_music2000_out1.jack_enabled) ? D_SELECTED : 0;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    m2000out1menu[i++].flags = (midi_music2000_out1.alsa_seq_enabled) ? D_SELECTED : 0;
    m2000out1menu[i++].flags = (midi_music2000_out1.alsa_raw_enabled) ? D_SELECTED : 0;
#endif
}

static void gui_m2000_out2_update(void) {
    int i = 0;
#ifdef HAVE_JACK_JACK_H
    m2000out2menu[i++].flags = (midi_music2000_out2.jack_enabled) ? D_SELECTED : 0;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    m2000out2menu[i++].flags = (midi_music2000_out2.alsa_seq_enabled) ? D_SELECTED : 0;
    m2000out2menu[i++].flags = (midi_music2000_out2.alsa_raw_enabled) ? D_SELECTED : 0;
#endif
}

static void gui_m2000_out3_update(void) {
    int i = 0;
#ifdef HAVE_JACK_JACK_H
    m2000out3menu[i++].flags = (midi_music2000_out3.jack_enabled) ? D_SELECTED : 0;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    m2000out3menu[i++].flags = (midi_music2000_out3.alsa_seq_enabled) ? D_SELECTED : 0;
    m2000out3menu[i++].flags = (midi_music2000_out3.alsa_raw_enabled) ? D_SELECTED : 0;
#endif
}

#endif

void gui_update()
{
        int x;
        discmenu[5].flags = (writeprot[0]) ? D_SELECTED : 0;
        discmenu[6].flags = (writeprot[1]) ? D_SELECTED : 0;
        discmenu[7].flags = (defaultwriteprot) ? D_SELECTED : 0;
        discmenu[9].flags = (vdfs_enabled) ? D_SELECTED : 0;
        tapespdmenu[0].flags = (!fasttape) ? D_SELECTED : 0;
        tapespdmenu[1].flags = (fasttape)  ? D_SELECTED : 0;
        for (x = 0; x < NUM_MODELS; x++) modelmenu[x].flags = 0;
        for (x = 0; x < NUM_MODELS; x++) {
            if (curmodel == (intptr_t)modelmenu[x].dp)
                modelmenu[x].flags = D_SELECTED;
        }
        #ifdef NS32016
        for (x = 0; x < 5; x++)  tubemenu[x].flags = (selecttube == (intptr_t)tubemenu[x].dp) ? D_SELECTED : 0;
        #else
        for (x = 0; x < 4; x++)  tubemenu[x].flags = (selecttube == (intptr_t)tubemenu[x].dp) ? D_SELECTED : 0;
        #endif
        for (x = 0; x < 5; x++)  tubespdmenu[x].flags = (tube_6502_speed == (intptr_t)tubespdmenu[x].dp) ? D_SELECTED : 0;
        displaymenu[0].flags = (vid_linedbl)   ? D_SELECTED : 0;
        displaymenu[1].flags = (vid_scanlines) ? D_SELECTED : 0;
        displaymenu[2].flags = (vid_interlace) ? D_SELECTED : 0;
        videomenu[2].flags = (fullscreen) ? D_SELECTED : 0;
        videomenu[3].flags = (nula_disable) ? 0 : D_SELECTED;
        for (x = 0; x < 3; x++)  bordersmenu[x].flags = (vid_fullborders == (intptr_t)bordersmenu[x].dp) ? D_SELECTED : 0;
        soundmenu[0].flags = (sound_internal) ? D_SELECTED : 0;
        soundmenu[1].flags = (sound_beebsid)  ? D_SELECTED : 0;
        soundmenu[2].flags = (sound_music5000)? D_SELECTED : 0;
        soundmenu[3].flags = (sound_dac)      ? D_SELECTED : 0;
        soundmenu[4].flags = (sound_ddnoise)  ? D_SELECTED : 0;
        soundmenu[5].flags = (sound_tape)     ? D_SELECTED : 0;
        soundmenu[6].flags = (sound_filter)   ? D_SELECTED : 0;
        for (x = 0; x < 5;  x++) waveformmenu[x].flags = (curwave == (intptr_t)waveformmenu[x].dp) ? D_SELECTED : 0;
        for (x = 0; x < 14; x++) sidtypemenu[x].flags  = (cursid  == (intptr_t)sidtypemenu[x].dp)  ? D_SELECTED : 0;
        methodmenu[0].flags = (!sidmethod)    ? D_SELECTED : 0;
        methodmenu[1].flags = (sidmethod)     ? D_SELECTED : 0;
        ddtypemenu[0].flags = (!ddnoise_type) ? D_SELECTED : 0;
        ddtypemenu[1].flags = (ddnoise_type)  ? D_SELECTED : 0;
        for (x = 0; x < 3; x++)  ddvolmenu[x].flags = (ddnoise_vol == (intptr_t)ddvolmenu[x].dp) ? D_SELECTED : 0;
#ifdef HAVE_MIDI
        gui_m4000_update();
        gui_m2000_out1_update();
        gui_m2000_out2_update();
        gui_m2000_out3_update();
#endif
        keymenu[1].flags = (keyas) ? D_SELECTED : 0;
        mousemenu[0].flags = (mouse_amx) ? D_SELECTED : 0;
        for (x = 0; x < 10; x++) speedmenu[x].flags = (emuspeed == (intptr_t)speedmenu[x].dp) ? D_SELECTED : 0;
        hdiskmenu[1].flags = (ide_enable) ? D_SELECTED : 0;
        hdiskmenu[2].flags = (scsi_enabled) ? D_SELECTED : 0;
}

int gui_keydefine();

int gui_return()
{
        return D_CLOSE;
}

int gui_reset()
{
        main_restart();
        return D_CLOSE;
}

int gui_exit()
{
        quited=1;
        return D_CLOSE;
}

int gui_loadss()
{
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        memcpy(tempname, discfns[0], sizeof tempname);
        ret = file_select_ex("Please choose a save state", tempname, "SNP", 260, xsize, ysize);
        if (ret)
        {
                strncpy(savestate_name, tempname, sizeof savestate_name);
                savestate_load();
        }
        gui_update();
        return D_CLOSE;
}

int gui_savess()
{
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        if (curtube != -1)
        {
                alert(NULL, "Second processor save states not supported yet.", NULL, "&OK", NULL, 0, 0);
                return D_CLOSE;
        }
        memcpy(tempname, discfns[0], sizeof tempname);
        ret = file_select_ex("Please choose a save state", tempname, "SNP", 260, xsize, ysize);
        if (ret)
        {
                strncpy(savestate_name, tempname, sizeof savestate_name);
                savestate_save();
        }
        gui_update();
        return D_CLOSE;
}

MENU filemenu[6]=
{
        {"&Return",     gui_return, NULL, 0, NULL},
        {"&Hard reset", gui_reset,  NULL, 0, NULL},
        {"&Load state", gui_loadss, NULL, 0, NULL},
        {"&Save state", gui_savess, NULL, 0, NULL},
        {"&Exit",       gui_exit,   NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

static int gui_load_drive(int drive, const char *prompt)
{
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        memcpy(tempname, discfns[drive], sizeof tempname);
        ret = file_select_ex(prompt, tempname, "SSD;DSD;IMG;ADF;ADL;FDI", 260, xsize, ysize);
        if (ret)
        {
            ALLEGRO_PATH *path = al_create_path(tempname);
            disc_close(drive);
            disc_load(drive, path);
            al_destroy_path(path);
            if (defaultwriteprot)
                writeprot[drive] = 1;
        }
        return ret;
}

int gui_autoboot()
{
        int ret;

        if ((ret = gui_load_drive(0, "Please choose a disc image to autoboot in drive 0/2")))
        {
                main_reset();
                autoboot = 150;
        }
        gui_update();
        return D_CLOSE;
}

int gui_load0()
{
        gui_load_drive(0, "Please choose a disc image to load in drive 0/2");
        gui_update();
        return D_CLOSE;
}

int gui_load1()
{
        gui_load_drive(1, "Please choose a disc image to load in drive 1/3");
        gui_update();
        return D_CLOSE;
}

int gui_eject0()
{
    disc_close(0);
    return D_CLOSE;
}

int gui_eject1()
{
    disc_close(1);
    return D_CLOSE;
}

int gui_wprot0()
{
        writeprot[0] = !writeprot[0];
        if (fwriteprot[0]) fwriteprot[0] = 1;
        gui_update();
        return D_CLOSE;
}
int gui_wprot1()
{
        writeprot[1] = !writeprot[1];
        if (fwriteprot[1]) fwriteprot[1] = 1;
        gui_update();
        return D_CLOSE;
}
int gui_wprotd()
{
        defaultwriteprot = !defaultwriteprot;
        gui_update();
        return D_CLOSE;
}

int gui_hdisk()
{
        intptr_t sel = (intptr_t)active_menu->dp;
        int changed = 0;

        if (ide_enable)
        {
                if (sel != 0)
                {
                        ide_enable = false;
                        changed = 1;
                }
        }
        else
        {
                if (sel == 0)
                {
                        ide_enable = true;
                        changed = 1;
                }
        }
        if (scsi_enabled)
        {
                if (sel != 1)
                {
                        scsi_enabled = false;
                        changed = 1;
                }
        }
        else
        {
                if (sel == 1)
                {
                        scsi_enabled = true;
                        changed = 1;
                }
        }
        if (changed)
                main_reset();
        gui_update();
        return D_CLOSE;
}

MENU hdiskmenu[4]=
{
        {"None",gui_hdisk,NULL,0,(void *)-1},
        {"IDE", gui_hdisk,NULL,0,(void *)0},
        {"SCSI", gui_hdisk,NULL,0,(void *)1},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_vdfs_en() {
        vdfs_enabled = !vdfs_enabled;
        gui_update();
        return D_CLOSE;
}

int gui_vdfs_root() {
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        strncpy(tempname, vdfs_get_root(), sizeof(tempname));
        memcpy(tempname, discfns[0], sizeof tempname);
        ret = file_select_ex("Please select VDFS root directory", tempname, NULL, 260, xsize, ysize);
        if (ret)
            vdfs_set_root(tempname);
        gui_update();
        return D_CLOSE;
}

MENU discmenu[12]=
{
        {"Autoboot disc in 0/2",    gui_autoboot,  NULL, 0, NULL },
        {"Load disc :&0/2...",      gui_load0,     NULL, 0, NULL},
        {"Load disc :&1/3...",      gui_load1,     NULL, 0, NULL},
        {"Eject disc :0/2",         gui_eject0,    NULL, 0, NULL},
        {"Eject disc :1/3",         gui_eject1,    NULL, 0, NULL},
        {"Write protect disc :0/2", gui_wprot0,    NULL, 0, NULL},
        {"Write protect disc :1/3", gui_wprot1,    NULL, 0, NULL},
        {"Default write protect",   gui_wprotd,    NULL, 0, NULL},
        {"&Hard Disc",              NULL, hdiskmenu,  0, NULL},
        {"Enable VDFS",             gui_vdfs_en,   NULL, 0, NULL},
        {"Choose VDFS Root",        gui_vdfs_root, NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_normal()
{
        fasttape = false;
        gui_update();
        return D_CLOSE;
}
int gui_fast()
{
        fasttape = true;
        gui_update();
        return D_CLOSE;
}

MENU tapespdmenu[3]=
{
        {"Normal", gui_normal, NULL, 0, NULL},
        {"Fast",   gui_fast,   NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_loadt()
{
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        memcpy(tempname, al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP), 260);
        ret=file_select_ex("Please choose a tape image", tempname, "UEF;CSW", 260, xsize, ysize);
        if (ret)
        {
            tape_close();
            tape_fn = al_create_path(tempname);
            tape_load(tape_fn);
            tape_loaded = 1;
        }
        return D_CLOSE;
}

int gui_rewind()
{
        tape_close();
        tape_load(tape_fn);
        return D_CLOSE;
}

int gui_ejectt()
{
        tape_close();
        tape_loaded = 0;
        return D_CLOSE;
}

MENU tapemenu[]=
{
        {"Load tape...", gui_loadt,  NULL,        0, NULL},
        {"Rewind tape",  gui_rewind, NULL,        0, NULL},
        {"Eject tape",   gui_ejectt, NULL,        0, NULL},
        {"Tape speed",   NULL,       tapespdmenu, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_model()
{
        model_save();
        oldmodel = curmodel;
        curmodel = (intptr_t)active_menu->dp;
        main_restart();
        gui_update();
        return D_CLOSE;
}

MENU modelmenu[NUM_MODELS+1]=
{
        {"BBC A w/OS 0.1",            gui_model, NULL, 0, (void *)0},
        {"BBC B w/OS 0.1",            gui_model, NULL, 0, (void *)1},
        {"BBC A",                     gui_model, NULL, 0, (void *)2},
        {"BBC B w/8271 FDC",          gui_model, NULL, 0, (void *)3},
        {"BBC B w/8271+SWRAM",        gui_model, NULL, 0, (void *)4},
        {"BBC B wo/FDC w/SWRAM",      gui_model, NULL, 0, (void *)16},
        {"BBC B w/Acorn 1770 FDC",    gui_model, NULL, 0, (void *)5},
        {"BBC B w/Opus 1770 FDC",     gui_model, NULL, 0, (void *)18},
        {"BBC B w/Solidisk 1770 FDC", gui_model, NULL, 0, (void *)17},
        {"BBC B w/Watford 1770 FDC",  gui_model, NULL, 0, (void *)19},
        {"BBC B with 65C02, no FDC",  gui_model, NULL, 0, (void *)20},
        {"BBC B 65C02, Acorn 1770",   gui_model, NULL, 0, (void *)21},
        {"BBC B US",                  gui_model, NULL, 0, (void *)6},
        {"BBC B German",              gui_model, NULL, 0, (void *)7},
        {"BBC B+ 64K",                gui_model, NULL, 0, (void *)8},
        {"BBC B+ 128K",               gui_model, NULL, 0, (void *)9},
        {"BBC Master 128",            gui_model, NULL, 0, (void *)10},
        {"BBC Master 128 w/MOS 3.50", gui_model, NULL, 0, (void *)15},
        {"BBC Master 512",            gui_model, NULL, 0, (void *)11},
        {"BBC Master Turbo",          gui_model, NULL, 0, (void *)12},
        {"BBC Master Compact",        gui_model, NULL, 0, (void *)13},
        {"ARM Evaluation System",     gui_model, NULL, 0, (void *)14},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_tubespd()
{
        tube_6502_speed = (intptr_t)active_menu->dp;
        tube_updatespeed();
        gui_update();
        return D_CLOSE;
}

MENU tubespdmenu[6]=
{
        {"4mhz",  gui_tubespd, NULL, 0, (void *)1},
        {"8mhz",  gui_tubespd, NULL, 0, (void *)2},
        {"16mhz", gui_tubespd, NULL, 0, (void *)3},
        {"32mhz", gui_tubespd, NULL, 0, (void *)4},
        {"64mhz", gui_tubespd, NULL, 0, (void *)5},
        {NULL, NULL, NULL, 0, NULL}
};


int gui_tube()
{
        selecttube = (intptr_t)active_menu->dp;
        main_restart();
        gui_update();
        return D_CLOSE;
}

#ifdef NS32016
MENU tubemenu[7]=
#else
MENU tubemenu[6]=
#endif
{
        {"None",gui_tube,NULL,0,(void *)-1},
        {"6502",gui_tube,NULL,0,(void *)0},
        {"65816",gui_tube,NULL,0,(void *)4},
        {"Z80",gui_tube,NULL,0,(void *)2},
#ifdef NS32016
        {"32016",gui_tube,NULL,0,(void *)5},
#endif
        {"6502 tube speed",NULL,tubespdmenu,0,NULL},
        {NULL,NULL,NULL,0,NULL}
};

int gui_linedbl()
{
        vid_linedbl = 1;
        vid_scanlines = vid_interlace = 0;
        gui_update();
        return D_CLOSE;
}
int gui_scanlines()
{
        vid_scanlines = 1;
        vid_linedbl = vid_interlace = 0;
        gui_update();
        return D_CLOSE;
}
int gui_interlaced()
{
        vid_interlace = 1;
        vid_linedbl = vid_scanlines = 0;
        gui_update();
        return D_CLOSE;
}

MENU displaymenu[4]=
{
        {"Line doubling",gui_linedbl,NULL,0,NULL},
        {"Scanlines",gui_scanlines,NULL,0,NULL},
        {"Interlaced",gui_interlaced,NULL,0,NULL},
        {NULL,NULL,NULL,0,NULL}
};

int gui_borders()
{
        vid_fullborders = (intptr_t)active_menu->dp;
        gui_update();
        return D_CLOSE;
}

MENU bordersmenu[4]=
{
        {"None", gui_borders,NULL,0,(void *)0},
        {"Small",gui_borders,NULL,0,(void *)1},
        {"Full", gui_borders,NULL,0,(void *)2},
        {NULL,NULL,NULL,0,NULL}
};

int gui_fullscreen()
{
        if (fullscreen)
        {
                fullscreen=0;
                video_leavefullscreen();
        }
        else
        {
                fullscreen=1;
                video_enterfullscreen();
        }
        return D_EXIT;
}

static int gui_video_nula()
{
    if (nula_disable)
        nula_disable = 0;
    else
        nula_disable = 1;
    return D_EXIT;
}

MENU videomenu[5] =
{
        {"Display type", NULL,           displaymenu, 0, NULL},
        {"Borders",      NULL,           bordersmenu, 0, NULL},
        {"Fullscreen",   gui_fullscreen, NULL,        0, NULL},
        {"NuLA",         gui_video_nula, NULL,        0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_sidtype()
{
        cursid = (intptr_t)active_menu->dp;
        sid_settype(sidmethod, cursid);
        gui_update();
        return D_CLOSE;
}

MENU sidtypemenu[15] =
{
        {"6581",                    gui_sidtype,NULL,0,(void *)SID_MODEL_6581},
        {"8580",                    gui_sidtype,NULL,0,(void *)SID_MODEL_8580},
        {"8580 + digi boost",       gui_sidtype,NULL,0,(void *)SID_MODEL_8580D},
        {"6581R4",                  gui_sidtype,NULL,0,(void *)SID_MODEL_6581R4},
        {"6581R3 4885",             gui_sidtype,NULL,0,(void *)SID_MODEL_6581R3_4885},
        {"6581R3 0486S",            gui_sidtype,NULL,0,(void *)SID_MODEL_6581R3_0486S},
        {"6581R3 3984",             gui_sidtype,NULL,0,(void *)SID_MODEL_6581R3_3984},
        {"6581R4AR 3789",           gui_sidtype,NULL,0,(void *)SID_MODEL_6581R4AR_3789},
        {"6581R3 4485",             gui_sidtype,NULL,0,(void *)SID_MODEL_6581R3_4485},
        {"6581R4 1986S",            gui_sidtype,NULL,0,(void *)SID_MODEL_6581R4_1986S},
        {"8580R5 3691",             gui_sidtype,NULL,0,(void *)SID_MODEL_8580R5_3691},
        {"8580R5 3691 + digi boost",gui_sidtype,NULL,0,(void *)SID_MODEL_8580R5_3691D},
        {"8580R5 1489",             gui_sidtype,NULL,0,(void *)SID_MODEL_8580R5_1489},
        {"8580R5 1489 + digi boost",gui_sidtype,NULL,0,(void *)SID_MODEL_8580R5_1489D},
        {NULL,NULL,NULL,0,NULL}
};

int gui_method()
{
        sidmethod = (intptr_t)active_menu->dp;
        sid_settype(sidmethod, cursid);
        gui_update();
        return D_CLOSE;
}

MENU methodmenu[3] =
{
        {"Interpolating", gui_method,NULL,0,(void *)0},
        {"Resampling",    gui_method,NULL,0,(void *)1},
        {NULL,NULL,NULL,0,NULL}
};

MENU residmenu[3] =
{
        {"Model",         NULL, sidtypemenu, 0, NULL},
        {"Sample method", NULL, methodmenu,  0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_waveform()
{
        curwave = (intptr_t)active_menu->dp;
        gui_update();
        return D_CLOSE;
}

MENU waveformmenu[6] =
{
        {"Square",   gui_waveform, NULL, 0, (void *)0},
        {"Saw",      gui_waveform, NULL, 0, (void *)1},
        {"Sine",     gui_waveform, NULL, 0, (void *)2},
        {"Triangle", gui_waveform, NULL, 0, (void *)3},
        {"SID",      gui_waveform, NULL, 0, (void *)4},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_ddtype()
{
        ddnoise_type = (intptr_t)active_menu->dp;
        ddnoise_close();
        ddnoise_init();
        gui_update();
        return D_CLOSE;
}

MENU ddtypemenu[3]=
{
        {"5.25", gui_ddtype, NULL, 0, (void *)0},
        {"3.5",  gui_ddtype, NULL, 0, (void *)1},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_ddvol()
{
        ddnoise_vol = (intptr_t)active_menu->dp;
        gui_update();
        return D_CLOSE;
}

MENU ddvolmenu[4]=
{
        {"33%",  gui_ddvol, NULL, 0, (void *)1},
        {"66%",  gui_ddvol, NULL, 0, (void *)2},
        {"100%", gui_ddvol, NULL, 0, (void *)3},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_internalsnd()
{
        sound_internal = !sound_internal;
        gui_update();
        return D_CLOSE;
}
int gui_beebsid()
{
        sound_beebsid = !sound_beebsid;
        gui_update();
        return D_CLOSE;
}
int gui_music5000()
{
        sound_music5000 = !sound_music5000;
        gui_update();
        return D_CLOSE;
}
int gui_dac()
{
        sound_dac = !sound_dac;
        gui_update();
        return D_CLOSE;
}
int gui_ddnoise()
{
        sound_ddnoise = !sound_ddnoise;
        gui_update();
        return D_CLOSE;
}
int gui_tnoise()
{
        sound_tape = !sound_tape;
        gui_update();
        return D_CLOSE;
}
int gui_filter()
{
        sound_filter = !sound_filter;
        gui_update();
        return D_CLOSE;
}

MENU soundmenu[12]=
{
        {"Internal sound chip",   gui_internalsnd, NULL,         0, NULL},
        {"BeebSID",               gui_beebsid,     NULL,         0, NULL},
        {"Music 5000",            gui_music5000,   NULL,         0, NULL},
        {"Printer Port DAC",      gui_dac,         NULL,         0, NULL},
        {"Disc drive noise",      gui_ddnoise,     NULL,         0, NULL},
        {"Tape noise",            gui_tnoise,      NULL,         0, NULL},
        {"Internal sound filter", gui_filter,      NULL,         0, NULL},
        {"Internal waveform",     NULL,            waveformmenu, 0, NULL},
        {"reSID configuration",   NULL,            residmenu,    0, NULL},
        {"Disc drive type",       NULL,            ddtypemenu,   0, NULL},
        {"Disc drive volume",     NULL,            ddvolmenu,    0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

#ifdef HAVE_MIDI

#ifdef HAVE_JACK_JACK_H

static int gui_m4000_jack(void) {
    midi_music4000.jack_enabled = !midi_music4000.jack_enabled;
    gui_m4000_update();
    return D_CLOSE;
}

static int gui_m2000_out1_jack(void) {
    midi_music2000_out1.jack_enabled = !midi_music2000_out1.jack_enabled;
    gui_m2000_out1_update();
    return D_CLOSE;
}

static int gui_m2000_out2_jack(void) {
    midi_music2000_out2.jack_enabled = !midi_music2000_out2.jack_enabled;
    gui_m2000_out2_update();
    return D_CLOSE;
}

static int gui_m2000_out3_jack(void) {
    midi_music2000_out3.jack_enabled = !midi_music2000_out3.jack_enabled;
    gui_m2000_out3_update();
    return D_CLOSE;
}

#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H

static int gui_m4000_alsa_seq(void) {
    midi_music4000.alsa_seq_enabled = !midi_music4000.alsa_seq_enabled;
    gui_m4000_update();
    return D_CLOSE;
}

static int gui_m2000_out1_alsa_seq(void) {
    midi_music2000_out1.alsa_seq_enabled = !midi_music2000_out1.alsa_seq_enabled;
    gui_m2000_out1_update();
    return D_CLOSE;
}

static int gui_m2000_out2_alsa_seq(void) {
    midi_music2000_out2.alsa_seq_enabled = !midi_music2000_out2.alsa_seq_enabled;
    gui_m2000_out2_update();
    return D_CLOSE;
}

static int gui_m2000_out3_alsa_seq(void) {
    midi_music2000_out3.alsa_seq_enabled = !midi_music2000_out3.alsa_seq_enabled;
    gui_m2000_out3_update();
    return D_CLOSE;
}

static int gui_m4000_alsa_raw(void) {
    midi_music4000.alsa_raw_enabled = !midi_music4000.alsa_raw_enabled;
    gui_m4000_update();
    return D_CLOSE;
}

static int gui_m2000_out1_alsa_raw(void) {
    midi_music2000_out1.alsa_raw_enabled = !midi_music2000_out1.alsa_raw_enabled;
    gui_m2000_out1_update();
    return D_CLOSE;
}

static int gui_m2000_out2_alsa_raw(void) {
    midi_music2000_out2.alsa_raw_enabled = !midi_music2000_out2.alsa_raw_enabled;
    gui_m2000_out2_update();
    return D_CLOSE;
}

static int gui_m2000_out3_alsa_raw(void) {
    midi_music2000_out3.alsa_raw_enabled = !midi_music2000_out3.alsa_raw_enabled;
    gui_m2000_out3_update();
    return D_CLOSE;
}

#endif

static MENU m4000menu[4]=
{
#ifdef HAVE_JACK_JACK_H
    {"JACK MIDI",      gui_m4000_jack,          NULL, 0, NULL},
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    {"ALSA Sequencer", gui_m4000_alsa_seq,      NULL, 0, NULL},
    {"ALSA Raw MIDI",  gui_m4000_alsa_raw,      NULL, 0, NULL},
#endif
        {NULL, NULL, NULL, 0, NULL}
};

static MENU m2000out1menu[4]=
{
#ifdef HAVE_JACK_JACK_H
    {"JACK MIDI",      gui_m2000_out1_jack,     NULL, 0, NULL},
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    {"ALSA Sequencer", gui_m2000_out1_alsa_seq, NULL, 0, NULL},
    {"ALSA Raw MIDI",  gui_m2000_out1_alsa_raw, NULL, 0, NULL},
#endif
        {NULL, NULL, NULL, 0, NULL}
};

static MENU m2000out2menu[4]=
{
#ifdef HAVE_JACK_JACK_H
    {"JACK MIDI",      gui_m2000_out2_jack,     NULL, 0, NULL},
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    {"ALSA Sequencer", gui_m2000_out2_alsa_seq, NULL, 0, NULL},
    {"ALSA Raw MIDI",  gui_m2000_out2_alsa_raw, NULL, 0, NULL},
#endif
        {NULL, NULL, NULL, 0, NULL}
};

static MENU m2000out3menu[4]=
{
#ifdef HAVE_JACK_JACK_H
    {"JACK MIDI",      gui_m2000_out3_jack,     NULL, 0, NULL},
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    {"ALSA Sequencer", gui_m2000_out3_alsa_seq, NULL, 0, NULL},
    {"ALSA Raw MIDI",  gui_m2000_out3_alsa_raw, NULL, 0, NULL},
#endif
        {NULL, NULL, NULL, 0, NULL}
};

static MENU midimenu[5]=
{
    {"Music 4000 Keyboard",  NULL, m4000menu,     0, NULL },
    {"Music 2000 I/F Out 1", NULL, m2000out1menu, 0, NULL },
    {"Music 2000 I/F Out 2", NULL, m2000out2menu, 0, NULL },
    {"Music 2000 I/F Out 3", NULL, m2000out3menu, 0, NULL },
    {NULL, NULL, NULL, 0, NULL}
};

#endif


int gui_mapas()
{
        keyas = !keyas;
        gui_update();
        return D_CLOSE;
}

MENU keymenu[3] =
{
        {"Redefine keyboard",     gui_keydefine, NULL, 0, NULL},
        {"&Map CAPS/CTRL to A/S", gui_mapas,     NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_mouseamx()
{
        mouse_amx = !mouse_amx;
        gui_update();
        return D_CLOSE;
}

MENU mousemenu[2] =
{
        {"&AMX mouse",     gui_mouseamx, NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};


MENU settingsmenu[9]=
{
        {"&Model",            NULL, modelmenu, 0, NULL},
        {"&Second processor", NULL, tubemenu,  0, NULL},
        {"&Video",            NULL, videomenu, 0, NULL},
        {"&Sound",            NULL, soundmenu, 0, NULL},
#ifdef HAVE_MIDI
        {"M&idi",             NULL, midimenu,  0, NULL},
#endif
        {"&Keyboard",         NULL, keymenu,   0, NULL},
        {"&Mouse",            NULL, mousemenu, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

int gui_scrshot()
{
        char tempname[260];
        int ret;
        int xsize = windx - 32, ysize = windy - 16;
        tempname[0] = 0;
        ret = file_select_ex("Please enter filename", tempname, "BMP", 260, xsize, ysize);
        if (ret)
        {
                memcpy(vid_scrshotname, tempname, 260);
                printf("Save scrshot\n");
                vid_savescrshot = 2;
        }
        return D_CLOSE;
}

int gui_speed()
{
        emuspeed = (intptr_t)active_menu->dp;
        changetimerspeed(timerspeeds[emuspeed]);
        vid_fskipmax = frameskips[emuspeed];
        gui_update();
        return D_CLOSE;
}

MENU speedmenu[11] =
{
        {"&10%",  gui_speed, NULL, 0, (void *)0},
        {"&25%",  gui_speed, NULL, 0, (void *)1},
        {"&50%",  gui_speed, NULL, 0, (void *)2},
        {"&75%",  gui_speed, NULL, 0, (void *)3},
        {"&100%", gui_speed, NULL, 0, (void *)4},
        {"&150%", gui_speed, NULL, 0, (void *)5},
        {"&200%", gui_speed, NULL, 0, (void *)6},
        {"&300%", gui_speed, NULL, 0, (void *)7},
        {"&400%", gui_speed, NULL, 0, (void *)8},
        {"&500%", gui_speed, NULL, 0, (void *)9},
        {NULL, NULL, NULL, 0, NULL}
};

static int gui_debug_core()
{
    debug_toggle_core();
    return D_CLOSE;
}

static int gui_debug_tube()
{
    debug_toggle_tube();
    return D_CLOSE;
}

static int gui_debug_break()
{
    debug_step = 1;
    return D_CLOSE;
}

MENU miscmenu[6] =
{
        {"&Speed", NULL,    speedmenu,             0, NULL},
        {"Save screenshot", gui_scrshot,     NULL, 0, NULL},
        {"&Debugger",       gui_debug_core,  NULL, 0, NULL},
        {"Debug &Tube",     gui_debug_tube,  NULL, 0, NULL},
        {"&Break",          gui_debug_break, NULL, 0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

static int gui_rom_togram(void) {
    rom_slot_t *slotp = active_menu->dp;

    if (!slotp->locked) {
        if (slotp->swram) {
            slotp->swram = 0;
            active_menu->flags &= ~D_SELECTED;
        } else {
            slotp->swram = 1;
            active_menu->flags |= D_SELECTED;
        }
    }
    return D_CLOSE;
}

static int gui_rom_clear(void) {
    rom_slot_t *slotp = active_menu->dp;

    if (!slotp->locked)
        mem_clearrom(slotp - rom_slots);
    return D_CLOSE;
}

static int gui_rom_load(void) {
    rom_slot_t *slotp = active_menu->dp;
    int ret, slot, xsize = windx - 32, ysize = windy - 16;
    char tempname[PATH_MAX];

    if (!slotp->locked) {
        if (slotp->name)
            strncpy(tempname, slotp->name, sizeof tempname-1);
        else
            tempname[0] = 0;
        ret = file_select_ex("Please choose a ROM image file", tempname, "ROM", sizeof tempname, xsize, ysize);
        if (ret) {
            slot = slotp - rom_slots;
            mem_clearrom(slot);
            mem_loadrom(slot, get_filename(tempname), tempname, 0);
        }
    }
    return D_CLOSE;
}

static MENU rom15menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 15 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 15 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 15 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom14menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 14 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 14 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 14 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom13menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 13 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 13 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 13 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom12menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 12 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 12 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 12 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom11menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 11 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 11 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 11 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom10menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 10 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 10 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 10 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom09menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 9 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 9 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 9 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom08menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 8 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 8 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 8 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom07menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 7 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 7 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 7 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom06menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 6 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 6 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 6 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom05menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 5 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 5 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 5 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom04menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 4 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 4 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 4 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom03menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 3 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 3 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 3 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom02menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 2 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 2 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 2 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom01menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 1 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 1 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 1 },
    {NULL, NULL, NULL, 0, NULL}
};

static MENU rom00menu[4] =
{
    { "RAM",   gui_rom_togram, NULL, 0, rom_slots + 0 },
    { "Clear", gui_rom_clear,  NULL, 0, rom_slots + 0 },
    { "Load",  gui_rom_load,   NULL, 0, rom_slots + 0 },
    {NULL, NULL, NULL, 0, NULL}
};

static char gui_rom_labels[ROM_NSLOT][ROM_LABEL_LEN];

static MENU rommenu[17] =
{
    { gui_rom_labels[15], NULL, rom15menu, 0, rom_slots + 15 },
    { gui_rom_labels[14], NULL, rom14menu, 0, rom_slots + 14 },
    { gui_rom_labels[13], NULL, rom13menu, 0, rom_slots + 13 },
    { gui_rom_labels[12], NULL, rom12menu, 0, rom_slots + 12 },
    { gui_rom_labels[11], NULL, rom11menu, 0, rom_slots + 11 },
    { gui_rom_labels[10], NULL, rom10menu, 0, rom_slots + 10 },
    { gui_rom_labels[9],  NULL, rom09menu, 0, rom_slots + 9 },
    { gui_rom_labels[8],  NULL, rom08menu, 0, rom_slots + 8 },
    { gui_rom_labels[7],  NULL, rom07menu, 0, rom_slots + 7 },
    { gui_rom_labels[6],  NULL, rom06menu, 0, rom_slots + 6 },
    { gui_rom_labels[5],  NULL, rom05menu, 0, rom_slots + 5 },
    { gui_rom_labels[4],  NULL, rom04menu, 0, rom_slots + 4 },
    { gui_rom_labels[3],  NULL, rom03menu, 0, rom_slots + 3 },
    { gui_rom_labels[2],  NULL, rom02menu, 0, rom_slots + 2 },
    { gui_rom_labels[1],  NULL, rom01menu, 0, rom_slots + 1 },
    { gui_rom_labels[0],  NULL, rom00menu, 0, rom_slots },
    {NULL, NULL, NULL, 0, NULL}
};

static void gui_init_rommenu(void) {
    int slot, ver;
    const uint8_t *detail;
    const char *rr, *name;
    MENU *menup;

    for (slot = 0; slot < ROM_NSLOT; slot++) {
        rr = rom_slots[slot].swram ? "RAM" : "ROM";
        detail = mem_romdetail(slot);
        name = rom_slots[slot].name;
        if (detail) {
            ver = *detail++;
            if (name)
                snprintf(gui_rom_labels[slot], ROM_LABEL_LEN, "%02d %s: %s %02X (%s)", slot, rr, detail, ver, name);
            else
                snprintf(gui_rom_labels[slot], ROM_LABEL_LEN, "%02d %s: %s %02X", slot, rr, detail, ver);
        } else {
            if (name)
                snprintf(gui_rom_labels[slot], ROM_LABEL_LEN, "%02d %s: %s", slot, rr, name);
            else
                snprintf(gui_rom_labels[slot], ROM_LABEL_LEN, "%02d %s", slot, rr);
        }
        menup = rommenu + ROM_NSLOT - 1 - slot;
        if (rom_slots[slot].swram)
            menup->child[0].flags |= D_SELECTED;
        else
            menup->child[0].flags &= ~D_SELECTED;
        if (rom_slots[slot].locked)
            menup->flags |= D_DISABLED;
        else
            menup->flags &= ~D_DISABLED;
    }
}

MENU mainmenu[7] =
{
        {"&File",     NULL,filemenu,     0, NULL},
        {"&Disc",     NULL,discmenu,     0, NULL},
        {"&Tape",     NULL,tapemenu,     0, NULL},
        {"&ROM",      NULL,rommenu,      0, NULL},
        {"&Settings", NULL,settingsmenu, 0, NULL},
        {"&Misc",     NULL,miscmenu,     0, NULL},
        {NULL, NULL, NULL, 0, NULL}
};

DIALOG bemgui[]=
{
        {d_ctext_proc, 200, 260, 0,  0, 15,0,0,0,     0,0, VERSION_STR},
        {d_menu_proc,  0,   0,   0,  0, 15,0,0,0,     0,0,mainmenu},
        {d_yield_proc},
        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

BITMAP *mouse, *_mouse_sprite;

BITMAP *guib;

void gui_enter()
{
        int x = 1;
        DIALOG_PLAYER *dp;

        while (keypressed()) readkey();
        while (key[KEY_F11]) rest(100);

        gui_update();

        if (curtube != 3 && !mouse_amx) install_mouse();

        set_color_depth(dcol);
        show_mouse(screen);
        bemgui[0].x  = (windx / 2) - 36;
        bemgui[0].y  = windy - 8;
        bemgui[0].fg = makecol(255,255,255);
        gui_init_rommenu();
        dp=init_dialog(bemgui, 0);
        while (x && !key[KEY_F11] && !key[KEY_ESC])
        {
                x = update_dialog(dp);
        }
        shutdown_dialog(dp);
        show_mouse(NULL);
        set_color_depth(8);

        if (curtube != 3 && !mouse_amx) remove_mouse();

        while (key[KEY_F11]) rest(100);

        video_clearscreen();
}
#endif
