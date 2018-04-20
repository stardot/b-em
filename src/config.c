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
int selecttube = -1;
int cursid = 0;
int sidmethod = 0;

ALLEGRO_CONFIG *bem_cfg;

int get_config_int(const char *sect, const char *key, int ival)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            ival = atoi(str);
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            ival = atoi(str);
            al_remove_config_key(bem_cfg, "", key);
        }
    }
    return ival;
}

static bool parse_bool(const char *value)
{
    return strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || atoi(value) > 0;
}

bool get_config_bool(const char *sect, const char *key, bool bval)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            bval = parse_bool(str);
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            bval = parse_bool(str);
            al_remove_config_key(bem_cfg, "", key);
        }
    }
    return bval;
}

const char *get_config_string(const char *sect, const char *key, const char *sval)
{
    const char *str;

    if (bem_cfg) {
        if ((str = al_get_config_value(bem_cfg, sect, key)))
            sval = str;
        else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
            al_set_config_value(bem_cfg, sect, key, str);
            al_remove_config_key(bem_cfg, "", key);
            sval = al_get_config_value(bem_cfg, sect, key);
        }
    }
    return sval;
}

void config_load(void)
{
    ALLEGRO_PATH *path;
    const char *cpath, *p;
    char s[16];
    int c;

    if ((path = find_cfg_file("b-em", ".cfg"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if (bem_cfg)
            al_destroy_config(bem_cfg);
        if (!(bem_cfg = al_load_config_file(cpath)))
            log_warn("config: unable to load config file '%s', using defaults", cpath);
        al_destroy_path(path);
    } else
        log_warn("config: no config file found, using defaults");

    if (bem_cfg) {
        if ((p = get_config_string("disc", "disc0", NULL))) {
            if (discfns[0])
                al_destroy_path(discfns[0]);
            discfns[0] = al_create_path(p);
        }
        if ((p = get_config_string("disc", "disc1", NULL))) {
            if (discfns[1])
                al_destroy_path(discfns[1]);
            discfns[1] = al_create_path(p);
        }
        if ((p = get_config_string("tape", "tape", NULL))) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(p);
        }
        al_remove_config_key(bem_cfg, "", "video_resize");
        al_remove_config_key(bem_cfg, "", "tube6502speed");
    }

    defaultwriteprot = get_config_int("disc", "defaultwriteprotect", 1);

    curmodel         = get_config_int(NULL, "model",         3);
    selecttube       = get_config_int(NULL, "tube",         -1);
    tube_speed_num   = get_config_int(NULL, "tubespeed",     0);

    sound_internal   = get_config_bool("sound", "sndinternal",   true);
    sound_beebsid    = get_config_bool("sound", "sndbeebsid",    true);
    sound_music5000  = get_config_bool("sound", "sndmusic5000",  false);
    sound_dac        = get_config_bool("sound", "snddac",        false);
    sound_ddnoise    = get_config_bool("sound", "sndddnoise",    true);
    sound_tape       = get_config_bool("sound", "sndtape",       false);
    sound_filter     = get_config_bool("sound", "soundfilter",   true);

    curwave          = get_config_int("sound", "soundwave",     0);
    sidmethod        = get_config_int("sound", "sidmethod",     0);
    cursid           = get_config_int("sound", "cursid",        2);

    ddnoise_vol      = get_config_int("sound", "ddvol",         2);
    ddnoise_type     = get_config_int("sound", "ddtype",        0);

    vid_fullborders  = get_config_int("video", "fullborders",   1);

    c                = get_config_int("video", "displaymode",   3);
    vid_scanlines    = (c == 2);
    vid_interlace    = (c == 1) || (c == 5);
    vid_linedbl      = (c == 3);
    vid_pal          = (c == 4) || (c == 5);

    fasttape         = get_config_bool("tape", "fasttape",      0);

    scsi_enabled     = get_config_bool("disc", "scsienable", 0);
    ide_enable       = get_config_bool("disc", "ideenable",     0);
    vdfs_enabled     = get_config_bool("disc", "vdfsenable", 0);

    keyas            = get_config_bool(NULL, "key_as",        0);
    mouse_amx        = get_config_bool(NULL, "mouse_amx",     0);
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

void set_config_bool(const char *sect, const char *key, bool value)
{
    al_set_config_value(bem_cfg, sect, key, value ? "true" : "false");
}

void set_config_string(const char *sect, const char *key, const char *value)
{
    if (value && *value)
        al_set_config_value(bem_cfg, sect, key, value);
    else
        al_remove_config_key(bem_cfg, sect, key);
}

void config_save(void)
{
    ALLEGRO_PATH *path;
    const char *cpath;
    char t[20];
    int c;

    if ((path = find_cfg_dest("b-em", ".cfg"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if (!bem_cfg) {
            if (!(bem_cfg = al_create_config())) {
                log_error("config: unable to save configuration");
                al_destroy_path(path);
                return;
            }
        }
        model_savecfg();

        if (discfns[0])
            al_set_config_value(bem_cfg, "disc", "disc0", al_path_cstr(discfns[0], ALLEGRO_NATIVE_PATH_SEP));
        if (discfns[1])
            al_set_config_value(bem_cfg, "disc", "disc1", al_path_cstr(discfns[1], ALLEGRO_NATIVE_PATH_SEP));
        if (tape_fn)
            al_set_config_value(bem_cfg, "tape", "tape", al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP));

        set_config_bool("disc", "defaultwriteprotect", defaultwriteprot);

        set_config_int(NULL, "model", curmodel);
        set_config_int(NULL, "tube", selecttube);
        set_config_int(NULL, "tubespeed", tube_speed_num);

        set_config_bool("sound", "sndinternal", sound_internal);
        set_config_bool("sound", "sndbeebsid",  sound_beebsid);
        set_config_bool("sound", "sndmusic5000",sound_music5000);
        set_config_bool("sound", "snddac",      sound_dac);
        set_config_bool("sound", "sndddnoise",  sound_ddnoise);
        set_config_bool("sound", "sndtape",     sound_tape);
        set_config_bool("sound", "soundfilter", sound_filter);

        set_config_int("sound", "soundwave", curwave);
        set_config_int("sound", "sidmethod", sidmethod);
        set_config_int("sound", "cursid", cursid);
        set_config_int("sound", "buflen_music5000", buflen_m5);

        set_config_int("sound", "ddvol", ddnoise_vol);
        set_config_int("sound", "ddtype", ddnoise_type);

        set_config_int("video", "fullborders", vid_fullborders);
        set_config_int("video", "displaymode", (vid_pal && vid_interlace) ? 5 : (vid_scanlines ? 2 : (vid_interlace ? 1 : (vid_linedbl ? 3 : (vid_pal ? 4 : 0)))));

        set_config_bool("tape", "fasttape", fasttape);

        set_config_bool("disc", "scsienable", scsi_enabled);
        set_config_bool("disc", "ideenable", ide_enable);
        set_config_bool("disc", "vdfsenable", vdfs_enabled);

        set_config_bool(NULL, "key_as", keyas);

        set_config_bool(NULL, "mouse_amx", mouse_amx);

        for (c = 0; c < 128; c++) {
            snprintf(t, sizeof t, "key_define_%03i", c);
            if (keylookup[c] == c)
                al_remove_config_key(bem_cfg, "user_keyboard", t);
            else
                set_config_int("user_keyboard", t, keylookup[c]);
        }
        midi_save_config();
        log_debug("config: saving config to %s", cpath);
        al_save_config_file(cpath, bem_cfg);
        al_destroy_path(path);
    } else
        log_error("config: no suitable destination for config file - config will not be saved");
}
