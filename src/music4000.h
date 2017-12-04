#ifndef M4000_INC_H
#define M4000_INC_H

// To be called by main
extern void music4000_reset(void);

// To be called by the ACIA emulation
extern void music4000_shift(int value);
extern uint8_t music4000_read(void);

// To be called by the OS-specific MIDI implementation.
extern void music4000_note_on(int note, int vel);
extern void music4000_note_off(int note, int vel);

#endif
