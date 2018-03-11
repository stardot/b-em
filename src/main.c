/*B-em v2.2 by Tom Walker
  Main loop + start/finish code*/

#ifdef WIN32
#include <winalleg.h>
#endif

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_native_dialog.h>

#include "6502.h"
#include "adc.h"
#include "model.h"
#include "cmos.h"
#include "config.h"
#include "csw.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "fdi.h"
#include "gui-allegro.h"
#include "i8271.h"
#include "ide.h"
#include "keyboard.h"
#include "main.h"
#include "mem.h"
#include "mouse.h"
#include "midi.h"
#include "music4000.h"
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
#include "sysacia.h"
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

bool quitting = false;
int autoboot=0;
int joybutton[2];

int printsec;
void secint()
{
        printsec = 1;
}

int bempause = 0;

static ALLEGRO_TIMER *timer;
static ALLEGRO_EVENT_QUEUE *queue;

static int fcount = 0;

char exedir[512];

void main_reset()
{
        m6502_reset();
        crtc_reset();
        video_reset();
        sysvia_reset();
        uservia_reset();
        serial_reset();
        acia_reset(&sysacia);
        wd1770_reset();
        i8271_reset();
        scsi_reset();
        vdfs_reset();
        sid_reset();
        music4000_reset();
        music5000_reset();
        sn_init();
        if (curtube != -1) tubes[curtube].reset();
        else               tube_exec = NULL;
        tube_reset();

        memset(ram, 0, 64 * 1024);
}


void main_init(int argc, char *argv[])
{
        int c;
        int tapenext = 0, discnext = 0;
        ALLEGRO_DISPLAY *display;

        startblit();

        log_open();
        log_info("main: starting %s", VERSION_STR);

        vid_fskipmax = 1;

        //TODO - do this properly.
        //append_filename(t, exedir, "roms/tube/ReCo6502ROM_816", 511);
        //if (!file_exists(t,FA_ALL,NULL) && selecttube == 4) selecttube = -1;

        curtube = selecttube;
        model_check();

        for (c = 1; c < argc; c++)
        {
//                log_debug("%i : %s",c,argv[c]);
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
                        debug_core = 1;
                }
                else if (!strcasecmp(argv[c], "-debugtube"))
                {
                        debug_tube = 1;
                }
                else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I'))
                {
                        vid_interlace = 1;
            vid_linedbl = vid_scanlines = 0;
                }
                else if (tapenext) {
                    if (tape_fn)
                        al_destroy_path(tape_fn);
                    tape_fn = al_create_path(argv[c]);
                }
                else if (discnext)
                {
                    if (discfns[discnext-1])
                        al_destroy_path(discfns[discnext-1]);
                    discfns[discnext-1] = al_create_path(argv[c]);
                    discnext = 0;
                }
                else
                {
                    if (discfns[0])
                        al_destroy_path(discfns[0]);
                    discfns[0] = al_create_path(argv[c]);
                    discnext = 0;
                    autoboot = 150;
                }
                if (tapenext) tapenext--;
        }

        display = video_init();
        mode7_makechars();

        mem_init();

    if (!(queue = al_create_event_queue())) {
        log_fatal("main: unable to create event queue");
        exit(1);
    }
    if (!al_install_audio()) {
        log_fatal("main: unable to initialise audio");
        exit(1);
    }
    if (!al_reserve_samples(3)) {
        log_fatal("main: unable to reserve audio samples");
        exit(1);
    }
    if (!al_init_acodec_addon()) {
        log_fatal("main: unable to initialise audio codecs");
        exit(1);
    }
    sound_init();
    sid_init();
    sid_settype(sidmethod, cursid);
    music5000_init(queue);
    ddnoise_init();
    tapenoise_init(queue);

    adc_init();
#ifdef WIN32
        pal_init();
#endif
        disc_init();
        fdi_init();

        scsi_init();
        ide_init();
        vdfs_init();

        model_init();

        midi_init();
        main_reset();


    gui_allegro_init(queue, display);

    if (!(timer = al_create_timer(1.0 / 50.0))) {
        log_fatal("main: unable to create timer");
        exit(1);
    }
    al_register_event_source(queue, al_get_timer_event_source(timer));

    if (!al_install_keyboard()) {
        log_fatal("main: umable to install keyboard");
        exit(1);
    }
    al_register_event_source(queue, al_get_keyboard_event_source());

