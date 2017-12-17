#ifndef MIDI_LINUX_INC_H
#define MIDI_LINUX_INC_H

#include "midi.h"

#ifdef HAVE_JACK_JACK_H
#include <jack/jack.h>
#include <jack/midiport.h>
#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H
#include <alsa/asoundlib.h>
#endif

typedef struct {
#ifdef HAVE_JACK_JACK_H
    int           jack_enabled;
    jack_port_t   *jack_port;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    int           alsa_seq_enabled;
    int           alsa_raw_enabled;
    int           alsa_seq_port;
    snd_rawmidi_t *alsa_raw_port;
#endif
} midi_dev_t;

extern midi_dev_t midi_music4000;
extern midi_dev_t midi_music2000_out1;
extern midi_dev_t midi_music2000_out2;
extern midi_dev_t midi_music2000_out3;
    
#endif
