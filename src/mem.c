/*B-em v2.2 by Tom Walker
  ROM handling*/

#include <allegro.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"
#include "6502.h"
#include "mem.h"
#include "model.h"

uint8_t *ram, *rom, *os;
uint8_t ram_fe30, ram_fe34;

rom_slot_t rom_slots[ROM_NSLOT];

static const char slotkeys[16][6] = {
    "rom00", "rom01", "rom02", "rom03",
    "rom04", "rom05", "rom06", "rom07",
    "rom08", "rom09", "rom10", "rom11",
    "rom12", "rom13", "rom14", "rom15"
};

void mem_init() {
    log_debug("mem_init\n");
    ram = (uint8_t *)malloc(RAM_SIZE);
    rom = (uint8_t *)malloc(ROM_NSLOT * ROM_SIZE);
    os  = (uint8_t *)malloc(ROM_SIZE);
    memset(ram, 0, RAM_SIZE);
}

static void rom_free(int slot) {
    if (rom_slots[slot].alloc) {
        if (rom_slots[slot].name)
            free(rom_slots[slot].name);
        if (rom_slots[slot].path)
            free(rom_slots[slot].path);
    }
}

void mem_close() {
    int slot;

    for (slot = 0; slot < ROM_NSLOT; slot++)
        rom_free(slot);

    if (ram) free(ram);
    if (rom) free(rom);
    if (os)  free(os);
}

static void dump_mem(void *start, size_t size, const char *which, const char *file) {
	FILE *f;

    if ((f = fopen(file, "wb"))) {
        fwrite(start, size, 1, f);
        fclose(f);
    } else
        log_error("mem: unable to open %s dump file %s: %s", which, file, strerror(errno));
}

void mem_dump(void) {
    dump_mem(ram, 64*1024, "RAM", "ram.dmp");
    dump_mem(rom, ROM_NSLOT*ROM_SIZE, "ROM", "rom.dmp");
}

static void load_os_rom(const char *sect) {
    const char *osname;
	FILE *f;
    char path[PATH_MAX];

    osname = get_config_string(sect, "os", models[curmodel].os);
    if (!find_dat_file(path, sizeof path, "roms/os", osname, "rom")) {
        if ((f = fopen(path, "rb"))) {
            fread(os, ROM_SIZE, 1, f);
            fclose(f);
            log_debug("mem: OS %s loaded from %s", osname, path);
            return;
        } else
            log_fatal("mem: unable to load OS %s, unable to open %s: %s", osname, path, strerror(errno));
    } else
        log_fatal("mem: unable to find OS %s", osname);
    exit(1);
}

const uint8_t *mem_romdetail(int slot) {
    uint8_t *base = rom + (slot * ROM_SIZE);
    uint8_t rtype, *copyr;

    rtype = base[6];
    if (rtype & 0xc0) {
        copyr = base + base[7];
        if (copyr[0] == 0 && copyr[1] == '(' && copyr[2] == 'C' && copyr[3] == ')') {
            return base + 8;
        }
    }
    return NULL;
}

void mem_loadrom(int slot, const char *name, const char *path, uint8_t use_name) {
    FILE *f;

    if ((f = fopen(path, "rb"))) {
        fread(rom + (slot * ROM_SIZE), ROM_SIZE, 1, f);
        fclose(f);
        log_debug("mem: ROM slot %02d loaded with %s from %s", slot, name, path);
        rom_slots[slot].use_name = use_name;
        rom_slots[slot].alloc = 1;
        rom_slots[slot].name = strdup(name);
        rom_slots[slot].path = strdup(path);
    }
    else
        log_warn("mem: unable to load ROM slot %02d with %s, uanble to open %s: %s", slot, name, path, strerror(errno));
}

static void cfg_load_rom(int slot, const char *sect, const char *def) {
    const char *key, *name;
    char path[PATH_MAX];

    key = slotkeys[slot];
    name = get_config_string(sect, key, def);
    if (name != NULL && *name != '\0') {
        if (is_relative_filename(name)) {
            if (!find_dat_file(path, sizeof path, "roms/general", name, "rom"))
                mem_loadrom(slot, name, path, 1);
            else
                log_warn("mem: unable to load ROM slot %02d with %s, ROM file not found", slot, name);
        } else
            mem_loadrom(slot, get_filename(name), name, 0);
    }
}

void mem_romsetup_os01() {
    const char *sect = models[curmodel].cfgsect;
    char *name, *path;
    int c;

	load_os_rom(sect);
    cfg_load_rom(15, sect, models[curmodel].basic);
    memcpy(rom + 14 * ROM_SIZE, rom + 15 * ROM_SIZE, ROM_SIZE);
    memcpy(rom + 12 * ROM_SIZE, rom + 14 * ROM_SIZE, ROM_SIZE * 2);
    memcpy(rom + 8 * ROM_SIZE, rom + 12 * ROM_SIZE, ROM_SIZE * 4);
    memcpy(rom, rom + 8 * ROM_SIZE, ROM_SIZE * 8);
    name = rom_slots[15].name;
    path = rom_slots[15].path;
    for (c = 0; c < 15; c++) {
        rom_slots[c].locked = 1;
        rom_slots[c].swram = 0;
        rom_slots[c].alloc = 0;
        rom_slots[c].name = name;
        rom_slots[c].path = path;
    }
}

