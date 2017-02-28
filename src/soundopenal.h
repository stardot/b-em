#ifndef __INC_SOUNDOPENAL_H
#define __INC_SOUNDOPENAL_H

void al_init_main(int argc, char *argv[]);
void al_init();
void al_close();
void al_givebuffer(int16_t *buf);
void al_givebufferdd(int16_t *buf);
void al_givebufferm5(int16_t *buf);

#endif
