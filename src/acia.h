#ifndef __INC_ACIA_H
#define __INC_ACIA_H

void acia_reset();
uint8_t acia_read(uint16_t addr);
void acia_write(uint16_t addr, uint8_t val);
void acia_poll();
void acia_receive(uint8_t val);

void acia_savestate(FILE *f);
void acia_loadstate(FILE *f);

extern int acia_tapespeed;
extern uint8_t acia_sr;

void dcd();
void dcdlow();

#endif
