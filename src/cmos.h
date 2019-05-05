#ifndef __INC_CMOS_H
#define __INC_CMOS_H

void cmos_update(uint8_t IC32, uint8_t sdbval);
void cmos_writeaddr(uint8_t val);
uint8_t cmos_read(void);
void cmos_load(MODEL m);
void cmos_save(MODEL m);

#endif
