#ifndef __MUSIC_4000_H
#define __MUSIC_4000_H

#include "b-em.h"
#include "acia.h"

uint8_t music2000_read(uint32_t addr);
void music2000_write(uint32_t addr, uint8_t val);
void music2000_poll(void);

#endif
