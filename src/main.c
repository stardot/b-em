/*B-em v2.2 by Tom Walker
  Main loop + start/finish code*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_image.h>
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
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "main.h"
#include "mem.h"
#include "mouse.h"
#include "midi.h"
#include "music4000.h"
#include "music5000.h"
#include "pal.h"
#include "savestate.h"
#include "scsi.h"
#include "serial.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
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
float joyaxes[4];
int emuspeed = 4;

static ALLEGRO_TIMER *timer;
static ALLEGRO_EVENT_QUEUE *queue;
static ALLEGRO_EVENT_SOURCE evsrc;

typedef enum {
    FSPEED_NONE,
    FSPEED_SELECTED,
    FSPEED_RUNNING
} fspeed_type_t;

static double time_limit;
static int fcount = 0;
static fspeed_type_t fullspeed = FSPEED_NONE;
static bool bempause  = false;

const emu_speed_t emu_speeds[NUM_EMU_SPEEDS] = {
    {  "10%", 1.0 / (50.0 * 0.10), 1 },
    {  "25%", 1.0 / (50.0 * 0.25), 1 },
    {  "50%", 1.0 / (50.0 * 0.50), 1 },
    {  "75%", 1.0 / (50.0 * 0.75), 1 },
    { "100%", 1.0 / 50.0,          1 },
    { "150%", 1.0 / (50.0 * 1.50), 2 },
    { "200%", 1.0 / (50.0 * 2.00), 2 },
    { "300%", 1.0 / (50.0 * 3.00), 3 },
    { "400%", 1.0 / (50.0 * 4.00), 4 },
    { "500%", 1.0 / (50.0 * 5.00), 5 }
};

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

static const char helptext[] =
    VERSION_STR " command line options:\n\n"
    "-mx             - start as model x (see readme.txt for models)\n"
    "-tx             - start with tube x (see readme.txt for tubes)\n"
    "-disc disc.ssd  - load disc.ssd into drives :0/:2\n"
    "-disc1 disc.ssd - load disc.ssd into drives :1/:3\n"
    "-autoboot       - boot disc in drive :0\n"
    "-tape tape.uef  - load tape.uef\n"
    "-fasttape       - set tape speed to fast\n"
    "-Fx             - set maximum video frames skipped\n"
    "-s              - scanlines display mode\n"
    "-i              - interlace display mode\n"
    "-debug          - start debugger\n"
    "-debugtube      - start debugging tube processor\n\n";

void main_init(int argc, char *argv[])
{
    int c;
    int tapenext = 0, discnext = 0;
    ALLEGRO_DISPLAY *display;
    ALLEGRO_PATH *path;
    const char *ext;

    if (!al_init()) {
        fputs("Failed to initialise Allegro!\n", stderr);
        exit(1);
    }

    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);

    config_load();
    log_open();
    log_info("main: starting %s", VERSION_STR);

    vid_fskipmax = 1;

    model_loadcfg();

    for (c = 1; c < argc; c++) {
        if (!strcasecmp(argv[c], "--help")) {
            fwrite(helptext, sizeof helptext-1, 1, stdout);
            exit(1);
        }
        else if (!strcasecmp(argv[c], "-tape"))
            tapenext = 2;
        else if (!strcasecmp(argv[c], "-disc") || !strcasecmp(argv[c], "-disk"))
            discnext = 1;
        else if (!strcasecmp(argv[c], "-disc1"))
            discnext = 2;
        else if (argv[c][0] == '-' && (argv[c][1] == 'm' || argv[c][1] == 'M'))
            sscanf(&argv[c][2], "%i", &curmodel);
        else if (argv[c][0] == '-' && (argv[c][1] == 't' || argv[c][1] == 'T'))
            sscanf(&argv[c][2], "%i", &curtube);
        else if (!strcasecmp(argv[c], "-fasttape"))
            fasttape = true;
        else if (!strcasecmp(argv[c], "-autoboot"))
            autoboot = 150;
        else if (argv[c][0] == '-' && (argv[c][1] == 'f' || argv[c][1]=='F')) {
            sscanf(&argv[c][2], "%i", &vid_fskipmax);
            if (vid_fskipmax < 1) vid_fskipmax = 1;
            if (vid_fskipmax > 9) vid_fskipmax = 9;
        }
        else if (argv[c][0] == '-' && (argv[c][1] == 's' || argv[c][1] == 'S'))
            vid_scanlines = 1;
        else if (!strcasecmp(argv[c], "-debug"))
            debug_core = 1;
        else if (!strcasecmp(argv[c], "-debugtube"))
            debug_tube = 1;
        else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I')) {
            vid_interlace = 1;
            vid_linedbl = vid_scanlines = 0;
        }
        else if (tapenext) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(argv[c]);
        }
        else if (discnext) {
            if (discfns[discnext-1])
                al_destroy_path(discfns[discnext-1]);
            discfns[discnext-1] = al_create_path(argv[c]);
            discnext = 0;
        }
        else {
            path = al_create_path(argv[c]);
            ext = al_get_path_extension(path);
            if (ext && !strcasecmp(ext, ".snp"))
                savestate_load(argv[c]);
            else if (ext && (!strcasecmp(ext, ".uef") || !strcasecmp(ext, ".csw"))) {
                if (tape_fn)
                    al_destroy_path(tape_fn);
                tape_fn = path;
                tapenext = 0;
            }
            else {
                if (discfns[0])
                    al_destroy_path(discfns[0]);
                discfns[0] = path;
                discnext = 0;
                autoboot = 150;
            }
        }
        if (tapenext) tapenext--;
    }

    display = video_init();
    mode7_makechars();
    al_init_image_addon();

    mem_init();

    if (!(queue = al_create_event_queue())) {
        log_fatal("main: unable to create event queue");
        exit(1);
    }
    al_register_event_source(queue, al_get_display_event_source(display));

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

    joystick_init(queue);

    gui_allegro_init(queue, display);

    time_limit = 2.0 / 50.0;
    if (!(timer = al_create_timer(1.0 / 50.0))) {
        log_fatal("main: unable to create timer");
        exit(1);
    }
    al_register_event_source(queue, al_get_timer_event_source(timer));
    al_init_user_event_source(&evsrc);
    al_register_event_source(queue, &evsrc);

    if (!al_install_keyboard()) {
        log_fatal("main: umable to install keyboard");
        exit(1);
    }
    al_register_event_source(queue, al_get_keyboard_event_source());

    oldmodel = curmodel;

    al_install_mouse();
    al_register_event_source(queue, al_get_mouse_event_source());

    disc_load(0, discfns[0]);
    disc_load(1, discfns[1]);
    tape_load(tape_fn);
    if (defaultwriteprot)
        writeprot[0] = writeprot[1] = 1;

    debug_start();
}

void main_restart()
{
    main_pause();
    cmos_save(models[oldmodel]);
    oldmodel = curmodel;

    model_init();
    main_reset();
    main_resume();
}

int resetting = 0;
int framesrun = 0;

void main_cleardrawit()
{
    fcount = 0;
}

static void main_start_fullspeed(void)
{
    ALLEGRO_EVENT event;

    log_debug("main: starting full-speed");
    al_stop_timer(timer);
    fullspeed = FSPEED_RUNNING;
    event.type = ALLEGRO_EVENT_TIMER;
    al_emit_user_event(&evsrc, &event, NULL);
}

static void main_key_down(ALLEGRO_EVENT *event)
{
    ALLEGRO_KEYBOARD_STATE kstate;
    int code = key_map(event);

    log_debug("main: key down, code=%d, fullspeed=%d", event->keyboard.keycode, fullspeed);

    switch(code) {
        case ALLEGRO_KEY_PGUP:
            if (fullspeed != FSPEED_RUNNING)
                main_start_fullspeed();
            break;
        case ALLEGRO_KEY_PGDN:
            if (bempause) {
                if (emuspeed != EMU_SPEED_PAUSED) {
                    bempause = false;
                    if (emuspeed != EMU_SPEED_FULL)
                        al_start_timer(timer);
                }
            } else {
                al_stop_timer(timer);
                bempause = true;
            }
        case ALLEGRO_KEY_ENTER:
            al_get_keyboard_state(&kstate);
            if (al_key_down(&kstate, ALLEGRO_KEY_ALT)) {
                video_toggle_fullscreen();
                return;
            }
            break;
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
            break;
        default:
            if (fullspeed == FSPEED_RUNNING) {
                fullspeed = FSPEED_SELECTED;
                if (emuspeed != EMU_SPEED_PAUSED)
                    al_start_timer(timer);
            }
    }
    key_down(code);
}

static void main_key_up(ALLEGRO_EVENT *event)
{
    ALLEGRO_KEYBOARD_STATE kstate;
    int code = key_map(event);

    log_debug("main: key up, code=%d, fullspeed=%d", event->keyboard.keycode, fullspeed);

    switch(code) {
        case ALLEGRO_KEY_PGUP:
            if (emuspeed != EMU_SPEED_FULL) {
                al_get_keyboard_state(&kstate);
                if (!al_key_down(&kstate, ALLEGRO_KEY_LSHIFT) && !al_key_down(&kstate, ALLEGRO_KEY_RSHIFT)) {
                    log_debug("main: stopping fullspeed (PgUp)");
                    if (fullspeed == FSPEED_RUNNING && emuspeed != EMU_SPEED_PAUSED)
                        al_start_timer(timer);
                    fullspeed = FSPEED_NONE;
                }
                else
                    fullspeed = FSPEED_SELECTED;
            }
            break;
    }
    if (fullspeed == FSPEED_SELECTED)
        main_start_fullspeed();
    key_up(code);
}

static void main_timer(ALLEGRO_EVENT *event)
{
    double delay = al_get_time() - event->any.timestamp;
    if (delay < time_limit) {
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
        if (fullspeed == FSPEED_RUNNING)
            al_emit_user_event(&evsrc, event, NULL);
    }
}

void main_run()
{
    ALLEGRO_EVENT event;

    log_debug("main: about to start timer");
    al_start_timer(timer);

    log_debug("main: entering main loop");
    while (!quitting) {
        al_wait_for_event(queue, &event);
        switch(event.type) {
            case ALLEGRO_EVENT_KEY_DOWN:
                main_key_down(&event);
                break;
            case ALLEGRO_EVENT_KEY_UP:
                main_key_up(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_AXES:
                mouse_axes(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                log_debug("main: mouse button down");
                mouse_btn_down(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
                log_debug("main: mouse button up");
                mouse_btn_up(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_AXIS:
                joystick_axis(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN:
                joystick_button_down(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_UP:
                joystick_button_up(&event);
                break;
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                log_debug("main: event display close - quitting");
                quitting = true;
                break;
            case ALLEGRO_EVENT_TIMER:
                main_timer(&event);
                break;
            case ALLEGRO_EVENT_MENU_CLICK:
                main_pause();
                gui_allegro_event(&event);
                main_resume();
                break;
            case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
                music5000_streamfrag();
                break;
            case ALLEGRO_EVENT_DISPLAY_RESIZE:
                video_update_window_size(&event);
                break;
        }
    }
    log_debug("main: end loop");
}

void main_close()
{
    gui_tapecat_close();
    gui_keydefine_close();

    debug_kill();

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

void main_setspeed(int speed)
{
    log_debug("main: setspeed %d", speed);
    if (speed == EMU_SPEED_FULL)
        main_start_fullspeed();
    else {
        al_stop_timer(timer);
        fullspeed = FSPEED_NONE;
        if (speed != EMU_SPEED_PAUSED) {
            if (speed >= NUM_EMU_SPEEDS) {
                log_warn("main: speed #%d out of range, defaulting to 100%%", speed);
                speed = 4;
            }
            al_set_timer_speed(timer, emu_speeds[speed].timer_interval);
            time_limit = emu_speeds[speed].timer_interval * 2.0;
            vid_fskipmax = emu_speeds[speed].fskipmax;
            log_debug("main: new speed#%d, timer interval=%g, vid_fskipmax=%d", speed, emu_speeds[speed].timer_interval, vid_fskipmax);
            al_start_timer(timer);
        }
    }
    emuspeed = speed;
}

void main_pause(void)
{
    al_stop_timer(timer);
}

void main_resume(void)
{
    if (emuspeed != EMU_SPEED_PAUSED && emuspeed != EMU_SPEED_FULL)
        al_start_timer(timer);
}

void main_setquit(void)
{
    quitting = 1;
}

int main(int argc, char **argv)
{
    main_init(argc, argv);
    main_run();
    main_close();
    return 0;
}
