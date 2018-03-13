#ifndef __INC_I8271_H
#define __INC_I8271_H

void i8271_reset(void);
uint8_t i8271_read(uint16_t addr);
void i8271_write(uint16_t addr, uint8_t val);

#endif
