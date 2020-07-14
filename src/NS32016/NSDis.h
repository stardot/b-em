#ifndef _NSDIS_H_
#define _NSDIS_H_

#include "../cpu_debug.h"

void n32016_show_instruction(uint32_t StartPc, uint32_t* pPC, uint32_t opcode, uint32_t Function, OperandSizeType *OperandSize);
uint32_t n32016_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize);

#endif // ! _NSDIS_H_
