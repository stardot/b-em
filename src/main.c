/*B-em v2.2 by Tom Walker
  Main loop + start/finish code*/

#include <allegro.h>

#ifdef WIN32
#include <winalleg.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "b-em.h"

#include "6502.h"
#include "acia.h"
#include "adc.h"
#include "adf.h"
#include "model.h"
#include "cmos.h"
#include "config.h"
#include "csw.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "fdi.h"
#include "i8271.h"
#include "ide.h"
#include "keyboard.h"
#include "main.h"
#include "mem.h"
#include "mouse.h"
#include "music5000.h"
#ifdef WIN32
#include "pal.h"
#endif
#include "savestate.h"
#include "scsi.h"
#include "serial.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "soundopenal.h"
#include "ssd.h"
#include "tape.h"
#include "tapenoise.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uef.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"
#include "wd1770.h"

#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"

#undef printf

int autoboot=0;
int joybutton[2];

int printsec;
void secint()
{
        printsec = 1;
}

int fcount = 0;
void int50()
{
        fcount++;
}

char exedir[512];
int debug = 0, debugon = 0;
int ddnoiseframes = 0;

void main_reset()
{
        m6502_reset();
        crtc_reset();
        sysvia_reset();
        uservia_reset();
        serial_reset();
        acia_reset();
        wd1770_reset();
        i8271_reset();
        scsi_reset();
        vdfs_reset();
        sid_reset();
        music5000_reset();
        sn_init();
        if (curtube != -1) tubes[curtube].reset();
        else               tube_exec = NULL;
        tube_reset();
        
        memset(ram, 0, 64 * 1024);
}


void main_init(int argc, char *argv[])
{
        char t[512];
        int c;
        int tapenext = 0, discnext = 0;

        debug_open();

        startblit();
        
        printf("%s\n", VERSION_STR);

	vid_fskipmax = 1;
        
        al_init_main(argc, argv);
        
        
        append_filename(t, exedir, "roms\\tube\\ReCo6502ROM_816", 511);
        if (!file_exists(t,FA_ALL,NULL) && selecttube == 4) selecttube = -1;

        curtube = selecttube;
        if (models[curmodel].tube != -1) curtube = models[curmodel].tube;


        for (c = 1; c < argc; c++)
        {
//                bem_debugf("%i : %s",c,argv[c]);
/*                if (!strcasecmp(argv[c],"-1770"))
                {
                        I8271=0;
                        WD1770=1;
                }
                else*/
//#ifndef WIN32
                if (!strcasecmp(argv[c], "--help"))
                {
                        printf("%s command line options :\n\n", VERSION_STR);
                        printf("-mx             - start as model x (see readme.txt for models)\n");
                        printf("-tx             - start with tube x (see readme.txt for tubes)\n");
                        printf("-disc disc.ssd  - load disc.ssd into drives :0/:2\n");
                        printf("-disc1 disc.ssd - load disc.ssd into drives :1/:3\n");
                        printf("-autoboot       - boot disc in drive :0\n");
                        printf("-tape tape.uef  - load tape.uef\n");
                        printf("-fasttape       - set tape speed to fast\n");
                        printf("-s              - scanlines display mode\n");
                        printf("-i              - interlace display mode\n");
                        printf("-debug          - start debugger\n");
                        printf("-allegro        - use Allegro for video rendering\n");
                        exit(-1);
                }
                else
//#endif
                if (!strcasecmp(argv[c], "-tape"))
                {
                        tapenext = 2;
                }
                else if (!strcasecmp(argv[c], "-disc") || !strcasecmp(argv[c], "-disk"))
                {
                        discnext = 1;
                }
                else if (!strcasecmp(argv[c], "-disc1"))
                {
                        discnext = 2;
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 'm' || argv[c][1] == 'M'))
                {
                        sscanf(&argv[c][2], "%i", &curmodel);
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 't' || argv[c][1] == 'T'))
                {
                        sscanf(&argv[c][2], "%i", &curtube);
                }
                else if (!strcasecmp(argv[c], "-fasttape"))
                {
                        fasttape = 1;
                }
                else if (!strcasecmp(argv[c], "-autoboot"))
                {
                        autoboot = 150;
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 'f' || argv[c][1]=='F'))
                {
                        sscanf(&argv[c][2], "%i", &vid_fskipmax);
			if (vid_fskipmax < 1) vid_fskipmax = 1;
			if (vid_fskipmax > 9) vid_fskipmax = 9;
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 's' || argv[c][1] == 'S'))
                {
                        vid_scanlines = 1;
                }
                else if (!strcasecmp(argv[c], "-debug"))
                {
                        debug = debugon = 1;
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I'))
                {
                        vid_interlace = 1;
                }
                else if (tapenext)
                   strcpy(tape_fn, argv[c]);
                else if (discnext)
                {
                        strcpy(discfns[discnext-1], argv[c]);
                        discnext = 0;
                }
                else
                {			
                        strcpy(discfns[0], argv[c]);
                        discnext = 0;
			autoboot = 150;
                }
                if (tapenext) tapenext--;
        }

        video_init();
        mode7_makechars();

