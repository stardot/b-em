/* -*- mode: c; c-basic-offset: 4 -*- */

#ifndef __INCLUDE_B_EM_6502DEBUG__
#define __INCLUDE_B_EM_6502DEBUG__

#include "cpu_debug.h"
#include "debugger.h"

typedef struct PREG
{
    int c,z,i,d,v,n;
} PREG;

enum register_numers {
    REG_A,
    REG_X,
    REG_Y,
    REG_S,
    REG_P,
    REG_PC
};

extern const char *dbg6502_reg_names[];
extern size_t dbg6502_print_flags(PREG *pp, char *buf, size_t bufsize);
extern uint32_t dbg6502_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize, int cmos);

#endif
