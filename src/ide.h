#ifndef __INC_IDE_H
#define __INC_IDE_H

extern int      ide_enable;
extern int      ide_count;

void            ide_init();
void            ide_close();
void            ide_write(uint16_t addr, uint8_t val);
uint8_t         ide_read(uint16_t addr);
void            ide_callback();

#endif
