#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "gui-allegro.h"

#include "ide.h"
#include "disc.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "savestate.h"
#include "scsi.h"
#include "tape.h"
#include "vdfs.h"

#define ROM_LABEL_LEN 50

ALLEGRO_MENU *disc_menu;
ALLEGRO_MENU *tspeed_menu;
ALLEGRO_MENU *rom_menu;

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

static ALLEGRO_MENU *create_file_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Hard Reset", IDM_FILE_RESET, 0, NULL, NULL);
    al_append_menu_item(menu, "Load state...", IDM_FILE_LOAD_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Save State...", IDM_FILE_SAVE_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Exit", IDM_FILE_EXIT, 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_disc_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    int flags;

    al_append_menu_item(menu, "Autoboo disc in 0/2...", IDM_DISC_AUTOBOOT, 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :0/2...", menu_id_num(IDM_DISC_LOAD, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :1/3...", menu_id_num(IDM_DISC_LOAD, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :0/2", menu_id_num(IDM_DISC_EJECT, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :1/3", menu_id_num(IDM_DISC_EJECT, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "New disc :0/2...", menu_id_num(IDM_DISC_NEW, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "New disc :1/3...", menu_id_num(IDM_DISC_NEW, 1), 0, NULL, NULL);
    flags = ALLEGRO_MENU_ITEM_CHECKBOX;
    if (writeprot[0])
        flags |= ALLEGRO_MENU_ITEM_CHECKED;
    al_append_menu_item(menu, "Write protect disc :0/2", menu_id_num(IDM_DISC_WPROT, 0), flags, NULL, NULL);
    flags = ALLEGRO_MENU_ITEM_CHECKBOX;
    if (writeprot[1])
        flags |= ALLEGRO_MENU_ITEM_CHECKED;    
    al_append_menu_item(menu, "Write protect disc :0/2", menu_id_num(IDM_DISC_WPROT, 1), flags, NULL, NULL);
    flags = ALLEGRO_MENU_ITEM_CHECKBOX;
    if (defaultwriteprot)
        flags |= ALLEGRO_MENU_ITEM_CHECKED;
    al_append_menu_item(menu, "Default write protect", IDM_DISC_WPROT_D, flags, NULL, NULL);
    al_append_menu_item(menu, "IDE hard disc", IDM_DISC_HARD_IDE, ALLEGRO_MENU_ITEM_CHECKBOX, NULL, NULL);
    al_append_menu_item(menu, "SCSI hard disc", IDM_DISC_HARD_SCSI, ALLEGRO_MENU_ITEM_CHECKBOX, NULL, NULL);
    al_append_menu_item(menu, "VDFS Enabled", IDM_DISC_VDFS_ENABLE, ALLEGRO_MENU_ITEM_CHECKBOX, NULL, NULL);
    al_append_menu_item(menu, "Choose VDFS Root...", IDM_DISC_VDFS_ROOT, 0, NULL, NULL);
    disc_menu = menu;
    return menu;
}

static ALLEGRO_MENU *create_tape_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *speed = al_create_menu();
    int nflags, fflags;
    al_append_menu_item(menu, "Load tape...", IDM_TAPE_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Rewind tape", IDM_TAPE_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject tape", IDM_TAPE_REWIND, 0, NULL, NULL);
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
    tspeed_menu = speed;
    return menu;
}

static void gen_rom_label(int slot, char *dest)
{
    int ver;
    const uint8_t *detail;
    const char *rr, *name;

    rr = rom_slots[slot].swram ? "RAM" : "ROM";
    detail = mem_romdetail(slot);
    name = rom_slots[slot].name;
    if (detail) {
        ver = *detail++;
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
    ALLEGRO_MENU *menu, *sub;
    int slot, flags;
    char label[ROM_LABEL_LEN];

    menu =  al_create_menu();
    for (slot = ROM_NSLOT-1; slot >= 0; slot--) {
        gen_rom_label(slot, label);
        sub = al_create_menu();
        al_append_menu_item(sub, "Load...", menu_id_num(IDM_ROMS_LOAD, slot), 0, NULL, NULL);
        al_append_menu_item(sub, "Clear", menu_id_num(IDM_ROMS_CLEAR, slot), 0, NULL, NULL);
        flags = ALLEGRO_MENU_ITEM_CHECKBOX;
        if (rom_slots[slot].swram)
            flags |= ALLEGRO_MENU_ITEM_CHECKED;
        al_append_menu_item(sub, "RAM", menu_id_num(IDM_ROMS_RAM, slot), flags, NULL, NULL);
        al_append_menu_item(menu, label, 0, 0, NULL, sub);
    }
    rom_menu = menu;
    return menu;
}
    
void gui_allegro_init(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_DISPLAY *display)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "File", 0, 0, NULL, create_file_menu());
    al_append_menu_item(menu, "Disc", 0, 0, NULL, create_disc_menu());
    al_append_menu_item(menu, "Tape", 0, 0, NULL, create_tape_menu());
    al_append_menu_item(menu, "ROM", 0, 0, NULL, create_rom_menu());
    al_set_display_menu(display, menu);
    al_register_event_source(queue, al_get_default_menu_event_source());
}

static void file_load_state(ALLEGRO_EVENT *event)
{
    ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, "Load state from file", "*.snp", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
    if (chooser) {
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                strncpy(savestate_name, al_get_native_file_dialog_path(chooser, 0), sizeof savestate_name);
                savestate_load();
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
}

static void file_save_state(ALLEGRO_EVENT *event)
{
    ALLEGRO_FILECHOOSER *chooser;
    ALLEGRO_DISPLAY *display;

    if (curtube == -1) {
        if ((chooser = al_create_native_file_dialog(savestate_name, "Save state from file", "*.snp", ALLEGRO_FILECHOOSER_SAVE))) {
            display = (ALLEGRO_DISPLAY *)(event->user.data2);
            if (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) > 0) {
                    strncpy(savestate_name, al_get_native_file_dialog_path(chooser, 0), sizeof savestate_name);
                    savestate_save();
                }
            }
            al_destroy_native_file_dialog(chooser);
        }
    } else
        log_error("Second processor save states not supported yet.");
}

