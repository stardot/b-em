#ifndef __INC_MEM_H
#define __INC_MEM_H

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

extern void mem_romsetup_os01();
extern void mem_romsetup_std();
extern void mem_romsetup_swram();
extern void mem_romsetup_bp128();
extern void mem_romsetup_master();
extern void mem_romsetup_compact();
extern void mem_fillswram();
extern int mem_findswram(int n);
extern void mem_clearroms(void);

void mem_clearrom(int slot);
void mem_loadrom(int slot, const char *name, const char *path, uint8_t rel);
const uint8_t *mem_romdetail(int slot);
void mem_save_romcfg(ALLEGRO_CONFIG *bem_cfg, const char *sect);

void mem_init();
void mem_reset();
void mem_close();

void mem_savestate(FILE *f);
void mem_loadstate(FILE *f);

void mem_dump();

extern uint8_t ram_fe30, ram_fe34;
extern uint8_t *ram, *rom, *os;
extern rom_slot_t rom_slots[ROM_NSLOT];

#endif
