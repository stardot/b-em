#ifndef MUSIC5000_INC
#define MUSIC5000_INC

#include "sound.h"

void music5000_init(int speed);
void music5000_close(void);
void music5000_loadstate(FILE *f);
void music5000_savestate(FILE *f);
void music5000_fillbuf(int16_t *buffer, int len);
void music5000_streamfrag(void);
void music5000_write(uint16_t addr, uint8_t val);
void music5000_reset(void);
void music5000_poll(int cycles);
bool music5000_ok(void);

extern int music5000_fno;
extern sound_rec_t music5000_rec;

#endif