#ifdef WIN32
                timeBeginPeriod(1);
#endif
        oldmodel = curmodel;

    al_install_mouse();
    al_register_event_source(queue, al_get_mouse_event_source());

        disc_load(0, discfns[0]);
        disc_load(1, discfns[1]);
        tape_load(tape_fn);
        if (defaultwriteprot)
            writeprot[0] = writeprot[1] = 1;

        endblit();

        debug_start();
}

void main_restart()
{
        bempause = 1;
        startblit();
        if (curtube == 3 || mouse_amx)
            al_uninstall_mouse();
        cmos_save(models[oldmodel]);
        oldmodel = curmodel;

        model_init();

        main_reset();

        if (curtube == 3 || mouse_amx)
            al_install_mouse();
        endblit();
        bempause = 0;
}

void main_setmouse()
{
    if (curtube != 3) {
        if (mouse_amx)
            al_install_mouse();
        else
            al_uninstall_mouse();
    }
}

int resetting = 0;
int framesrun = 0;

void main_cleardrawit()
{
        fcount = 0;
}

#if 0
        if ((fcount > 0 || key[KEY_PGUP] || (motor && fasttape)))
        {
                if (key[KEY_PGUP] || (motor && fasttape)) fcount=0;
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
#endif

static void main_key_down(ALLEGRO_EVENT *event)
{
    switch(event->keyboard.keycode) {
        case ALLEGRO_KEY_F10:
            if (debug_core || debug_tube)
                debug_step = 1;
            break;
        case ALLEGRO_KEY_F12:
            m6502_reset();
            video_reset();
            i8271_reset();
            wd1770_reset();
            sid_reset();
            music5000_reset();

            if (curtube != -1)
                tubes[curtube].reset();
            tube_reset();
    }
}

static void main_timer(void) {
    if (autoboot)
        autoboot--;
    framesrun++;

    if (x65c02)
        m65c02_exec();
    else
        m6502_exec();

    if (ddnoise_ticks > 0 && --ddnoise_ticks == 0)
        ddnoise_headdown();

    if (savestate_wantload)
        savestate_doload();
    if (savestate_wantsave)
        savestate_dosave();
}

static int timer_count = 0;

void main_run()
{
    ALLEGRO_EVENT event;

    log_debug("main: about to start timer");
    al_start_timer(timer);

    log_debug("main: entering main loop");
    while (!quitting) {
        //log_debug("main: waiting for event");
        al_wait_for_event(queue, &event);
        //log_debug("main: event received");
        switch(event.type) {
            case ALLEGRO_EVENT_KEY_DOWN:
                log_debug("main: key down, code=%d", event.keyboard.keycode);
                key_down(&event);
                main_key_down(&event);
                break;
            case ALLEGRO_EVENT_KEY_UP:
                log_debug("main: key up, code=%d", event.keyboard.keycode);
                key_up(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_AXES:
                mouse_axes(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                mouse_btn_down(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
                mouse_btn_up(&event);
                break;
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                quitting = true;
                break;
            case ALLEGRO_EVENT_TIMER:
                main_timer();
                if (++timer_count >= 500) {
                    log_debug("reached 10s of timer events");
                    timer_count = 0;
                }
                break;
            case ALLEGRO_EVENT_MENU_CLICK:
                al_stop_timer(timer);
                gui_allegro_event(&event);
                al_start_timer(timer);
                break;
            case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
                music5000_streamfrag();
        }
    }
}
                
void main_close()
{
        debug_kill();

#ifdef WIN32
        timeEndPeriod(1);
#endif

        config_save();
        cmos_save(models[curmodel]);

        midi_close();
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

        video_close();
        log_close();
}

void changetimerspeed(int i)
{
    al_set_timer_speed(timer, 1.0/i);
}

void main_pause(void)
{
    al_stop_timer(timer);
}

void main_resume(void)
{
    al_start_timer(timer);
}
