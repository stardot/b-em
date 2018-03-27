/*B-em v2.2 by Tom Walker
  Configuration handling*/

#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "disc.h"
#include "keyboard.h"
#include "model.h"
#include "mouse.h"
#include "ide.h"
#include "midi.h"
#include "scsi.h"
#include "sn76489.h"
#include "sound.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video_render.h"

int curmodel;
int fasttape = 0;
int selecttube = -1;
int cursid = 0;
int sidmethod = 0;

static ALLEGRO_CONFIG *bem_cfg;

int get_config_int(const char *sect, const char *key, int ival) {
    const char *str;

    if (bem_cfg && (str = al_get_config_value(bem_cfg, sect, key)))
        ival = atoi(str);
    return ival;
}

const char *get_config_string(const char *sect, const char *key, const char *sval) {
    const char *str;

    if (bem_cfg && (str = al_get_config_value(bem_cfg, sect, key)))
        sval = str;
    return sval;
}

void config_load(void)
{
    int c;
    char s[PATH_MAX];
    const char *p;
    if (!find_cfg_file(s, sizeof s, "b-em", "cfg")) {
        if (bem_cfg)
            al_destroy_config(bem_cfg);
        if (!(bem_cfg = al_load_config_file(s)))
            log_warn("config: unable to load confif file '%s', using defaults", s);
    } else
        log_warn("config: no config file found, using defaults");

    if (bem_cfg) {
        if ((p = al_get_config_value(bem_cfg, NULL, "disc0"))) {
            if (discfns[0])
                al_destroy_path(discfns[0]);
            discfns[0] = al_create_path(p);
        }
        if ((p = al_get_config_value(bem_cfg, NULL, "disc1"))) {
            if (discfns[1])
                al_destroy_path(discfns[1]);
            discfns[1] = al_create_path(p);
        }
        if ((p = al_get_config_value(bem_cfg, NULL, "tape"))) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(p);
        }
    }

    defaultwriteprot = get_config_int(NULL, "defaultwriteprotect", 1);

    curmodel         = get_config_int(NULL, "model",         3);
    selecttube       = get_config_int(NULL, "tube",         -1);
    tube_speed_num   = get_config_int(NULL, "tubespeed",     0);

    sound_internal   = get_config_int(NULL, "sndinternal",   1);
    sound_beebsid    = get_config_int(NULL, "sndbeebsid",    1);
    sound_music5000  = get_config_int(NULL, "sndmusic5000",  0);
    sound_dac        = get_config_int(NULL, "snddac    ",    0);
    sound_ddnoise    = get_config_int(NULL, "sndddnoise",    1);
    sound_tape       = get_config_int(NULL, "sndtape",       0);

    sound_filter     = get_config_int(NULL, "soundfilter",   1);
    curwave          = get_config_int(NULL, "soundwave",     0);

    sidmethod        = get_config_int(NULL, "sidmethod",     0);
    cursid           = get_config_int(NULL, "cursid",        2);

    ddnoise_vol      = get_config_int(NULL, "ddvol",         2);
    ddnoise_type     = get_config_int(NULL, "ddtype",        0);

    vid_fullborders  = get_config_int(NULL, "fullborders",   1);

    c                = get_config_int(NULL, "displaymode",   3);
    vid_scanlines    = (c == 2);
    vid_interlace    = (c == 1) || (c == 5);
    vid_linedbl      = (c == 3);
    vid_pal          = (c == 4) || (c == 5);

    fasttape         = get_config_int(NULL, "fasttape",      0);

    scsi_enabled     = get_config_int(NULL, "scsienable", 0);
    ide_enable       = get_config_int(NULL, "ideenable",     0);
    vdfs_enabled     = get_config_int(NULL, "vdfsenable", 0);

    keyas            = get_config_int(NULL, "key_as",        0);
    mouse_amx        = get_config_int(NULL, "mouse_amx",     0);
    kbdips           = get_config_int(NULL, "kbdips", 0);

    buflen_m5        = get_config_int("sound", "buflen_music5000", BUFLEN_M5);

    for (c = 0; c < ALLEGRO_KEY_MAX; c++) {
        sprintf(s, "key_define_%03i", c);
        keylookup[c] = get_config_int("user_keyboard", s, c);
    }
    midi_load_config();
}

void set_config_int(const char *sect, const char *key, int value)
{
    char buf[10];

    snprintf(buf, sizeof buf, "%d", value);
    al_set_config_value(bem_cfg, sect, key, buf);
}

void set_config_string(const char *sect, const char *key, const char *value)
{
    al_set_config_value(bem_cfg, sect, key, value);
}

void config_save(void)
{
    int c;
    char s[PATH_MAX], t[20];

    if (!find_cfg_dest(s, sizeof s, "b-em", "cfg")) {
        if (!bem_cfg) {
            if (!(bem_cfg = al_create_config())) {
                log_error("config: unable to save configuration");
                return;
            }
        }
        model_save(bem_cfg);

        if (discfns[0])
            al_set_config_value(bem_cfg, NULL, "disc0", al_path_cstr(discfns[0], ALLEGRO_NATIVE_PATH_SEP));
        if (discfns[1])
            al_set_config_value(bem_cfg, NULL, "disc1", al_path_cstr(discfns[1], ALLEGRO_NATIVE_PATH_SEP));
        if (tape_fn)
            al_set_config_value(bem_cfg, NULL, "tape", al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP));

        set_config_int(NULL, "defaultwriteprotect", defaultwriteprot);

        set_config_int(NULL, "model", curmodel);
        set_config_int(NULL, "tube", selecttube);
        set_config_int(NULL, "tubespeed", tube_speed_num);

        set_config_int(NULL, "sndinternal", sound_internal);
        set_config_int(NULL, "sndbeebsid",  sound_beebsid);
        set_config_int(NULL, "sndmusic5000",sound_music5000);
        set_config_int(NULL, "snddac",      sound_dac);
        set_config_int(NULL, "sndddnoise",  sound_ddnoise);
        set_config_int(NULL, "sndtape",     sound_tape);

        set_config_int(NULL, "soundfilter", sound_filter);
        set_config_int(NULL, "soundwave", curwave);

        set_config_int(NULL, "sidmethod", sidmethod);
        set_config_int(NULL, "cursid", cursid);

        set_config_int(NULL, "ddvol", ddnoise_vol);
        set_config_int(NULL, "ddtype", ddnoise_type);

        set_config_int(NULL, "fullborders", vid_fullborders);
        set_config_int(NULL, "displaymode", (vid_pal && vid_interlace) ? 5 : (vid_scanlines ? 2 : (vid_interlace ? 1 : (vid_linedbl ? 3 : (vid_pal ? 4 : 0)))));

        set_config_int(NULL, "fasttape", fasttape);

        set_config_int(NULL, "scsienable", scsi_enabled);
        set_config_int(NULL, "ideenable", ide_enable);
        set_config_int(NULL, "vdfsenable", vdfs_enabled);

        set_config_int(NULL, "key_as", keyas);

        set_config_int(NULL, "mouse_amx", mouse_amx);
        set_config_int("sound", "buflen_music5000", buflen_m5);

        for (c = 0; c < 128; c++) {
            snprintf(t, sizeof t, "key_define_%03i", c);
            set_config_int("user_keyboard", t, keylookup[c]);
        }
        midi_save_config();
        log_debug("config: saving config to %s", s);
        al_save_config_file(s, bem_cfg);
    } else
        log_error("config: no suitable destination for config file - config will not be saved");
}