#ifndef WIN32
        install_keyboard();
#endif
        install_timer();

        mem_init();
        ddnoise_init();
        tapenoise_init();
        
        sound_init();
        al_init();
        sid_init();
        sid_settype(sidmethod, cursid);
        music5000_init();

	adc_init();
#ifdef WIN32
        pal_init();
#endif
        disc_init();
        ssd_init();
        adf_init();
        fdi_init();

        scsi_init();
        ide_init();
        vdfs_init();

        debug_start();

        model_init();

        main_reset();


        install_int_ex(secint, MSEC_TO_TIMER(1000));
        install_int_ex(int50,  MSEC_TO_TIMER(20));
        
        set_display_switch_mode(SWITCH_BACKGROUND);
#ifdef WIN32        
                timeBeginPeriod(1);
#endif
        oldmodel = curmodel;
        
        if (curtube == 3 || mouse_amx) install_mouse();

//printf("Disc 0 : %s\n",discfns[0]);
//printf("Disc 1 : %s\n",discfns[1]);
//printf("Tape   : %s\n",tape_fn);
        disc_load(0, discfns[0]);
        disc_load(1, discfns[1]);
        tape_load(tape_fn);
        if (defaultwriteprot) writeprot[0] = writeprot[1] = 1;
        
        endblit();
}

void main_restart()
{
        startblit();
        if (curtube == 3 || mouse_amx) remove_mouse();
        cmos_save(models[oldmodel]);
        oldmodel = curmodel;

        model_init();
        
        main_reset();

        resumeready();
        if (curtube == 3 || mouse_amx) install_mouse();
        endblit();
}

void main_setmouse()
{
        if (curtube != 3)
        {
                if (mouse_amx) install_mouse();
                else           remove_mouse();
        }
}

int resetting = 0;
int framesrun = 0;

void main_cleardrawit()
{
        fcount = 0;
}

void main_run()
{
        int c, d;
        if ((fcount > 0 || key[KEY_PGUP] || (motor && fasttape)))
        {
                if (autoboot) autoboot--;
                fcount--;
                framesrun++;
                if (key[KEY_PGUP] || (motor && fasttape)) fcount=0;
                if (x65c02) m65c02_exec();
                else        m6502_exec();
                ddnoiseframes++;
                if (ddnoiseframes >= 5)
                {
                        ddnoiseframes = 0;
                        ddnoise_mix();
                }
                key_check();
                poll_joystick();
                for (c = 0; c < 2; c++)
                {
                        joybutton[c] = 0;
                        for (d = 0; d < joy[c].num_buttons; d++)
                        {
                                if (joy[c].button[d].b) joybutton[c] = 1;
                        }
                }
                if (savestate_wantload) savestate_doload();
                if (savestate_wantsave) savestate_dosave();
                if (key[KEY_F10] && debugon) debug = 1;
                if (key[KEY_F12] && !resetting)
                {
                        m6502_reset();
                        i8271_reset();
                        wd1770_reset();
                        sid_reset();
                        music5000_reset();

                        if (curtube != -1) tubes[curtube].reset();
                        tube_reset();
                }
                resetting = key[KEY_F12];
        }
        else
        {
                framesrun = 0;
                rest(1);
        }
        if (framesrun > 10) fcount = 0;
}

void main_close()
{
        debug_kill();

#ifdef WIN32
        timeEndPeriod(1);
#endif

        config_save();
        cmos_save(models[curmodel]);
        
        mem_close();
        uef_close();
        csw_close();
        tube_6502_close();
        arm_close();
        x86_close();
        z80_close();
        w65816_close();
        n32016_close();
        disc_close(0);
        disc_close(1);
        scsi_close();
        ide_close();
        vdfs_close();
        ddnoise_close();
        tapenoise_close();
        
        al_close();
        video_close();
        debug_close();
}

void changetimerspeed(int i)
{
        remove_int(int50);
        install_int_ex(int50, BPS_TO_TIMER(i));
}
