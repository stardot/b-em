#ifndef __INC_Z80_H
#define __INC_Z80_H

#include "cpu_debug.h"
#include "savestate.h"
#include <stdio.h>
#include <stdbool.h>

bool z80_init(FILE *romf);
void z80_reset(void);
void z80_exec(void);
void z80_close(void);
uint8_t tube_z80_readmem(uint32_t addr);
void tube_z80_writemem(uint32_t addr, uint8_t byte);
void z80_savestate(ZFILE *zfp);
void z80_loadstate(ZFILE *zfp);

extern cpu_debug_t tubez80_cpu_debug;

#endif
