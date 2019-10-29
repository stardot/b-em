#ifndef __INC_X86_TUBE_H
#define __INC_X86_TUBE_H

#include "cpu_debug.h"
#include "savestate.h"

bool x86_init(void *rom);
void x86_reset(void);
void x86_exec(void);
void x86_close(void);

extern cpu_debug_t tubex86_cpu_debug;

#endif
