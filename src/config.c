/*B-em v2.2 by Tom Walker
  Configuration handling*/

#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "disc.h"
#include "keyboard.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "mouse.h"
#include "mmccard.h"
#include "music5000.h"
#include "ide.h"
#include "midi.h"
#include "scsi.h"
#include "sdf.h"
#include "sn76489.h"
#include "sound.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"

int curmodel;
int selecttube = -1;
int cursid = 0;
int sidmethod = 0;

ALLEGRO_CONFIG *bem_cfg;

int get_config_int(const char *sect, const char *key, int ival)
{
    const char *str = al_get_config_value(bem_cfg, sect, key);
    if (!str && sect) {
        if ((str = al_get_config_value(bem_cfg, NULL, key)))
            al_remove_config_key(bem_cfg, "", key);
    }
    if (str) {
        char *end;
        long nval = strtol(str, &end, 0);
        if (end > str && !end[0])
            ival = nval;
        else if (sect)
            log_warn("config: section '%s', key '%s': invalid integer %s", sect, key, str);
        else
            log_warn("config: global section, key '%s': invalid integer %s", key, str);
    }
    return ival;
}

bool get_config_bool(const char *sect, const char *key, bool bval)
{
    const char *str = al_get_config_value(bem_cfg, sect, key);
    if (!str && sect) {
        if ((str = al_get_config_value(bem_cfg, NULL, key)))
            al_remove_config_key(bem_cfg, "", key);
    }
    if (str) {
        if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0)
            bval = true;
        else if (strcasecmp(str, "false") == 0 || strcasecmp(str, "no") == 0)
            bval = false;
        else {
            char *end;
            long nval = strtol(str, &end, 0);
            if (end > str && !end[0])
                bval = (nval > 0);
            else if (sect)
                log_warn("config: section '%s', key '%s': invalid boolean %s", sect, key, str);
            else
                log_warn("config: global section, key '%s': invalid boolean %s", key, str);
        }
    }
    return bval;
}

const char *get_config_string(const char *sect, const char *key, const char *sval)
{
    const char *str = al_get_config_value(bem_cfg, sect, key);
    if (str)
        sval = str;
    else if (sect && (str = al_get_config_value(bem_cfg, NULL, key))) {
        al_set_config_value(bem_cfg, sect, key, str);
        al_remove_config_key(bem_cfg, "", key);
        sval = al_get_config_value(bem_cfg, sect, key);
    }
    return sval;
}

ALLEGRO_COLOR get_config_colour(const char *sect, const char *key, ALLEGRO_COLOR cdefault)
{
    const char *str = al_get_config_value(bem_cfg, sect, key);
    if (str) {
        if (*str == '#') {
            unsigned long col = strtoul(str+1, NULL, 16);
            unsigned r = (col >> 16) & 0xff;
            unsigned g = (col >> 8) & 0xff;
            unsigned b = col & 0xff;
            log_debug("config: get_config_colour, sect=%s, key=%s, hex, r=%u, g=%u, b=%u", sect, key, r, g, b);
            return al_map_rgb(r, g, b);
        }
        else {
            unsigned r, g, b;
            if (sscanf(str, "%u,%u,%u", &r, &g, &b) == 3) {
                log_debug("config: get_config_colour, sect=%s, key=%s, decimal, r=%u, g=%u, b=%u", sect, key, r, g, b);
                return al_map_rgb(r, g, b);
            }
        }
    }
    return cdefault;
}

