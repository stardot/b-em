#ifndef Z80_DIS_INC
#define Z80_DIS_INC

#include "cpu_debug.h"

extern uint32_t z80_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize);

#endif
