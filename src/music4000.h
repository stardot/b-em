#ifndef M4000_INC_H
#define M4000_INC_H

extern void music4000_init(void);
extern void music4000_reset(void);
extern void music4000_shift(int value);
extern uint8_t music4000_read(void);

#endif
