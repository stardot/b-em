#ifndef __INC_SOUNDOPENAL_H
#define __INC_SOUNDOPENAL_H

#define SOUND_SO  0
#define SOUND_DD  1
#define SOUND_M5  2

/* Source frequencies in Hz */

#define FREQ_SO  31250   // normal sound
#define FREQ_DD  44100   // disc drive noise
#define FREQ_M5  46875   // music 5000

/* Source buffer lengths in time samples */

#define BUFLEN_SO 2000   //  64ms @ 31.25KHz  (must be multiple of 2)
#define BUFLEN_DD 4410   // 100ms @ 44.1KHz
#define BUFLEN_M5 3000   //  64ms @ 46.875KHz (must be multiple of 3)

extern size_t buflen_m5;

/* Should be the same or larger than the maximum of the above */

#define BUFLEN_MAX 4410

void al_init_main(int argc, char *argv[]);
/*void al_init();*/
void al_close();
void al_givebuffer(int16_t *buf);
void al_givebufferdd(int16_t *buf);
void al_givebufferm5(int16_t *buf);

#endif
