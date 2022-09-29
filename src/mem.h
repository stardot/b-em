#ifndef __INC_MEM_H
#define __INC_MEM_H

#include "savestate.h"

#define RAM_SIZE  (64*1024)
#define ROM_SIZE  (16*1024)
#define ROM_NSLOT 16

typedef struct {
    uint8_t swram;    // this slot behaves as sideways RAM.
    uint8_t locked;   // slot is essential to machine config.
    uint8_t use_name; // use a short name writing to config file.
    uint8_t alloc;    // name/path are from malloc(3)
    char *name;       // short name for the loaded ROM.
    char *path;       // full filestystem path for the loaded ROM.
} rom_slot_t;

extern void mem_romsetup_os01(void);
extern void mem_romsetup_std(void);
extern void mem_romsetup_swram(void);
extern void mem_romsetup_bp128(void);
extern void mem_romsetup_master(void);
extern void mem_romsetup_compact(void);
extern void mem_fillswram(void);
extern int mem_findswram(int n);
extern void mem_clearroms(void);

void mem_clearrom(int slot);
void mem_loadrom(int slot, const char *name, const char *path, uint8_t rel);
const uint8_t *mem_romdetail(int slot);
void mem_save_romcfg(const char *sect);

void mem_init(void);
void mem_reset(void);
void mem_close(void);

void mem_savezlib(ZFILE *zfp);
void mem_loadzlib(ZFILE *zfp);
void mem_loadstate(FILE *f);

void mem_dump(void);

extern uint8_t ram_fe30, ram_fe34;
extern uint8_t *ram, *rom, *os;
extern rom_slot_t rom_slots[ROM_NSLOT];

enum mem_jim_sz {
    JIM_NONE,
    JIM_16M,
    JIM_64M,
    JIM_256M,
    JIM_480M,
    JIM_996M
};

extern enum mem_jim_sz mem_jim_size;
extern void mem_jim_setsize(enum mem_jim_sz size);
extern uint8_t mem_jim_getsize(void);
extern uint8_t mem_jim_read(uint16_t addr);
extern void mem_jim_write(uint16_t addr, uint8_t value);

#endif
