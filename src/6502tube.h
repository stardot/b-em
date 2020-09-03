#ifndef __INC_6502TUBE_H
#define __INC_6502TUBE_H

#include "cpu_debug.h"
#include "savestate.h"

bool tube_6502_init(void *rom);
void tube_6502_reset(void);
void tube_6502_exec(void);
void tube_6502_close(void);
void tube_6502_mapoutrom(void);

extern cpu_debug_t tube6502_cpu_debug;
extern bool tube_6502_rom_in;

#endif
