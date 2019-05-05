#ifndef __INC_WD1770_H
#define __INC_WD1770_H

void wd1770_reset(void);
uint8_t wd1770_read(uint16_t addr);
void wd1770_write(uint16_t addr, uint8_t val);

// Values for the WD1770

#define WD1770_NONE     0
#define WD1770_ACORN    1
#define WD1770_MASTER   2
#define WD1770_OPUS     3
#define WD1770_STL      4
#define WD1770_WATFORD  5

#endif
