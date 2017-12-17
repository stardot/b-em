#include "b-em.h"
#include "music4000.h"
#include "midi-linux.h"
#include "sound.h"
#include <allegro.h>
#include <pthread.h>
#include <unistd.h>

midi_dev_t midi_music4000;
midi_dev_t midi_music2000_out1;
midi_dev_t midi_music2000_out2;
midi_dev_t midi_music2000_out3;

#ifdef HAVE_JACK_JACK_H

static int jack_started = 0;
static pthread_t jack_thread;
static jack_client_t *jack_client;
static jack_port_t *jack_port;

static int midi_jack_process(jack_nframes_t nframes, void *arg) {
    void *port_buf;
    jack_midi_event_t ev;
    jack_midi_data_t *md;
    int i, midi_status;

    port_buf = jack_port_get_buffer(jack_port, nframes);
    for (i = 0; jack_midi_event_get(&ev, port_buf, i) == 0; i++) {
        md = ev.buffer;
        midi_status = md[0];
        switch(midi_status & 0xf0) {
            case 0x80:
                log_debug("midi-linux: jack midi note off, note=%d, vel=%d", md[1], md[2]);
                music4000_note_off(md[1], md[2]);
                break;
            case 0x90:
                log_debug("midi-linux: jack midi note on, note=%d, vel=%d", md[1], md[2]);
                music4000_note_on(md[1], md[2]);
                break;
        }
    }
    return 0;
}

void *jack_midi_run(void *arg) {
    jack_status_t status;
    int err;

    if ((jack_client = jack_client_open("B-Em", JackNullOption, &status))) {
        log_debug("midi-linux: jack client open");
        if ((err = jack_set_process_callback(jack_client, midi_jack_process, NULL)) == 0) {
            log_debug("midi-linux: jack process callback set");
            if ((jack_port = jack_port_register(jack_client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput|JackPortIsTerminal, 0))) {
                log_debug("midi-linux: jack midi port open");
                if ((err = jack_activate(jack_client)) == 0) {
                    log_debug("midi-linux: jack active");
                    jack_started = 1;
                    for (;;)
                        pause();
                }
                else
                    log_error("midi-linux: unable to activate jack client: %d", err);
                jack_port_unregister(jack_client, jack_port);
            } else
                log_error("midi-linux: unable to register jack midi port: %d", err);
        } else
            log_error("midi-linux: unable to set jack callback, err=%d", err);
        jack_client_close(jack_client);

    } else
        log_warn("midi-linux: m4000: unable to register as jack client, status=%x", status);
    return NULL;
}

static inline void midi_jack_init(void) {
    int err;

    if (midi_music4000.jack_enabled)
        if ((err = pthread_create(&jack_thread, NULL, jack_midi_run, NULL)) != 0)
            log_error("midi-linux: unable to create Jack MIDI thread: %s", strerror(err));
}

static inline void midi_jack_close(void) {
    if (jack_started) {
        pthread_cancel(jack_thread);
        jack_client_close(jack_client);
    }
}

static inline void midi_jack_load_config(void) {
    midi_music4000.jack_enabled = get_config_int("midi", "music4000_jack_enabled", 0);
}

static inline void midi_jack_save_config(void) {
    set_config_int("midi", "music4000_jack_enabled", midi_music4000.jack_enabled);
}

#else
#define midi_jack_init()
#define midi_jack_close()
#define midi_jack_load_config()
#define midi_jack_save_config()
#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H

extern int quited;

static pthread_t alsa_seq_thread;

static void *alsa_seq_midi_run(void *arg) {
    int status;
    snd_seq_t *midi_seq = NULL;
    int alsa_seq_port = -1;
    snd_seq_event_t *ev;

    if ((status = snd_seq_open(&midi_seq, "default", SND_SEQ_OPEN_INPUT, 0)) < 0)
        log_warn("midi-linux: unable to open ALSA MIDI sequencer: %s", snd_strerror(status));
    else {
        snd_seq_set_client_name(midi_seq, "B-Em");
        log_debug("midi-linux: ALSA MIDI sequencer client created");
        alsa_seq_port = snd_seq_create_simple_port(midi_seq, "b-em:in", SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_SOFTWARE|SND_SEQ_PORT_TYPE_SYNTHESIZER);
        if (alsa_seq_port < 0)
            log_error("midi-linux: unable to create ALSA MIDI sequencer port: %s", strerror(errno));
        else {
            log_debug("midi-linux: ALSA MIDI sequencer port created");
            while (!quited) {
                log_debug("midi-linux: waiting for ALSA MIDI sequencer event");
                if (snd_seq_event_input(midi_seq, &ev) >= 0) {
                    log_debug("midi-linux: got ALSA MIDI sequencer event");
                    switch(ev->type) {
                        case SND_SEQ_EVENT_NOTEON:
                            log_debug("midi-linux: ALSA sequencer note on, tick=%d, note=%d, vel=%d", ev->time.tick, ev->data.note.note, ev->data.note.velocity);
                            music4000_note_on(ev->data.note.note, ev->data.note.velocity);
                            break;
                        case SND_SEQ_EVENT_NOTEOFF:
                            log_debug("midi-linux: ALSA sequencer note off, tick=%d, note=%d, evl=%d", ev->time.tick, ev->data.note.note, ev->data.note.velocity);
                            music4000_note_off(ev->data.note.note, ev->data.note.velocity);
                    }
                }
            }
            log_debug("midi-linux: ALSA sequencer thread finishing");
        }
        snd_seq_close(midi_seq);
    }
    return NULL;
}

static inline void midi_alsa_seq_init(void) {
    int err;

    if (midi_music4000.alsa_seq_enabled)
        if ((err = pthread_create(&alsa_seq_thread, NULL, alsa_seq_midi_run, NULL)) != 0)
            log_error("midi-linux: unable to create ALSA sequencer thread: %s", strerror(err));
}

