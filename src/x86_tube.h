#ifndef __INC_X86_TUBE_H
#define __INC_X86_TUBE_H

void x86_init();
void x86_reset();
void x86_exec();
void x86_close();
uint8_t x86_readmem(uint32_t addr);
void x86_writemem(uint32_t addr, uint8_t byte);
#endif
