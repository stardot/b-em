#ifndef __INC_Z80_H
#define __INC_Z80_H

#include "cpu_debug.h"
#include <stdio.h>

void z80_init(FILE *romf);
void z80_reset();
void z80_exec();
void z80_close();
uint8_t tube_z80_readmem(uint32_t addr);
void tube_z80_writemem(uint32_t addr, uint8_t byte);

extern cpu_debug_t tubez80_cpu_debug;

#endif
