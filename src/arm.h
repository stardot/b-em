/*ARM*/

#ifndef __INC_ARM_H
#define __INC_ARM_H

#include "cpu_debug.h"
#include "savestate.h"

//uint32_t *usrregs[16],userregs[16],superregs[16],fiqregs[16],irqregs[16];
//uint32_t armregs[16];
//int armirq,armfiq;
//#define PC ((armregs[15])&0x3FFFFFC)

//void dumparmregs();
//int databort;

bool arm1_init(void *rom);
bool arm2_init(void *rom);
void arm_reset(void);
void arm_exec(void);
void arm_close(void);

extern cpu_debug_t tubearm_cpu_debug;

#endif