void config_load(void)
{
    ALLEGRO_PATH *path;
    const char *cpath, *p;
    int c;

    if ((path = find_cfg_file("b-em", ".cfg"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if (bem_cfg)
            al_destroy_config(bem_cfg);
        if (!(bem_cfg = al_load_config_file(cpath))) {
            log_fatal("config: unable to load config file '%s'", cpath);
            exit(1);
        }
        al_destroy_path(path);
    }
    else {
        log_fatal("config: no config file found");
        exit(1);
    }

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
    if ((p = get_config_string("disc", "mmb", NULL))) {
        if (mmb_fn)
            free(mmb_fn);
        mmb_fn = strdup(p);
    }
    if ((p = get_config_string("disc", "mmccard", NULL))) {
        if (mmccard_fn)
            free(mmccard_fn);
        mmccard_fn = strdup(p);
    }
    if ((p = get_config_string("tape", "tape", NULL))) {
        if (tape_fn)
            al_destroy_path(tape_fn);
        tape_fn = al_create_path(p);
    }
    al_remove_config_key(bem_cfg, "", "video_resize");
    al_remove_config_key(bem_cfg, "", "tube6502speed");
    defaultwriteprot = get_config_bool("disc", "defaultwriteprotect", 1);

    autopause        = get_config_bool(NULL, "autopause", false);

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
    sound_paula      = get_config_bool("sound", "soundpaula",    false);
    music5000_fno    = get_config_int("sound", "music5000_filter", 0);
    buflen_m5        = get_config_int("sound", "buflen_music5000", BUFLEN_M5);

    curwave          = get_config_int("sound", "soundwave",     0);
    sidmethod        = get_config_int("sound", "sidmethod",     0);
    cursid           = get_config_int("sound", "cursid",        2);

    ddnoise_vol      = get_config_int("sound", "ddvol",         2);
    ddnoise_type     = get_config_int("sound", "ddtype",        0);

    vid_fullborders  = get_config_int("video", "fullborders",   1);
    vid_win_multiplier = get_config_int("video", "winmultipler", 1);
    winsizex         = get_config_int("video", "winsizex", 800);
    winsizey         = get_config_int("video", "winsizey", 600);

    vid_ledlocation  = get_config_int("video", "ledlocation",   0);
    vid_ledvisibility = get_config_int("video", "ledvisibility", 2);

    c                = get_config_int("video", "displaymode",   0);
    if (c >= 4) {
        c -= 4;
        vid_pal = 1;
    }
    video_set_disptype(c);

    mode7_fontfile   = get_config_string("video", "mode7font", "saa5050");

    fasttape         = get_config_bool("tape", "fasttape",      0);

    scsi_enabled     = get_config_bool("disc", "scsienable", 0);
    ide_enable       = get_config_bool("disc", "ideenable",     0);
    vdfs_enabled     = get_config_bool("disc", "vdfsenable", 0);
    vdfs_cfg_root    = get_config_string("disc", "vdfs_root", 0);

    keyas            = get_config_bool(NULL, "key_as",        0);
    keylogical       = get_config_bool(NULL, "key_logical",   0);
    keypad           = get_config_bool(NULL, "keypad", false);

    mem_jim_setsize(get_config_int(NULL, "jim_mem_size", 0));

    mouse_amx        = get_config_bool(NULL, "mouse_amx",     0);
    mouse_stick      = get_config_bool(NULL, "mouse_stick",   0);
    kbdips           = get_config_int(NULL, "kbdips", 0);

    for (int act = 0; act < KEY_ACTION_MAX; act++) {
        const char *str = al_get_config_value(bem_cfg, "key_actions", keyact_const[act].name);
        if (str) {
            const char *sep = strchr(str, ',');
            size_t size = sep ? sep - str : strlen(str);
            int keycode = -1;
            for (int kc = 0; kc < ALLEGRO_KEY_MAX; kc++) {
                if (!strncmp(al_keycode_to_name(kc), str, size)) {
                    keycode = kc;
                    break;
                }
            }
            if (keycode >= 0) {
                keyactions[act].keycode = keycode;
                keyactions[act].altstate = (sep && !strcmp(sep, ",down"));
            }
        }
    }

    for (c = 0; c < ALLEGRO_KEY_MAX; c++) {
        const char *str = al_get_config_value(bem_cfg, "user_keyboard", al_keycode_to_name(c));
        if (str) {
            unsigned bbckey = strtoul(str, NULL, 16);
            if (bbckey)
                keylookup[c] = bbckey;
        }
    }
    midi_load_config();
}

void set_config_int(const char *sect, const char *key, int value)
{
    char buf[11];

    snprintf(buf, sizeof buf, "%d", value);
    al_set_config_value(bem_cfg, sect, key, buf);
}

void set_config_hex(const char *sect, const char *key, unsigned value)
{
    char buf[11];

    snprintf(buf, sizeof buf, "0x%x", value);
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

static void set_config_path(const char *sect, const char *key, ALLEGRO_PATH *path)
{
    if (path)
        al_set_config_value(bem_cfg, sect, key, al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP));
    else
        al_remove_config_key(bem_cfg, sect, key);
}

void config_save(void)
{
    ALLEGRO_PATH *path;
    const char *cpath;
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

        set_config_path("disc", "disc0", discfns[0]);
        set_config_path("disc", "disc1", discfns[1]);
        set_config_string("disc", "mmb", mmb_fn);
        set_config_string("disc", "mmccard", mmccard_fn);
        set_config_bool("disc", "defaultwriteprotect", defaultwriteprot);

        if (tape_loaded)
            al_set_config_value(bem_cfg, "tape", "tape", al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP));
        else
            al_remove_config_key(bem_cfg, "tape", "tape");

        set_config_bool(NULL, "autopause", autopause);

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
        set_config_bool("sound", "soundpaula",  sound_paula);
        set_config_int("sound", "music5000_filter", music5000_fno);
        set_config_int("sound", "buflen_music5000", buflen_m5);

        set_config_int("sound", "soundwave", curwave);
        set_config_int("sound", "sidmethod", sidmethod);
        set_config_int("sound", "cursid", cursid);

        set_config_int("sound", "ddvol", ddnoise_vol);
        set_config_int("sound", "ddtype", ddnoise_type);

        set_config_int("video", "fullborders", vid_fullborders);
        set_config_int("video", "winmultipler", vid_win_multiplier);
        set_config_int("video", "winsizex", winsizex);
        set_config_int("video", "winsizey", winsizey);

        c = vid_dtype_user;
        if (vid_pal)
            c += 4;
        set_config_int("video", "displaymode", c);
        if (vid_ledlocation >= 0)
            set_config_int("video", "ledlocation", vid_ledlocation);
        set_config_int("video", "ledvisibility", vid_ledvisibility);
        set_config_string("video", "mode7font", mode7_fontfile);

        set_config_bool("tape", "fasttape", fasttape);

        set_config_bool("disc", "scsienable", scsi_enabled);
        set_config_bool("disc", "ideenable", ide_enable);
        set_config_bool("disc", "vdfsenable", vdfs_enabled);
        const char *vdfs_root = vdfs_get_root();
        if (vdfs_root)
            set_config_string("disc", "vdfs_root", vdfs_root);

        set_config_bool(NULL, "key_as", keyas);
        set_config_bool(NULL, "key_logical", keylogical);
        set_config_bool(NULL, "keypad", keypad);
        set_config_int(NULL, "jim_mem_size", mem_jim_size);

        set_config_bool(NULL, "mouse_amx", mouse_amx);
        set_config_bool(NULL, "mouse_stick", mouse_stick);

        for (int c = 0; c < KEY_ACTION_MAX; c++) {
            if (keyactions[c].keycode == keyact_const[c].keycode && keyactions[c].altstate == keyact_const[c].altstate)
                al_remove_config_key(bem_cfg, "key_actions", keyact_const[c].name);
            else {
                char buf[20];
                snprintf(buf, sizeof(buf), "%s,%s", al_keycode_to_name(keyactions[c].keycode), keyactions[c].altstate ? "down" : "up");
                al_set_config_value(bem_cfg, "key_actions", keyact_const[c].name, buf);
            }
        }
        for (int c = 0; c < ALLEGRO_KEY_MAX; c++) {
            if (keylookup[c] == key_allegro2bbc[c])
                al_remove_config_key(bem_cfg, "user_keyboard", al_keycode_to_name(c));
            else {
                char buf[11];
                snprintf(buf, sizeof(buf), "%02x", keylookup[c]);
                al_set_config_value(bem_cfg, "user_keyboard", al_keycode_to_name(c), buf);
            }
        }
        midi_save_config();
        log_debug("config: saving config to %s", cpath);
        al_save_config_file(cpath, bem_cfg);
        al_destroy_path(path);
    } else
        log_error("config: no suitable destination for config file - config will not be saved");
}
