#ifndef __INC_USERVIA_H
#define __INC_USERVIA_H

extern VIA      uservia;

void            uservia_reset();
void            uservia_write(uint16_t addr, uint8_t val);
uint8_t         uservia_read(uint16_t addr);
void            uservia_updatetimers();

void            uservia_savestate(FILE *f);
void            uservia_loadstate(FILE *f);

extern uint8_t  lpt_dac;

void            uservia_set_ca1(int level);
void            uservia_set_ca2(int level);
void            uservia_set_cb1(int level);
void            uservia_set_cb2(int level);

#endif