static bool disc_choose(ALLEGRO_EVENT *event, const char *opname, void (*callback)(int drive, ALLEGRO_PATH *fn), int flags)
{
    ALLEGRO_FILECHOOSER *chooser;
    ALLEGRO_DISPLAY *display;
    ALLEGRO_PATH *apath;
    int drive;
    const char *fpath;
    char title[50];
    bool rc = false;

    drive = menu_get_num(event);
    if (!(apath = discfns[drive]) || !(fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    snprintf(title, sizeof title, "Choose a disc to %s drive %d/%d", opname, drive, drive+2);
    if ((chooser = al_create_native_file_dialog(fpath, title, "*.ssd;*.dsd;*,ing;*.adf;*.adl;*.fdi", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST))) {
        display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                disc_close(drive);
                callback(drive, path);
                rc = true;
                al_destroy_path(path);
                if (defaultwriteprot) {
                    writeprot[drive] = 1;
                    al_set_menu_item_flags(disc_menu, menu_id_num(IDM_DISC_WPROT, drive), ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
                } else if (writeprot[drive])
                    al_set_menu_item_flags(disc_menu, menu_id_num(IDM_DISC_WPROT, drive), ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
                else
                    al_set_menu_item_flags(disc_menu, menu_id_num(IDM_DISC_WPROT, drive), ALLEGRO_MENU_ITEM_CHECKBOX);
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
    return rc;
}

static void disc_autoboot(ALLEGRO_EVENT *event)
{
    if (disc_choose(event, "autoboot in", disc_load, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST)) {
        main_reset();
        autoboot = 150;
    }
}

static void disc_eject(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    disc_close(drive);
}

static void disc_wprot(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    writeprot[drive] = !writeprot[drive];
}

static void disc_toggle_ide(void)
{
    if (ide_enable)
        ide_enable = 0;
    else {
        ide_enable = 1;
        if (scsi_enabled) {
            scsi_enabled = 0;
            al_set_menu_item_flags(disc_menu, IDM_DISC_HARD_SCSI, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
    }
}

static void disc_toggle_scsi(void)
{
    if (scsi_enabled)
        scsi_enabled = 0;
    else {
        scsi_enabled = 1;
        if (ide_enable) {
            ide_enable = 0;
            al_set_menu_item_flags(disc_menu, IDM_DISC_HARD_IDE, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
    }
}

static void disc_vdfs_root(ALLEGRO_EVENT *event)
{
    ALLEGRO_FILECHOOSER *chooser;
    ALLEGRO_DISPLAY *display;

    if ((chooser = al_create_native_file_dialog(vdfs_get_root(), "Choose a folder to be the VDFS root", "*", ALLEGRO_FILECHOOSER_FOLDER))) {
        display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0)
                vdfs_set_root(al_get_native_file_dialog_path(chooser, 0));
        }
    }
}

static void tape_load_ui(ALLEGRO_EVENT *event)
{
    ALLEGRO_FILECHOOSER *chooser;
    ALLEGRO_DISPLAY *display;
    const char *fpath;

    if (!tape_fn || !(fpath = al_path_cstr(tape_fn, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    if ((chooser = al_create_native_file_dialog(fpath, "Choose a tape to load", "*.uef;*.csw", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST))) {
        display = (ALLEGRO_DISPLAY *)(event->user.data2);
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

static void tape_normal(void)
{
    if (fasttape) {
        fasttape = 0;
        al_set_menu_item_flags(tspeed_menu, IDM_TAPE_SPEED_FAST, ALLEGRO_MENU_ITEM_CHECKBOX);
    }
}

static void tape_fast(void)
{
    if (!fasttape) {
        fasttape = 1;
        al_set_menu_item_flags(tspeed_menu, IDM_TAPE_SPEED_NORMAL, ALLEGRO_MENU_ITEM_CHECKBOX);
    }
}

static void rom_load(ALLEGRO_EVENT *event)
{
    rom_slot_t *slotp;
    int slot;
    char tempname[PATH_MAX], label[ROM_LABEL_LEN];
    ALLEGRO_FILECHOOSER *chooser;
    ALLEGRO_DISPLAY *display;

    slot = menu_get_num(event);
    slotp = rom_slots + slot;
    if (!slotp->locked) {
        if (slotp->name)
            strncpy(tempname, slotp->name, sizeof tempname-1);
        else
            tempname[0] = 0;
        if ((chooser = al_create_native_file_dialog(tempname, "Choose a ROM to load", "*.rom", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST))) {
            display = (ALLEGRO_DISPLAY *)(event->user.data2);
            if (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) > 0) {
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

void gui_allegro_event(ALLEGRO_EVENT *event)
{
    switch(menu_get_id(event)) {
        case IDM_FILE_RESET:
            main_restart();
            break;
        case IDM_FILE_LOAD_STATE:
            file_load_state(event);
            break;
        case IDM_FILE_SAVE_STATE:
            file_save_state(event);
            break;
        case IDM_FILE_EXIT:
            quitting = true;
            break;
        case IDM_DISC_AUTOBOOT:
            disc_autoboot(event);
            break;
        case IDM_DISC_LOAD:
            disc_choose(event, "load into", disc_load, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
            break;
        case IDM_DISC_EJECT:
            disc_eject(event);
            break;
        case IDM_DISC_NEW:
            disc_choose(event, "create in", disc_new, ALLEGRO_FILECHOOSER_SAVE);
            break;
        case IDM_DISC_WPROT:
            disc_wprot(event);
            break;
        case IDM_DISC_WPROT_D:
            defaultwriteprot = !defaultwriteprot;
            break;
        case IDM_DISC_HARD_IDE:
            disc_toggle_ide();
            break;
        case IDM_DISC_HARD_SCSI:
            disc_toggle_scsi();
            break;
        case IDM_DISC_VDFS_ENABLE:
            vdfs_enabled = !vdfs_enabled;
            break;
        case IDM_DISC_VDFS_ROOT:
            disc_vdfs_root(event);
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
            tape_normal();
            break;
        case IDM_TAPE_SPEED_FAST:
            tape_fast();
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
        default:
            log_warn("gui-allegro: menu ID %d not handled", menu_get_id(event));
    }
}
