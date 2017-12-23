#ifndef MIDI_INC_H
#define MIDI_INC_H

typedef struct _midi_dev midi_dev_t;

extern void midi_init(void);
extern void midi_close(void);
extern void midi_load_config(void);
extern void midi_save_config(void);
extern void midi_send_msg(midi_dev_t *dev, uint8_t *msg, size_t size);

#endif
