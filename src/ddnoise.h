#ifndef __INC_DDNOISE_H
#define __INC_DDNOISE_H

#include <allegro5/allegro_audio.h>
extern ALLEGRO_SAMPLE *find_load_wav(ALLEGRO_PATH *dir, const char *name);
void ddnoise_init(void);
void ddnoise_close(void);
void ddnoise_seek(int len);
void ddnoise_spinup(void);
void ddnoise_headdown(void);
void ddnoise_spindown(void);
extern int ddnoise_vol;
extern int ddnoise_type;
extern int ddnoise_ticks;

#endif