typedef enum {
    MS_GROUND,
    MS_GOT_CMD,
    MS_GOT_NOTE
} midi_state_t;

static pthread_t alsa_raw_thread;

static void *alsa_raw_midi_run(void *arg) {
    int status;
    const char *device;
    snd_rawmidi_t *midiin;
    uint8_t buffer[16], *ptr, byte, note = 0;
    midi_state_t midi_state = MS_GROUND;
    void (*note_func)(int note, int vel) = music4000_note_off;

    device = get_config_string("midi", "alsa_raw_device", "default");
    if ((status = snd_rawmidi_open(&midiin, NULL, device, 0)) < 0)
        log_warn("midi-linux: unable to open ALSA raw MIDI port '%s': %s", device, snd_strerror(status));
    else {
        while (!quited) {
            log_debug("midi-linux: waiting to read ALSA raw MIDI port");
            if ((status = snd_rawmidi_read(midiin, buffer, sizeof buffer)) < 0)
                log_warn("midi-linux: ALSA raw MIDI read failed: %s", snd_strerror(status));
            else {
                log_debug("midi-linux: read %d bytes from ALSA raw MIDI port", status);
                for (ptr = buffer; status--; ) {
                    byte = *ptr++;
                    log_debug("midi-linux: ALSA raw MIDI read byte %02X", byte);
                    if (byte & 0x80) {
                        log_debug("midi-linux: ALSA raw MIDI byte is 'status'");
                        switch(byte >> 4) {
                            case 0x8:
                                log_debug("midi-linux: ALSA raw MIDI note off");
                                note_func = music4000_note_off;
                                midi_state = MS_GOT_CMD;
                                break;
                            case 0x9:
                                log_debug("midi-linux: ALSA raw MIDI note on");
                                note_func = music4000_note_on;
                                midi_state = MS_GOT_CMD;
                                break;
                            case 0xf:
                                if (byte < 0xf8) // system common messages
                                    midi_state = MS_GROUND;
                            default:;
                        }
                    } else {
                        log_debug("midi-linux: ALSA raw MIDI byte is data, state=%d", midi_state);
                        switch(midi_state) {
                            case MS_GOT_CMD:
                                note = byte;
                                midi_state = MS_GOT_NOTE;
                                break;
                            case MS_GOT_NOTE:
                                note_func(note, byte);
                                midi_state = MS_GOT_CMD;
                                break;
                            default:
                                midi_state = MS_GROUND;
                        }
                    }
                }
            }
        }
        snd_rawmidi_close(midiin);
    }
    return NULL;
}

static inline void midi_alsa_raw_init(void) {
    int err;

    if (midi_music4000.alsa_raw_enabled)
        if ((err = pthread_create(&alsa_raw_thread, NULL, alsa_raw_midi_run, NULL)) != 0)
            log_error("midi-linux: unable to create ALSA raw MIDI thread: %s", strerror(err));
}

static inline void midi_alsa_load_config(void) {
    midi_music4000.alsa_seq_enabled = get_config_int("midi", "m4000_alsa_seq_enabled", 1);
    midi_music4000.alsa_raw_enabled = get_config_int("midi", "m4000_alsa_raw_enabled", 1);
    midi_music2000_out1.alsa_seq_enabled = get_config_int("midi", "music2000_out1_alsa_seq_enabled", 0);
    midi_music2000_out2.alsa_seq_enabled = get_config_int("midi", "music2000_out2_alsa_seq_enabled", 0);
    midi_music2000_out3.alsa_seq_enabled = get_config_int("midi", "music2000_out3_alsa_seq_enabled", 0);
    midi_music2000_out1.alsa_raw_enabled = get_config_int("midi", "music2000_out1_alsa_raw_enabled", 1);
    midi_music2000_out2.alsa_raw_enabled = get_config_int("midi", "music2000_out2_alsa_raw_enabled", 0);
    midi_music2000_out3.alsa_raw_enabled = get_config_int("midi", "music2000_out3_alsa_raw_enabled", 0);
}

static inline void midi_alsa_save_config(void) {
    set_config_int("midi", "m4000_alsa_seq_enabled", midi_music4000.alsa_seq_enabled);
    set_config_int("midi", "m4000_alsa_raw_enabled", midi_music4000.alsa_raw_enabled);
    set_config_int("midi", "music2000_out1_alsa_seq_enabled", midi_music2000_out1.alsa_seq_enabled);
    set_config_int("midi", "music2000_out2_alsa_seq_enabled", midi_music2000_out2.alsa_seq_enabled);
    set_config_int("midi", "music2000_out3_alsa_seq_enabled", midi_music2000_out3.alsa_seq_enabled);
    set_config_int("midi", "music2000_out1_alsa_raw_enabled", midi_music2000_out1.alsa_raw_enabled);
    set_config_int("midi", "music2000_out2_alsa_raw_enabled", midi_music2000_out2.alsa_raw_enabled);
    set_config_int("midi", "music2000_out3_alsa_raw_enabled", midi_music2000_out3.alsa_raw_enabled);
}

#else
#define midi_alsa_seq_init()
#define midi_alsa_raw_init()
#define midi_alsa_load_config()
#define midi_alsa_save_config()
#endif

void midi_init(void) {
    if (sound_music5000) {
        midi_jack_init();
        midi_alsa_seq_init();
        midi_alsa_raw_init();
    }
}

void midi_close(void) {
    midi_jack_close();
}

void midi_load_config(void) {
    midi_jack_load_config();
    midi_alsa_load_config();
}

void midi_save_config(void) {
    midi_jack_save_config();
    midi_alsa_save_config();
}
