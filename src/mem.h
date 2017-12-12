#ifndef __INC_MEM_H
#define __INC_MEM_H

#define RAM_SIZE  (64*1024)
#define ROM_SIZE  (16*1024)
#define ROM_SLOTS 16

extern void mem_romsetup_os01();
extern void mem_romsetup_std();
extern void mem_romsetup_swram();
extern void mem_romsetup_master();
extern void mem_romsetup_compact();
extern void mem_fillswram();
extern int mem_findswram(int n);
extern void mem_clearroms();

void mem_init();
void mem_reset();
void mem_close();
void mem_loadroms(char *os, char *romdir);

void mem_savestate(FILE *f);
void mem_loadstate(FILE *f);

void mem_dump();

extern uint8_t ram_fe30, ram_fe34;
extern uint8_t *ram, *rom, *os;
extern int swram[16];

#endif
