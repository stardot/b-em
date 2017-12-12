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

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

uint8_t *ram, *rom, *os;
uint8_t ram_fe30, ram_fe34;

int romused[ROM_SLOTS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int swram[ROM_SLOTS]   = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void mem_init() {
    log_debug("mem_init\n");
    ram = (uint8_t *)malloc(RAM_SIZE);
    rom = (uint8_t *)malloc(ROM_SLOTS * ROM_SIZE);
    os  = (uint8_t *)malloc(ROM_SIZE);
    memset(ram, 0, RAM_SIZE);
}

void mem_reset() {
    memset(romused,0, sizeof(romused));
    memset(swram,  0, sizeof(swram));
    memset(ram,    0, RAM_SIZE);
    memset(rom,    0, ROM_SLOTS * ROM_SIZE);
    memset(os,     0, ROM_SIZE);
}

void mem_close() {
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
    dump_mem(rom, ROM_SLOTS*ROM_SIZE, "ROM", "rom.dmp");
}

static void load_os_rom(const char *sect) {
    const char *osname;
	FILE *f;
    char path[PATH_MAX];

    osname = get_config_string(sect, "os", models[curmodel].os);
    if (!find_dat_file(path, sizeof path, "roms", osname, "rom")) {
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

static void cfg_load_rom(int slot, const char *sect, const char *def) {
    const char *name;
    char key[6];
    FILE *f;
    char path[PATH_MAX];

    snprintf(key, sizeof key, "%x", slot);
    name = get_config_string(sect, key, def);
    if (strcasecmp(name, "ram") == 0) {
        log_debug("mem: ROM slot %02d set as sideways RAM", slot);
        swram[slot] = 1;
    } else if (strcasecmp(name, "empty") == 0) {
        log_debug("mem: ROM slot %02d set as empty", slot);
        romused[slot] = 1;
    }
    else if (!find_dat_file(path, sizeof path, "roms", name, "rom")) {
        if ((f = fopen(path, "rb"))) {
            fread(rom + (slot * ROM_SIZE), ROM_SIZE, 1, f);
            fclose(f);
            log_debug("mem: ROM slot %02d loaded with %s from %s", slot, name, path);
            romused[slot] = 1;
        } else
            log_warn("mem: unable to load ROM slot %02d with %s, uanble to open %s: %s", slot, name, path, strerror(errno));
    } else
        log_warn("mem: unable to load ROM slot %02d with %s, ROM file not found", slot, name);
}

void mem_romsetup_os01() {
    const char *sect = models[curmodel].cfgsect;
    int c;

	load_os_rom(sect);
    cfg_load_rom(15, sect, models[curmodel].basic);
    memcpy(rom + 14 * ROM_SIZE, rom + 15 * ROM_SIZE, ROM_SIZE);
    memcpy(rom + 12 * ROM_SIZE, rom + 14 * ROM_SIZE, ROM_SIZE * 2);
    memcpy(rom + 8 * ROM_SIZE, rom + 12 * ROM_SIZE, ROM_SIZE * 4);
    memcpy(rom, rom + 8 * ROM_SIZE, ROM_SIZE * 8);
    for (c = 0; c < ROM_SLOTS; c++)
        romused[c] = 1;
    for (c = 0; c < ROM_SLOTS; c++)
        swram[c] = 0;
}

void mem_romsetup_common(const char *rest) {
    const char *sect = models[curmodel].cfgsect;
    int slot;

    load_os_rom(sect);
    cfg_load_rom(15, sect, models[curmodel].basic);
    cfg_load_rom(14, sect, "vdfs");
    cfg_load_rom(13, sect, models[curmodel].dfs);
    for (slot = 12; slot >= 0; slot--)
        cfg_load_rom(slot, sect, rest);
}

void mem_romsetup_std(void) {
    mem_romsetup_common("empty");
}

void mem_romsetup_swram(void) {
    mem_romsetup_common("ram");
}

void mem_romsetup_master(void) {
    const char *sect = models[curmodel].cfgsect;
    const char *osname;
	FILE *f;
    char path[PATH_MAX];
    int slot;


    osname = get_config_string(sect, "os", models[curmodel].os);
    if (!find_dat_file(path, sizeof path, "roms", osname, "rom")) {
        if ((f = fopen(path, "rb"))) {
            if (fread(os, ROM_SIZE, 1, f) == 1) {
                if (fread(rom + (9 * ROM_SIZE), 7 * ROM_SIZE, 1, f) == 1) {
                    fclose(f);
                    for (slot = ROM_SLOTS-1; slot >= 9; slot--) {
                        romused[slot] = 1;
                        swram[slot] = 0;
                    }
                    cfg_load_rom(8, sect, "vdfs");
                    for (slot = 7; slot >= 4; slot--)
                        cfg_load_rom(slot, sect, "ram");
                    for (; slot >= 0; slot--)
                        cfg_load_rom(slot, sect, "empty");
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
        cfg_load_rom(slot, sect, "ram");
}

int mem_findswram(int n) {
    int c;

    for (c = 0; c < ROM_SLOTS; c++)
        if (swram[c])
            if (n-- <= 0)
                return c;
    return -1;
}

void mem_clearroms() {
    int c;

    memset(rom, 0, ROM_SLOTS * ROM_SIZE);
    for (c = 0; c < ROM_SLOTS; c++)
        romused[c] = 0;
    for (c = 0; c < ROM_SLOTS; c++)
        swram[c] = 0;
}

void mem_savestate(FILE *f) {
    putc(ram_fe30, f);
    putc(ram_fe34, f);
    fwrite(ram, RAM_SIZE, 1, f);
    fwrite(rom, ROM_SIZE*ROM_SLOTS, 1, f);
}

void mem_loadstate(FILE *f) {
    writemem(0xFE30, getc(f));
    writemem(0xFE34, getc(f));
    fread(ram, RAM_SIZE, 1, f);
    fread(rom, ROM_SIZE*ROM_SLOTS, 1, f);
}
