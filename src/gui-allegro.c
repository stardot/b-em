#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "gui-allegro.h"

#include "6502.h"
#include "ide.h"
#include "config.h"
#include "debugger.h"
#include "ddnoise.h"
#include "disc.h"
#include "fullscreen.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "main.h"
#include "mem.h"
#include "mmb.h"
#include "model.h"
#include "mouse.h"
#include "music5000.h"
#include "mmccard.h"
#include "paula.h"
#include "savestate.h"
#include "sid_b-em.h"
#include "scsi.h"
#include "sdf.h"
#include "sound.h"
#include "sn76489.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
#include "textsave.h"
#include "tube.h"
#include "uservia.h"
#include "video.h"
#include "video_render.h"
#include "vdfs.h"

#if defined(HAVE_JACK_JACK_H) || defined(HAVE_ALSA_ASOUNDLIB_H)
#define HAVE_LINUX_MIDI
#include "midi-linux.h"
#endif

#define ROM_LABEL_LEN 50

typedef struct {
    const char *label;
    int itemno;
} menu_map_t;

static ALLEGRO_MENU *disc_menu;
static ALLEGRO_MENU *rom_menu;

static inline int menu_id_num(menu_id_t id, int num)
{
    return (num << 8) | id;
}

static inline menu_id_t menu_get_id(ALLEGRO_EVENT *event)
{
    return event->user.data1 & 0xff;
}

static inline int menu_get_num(ALLEGRO_EVENT *event)
{
    return event->user.data1 >> 8;
}

static void add_checkbox_item(ALLEGRO_MENU *parent, char const *title, uint16_t id, bool checked)
{
    int flags = ALLEGRO_MENU_ITEM_CHECKBOX;
    if (checked)
        flags |= ALLEGRO_MENU_ITEM_CHECKED;
    al_append_menu_item(parent, title, id, flags, NULL, NULL);
}

static void add_radio_item(ALLEGRO_MENU *parent, char const *title, uint16_t id, int this_value, int cur_value)
{
    add_checkbox_item(parent, title, menu_id_num(id, this_value), this_value == cur_value);
}

static void add_radio_set(ALLEGRO_MENU *parent, char const **labels, uint16_t id, int cur_value)
{
    const char *label;

    for (int i = 0; (label = *labels++); i++)
        add_checkbox_item(parent, label, menu_id_num(id, i), i == cur_value);
}

static int menu_cmp(const void *va, const void *vb)
{
    menu_map_t *a = (menu_map_t *)va;
    menu_map_t *b = (menu_map_t *)vb;
    return strcasecmp(a->label, b->label);
}

static void add_sorted_set(ALLEGRO_MENU *parent, menu_map_t *map, size_t items, uint16_t id, int cur_value)
{
    qsort(map, items, sizeof(menu_map_t), menu_cmp);
    for (int i = 0; i < items; i++) {
        int ino = map[i].itemno;
        add_checkbox_item(parent, map[i].label, menu_id_num(id, ino), ino == cur_value);
    }
}

static ALLEGRO_MENU *create_print_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_radio_item(menu, "Print to stdout (text)", IDM_FILE_PRINT, PDEST_STDOUT, print_dest);
    add_radio_item(menu, "Print to file (text)", IDM_FILE_PRINT, PDEST_FILE_TEXT, print_dest);
    add_radio_item(menu, "Print to file (binary)", IDM_FILE_PRINT, PDEST_FILE_BIN, print_dest);
    add_radio_item(menu, "Print to command (text)", IDM_FILE_PRINT, PDEST_PIPE_TEXT, print_dest);
    add_radio_item(menu, "Print to command (binary)", IDM_FILE_PRINT, PDEST_PIPE_BIN, print_dest);
    return menu;
}

static ALLEGRO_MENU *create_file_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Hard Reset", IDM_FILE_RESET, 0, NULL, NULL);
    al_append_menu_item(menu, "Load state...", IDM_FILE_LOAD_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Save State...", IDM_FILE_SAVE_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Save Screenshot...", IDM_FILE_SCREEN_SHOT, 0, NULL, NULL);
    al_append_menu_item(menu, "Save Screen as Text...", IDM_FILE_SCREEN_TEXT, 0, NULL, NULL);
    al_append_menu_item(menu, "Printing...", 0, 0, NULL, create_print_menu());
    add_checkbox_item(menu, "Serial to file", IDM_FILE_SERIAL, sysacia_fp);
    add_checkbox_item(menu, music5000_rec.prompt, IDM_FILE_M5000, music5000_rec.fp);
    add_checkbox_item(menu, paula_rec.prompt, IDM_FILE_PAULAREC, paula_rec.fp);
    add_checkbox_item(menu, sound_rec.prompt, IDM_FILE_SOUNDREC, sound_rec.fp);
    al_append_menu_item(menu, "Exit", IDM_FILE_EXIT, 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_edit_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Paste via OS", IDM_EDIT_PASTE_OS, 0, NULL, NULL);
    al_append_menu_item(menu, "Paste via keyboard", IDM_EDIT_PASTE_KB, 0, NULL, NULL);
    add_checkbox_item(menu, "Printer to clipboard", IDM_EDIT_COPY, prt_clip_str);
    return menu;
}

