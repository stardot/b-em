#ifndef __INC_SOUND_H
#define __INC_SOUND_H

extern int sound_internal, sound_beebsid, sound_dac, sound_ddnoise, sound_tape, sound_music5000;
extern int sound_filter;

void sound_init();
void sound_poll();

#endif
