#ifndef __INC_DDNOISE_H
#define __INC_DDNOISE_H

void ddnoise_init();
void ddnoise_close();
void ddnoise_seek(int len);
void ddnoise_mix();
extern int ddnoise_vol;
extern int ddnoise_type;

#endif
