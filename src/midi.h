#ifndef MIDI_LINUX_INC_H
#define MIDI_LINUX_INC_H

extern void midi_init(void);
extern void midi_close(void);
extern void midi_load_config(void);
extern void midi_save_config(void);

#ifdef HAVE_JACK_JACK_H
extern int midi_m4000_jack_enabled;
extern int midi_m2000_out1_jack_enabled;
extern int midi_m2000_out2_jack_enabled;
extern int midi_m2000_out3_jack_enabled;
#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H
extern int midi_m4000_alsa_seq_enabled;
extern int midi_m4000_alsa_raw_enabled;
extern int midi_m2000_out1_alsa_seq_enabled;
extern int midi_m2000_out2_alsa_seq_enabled;
extern int midi_m2000_out3_alsa_seq_enabled;
extern int midi_m2000_out1_alsa_raw_enabled;
extern int midi_m2000_out2_alsa_raw_enabled;
extern int midi_m2000_out3_alsa_raw_enabled;
#endif

#endif