void mem_romsetup_std(void) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

    load_os_rom(sect);
    cfg_load_rom(15, sect, models[curmodel].basic);
    cfg_load_rom(14, sect, "vdfs");
    cfg_load_rom(13, sect, models[curmodel].dfs);
    for (slot = 12; slot >= 0; slot--)
        cfg_load_rom(slot, sect, NULL);
}

static void fill_swram(void) {
    int slot;

    for (slot = 0; slot < ROM_NSLOT; slot++)
        if (!rom_slots[slot].name)
            rom_slots[slot].swram = 1;
}

void mem_romsetup_swram(void) {

    mem_romsetup_std();
    fill_swram();
}

void mem_romsetup_bp128(const char *rest) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

    load_os_rom(sect);
    cfg_load_rom(15, sect, models[curmodel].basic);
    cfg_load_rom(14, sect, "vdfs");
    rom_slots[13].swram = 1;
    rom_slots[12].swram = 1;
    cfg_load_rom(11, sect, models[curmodel].dfs);
    for (slot = 10; slot >= 0; slot--)
        cfg_load_rom(slot, sect, NULL);
    rom_slots[1].swram = 1;
    rom_slots[0].swram = 1;
}

void mem_romsetup_master(void) {
    const char *sect = models[curmodel].cfgsect;
    const char *osname;
	FILE *f;
    char path[PATH_MAX];
    int slot;

    osname = get_config_string(sect, "os", models[curmodel].os);
    if (!find_dat_file(path, sizeof path, "roms/os", osname, "rom")) {
        if ((f = fopen(path, "rb"))) {
            if (fread(os, ROM_SIZE, 1, f) == 1) {
                if (fread(rom + (9 * ROM_SIZE), 7 * ROM_SIZE, 1, f) == 1) {
                    fclose(f);
                    for (slot = ROM_NSLOT-1; slot >= 9; slot--) {
                        rom_slots[slot].swram = 0;
                        rom_slots[slot].locked = 1;
                        rom_slots[slot].name = models[curmodel].os;
                    }
                    cfg_load_rom(8, sect, "vdfs");
                    rom_slots[7].swram = 1;
                    rom_slots[6].swram = 1;
                    rom_slots[5].swram = 1;
                    rom_slots[4].swram = 1;
                    for (slot = 7; slot >= 0; slot--)
                        cfg_load_rom(slot, sect, NULL);
                    return;
                }
            }
            log_fatal("mem: unable to read complete OS ROM %s: %s", osname, strerror(errno));
            fclose(f);
        } else
            log_fatal("mem: unable to load OS %s, unable to open %s: %s", osname, path, strerror(errno));
    } else
        log_fatal("mem: unable to find OS %s", osname);
    exit(1);
}

void mem_romsetup_compact(void) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

	load_os_rom(sect);
    cfg_load_rom(15, sect, "utils");
    cfg_load_rom(14, sect, models[curmodel].basic);
    cfg_load_rom(13, sect, models[curmodel].dfs);
    cfg_load_rom(12, sect, "vdfs");
    for (slot = 11; slot >= 0; slot--)
        cfg_load_rom(slot, sect, NULL);
    fill_swram();
}

int mem_findswram(int n) {
    int c;

    for (c = 0; c < ROM_NSLOT; c++)
        if (rom_slots[c].swram)
            if (n-- <= 0)
                return c;
    return -1;
}

static void rom_clearmeta(int slot) {
    rom_free(slot);
    rom_slots[slot].locked = 0;
    rom_slots[slot].use_name = 0;
    rom_slots[slot].alloc = 0;
    rom_slots[slot].name = NULL;
    rom_slots[slot].path = NULL;
}

void mem_clearrom(int slot) {
    uint8_t *base = rom + (slot * ROM_SIZE);
    
    memset(base, 0xff, ROM_SIZE);
    rom_clearmeta(slot);
}

void mem_clearroms(void) {
    int slot;

    memset(rom, 0xff, ROM_NSLOT * ROM_SIZE);
    for (slot = 0; slot < ROM_NSLOT; slot++) {
        rom_clearmeta(slot);
        rom_slots[slot].swram = 0;
    }
}

void mem_savestate(FILE *f) {
    putc(ram_fe30, f);
    putc(ram_fe34, f);
    fwrite(ram, RAM_SIZE, 1, f);
    fwrite(rom, ROM_SIZE*ROM_NSLOT, 1, f);
}

void mem_loadstate(FILE *f) {
    writemem(0xFE30, getc(f));
    writemem(0xFE34, getc(f));
    fread(ram, RAM_SIZE, 1, f);
    fread(rom, ROM_SIZE*ROM_NSLOT, 1, f);
}

void mem_save_romcfg(const char *sect) {
    int slot;
    rom_slot_t *slotp;
    const char *value;

    for (slot = ROM_NSLOT-1; slot >= 0; slot--) {
        slotp = rom_slots + slot;
        if (!slotp->locked) {
            value = slotp->use_name ? slotp->name : slotp->path;
            if (value)
                set_config_string(sect, slotkeys[slot], value);
        }
    }
}
