#ifndef __INC_SYSVIA_H
#define __INC_SYSVIA_H

extern VIA sysvia;

void    sysvia_reset(void);
void    sysvia_write(uint16_t addr, uint8_t val);
uint8_t sysvia_read(uint16_t addr);

void    sysvia_savestate(FILE *f);
void    sysvia_loadstate(FILE *f);

extern uint8_t IC32;
extern uint8_t sdbval;
extern int scrsize;

void sysvia_set_ca1(int level);
void sysvia_set_ca2(int level);
void sysvia_set_cb1(int level);
void sysvia_set_cb2(int level);

#endif
