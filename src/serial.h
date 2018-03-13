#ifndef __INC_SERIAL_H
#define __INC_SERIAL_H

void    serial_write(uint16_t addr, uint8_t val);
uint8_t serial_read(uint16_t addr);
void    serial_reset(void);

void serial_savestate(FILE *f);
void serial_loadstate(FILE *f);

extern int motor;
extern int acia_is_tape;
#endif
