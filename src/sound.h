#ifndef __INC_SOUND_H
#define __INC_SOUND_H

/* Source frequencies in Hz */

#define FREQ_SO  31250   // normal sound
#define FREQ_DD  44100   // disc drive noise
#define FREQ_M5  46875   // music 5000

/* Source buffer lengths in time samples */

#define BUFLEN_SO 2000   //  64ms @ 31.25KHz  (must be multiple of 2)
#define BUFLEN_DD 4410   // 100ms @ 44.1KHz
#define BUFLEN_M5 1500   //  64ms @ 46.875KHz (must be multiple of 3)

extern size_t buflen_m5;

extern bool sound_internal, sound_beebsid, sound_dac;
extern bool sound_ddnoise, sound_tape;
extern bool sound_music5000, sound_filter;

void sound_init(void);
void sound_poll(void);

#endif