static ALLEGRO_MENU *create_disc_new_menu(int drive)
{
    ALLEGRO_MENU *menu = al_create_menu();

    al_append_menu_item(menu, "Acorn DFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_10S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_10S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Double-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_10S_INT_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_10S_INT_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Single-sided, 40T (S)", menu_id_num(IDM_DISC_NEW_ADFS_S, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Single-sided, 80T (M)", menu_id_num(IDM_DISC_NEW_ADFS_M, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Double-sided, 80T (L)", menu_id_num(IDM_DISC_NEW_ADFS_L, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_16S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_16S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_16S_INT_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_18S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_18S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_18S_INT_80T, drive), 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_disc_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();

    al_append_menu_item(menu, "Autoboot disc in 0/2...", IDM_DISC_AUTOBOOT, 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :0/2...", menu_id_num(IDM_DISC_LOAD, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :1/3...", menu_id_num(IDM_DISC_LOAD, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "Load MMB file...", IDM_DISC_MMB_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Load SD Card...", IDM_DISC_MMC_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :0/2", menu_id_num(IDM_DISC_EJECT, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :1/3", menu_id_num(IDM_DISC_EJECT, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject MMB file", IDM_DISC_MMB_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject SD Card", IDM_DISC_MMC_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "New disc :0/2...", 0, 0, NULL, create_disc_new_menu(0));
    al_append_menu_item(menu, "New disc :1/3...", 0, 0, NULL, create_disc_new_menu(1));
    add_checkbox_item(menu, "Write protect disc :0/2", menu_id_num(IDM_DISC_WPROT, 0), drives[0].writeprot);
    add_checkbox_item(menu, "Write protect disc :1/3", menu_id_num(IDM_DISC_WPROT, 1), drives[1].writeprot);
    add_checkbox_item(menu, "Default write protect", IDM_DISC_WPROT_D, defaultwriteprot);
    add_checkbox_item(menu, "IDE hard disc", IDM_DISC_HARD_IDE, ide_enable);
    add_checkbox_item(menu, "SCSI hard disc", IDM_DISC_HARD_SCSI, scsi_enabled);
    add_checkbox_item(menu, "VDFS Enabled", IDM_DISC_VDFS_ENABLE, vdfs_enabled);
    al_append_menu_item(menu, "Choose VDFS Root...", IDM_DISC_VDFS_ROOT, 0, NULL, NULL);
    disc_menu = menu;
    return menu;
}

void gui_allegro_set_eject_text(int drive, ALLEGRO_PATH *path)
{
    char temp[256];
    if (path)
        snprintf(temp, sizeof temp, "Eject drive %s: %s", drive ? "1/3" : "0/2", al_get_path_filename(path));
    else
        snprintf(temp, sizeof temp, "Eject drive %s", drive ? "1/3" : "0/2");
    al_set_menu_item_caption(disc_menu, menu_id_num(IDM_DISC_EJECT, drive), temp);
}

static ALLEGRO_MENU *create_tape_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *speed = al_create_menu();
    int nflags, fflags;
    al_append_menu_item(menu, "Load tape...", IDM_TAPE_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Rewind tape", IDM_TAPE_REWIND, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject tape", IDM_TAPE_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "Catalogue tape", IDM_TAPE_CAT, 0, NULL, NULL);
    if (fasttape) {
        nflags = ALLEGRO_MENU_ITEM_CHECKBOX;
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        nflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(speed, "Normal", IDM_TAPE_SPEED_NORMAL, nflags, NULL, NULL);
    al_append_menu_item(speed, "Fast", IDM_TAPE_SPEED_FAST, fflags, NULL, NULL);
    al_append_menu_item(menu, "Tape speed", 0, 0, NULL, speed);
    return menu;
}

static void gen_rom_label(int slot, char *dest)
{
    const char *rr = rom_slots[slot].swram ? "RAM" : "ROM";
    const uint8_t *detail = mem_romdetail(slot);
    const char *name = rom_slots[slot].name;
    if (detail) {
        int ver = *detail++;
        if (name)
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s %02X (%s)", slot, rr, detail, ver, name);
        else
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s %02X", slot, rr, detail, ver);
    } else {
        if (name)
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s", slot, rr, name);
        else
            snprintf(dest, ROM_LABEL_LEN, "%02d %s", slot, rr);
    }
}

static ALLEGRO_MENU *create_rom_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int slot = ROM_NSLOT-1; slot >= 0; slot--) {
        char label[ROM_LABEL_LEN];
        gen_rom_label(slot, label);
        ALLEGRO_MENU *sub = al_create_menu();
        al_append_menu_item(sub, "Load...", menu_id_num(IDM_ROMS_LOAD, slot), 0, NULL, NULL);
        al_append_menu_item(sub, "Clear", menu_id_num(IDM_ROMS_CLEAR, slot), 0, NULL, NULL);
        add_checkbox_item(sub, "RAM", menu_id_num(IDM_ROMS_RAM, slot), rom_slots[slot].swram);
        al_append_menu_item(menu, label, slot+1, 0, NULL, sub);
    }
    rom_menu = menu;
    return menu;
}

static void update_rom_menu(void)
{
    ALLEGRO_MENU *menu = rom_menu;
    
    for (int slot = ROM_NSLOT-1; slot >= 0; slot--) {
        char label[ROM_LABEL_LEN];
        gen_rom_label(slot, label);
        al_set_menu_item_caption(menu, slot-ROM_NSLOT+1, label);
        ALLEGRO_MENU *sub = al_find_menu(menu, slot+1);
        if (sub) {
            int flags = rom_slots[slot].swram ? ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED : ALLEGRO_MENU_ITEM_CHECKBOX;
            al_set_menu_item_flags(sub, menu_id_num(IDM_ROMS_RAM, slot), flags);
        }
        else
            log_debug("gui-allegro: ROM sub-menu not found for slot %d", slot);
    }
}

static ALLEGRO_MENU *create_model_menu(void)
{
    menu_map_t *map = calloc(model_count * 2, sizeof(menu_map_t));
    if (map) {
        ALLEGRO_MENU *menu = al_create_menu();
        menu_map_t *groups = map + model_count;
        int ngroup = 0;
        for (int model_no = 0; model_no < model_count; ++model_no) {
            const char *group = models[model_no].group;
            if (group) {
                bool found = false;
                for (int group_no = 0; group_no < ngroup; ++group_no) {
                    if (!strcmp(group, groups[group_no].label)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    groups[ngroup].label = group;
                    groups[ngroup].itemno = ngroup;
                    ++ngroup;
                }
            }
        }
        qsort(groups, ngroup, sizeof(menu_map_t), menu_cmp);
        for (int group_no = 0; group_no < ngroup; ++group_no) {
            const char *group_label = groups[group_no].label;
            int item_no = 0;
            for (int model_no = 0; model_no < model_count; ++model_no) {
                const char *model_group = models[model_no].group;
                if (model_group && !strcmp(model_group, group_label)) {
                    map[item_no].label = models[model_no].name;
                    map[item_no].itemno = model_no;
                    ++item_no;
                }
            }
            ALLEGRO_MENU *sub = al_create_menu();
            add_sorted_set(sub, map, item_no, IDM_MODEL, curmodel);
            al_append_menu_item(menu, groups[group_no].label, 0, 0, NULL, sub);
        }
        int item_no = 0;
        for (int model_no = 0; model_no < model_count; ++model_no) {
            if (!models[model_no].group) {
                map[item_no].label = models[model_no].name;
                map[item_no].itemno = model_no;
                ++item_no;
            }
        }
        add_sorted_set(menu, map, item_no, IDM_MODEL, curmodel);
        free(map);
        return menu;
    }
    else {
        log_fatal("gui-allegro: out of memory");
        exit(1);
    }
}

static ALLEGRO_MENU *create_tube_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    menu_map_t *map = malloc(num_tubes * sizeof(menu_map_t));
    if (map) {
        for (int i = 0; i < num_tubes; ++i) {
            map[i].label = tubes[i].name;
            map[i].itemno = i;
        }
        add_sorted_set(menu, map, num_tubes, IDM_TUBE, curtube);
        for (int i = 0; i < NUM_TUBE_SPEEDS; i++)
            add_radio_item(sub, tube_speeds[i].name, IDM_TUBE_SPEED, i, tube_speed_num);
        al_append_menu_item(menu, "Tube speed", 0, 0, NULL, sub);
        return menu;
    }
    else {
        log_fatal("gui-allegro: out of memory");
        exit(1);
    }
}

static const char *mode7_font_files[] = { "saa5050", "brandy", "basicsdl", "original", NULL };
static const char *mode7_font_names[] = { "SAA5050", "Brandy BASIC", "BBC BASIC for SDL", "B-Em Original", NULL };
static int mode7_font_index;

static ALLEGRO_MENU *create_m7font_menu(void)
{
    int c = 0;
    const char *ptr;
    while ((ptr = mode7_font_files[c])) {
        if (!strcmp(ptr, mode7_fontfile)) {
            mode7_font_index = c;
            break;
        }
        ++c;
    }
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, mode7_font_names, IDM_VIDEO_MODE7_FONT, mode7_font_index);
    return sub;
}

static const char *border_names[] = { "None", "Medium", "Full", NULL };
static const char *vmode_names[] = { "Scaled", "Interlace", "Scanlines", "Line doubling", NULL };
static const char *colout_names[] = { "RGB", "PAL", "Green Mono", "Amber Mono", "White Mono", NULL };
static const char *win_mult_names[] = { "Freeform", "1x", "2x", "3x", NULL };
static const char *led_location_names[] = { "None", "Overlapped", "Separate", NULL };
static const char *led_visibility_names[] = { "When changed", "When changed or transient", "Always", NULL };

static ALLEGRO_MENU *create_video_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, vmode_names, IDM_VIDEO_DISPTYPE, vid_dtype_user);
    al_append_menu_item(menu, "Display type...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, colout_names, IDM_VIDEO_COLTYPE, vid_colour_out);
    al_append_menu_item(menu, "Colour type...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, border_names, IDM_VIDEO_BORDERS, vid_fullborders);
    al_append_menu_item(menu, "Borders...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, win_mult_names, IDM_VIDEO_WIN_MULT, vid_win_multiplier);
    al_append_menu_item(menu, "Default window scaling...", 0, 0, NULL, sub);
    al_append_menu_item(menu, "Reset Window Size", IDM_VIDEO_WINSIZE, 0, NULL, NULL);
    add_checkbox_item(menu, "Fullscreen", IDM_VIDEO_FULLSCR, fullscreen);
    add_checkbox_item(menu, "NuLA", IDM_VIDEO_NULA, !nula_disable);
    sub = al_create_menu();
    al_append_menu_item(menu, "LED location...", 0, 0, NULL, sub);
    add_radio_set(sub, led_location_names, IDM_VIDEO_LED_LOCATION, vid_ledlocation);
    sub = al_create_menu();
    al_append_menu_item(menu, "LED visibility...", 0, 0, NULL, sub);
    add_radio_set(sub, led_visibility_names, IDM_VIDEO_LED_VISIBILITY, vid_ledvisibility);
    al_append_menu_item(menu, "Mode 7 Font...", 0, 0, NULL, create_m7font_menu());
    add_checkbox_item(menu, "Write-only Bitmaps", IDM_VIDEO_LOCK, vid_lock_type == ALLEGRO_LOCK_WRITEONLY);
    return menu;
}

static const char *sid_names[] =
{
    "6581",
    "8580",
    "8580 + digi boost",
    "6581R4",
    "6581R3 4885",
    "6581R3 0486S",
    "6581R3 3984",
    "6581R4AR 3789",
    "6581R3 4485",
    "6581R4 1986S",
    "8580R5 3691",
    "8580R5 3691 + digi boost",
    "8580R5 1489",
    "8580R5 1489 + digi boost",
    NULL
};

static ALLEGRO_MENU *create_sid_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, sid_names, IDM_SID_TYPE, cursid);
    al_append_menu_item(menu, "Model", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_item(sub, "Interpolating", IDM_SID_METHOD, 0, sidmethod);
    add_radio_item(sub, "Resampling",    IDM_SID_METHOD, 1, sidmethod);
    al_append_menu_item(menu, "Sample method", 0, 0, NULL, sub);
    return menu;
}

static const char *wave_names[] = { "Square", "Saw", "Sine", "Triangle", "SID", NULL };
static const char *dd_type_names[] = { "5.25\"", "3.5\"", NULL };
static const char *dd_noise_vols[] = { "33%", "66%", "100%", NULL };
static const char *filt_freq[] = { "Original (3214Hz)", "16kHz", NULL };

static ALLEGRO_MENU *create_sound_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub;
    add_checkbox_item(menu, "Internal sound chip",   IDM_SOUND_INTERNAL,  sound_internal);
    add_checkbox_item(menu, "BeebSID",               IDM_SOUND_BEEBSID,   sound_beebsid);
    add_checkbox_item(menu, "Music 5000",            IDM_SOUND_MUSIC5000, sound_music5000);
    sub = al_create_menu();
    add_radio_set(sub, filt_freq, IDM_SOUND_MFILT, music5000_fno);
    al_append_menu_item(menu, "Music 5000 Filter", 0, 0, NULL, sub);
    add_checkbox_item(menu, "Paula",                 IDM_SOUND_PAULA,     sound_paula);
    add_checkbox_item(menu, "Printer port DAC",      IDM_SOUND_DAC,       sound_dac);
    add_checkbox_item(menu, "Disc drive noise",      IDM_SOUND_DDNOISE,   sound_ddnoise);
    add_checkbox_item(menu, "Tape noise",            IDM_SOUND_TAPE,      sound_tape);
    add_checkbox_item(menu, "Internal sound filter", IDM_SOUND_FILTER,    sound_filter);
    sub = al_create_menu();
    add_radio_set(sub, wave_names, IDM_WAVE, curwave);
    al_append_menu_item(menu, "Internal waveform", 0, 0, NULL, sub);
    al_append_menu_item(menu, "reSID configuration", 0, 0, NULL, create_sid_menu());
    sub = al_create_menu();
    add_radio_set(sub, dd_type_names, IDM_DISC_TYPE, ddnoise_type);
    al_append_menu_item(menu, "Disc drive type", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, dd_noise_vols, IDM_DISC_VOL, ddnoise_vol);
    al_append_menu_item(menu, "Disc noise volume", 0, 0, NULL, sub);
    return menu;
}

#ifdef HAVE_LINUX_MIDI

static ALLEGRO_MENU *create_midi_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M4000_JACK, midi_music4000.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M4000_ASEQ, midi_music4000.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M4000_ARAW, midi_music4000.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M4000 Keyboard", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT1_JACK, midi_music2000_out1.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT1_ASEQ, midi_music2000_out1.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT1_ARAW, midi_music2000_out1.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 1", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT2_JACK, midi_music2000_out2.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT2_ASEQ, midi_music2000_out2.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT2_ARAW, midi_music2000_out2.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 2", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT3_JACK, midi_music2000_out3.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT3_ASEQ, midi_music2000_out3.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT3_ARAW, midi_music2000_out3.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 3", 0, 0, NULL, sub);
    return menu;
}

