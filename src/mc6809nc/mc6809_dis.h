#ifndef _mc6809_dis_h_
#define _mc6809_dis_h_

#include "../cpu_debug.h"

uint32_t mc6809_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize);

#endif
