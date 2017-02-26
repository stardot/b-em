#ifndef __INC_MEM_H
#define __INC_MEM_H

void            mem_romsetup_os01();
void            mem_romsetup_bplus128();
void            mem_romsetup_master128();
void            mem_romsetup_master128_35();
void            mem_romsetup_mastercompact();
void            mem_fillswram();
void            mem_clearroms();

void            mem_init();
void            mem_reset();
void            mem_close();
void            mem_loadroms(char *os, char *romdir);

void            mem_savestate(FILE *f);
void            mem_loadstate(FILE *f);

void            mem_dump();

extern uint8_t  ram_fe30, ram_fe34;
extern uint8_t *ram, *rom, *os;
extern int      swram[16];

#endif
