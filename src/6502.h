#ifndef __INC_6502_H
#define __INC_6502_H

#include "6502debug.h"

extern uint8_t a,x,y,s;
extern uint16_t pc;
extern uint_least32_t cycles_6502;

extern PREG p;

extern int output;
extern int interrupt;
/* Bit fields for interrupt
 * Bit  0 (00001): System VIA
 * Bit  1 (00002): User VIA
 * Bit  2 (00004): ACIA
 * Bit  3 (00008): TUBE
 * Bit  4 (00010):
 * Bit  5 (00020):
 * Bit  6 (00040):
 * Bit  7 (00080): Sys VIA T2 special case
 * Bit  8 (00100):
 * Bit  9 (00200):
 * Bit 10 (00400):
 * Bit 11 (00800):
 * Bit 12 (01000):
 * Bit 13 (02000):
 * Bit 14 (04000):
 * Bit 15 (08000):
 * Bit 16 (10000): SCSI
 */
extern int nmi;

extern int romsel;
extern uint8_t ram1k, ram4k, ram8k;

void m6502_reset(void);
void m6502_exec(void);
void m65c02_exec(void);
void dumpregs(void);
void m6502_update_swram(void);

uint8_t readmem(uint16_t addr);
void writemem(uint16_t addr, uint8_t val);

void m6502_savestate(FILE *f);
void m6502_loadstate(FILE *f);

extern cpu_debug_t core6502_cpu_debug;

void os_paste_start(char *str);

#endif
