#ifndef __INC_DDNOISE_H
#define __INC_DDNOISE_H

extern SAMPLE *find_load_wav(const char *subdir, const char *name);
void ddnoise_init();
void ddnoise_close();
void ddnoise_seek(int len);
void ddnoise_mix();
extern int ddnoise_vol;
extern int ddnoise_type;

#endif
