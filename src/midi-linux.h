#ifndef MIDI_LINUX_INC_H
#define MIDI_LINUX_INC_H

#include "midi.h"

#ifdef HAVE_JACK_JACK_H
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H
#include <alsa/asoundlib.h>
#endif

struct _midi_dev {
#ifdef HAVE_JACK_JACK_H
    bool          jack_enabled;
    jack_port_t   *jack_port;
    jack_ringbuffer_t *ring_buf;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    bool          alsa_seq_enabled;
    bool          alsa_raw_enabled;
    int           alsa_seq_port;
    snd_rawmidi_t *alsa_raw_port;
    const char    *alsa_raw_device;
#endif
};

extern midi_dev_t midi_music4000;
extern midi_dev_t midi_music2000_out1;
extern midi_dev_t midi_music2000_out2;
extern midi_dev_t midi_music2000_out3;

#endif