#endif

static ALLEGRO_MENU *create_keyboard_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, bem_key_modes, IDM_KEY_MODE, key_mode);
    al_append_menu_item(menu, "Keyboard Mode", 0, 0, NULL, sub);
    add_checkbox_item(menu, "Map CAPS/CTRL to A/S", IDM_KEY_AS, keyas);
    add_checkbox_item(menu, "PC/XT Keypad Mode", IDM_KEY_PAD, keypad);
    al_append_menu_item(menu, "Remap Keyboard", IDM_KEY_REDEFINE, 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_joystick_menu(int joystick)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int i = 0; i < joystick_count; i++) if (joystick_names[i])
        add_checkbox_item(menu, joystick_names[i], menu_id_num(IDM_JOYSTICK + joystick, i), i == joystick_index[joystick]);
    return menu;
}

static ALLEGRO_MENU *create_joymap_menu(int joystick)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int i = 0; i < joymap_count; i++)
        add_checkbox_item(menu, joymaps[i].name, menu_id_num(IDM_JOYMAP + joystick, i), i == joymap_index[joystick]);
    return menu;
}

static ALLEGRO_MENU *create_joysticks_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_checkbox_item(menu, "Tricky SEGA Adapter", IDM_TRIACK_SEGA_ADAPTER, autopause);
    al_append_menu_item(menu, "Joystick", 0, 0, NULL, create_joystick_menu(0));
    al_append_menu_item(menu, "Joystick Map", 0, 0, NULL, create_joymap_menu(0));
    if (joystick_count > 1) {
        al_append_menu_item(menu, "Joystick 2", 0, 0, NULL, create_joystick_menu(1));
        al_append_menu_item(menu, "Joystick 2 Map", 0, 0, NULL, create_joymap_menu(1));
    }
    return menu;
}


