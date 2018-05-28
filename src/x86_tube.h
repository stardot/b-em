#ifndef __INC_X86_TUBE_H
#define __INC_X86_TUBE_H

#include "cpu_debug.h"
#include "savestate.h"

bool x86_init(FILE *romf);
void x86_reset(void);
void x86_exec(void);
void x86_close(void);
void x86_savestate(ZFILE *zfp);
void x86_loadstate(ZFILE *zfp);
uint8_t x86_readmem(uint32_t addr);
void x86_writemem(uint32_t addr, uint8_t byte);

extern cpu_debug_t tubex86_cpu_debug;

#endif
