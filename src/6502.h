#ifndef __INC_6502_H
#define __INC_6502_H

#include "6502debug.h"

extern uint8_t a,x,y,s;
extern uint16_t pc;

extern PREG p;

extern int output;
extern int interrupt;
/* Bit fields for interrupt
 * Bit  0 (01): System VIA
 * Bit  1 (02): User VIA
 * Bit  2 (04): ACIA
 * Bit  3 (08): TUBE
 * Bit  4 (10): SCSI
 * Bit  5 (20):
 * Bit  6 (40):
 * Bit  7 (80): Sys VIA T2 special case
 */
extern int nmi;

extern uint64_t stopwatch;
extern int cycles;
extern int romsel;
extern uint8_t ram1k, ram4k, ram8k;

void m6502_reset(void);
void m6502_exec(int slice);
void m65c02_exec(int slice);
void dumpregs(void);
void m6502_update_swram(void);

uint8_t readmem(uint16_t addr);
void writemem(uint16_t addr, uint8_t val);

void m6502_savestate(FILE *f);
void m6502_loadstate(FILE *f);

extern cpu_debug_t core6502_cpu_debug;

void os_paste_start(char *str);

#endif