static const char *jim_sizes[] =
{
    "None (disabled)",
    "16M",
    "64M",
    "256M",
    "480M",
    "996M",
    NULL
};

static ALLEGRO_MENU *create_jim_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_radio_set(menu, jim_sizes, IDM_JIM_SIZE, mem_jim_size);
    return menu;
}

static ALLEGRO_MENU *create_settings_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Video...", 0, 0, NULL, create_video_menu());
    al_append_menu_item(menu, "Sound...", 0, 0, NULL, create_sound_menu());
#ifdef HAVE_LINUX_MIDI
    al_append_menu_item(menu, "MIDI", 0, 0, NULL, create_midi_menu());
#endif
    al_append_menu_item(menu, "Keyboard", 0, 0, NULL, create_keyboard_menu());
    al_append_menu_item(menu, "Jim Memory", 0, 0, NULL, create_jim_menu());
    add_checkbox_item(menu, "Auto-Pause", IDM_AUTO_PAUSE, autopause);
    add_checkbox_item(menu, "Mouse (AMX)", IDM_MOUSE_AMX, mouse_amx);
    if (joystick_count > 0)
        al_append_menu_item(menu, "Joysticks", 0, 0, NULL, create_joysticks_menu());
    add_checkbox_item(menu, "Joystick Mouse", IDM_MOUSE_STICK, mouse_stick);
    return menu;
}

static ALLEGRO_MENU *create_speed_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_radio_item(menu, "Paused", IDM_SPEED, EMU_SPEED_PAUSED, emuspeed);
    for (int i = 0; i < num_emu_speeds; i++)
        add_radio_item(menu, emu_speeds[i].name, IDM_SPEED, i, emuspeed);
    add_radio_item(menu, "Full-speed", IDM_SPEED, EMU_SPEED_FULL, emuspeed);
    add_checkbox_item(menu, "Auto Frameskip", IDM_AUTOSKIP, autoskip);
    return menu;
}

static ALLEGRO_MENU *create_debug_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_checkbox_item(menu, "Debugger", IDM_DEBUGGER, debug_core);
    add_checkbox_item(menu, "Debug Tube", IDM_DEBUG_TUBE, debug_tube);
    al_append_menu_item(menu, "Break", IDM_DEBUG_BREAK, 0, NULL, NULL);
    return menu;
}

void gui_allegro_init(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_DISPLAY *display)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "File", 0, 0, NULL, create_file_menu());
    al_append_menu_item(menu, "Edit", 0, 0, NULL, create_edit_menu());
    al_append_menu_item(menu, "Disc", 0, 0, NULL, create_disc_menu());
    al_append_menu_item(menu, "Tape", 0, 0, NULL, create_tape_menu());
    al_append_menu_item(menu, "ROM", 0, 0, NULL, create_rom_menu());
    al_append_menu_item(menu, "Model", 0, 0, NULL, create_model_menu());
    al_append_menu_item(menu, "Tube", 0, 0, NULL, create_tube_menu());
    al_append_menu_item(menu, "Settings", 0, 0, NULL, create_settings_menu());
    al_append_menu_item(menu, "Speed", 0, 0, NULL, create_speed_menu());
    al_append_menu_item(menu, "Debug", 0, 0, NULL, create_debug_menu());
    al_set_display_menu(display, menu);
    al_register_event_source(queue, al_get_default_menu_event_source());
}

