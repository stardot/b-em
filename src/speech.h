#ifndef BEM_SPEECH
#define BEM_SPEECH

#include "b-em.h"

extern uint8_t speech_status;

extern void speech_init(void);
extern void speech_reset(void);
extern void speech_close(void);
extern void speech_poll(void);
extern void speech_savestate(FILE *f);
extern void speech_loadstate(FILE *f);
extern void speech_streamfrag(void);
extern void speech_set_rs(bool value);
extern void speech_set_ws(bool value);
extern void speech_write(uint8_t val);
extern uint8_t speech_read(void);

#endif
