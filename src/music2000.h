#ifndef __MUSIC_4000_H
#define __MUSIC_4000_H

#include "b-em.h"
#include "acia.h"
#include "midi.h"

uint8_t music2000_read(uint32_t addr);
void music2000_write(uint32_t addr, uint8_t val);
void music2000_poll(void);
void music2000_init(midi_dev_t *out1, midi_dev_t *out2, midi_dev_t *out3);

#endif