void gui_allegro_destroy(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_DISPLAY *display)
{
    al_unregister_event_source(queue, al_get_default_menu_event_source());
    al_set_display_menu(display, NULL);
}

static int radio_event_simple(ALLEGRO_EVENT *event, int current)
{
    int id = menu_get_id(event);
    int num = menu_get_num(event);
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    al_set_menu_item_flags(menu, menu_id_num(id, current), ALLEGRO_MENU_ITEM_CHECKBOX);
    return num;
}

static int radio_event_with_deselect(ALLEGRO_EVENT *event, int current)
{
    int id = menu_get_id(event);
    int num = menu_get_num(event);
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (num == current)
        num = -1;
    else
        al_set_menu_item_flags(menu, menu_id_num(id, current), ALLEGRO_MENU_ITEM_CHECKBOX);
    return num;
}

static void file_chooser_generic(ALLEGRO_EVENT *event, const char *initial_path, const char *title, const char *patterns, int flags, void (*callback)(const char *))
{
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(initial_path, title, patterns, flags);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0)
                callback(al_get_native_file_dialog_path(chooser, 0));
        }
        al_destroy_native_file_dialog(chooser);
    }
}

static void file_save_scrshot(const char *path)
{
    strncpy(vid_scrshotname, path, sizeof vid_scrshotname-1);
    vid_scrshotname[sizeof vid_scrshotname-1] = 0;
    vid_savescrshot = 2;
}

static void file_print_chooser(ALLEGRO_EVENT *event, enum print_dest_type new_dest)
{
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, "Print to file", "*.prn", ALLEGRO_FILECHOOSER_SAVE);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                const char *new_name = al_get_native_file_dialog_path(chooser, 0);
                if (new_name) {
                    char *new_copy = strdup(new_name);
                    if (new_copy) {
                        if (print_filename_alloc)
                            free(print_filename);
                        print_filename = new_copy;
                        print_filename_alloc = true;
                        print_dest = new_dest;
                    }
                }
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
    else
        al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), new_dest, 0);
}

static void file_print_change(ALLEGRO_EVENT *event)
{
    enum print_dest_type new_dest = menu_get_num(event);
    enum print_dest_type old_dest = print_dest;
    printer_close();
    if (new_dest != old_dest) {
        if (new_dest == PDEST_FILE_TEXT || new_dest == PDEST_FILE_BIN)
            file_print_chooser(event, new_dest);
        else
            print_dest = new_dest;
        al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), menu_id_num(IDM_FILE_PRINT, old_dest), 0);
    }
}

static void serial_rec(ALLEGRO_EVENT *event)
{

    if (sysacia_fp)
        sysacia_rec_stop();
    else {
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, "Record serial to file", "*.txt", ALLEGRO_FILECHOOSER_SAVE);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            while (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) <= 0)
                    break;
                if (sysacia_rec_start(al_get_native_file_dialog_path(chooser, 0)))
                    break;
            }
            al_destroy_native_file_dialog(chooser);
        }
    }
}

static void toggle_record(ALLEGRO_EVENT *event, sound_rec_t *rec)
{
    if (rec->fp)
        sound_stop_rec(rec);
    else {
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, rec->prompt, "*.wav", ALLEGRO_FILECHOOSER_SAVE);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            while (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) <= 0)
                    break;
                if (sound_start_rec(rec, al_get_native_file_dialog_path(chooser, 0)))
                    break;
            }
            al_destroy_native_file_dialog(chooser);
        }
    }
}

static void edit_paste_start(ALLEGRO_EVENT *event, void (*paste_start)(char *str))
{
    ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
    char *text = al_get_clipboard_text(display);
#ifndef WIN32
    if (!text) {
        sleep(1);  // try again - Allegro bug.
        text = al_get_clipboard_text(display);
    }
#endif
    if (text)
        paste_start(text);
}

static void edit_print_clip(ALLEGRO_EVENT *event)
{
    if (prt_clip_str) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        al_set_clipboard_text(display, al_cstr(prt_clip_str));
        al_ustr_free(prt_clip_str);
        prt_clip_str = NULL;
    }
    else
        prt_clip_str = al_ustr_dup(al_ustr_empty_string());
}

void gui_set_disc_wprot(int drive, bool enabled)
{
    al_set_menu_item_flags(disc_menu, menu_id_num(IDM_DISC_WPROT, drive), enabled ? ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED : ALLEGRO_MENU_ITEM_CHECKBOX);
}

static void disc_choose_new(ALLEGRO_EVENT *event, const char *ext)
{
    int drive = menu_get_num(event);
    ALLEGRO_PATH *apath = drives[drive].discfn;
    ALLEGRO_FILECHOOSER *chooser;
    const char *fpath;
    char name[20], title[70];
    snprintf(name, sizeof(name), "new%s", strchr(ext, '.'));
    if (apath) {
        apath = al_clone_path(apath);
        al_set_path_filename(apath, name);
        fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP);
    }
    else
        fpath = name;
    snprintf(title, sizeof title, "Choose an image file name to create for drive %d/%d", drive, drive+2);
    if ((chooser = al_create_native_file_dialog(fpath, title, ext, ALLEGRO_FILECHOOSER_SAVE))) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                disc_close(drive);
                if (drives[drive].discfn)
                    al_destroy_path(drives[drive].discfn);
                drives[drive].discfn = path;
                switch(menu_get_id(event)) {
                    case IDM_DISC_NEW_ADFS_S:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_s);
                        break;
                    case IDM_DISC_NEW_ADFS_M:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_m);
                        break;
                    case IDM_DISC_NEW_ADFS_L:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_l);
                        break;
                    case IDM_DISC_NEW_DFS_10S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_INT_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_int_40t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_int_80t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_int_80t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_int_80t);
                        break;
                    default:
                        break;
                }
                gui_set_disc_wprot(drive, drives[drive].writeprot);
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
    if (fpath != name)
        al_destroy_path(apath);
}

