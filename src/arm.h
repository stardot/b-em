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

bool arm_init(FILE *romf);
void arm_reset(void);
void arm_exec(void);
void arm_close(void);
void arm_savestate(ZFILE *zfp);
void arm_loadstate(ZFILE *zfp);
uint8_t readarmb(uint32_t addr);
void writearmb(uint32_t addr, uint8_t val);

extern cpu_debug_t tubearm_cpu_debug;

#endif
