#ifndef PAULA_INC
#define PAULA_INC

#include "sound.h"

#define DEVNO_PAULA 0xD0

void paula_init();
void paula_close(void);
void paula_loadstate(FILE *f);
void paula_savestate(FILE *f);
void paula_fillbuf(int16_t *buffer, int len);
void paula_write(uint16_t addr, uint8_t val);
bool paula_read(uint16_t addr, uint8_t *r);
void paula_reset();

extern sound_rec_t paula_rec;

#endif