static void disc_choose(ALLEGRO_EVENT *event, const char *opname, const char *exts, int flags)
{
    int drive = menu_get_num(event);
    ALLEGRO_PATH *apath;
    const char *fpath;
    if (!(apath = drives[drive].discfn) || !(fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    char title[70];
    snprintf(title, sizeof title, "Choose a disc to %s drive %d/%d", opname, drive, drive+2);
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(fpath, title, exts, flags);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                disc_close(drive);
                if (drives[drive].discfn)
                    al_destroy_path(drives[drive].discfn);
                drives[drive].discfn = path;
                switch(menu_get_id(event)) {
                    case IDM_DISC_AUTOBOOT:
                        main_reset();
                        autoboot = 150;
                        /* FALLTHROUGH */
                    case IDM_DISC_LOAD:
                        if (!disc_load(drive, path)) {
                            if (defaultwriteprot)
                                drives[drive].writeprot = 1;
                        }
                        else {
                            al_destroy_path(path);
                            drives[drive].discfn = NULL;
                        }
                        break;
                    default:
                        break;
                }
                gui_set_disc_wprot(drive, drives[drive].writeprot);
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
}

static void disc_eject(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    disc_close(drive);
    if (drives[drive].discfn) {
        al_destroy_path(drives[drive].discfn);
        drives[drive].discfn = NULL;
    }
    al_set_menu_item_caption(disc_menu, menu_id_num(IDM_DISC_EJECT, drive), drive ? "Eject disc :1/3" : "Eject disc :0/2");
}

static void disc_wprot(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    drives[drive].writeprot = !drives[drive].writeprot;
}

static void disc_mmb_load(const char *path)
{
    char *fn = strdup(path);
    mmb_eject();
    mmb_load(fn);
}

static void disc_mmc_load(const char *path)
{
    char *fn = strdup(path);
    mmccard_eject();
    mmccard_load(fn);
}

static void disc_toggle_ide(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (ide_enable) {
        ide_close();
        ide_enable = false;
    }
    else {
        if (scsi_enabled) {
            scsi_close();
            scsi_enabled = false;
            al_set_menu_item_flags(menu, IDM_DISC_HARD_SCSI, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
        ide_enable = true;
        ide_init();
    }
}

static void disc_toggle_scsi(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (scsi_enabled) {
        scsi_close();
        scsi_enabled = false;
    }
    else {
        if (ide_enable) {
            ide_close();
            ide_enable = false;
            al_set_menu_item_flags(menu, IDM_DISC_HARD_IDE, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
        scsi_enabled = true;
        scsi_init();
    }
}

static void disc_vdfs_root(const char *path)
{
    vdfs_set_root(path);
    config_save();
}

static void tape_load_ui(ALLEGRO_EVENT *event)
{
    const char *fpath;
    if (!tape_fn || !(fpath = al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(fpath, "Choose a tape to load", "*.uef;*.csw", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                tape_close();
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                tape_load(path);
                tape_fn = path;
                tape_loaded = 1;
            }
        }
    }
}

static void tape_rewind(void)
{
    tape_close();
    tape_load(tape_fn);
}

static void tape_eject(void)
{
    tape_close();
    tape_loaded = 0;
}

static void tape_normal(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (fasttape) {
        fasttape = false;
        al_set_menu_item_flags(menu, IDM_TAPE_SPEED_FAST, ALLEGRO_MENU_ITEM_CHECKBOX);
    }
}

static void tape_fast(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (!fasttape) {
        fasttape = true;
        al_set_menu_item_flags(menu, IDM_TAPE_SPEED_NORMAL, ALLEGRO_MENU_ITEM_CHECKBOX);
    }
}

static void rom_load(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    rom_slot_t *slotp = rom_slots + slot;
    if (!slotp->locked) {
        char tempname[PATH_MAX];
        if (slotp->name)
            strncpy(tempname, slotp->name, sizeof tempname-1);
        else
            tempname[0] = 0;
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(tempname, "Choose a ROM to load", "*.rom", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            if (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) > 0) {
                    char label[ROM_LABEL_LEN];
                    ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                    mem_clearrom(slot);
                    mem_loadrom(slot, al_get_path_filename(path), al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP), 0);
                    al_destroy_path(path);
                    gen_rom_label(slot, label);
                    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
                }
            }
        }
    }
}

static void rom_clear(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    char label[ROM_LABEL_LEN];

    mem_clearrom(slot);
    gen_rom_label(slot, label);
    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
}

static void rom_ram_toggle(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    char label[ROM_LABEL_LEN];

    rom_slots[slot].swram = !rom_slots[slot].swram;
    gen_rom_label(slot, label);
    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
}

static void change_model(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);
    if (curmodel == menu_get_num(event)) {
        al_set_menu_item_flags(menu, menu_id_num(IDM_MODEL, curmodel), ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
        return;
    }
    al_set_menu_item_flags(menu, menu_id_num(IDM_MODEL, curmodel), ALLEGRO_MENU_ITEM_CHECKBOX);
    config_save();
    oldmodel = curmodel;
    curmodel = menu_get_num(event);
    main_restart();
    oldmodel = curmodel;
    update_rom_menu();
}

static void change_tube(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);
    int newtube = menu_get_num(event);
    log_debug("gui: change_tube newtube=%d", newtube);
    if (newtube == selecttube)
        selecttube = -1;
    else {
        al_set_menu_item_flags(menu, menu_id_num(IDM_TUBE, selecttube), ALLEGRO_MENU_ITEM_CHECKBOX);
        selecttube = newtube;
    }
    main_restart();
    update_rom_menu();
}

static void change_tube_speed(ALLEGRO_EVENT *event)
{
    tube_speed_num = radio_event_simple(event, tube_speed_num);
    tube_updatespeed();
}

static void set_sid_type(ALLEGRO_EVENT *event)
{
    cursid = radio_event_simple(event, cursid);
    sid_settype(sidmethod, cursid);
}

static void set_sid_method(ALLEGRO_EVENT *event)
{
    sidmethod = radio_event_simple(event, sidmethod);
    sid_settype(sidmethod, cursid);
}

static void change_ddnoise_dtype(ALLEGRO_EVENT *event)
{
    ddnoise_type = radio_event_simple(event, ddnoise_type);
    ddnoise_close();
    ddnoise_init();
}

static void change_mode7_font(ALLEGRO_EVENT *event)
{
    int newix = radio_event_simple(event, mode7_font_index);
    if (mode7_loadchars(mode7_font_files[newix]))
        mode7_font_index = newix;
}

static void change_video_lock(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)event->user.data3;
    int flags = al_get_menu_item_flags(menu, IDM_VIDEO_LOCK);
    log_debug("gui-allegro: menu_id=%p, flags=%04x", menu, flags);
    if (flags & ALLEGRO_MENU_ITEM_CHECKED)
        vid_lock_type = ALLEGRO_LOCK_WRITEONLY;
    else
        vid_lock_type = ALLEGRO_LOCK_READWRITE;
}

static void toggle_music5000(void)
{
    if (sound_music5000) {
        sound_music5000 = false;
        music5000_close();
    }
    else {
        sound_music5000 = true;
        music5000_init(emuspeed);
    }
}    

static const char all_dext[] = "*.ssd;*.dsd;*.img;*.adf;*.ads;*.adm;*.adl;*.sdd;*.ddd;*.fdi;*.imd;*.hfe;"
                               "*.SSD;*.DSD;*.IMG;*.ADF;*.ADS;*.ADM;*.ADL;*.SDD;*.DDD;*.FDI;*.IMD;*.HFE";

void gui_allegro_event(ALLEGRO_EVENT *event)
{
    switch(menu_get_id(event)) {
        case IDM_ZERO:
            break;
        case IDM_FILE_RESET:
            nula_reset();
            main_restart();
            update_rom_menu();
            break;
        case IDM_FILE_LOAD_STATE:
            file_chooser_generic(event, savestate_name, "Load state from file", "*.snp", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, savestate_load);
            break;
        case IDM_FILE_SAVE_STATE:
            file_chooser_generic(event, savestate_name, "Save state to file", "*.snp", ALLEGRO_FILECHOOSER_SAVE, savestate_save);
            break;
        case IDM_FILE_SCREEN_SHOT:
            file_chooser_generic(event, vid_scrshotname, "Save screenshot to file", "*.bmp;*.pcx;*.tga;*.png;*.jpg", ALLEGRO_FILECHOOSER_SAVE, file_save_scrshot);
            break;
        case IDM_FILE_SCREEN_TEXT:
            file_chooser_generic(event, savestate_name, "Save screen as text to file", "*.txt", ALLEGRO_FILECHOOSER_SAVE, textsave);
            break;
        case IDM_FILE_PRINT:
            file_print_change(event);
            break;
        case IDM_FILE_SERIAL:
            serial_rec(event);
            break;
        case IDM_FILE_M5000:
            toggle_record(event, &music5000_rec);
            break;
        case IDM_FILE_PAULAREC:
            toggle_record(event, &paula_rec);
            break;
        case IDM_FILE_SOUNDREC:
            toggle_record(event, &sound_rec);
            break;
        case IDM_FILE_EXIT:
            set_quit();
            break;
        case IDM_EDIT_PASTE_OS:
            edit_paste_start(event, os_paste_start);
            break;
        case IDM_EDIT_PASTE_KB:
            edit_paste_start(event, key_paste_start);
            break;
        case IDM_EDIT_COPY:
            edit_print_clip(event);
            break;
        case IDM_DISC_AUTOBOOT:
            disc_choose(event, "autoboot in", all_dext, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
            break;
        case IDM_DISC_LOAD:
            disc_choose(event, "load into", all_dext, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
            break;
        case IDM_DISC_MMB_LOAD:
            file_chooser_generic(event, mmb_fn ? mmb_fn : ".", "Choose an MMB file", "*.mmb", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, disc_mmb_load);
            break;
        case IDM_DISC_EJECT:
            disc_eject(event);
            break;
        case IDM_DISC_MMB_EJECT:
            mmb_eject();
            break;
        case IDM_DISC_MMC_LOAD:
            file_chooser_generic(event, mmccard_fn ? mmccard_fn : ".", "Choose an MMC card image", "*", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, disc_mmc_load);
            break;
        case IDM_DISC_MMC_EJECT:
            mmccard_eject();
            break;
        case IDM_DISC_NEW_ADFS_S:
            disc_choose_new(event, "*.ads");
            break;
        case IDM_DISC_NEW_ADFS_M:
            disc_choose_new(event, "*.adm");
            break;
        case IDM_DISC_NEW_ADFS_L:
            disc_choose_new(event, "*.adl");
            break;
        case IDM_DISC_NEW_DFS_10S_SIN_40T:
        case IDM_DISC_NEW_DFS_10S_SIN_80T:
            disc_choose_new(event, "*.ssd");
            break;
        case IDM_DISC_NEW_DFS_10S_INT_40T:
        case IDM_DISC_NEW_DFS_10S_INT_80T:
            disc_choose_new(event, "*.dsd");
            break;
        case IDM_DISC_NEW_DFS_16S_SIN_40T:
        case IDM_DISC_NEW_DFS_16S_SIN_80T:
        case IDM_DISC_NEW_DFS_18S_SIN_40T:
        case IDM_DISC_NEW_DFS_18S_SIN_80T:
            disc_choose_new(event, "*.sdd");
            break;
        case IDM_DISC_NEW_DFS_16S_INT_80T:
        case IDM_DISC_NEW_DFS_18S_INT_80T:
            disc_choose_new(event, "*.ddd");
            break;
        case IDM_DISC_WPROT:
            disc_wprot(event);
            break;
        case IDM_DISC_WPROT_D:
            defaultwriteprot = !defaultwriteprot;
            break;
        case IDM_DISC_HARD_IDE:
            disc_toggle_ide(event);
            break;
        case IDM_DISC_HARD_SCSI:
            disc_toggle_scsi(event);
            break;
        case IDM_DISC_VDFS_ENABLE:
            vdfs_enabled = !vdfs_enabled;
            break;
        case IDM_DISC_VDFS_ROOT:
            file_chooser_generic(event, vdfs_get_root(), "Choose a folder to be the VDFS root", "*", ALLEGRO_FILECHOOSER_FOLDER, disc_vdfs_root);
            break;
        case IDM_TAPE_LOAD:
            tape_load_ui(event);
            break;
        case IDM_TAPE_REWIND:
            tape_rewind();
            break;
        case IDM_TAPE_EJECT:
            tape_eject();
            break;
        case IDM_TAPE_SPEED_NORMAL:
            tape_normal(event);
            break;
        case IDM_TAPE_SPEED_FAST:
            tape_fast(event);
            break;
        case IDM_TAPE_CAT:
            gui_tapecat_start();
            break;
        case IDM_ROMS_LOAD:
            rom_load(event);
            break;
        case IDM_ROMS_CLEAR:
            rom_clear(event);
            break;
        case IDM_ROMS_RAM:
            rom_ram_toggle(event);
            break;
        case IDM_MODEL:
            change_model(event);
            break;
        case IDM_TUBE:
            change_tube(event);
            break;
        case IDM_TUBE_SPEED:
            change_tube_speed(event);
            break;
        case IDM_VIDEO_DISPTYPE:
            video_set_disptype(radio_event_simple(event, vid_dtype_user));
            break;
        case IDM_VIDEO_COLTYPE:
            vid_colour_out = radio_event_simple(event, vid_colour_out);
            break;
        case IDM_VIDEO_BORDERS:
            video_set_borders(radio_event_simple(event, vid_fullborders));
            break;
        case IDM_VIDEO_WIN_MULT:
            video_set_multipier(radio_event_simple(event, vid_win_multiplier));
            break;
        case IDM_VIDEO_WINSIZE:
            video_set_borders(vid_fullborders);
            break;
        case IDM_VIDEO_FULLSCR:
            toggle_fullscreen();
            break;
        case IDM_VIDEO_NULA:
            nula_disable = !nula_disable;
            break;
        case IDM_VIDEO_LED_LOCATION:
            video_set_led_location(radio_event_simple(event, vid_ledlocation));
            break;
        case IDM_VIDEO_LED_VISIBILITY:
            video_set_led_visibility(radio_event_simple(event, vid_ledvisibility));
            break;
        case IDM_VIDEO_MODE7_FONT:
            change_mode7_font(event);
            break;
        case IDM_VIDEO_LOCK:
            change_video_lock(event);
            break;
        case IDM_SOUND_INTERNAL:
            sound_internal = !sound_internal;
            break;
        case IDM_SOUND_BEEBSID:
            sound_beebsid = !sound_beebsid;
            break;
        case IDM_SOUND_MUSIC5000:
            toggle_music5000();
            break;
        case IDM_SOUND_MFILT:
            music5000_fno = radio_event_with_deselect(event, music5000_fno);
            break;
        case IDM_SOUND_PAULA:
            sound_paula = !sound_paula;
            break;
        case IDM_SOUND_DAC:
            sound_dac = !sound_dac;
            break;
        case IDM_SOUND_DDNOISE:
            sound_ddnoise = !sound_ddnoise;
            break;
        case IDM_SOUND_TAPE:
            sound_tape = !sound_tape;
            break;
        case IDM_SOUND_FILTER:
            sound_filter = !sound_filter;
            break;
        case IDM_WAVE:
            curwave = radio_event_simple(event, curwave);
            break;
        case IDM_SID_TYPE:
            set_sid_type(event);
            break;
        case IDM_SID_METHOD:
            set_sid_method(event);
            break;
        case IDM_DISC_TYPE:
            change_ddnoise_dtype(event);
            break;
        case IDM_DISC_VOL:
            ddnoise_vol = radio_event_simple(event, ddnoise_vol);
            break;
#ifdef HAVE_JACK_JACK_H
        case IDM_MIDI_M4000_JACK:
            midi_music4000.jack_enabled = !midi_music4000.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_JACK:
            midi_music2000_out1.jack_enabled = !midi_music2000_out1.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_JACK:
            midi_music2000_out2.jack_enabled = !midi_music2000_out2.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_JACK:
            midi_music2000_out3.jack_enabled = !midi_music2000_out3.jack_enabled;
            break;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
        case IDM_MIDI_M4000_ASEQ:
            midi_music4000.alsa_seq_enabled = !midi_music4000.alsa_seq_enabled;
            break;
        case IDM_MIDI_M4000_ARAW:
            midi_music4000.alsa_raw_enabled = !midi_music4000.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_ASEQ:
            midi_music2000_out1.alsa_seq_enabled = !midi_music2000_out1.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_ARAW:
            midi_music2000_out1.alsa_raw_enabled = !midi_music2000_out1.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_ASEQ:
            midi_music2000_out2.alsa_seq_enabled = !midi_music2000_out2.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_ARAW:
            midi_music2000_out2.alsa_raw_enabled = !midi_music2000_out2.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_ASEQ:
            midi_music2000_out3.alsa_seq_enabled = !midi_music2000_out3.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_ARAW:
            midi_music2000_out3.alsa_raw_enabled = !midi_music2000_out3.alsa_raw_enabled;
            break;
#endif
        case IDM_SPEED:
            main_setspeed(radio_event_simple(event, emuspeed));
            break;
        case IDM_AUTOSKIP:
            autoskip = !autoskip;
            break;
        case IDM_DEBUGGER:
            debug_toggle_core(true);
            break;
        case IDM_DEBUG_TUBE:
            debug_toggle_tube();
            break;
        case IDM_DEBUG_BREAK:
            debug_step = 1;
            break;
        case IDM_KEY_REDEFINE:
            gui_keydefine_open();
            break;
        case IDM_KEY_AS:
            keyas = !keyas;
            break;
        case IDM_KEY_MODE:
            key_mode = radio_event_simple(event, key_mode);
            key_reset();
            break;
        case IDM_KEY_PAD:
            keypad = !keypad;
            break;
        case IDM_JIM_SIZE:
            mem_jim_setsize(radio_event_simple(event, mem_jim_size));
            break;
        case IDM_AUTO_PAUSE:
            autopause = !autopause;
            break;
        case IDM_MOUSE_AMX:
            mouse_amx = !mouse_amx;
            break;
        case IDM_TRIACK_SEGA_ADAPTER:
            tricky_sega_adapter = !tricky_sega_adapter;
            remap_joystick(0);
            remap_joystick(1);
            break;
        case IDM_JOYSTICK:
            change_joystick(0, radio_event_with_deselect(event, joystick_index[0]));
            break;
        case IDM_JOYSTICK2:
            change_joystick(1, radio_event_with_deselect(event, joystick_index[1]));
        case IDM_MOUSE_STICK:
            mouse_stick = !mouse_stick;
            break;
        case IDM_JOYMAP:
            joymap_index[0] = radio_event_simple(event, joymap_index[0]);
            remap_joystick(0);
        case IDM_JOYMAP2:
            joymap_index[1] = radio_event_simple(event, joymap_index[1]);
            remap_joystick(1);
            break;
    }
}
