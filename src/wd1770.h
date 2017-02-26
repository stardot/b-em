#ifndef __INC_WD1770_H
#define __INC_WD1770_H

void            wd1770_reset();
uint8_t         wd1770_read(uint16_t addr);
void            wd1770_write(uint16_t addr, uint8_t val);

#endif
