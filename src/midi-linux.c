#include "b-em.h"
#include "config.h"
#include "music2000.h"
#include "music4000.h"
#include "midi-linux.h"
#include "sound.h"
#include <pthread.h>
#include <unistd.h>

midi_dev_t midi_music4000;
midi_dev_t midi_music2000_out1;
midi_dev_t midi_music2000_out2;
midi_dev_t midi_music2000_out3;

#ifdef HAVE_JACK_JACK_H

#define JACK_RINGBUF_SIZE 1024

static int jack_started = 0;
static jack_client_t *jack_client;

static void midi_jack_m4000_proc(jack_nframes_t nframes) {
    void *port_buf;
    jack_midi_event_t ev;
    jack_midi_data_t *md;
    int i, midi_status;

    if (midi_music4000.jack_enabled) {
        port_buf = jack_port_get_buffer(midi_music4000.jack_port, nframes);
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
    }
}

static void midi_jack_m2000_proc(midi_dev_t *midi, jack_nframes_t nframes, jack_nframes_t *frame_time) {
    void *port_buf;
    size_t bytes;
    jack_midi_data_t md[4], *dest;

    if (midi->jack_enabled || midi->jack_port) {
        port_buf = jack_port_get_buffer(midi->jack_port, nframes);
        jack_midi_clear_buffer(port_buf);
        while ((bytes = jack_ringbuffer_read(midi->ring_buf, (char *)md, sizeof md)) > 0) {
            if ((dest = jack_midi_event_reserve(port_buf, (*frame_time)++, bytes)))
                memcpy(dest, md, bytes);
            else {
                log_debug("midi-linux: jack failed to provide buffer");
                break;
            }
        }
    }
}

static int midi_jack_process(jack_nframes_t nframes, void *arg) {
    jack_nframes_t frame_time;

    midi_jack_m4000_proc(nframes);
    frame_time = 0;
    midi_jack_m2000_proc(&midi_music2000_out1, nframes, &frame_time);
    midi_jack_m2000_proc(&midi_music2000_out2, nframes, &frame_time);
    midi_jack_m2000_proc(&midi_music2000_out3, nframes, &frame_time);
    return 0;
}

static inline void m4000_jack_init(void) {
    if (midi_music4000.jack_enabled) {
        if ((midi_music4000.jack_port = jack_port_register(jack_client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput|JackPortIsTerminal, 0)))
            log_debug("midi-linux: M4000 jack midi port open");
        else
            log_error("midi-linux: unable to register jack midi-in port");
    }
}

static inline void m2000_jack_init(midi_dev_t *midi, const char *pname) {
    if (midi->jack_enabled) {
        if ((midi->jack_port = jack_port_register(jack_client, pname, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0))) {
            log_debug("midi-linux: M2000 jack midi port %s open", pname);
            if (!(midi->ring_buf = jack_ringbuffer_create(JACK_RINGBUF_SIZE))) {
                log_error("midi-linux: unable to create ring buffer for jack %s port", pname);
                jack_port_unregister(jack_client, midi->jack_port);
                midi->jack_port = NULL;
            }
        }
        else
            log_error("midi-linux: unable to register jack %s port", pname);
    }
}

static inline void midi_jack_init(void) {
    jack_status_t status;
    int err;

    if (midi_music4000.jack_enabled || midi_music2000_out1.jack_enabled || midi_music2000_out2.jack_enabled || midi_music2000_out3.jack_enabled) {
        if ((jack_client = jack_client_open("B-Em", JackNullOption, &status))) {
            log_debug("midi-linux: jack client open");
            if ((err = jack_set_process_callback(jack_client, midi_jack_process, NULL)) == 0) {
                log_debug("midi-linux: jack process callback set");
                m4000_jack_init();
                m2000_jack_init(&midi_music2000_out1, "midi-out1");
                m2000_jack_init(&midi_music2000_out2, "midi-out2");
                m2000_jack_init(&midi_music2000_out3, "midi-out3");
                if ((err = jack_activate(jack_client)) == 0) {
                    log_debug("midi-linux: jack active");
                    jack_started = 1;
                    return;
                }
                else
                    log_error("midi-linux: unable to activate jack client: %d", err);
            } else
                log_error("midi-linux: unable to set jack callback, err=%d", err);
            jack_client_close(jack_client);
        } else
            log_warn("midi-linux: m4000: unable to register as jack client, status=%x", status);
    }
}

static inline void midi_jack_close(void) {
    if (jack_started)
        jack_client_close(jack_client);
}

static inline void midi_jack_load_config(void) {
    midi_music4000.jack_enabled = get_config_bool("midi", "music4000_jack_enabled", 0);
    midi_music2000_out1.jack_enabled = get_config_bool("midi", "music2000_out1_jack_enabled", 0);
    midi_music2000_out2.jack_enabled = get_config_bool("midi", "music2000_out2_jack_enabled", 0);
    midi_music2000_out3.jack_enabled = get_config_bool("midi", "music2000_out3_jack_enabled", 0);
}

static inline void midi_jack_save_config(void) {
    set_config_bool("midi", "music4000_jack_enabled", midi_music4000.jack_enabled);
    set_config_bool("midi", "music2000_out1_jack_enabled", midi_music2000_out1.jack_enabled);
    set_config_bool("midi", "music2000_out2_jack_enabled", midi_music2000_out2.jack_enabled);
    set_config_bool("midi", "music2000_out3_jack_enabled", midi_music2000_out3.jack_enabled);
}

static inline void midi_jack_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {
    if (midi->jack_enabled && midi->jack_port)
         jack_ringbuffer_write(midi->ring_buf, (char *)buffer, size);
}

#else
static inline void midi_jack_init(void) {}
static inline void midi_jack_close(void) {}
static inline void midi_jack_load_config(void) {}
static inline void midi_jack_save_config(void) {}
static inline void midi_jack_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {}
#endif

#ifdef HAVE_ALSA_ASOUNDLIB_H

extern int quitting;

static pthread_t alsa_seq_thread;
static snd_seq_t *midi_seq = NULL;

static void *alsa_seq_midi_run(void *arg) {
    snd_seq_event_t *ev;

    while (!quitting) {
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
    return NULL;
}

static void m4000_seq_init(void) {
    int port, err;

    if (midi_music4000.alsa_seq_enabled) {
        port = snd_seq_create_simple_port(midi_seq, "b-em:in", SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_SOFTWARE|SND_SEQ_PORT_TYPE_APPLICATION);
        if (port < 0)
            log_error("midi-linux: unable to create ALSA sequencer port b-em:in: %s", strerror(errno));
        else {
            midi_music4000.alsa_seq_port = port;
            log_debug("midi-linux: created ALSA sequencer port b-em:in %d", port);
            if ((err = pthread_create(&alsa_seq_thread, NULL, alsa_seq_midi_run, NULL)) != 0)
                log_error("midi-linux: unable to create ALSA sequencer thread: %s", strerror(err));
        }
    }
}

static void m2000_seq_init(midi_dev_t *midi, const char *pname) {
    int port;

    if (midi->alsa_seq_enabled) {
        port = snd_seq_create_simple_port(midi_seq, pname, SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_SOFTWARE);
        if (port < 0)
            log_error("midi-linux: unable to create ALSA MIDI sequencer port %s: %s", pname, strerror(errno));
        else {
            midi->alsa_seq_port = port;
            log_debug("midi-linux: created ALSA sequencer port %s %d", pname, port);
        }
    }
}

static inline void midi_alsa_seq_init(void) {
    int status, mode;

    if (midi_music2000_out1.alsa_seq_enabled || midi_music2000_out2.alsa_seq_enabled || midi_music2000_out3.alsa_seq_enabled) {
        if (midi_music4000.alsa_seq_enabled) {
            mode = SND_SEQ_OPEN_DUPLEX;
            log_debug("linux-midi: opening sequencer duplex");
        }
        else {
            mode = SND_SEQ_OPEN_OUTPUT;
            log_debug("linux-midi: opening sequencer for output");
        }
    } else {
        if (midi_music4000.alsa_seq_enabled) {
            mode = SND_SEQ_OPEN_INPUT;
            log_debug("linux-midi: opening sequencer for intput");
        }
        else
            mode = 0;
    }
    if (mode) {
        if ((status = snd_seq_open(&midi_seq, "default", mode, 0)) < 0)
            log_warn("midi-linux: unable to open ALSA MIDI sequencer: %s", snd_strerror(status));
        else {
            snd_seq_set_client_name(midi_seq, "B-Em");
            log_debug("midi-linux: ALSA MIDI sequencer client created");
            m4000_seq_init();
            m2000_seq_init(&midi_music2000_out1, "b-em:out1");
            m2000_seq_init(&midi_music2000_out2, "b-em:out2");
            m2000_seq_init(&midi_music2000_out3, "b-em:out3");
        }
    }
}

static inline void alsa_seq_ctrl(snd_seq_event_t *ev, uint8_t *buffer) {
    ev->data.control.channel = buffer[0] & 0x0f;
    ev->data.control.param = buffer[1];
    ev->data.control.value = buffer[2];
}

static void midi_alsa_seq_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {
    snd_seq_event_t ev;
    int res;

    if (midi->alsa_seq_enabled && midi->alsa_seq_port) {
        snd_seq_ev_clear(&ev);

        switch(buffer[0] >> 4) {
            case 0x8:
                snd_seq_ev_set_noteoff(&ev, buffer[0] & 0x0f, buffer[1], buffer[2]);
                break;
            case 0x9:
                snd_seq_ev_set_noteon(&ev, buffer[0] & 0x0f, buffer[1], buffer[2]);
                break;
            case 0xA:
                snd_seq_ev_set_keypress(&ev, buffer[0] & 0x0f, buffer[1], buffer[2]);
                break;
            case 0xB:
                ev.type = SND_SEQ_EVENT_CONTROLLER;
                alsa_seq_ctrl(&ev, buffer);
                break;
            case 0xC:
                snd_seq_ev_set_pgmchange(&ev, buffer[0] & 0x0f, buffer[1]);
                break;
            case 0xD:
                ev.type = SND_SEQ_EVENT_CHANPRESS;
                alsa_seq_ctrl(&ev, buffer);
                break;
            case 0xE:
                snd_seq_ev_set_pitchbend(&ev, buffer[0] & 0x0f, buffer[1]);
                break;
            case 0xF:
                switch (buffer[0] & 0x0f) {
                    case 0x1:
                        ev.type = SND_SEQ_EVENT_QFRAME;
                        ev.data.control.value = buffer[1];
                        break;
                    case 0x2:
                        ev.type = SND_SEQ_EVENT_SONGPOS;
                        ev.data.control.value = buffer[1] | (buffer[2] << 7);
                        break;
                    case 0x3:
                        ev.type = SND_SEQ_EVENT_SONGSEL;
                        ev.data.control.value = buffer[1];
                        break;
                    case 0x4:
                    case 0x5:
                    case 0x9:
                    case 0xD:
                    case 0xE:
                        ev.type = SND_SEQ_EVENT_NONE;
                        break;
                    case 0x6:
                        ev.type = SND_SEQ_EVENT_TUNE_REQUEST;
                        break;
                    case 0x8:
                        ev.type = SND_SEQ_EVENT_TICK;
                        break;
                    case 0xA:
                        ev.type = SND_SEQ_EVENT_START;
                        break;
                    case 0xB:
                        ev.type = SND_SEQ_EVENT_CONTINUE;
                        break;
                    case 0xC:
                        ev.type = SND_SEQ_EVENT_STOP;
                        break;
                    case 0xf:
                        ev.type = SND_SEQ_EVENT_RESET;
                }
        }
        if (ev.type != SND_SEQ_EVENT_NONE) {
            snd_seq_ev_set_fixed(&ev);
            snd_seq_ev_set_source(&ev, midi->alsa_seq_port);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            log_debug("midi-linux: ALSA seq event, type=%d, flags=%02X, tag=%02X, queue=%02X", ev.type, ev.flags, ev.tag, ev.queue);
            if ((res = snd_seq_event_output(midi_seq, &ev)) < 0)
                log_warn("midi-linux: unable to send to sequencer: %s", snd_strerror(res));
            else
                snd_seq_drain_output(midi_seq);
        }
    }
}

typedef enum {
    MS_GROUND,
    MS_GOT_CMD,
    MS_GOT_NOTE
} linux_midi_state_t;

static pthread_t alsa_raw_thread;

static void *alsa_raw_midi_run(void *arg) {
    int status;
    const char *device;
    snd_rawmidi_t *midiin;
    uint8_t buffer[16], *ptr, byte, note = 0;
    linux_midi_state_t midi_state = MS_GROUND;
    void (*note_func)(int note, int vel) = music4000_note_off;

    device = get_config_string("midi", "alsa_raw_device", "default");
    if ((status = snd_rawmidi_open(&midiin, NULL, device, 0)) < 0)
        log_warn("midi-linux: unable to open ALSA raw MIDI port '%s': %s", device, snd_strerror(status));
    else {
        while (!quitting) {
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

static void m2000_raw_init(midi_dev_t *midi) {
    int status;

    if (midi->alsa_raw_enabled) {
        if ((status = snd_rawmidi_open(NULL, &midi->alsa_raw_port, midi->alsa_raw_device, 0)) < 0) {
            log_warn("midi-linux: unable to open ALSA raw MIDI port '%s': %s", midi->alsa_raw_device, snd_strerror(status));
            midi->alsa_raw_port = NULL;
        }
    }
}

static inline void midi_alsa_raw_init(void) {
    int err;

    if (midi_music4000.alsa_raw_enabled)
        if ((err = pthread_create(&alsa_raw_thread, NULL, alsa_raw_midi_run, NULL)) != 0)
            log_error("midi-linux: unable to create ALSA raw MIDI thread: %s", strerror(err));

    m2000_raw_init(&midi_music2000_out1);
    m2000_raw_init(&midi_music2000_out2);
    m2000_raw_init(&midi_music2000_out3);
}

static inline void midi_alsa_load_config(void) {
    midi_music4000.alsa_seq_enabled = get_config_bool("midi", "music4000_alsa_seq_enabled", 1);
    midi_music4000.alsa_raw_enabled = get_config_bool("midi", "music4000_alsa_raw_enabled", 1);
    midi_music4000.alsa_raw_device  = strdup(get_config_string("midi", "music4000_alsa_raw_device", "default"));
    midi_music2000_out1.alsa_seq_enabled = get_config_bool("midi", "music2000_out1_alsa_seq_enabled", 0);
    midi_music2000_out2.alsa_seq_enabled = get_config_bool("midi", "music2000_out2_alsa_seq_enabled", 0);
    midi_music2000_out3.alsa_seq_enabled = get_config_bool("midi", "music2000_out3_alsa_seq_enabled", 0);
    midi_music2000_out1.alsa_raw_enabled = get_config_bool("midi", "music2000_out1_alsa_raw_enabled", 1);
    midi_music2000_out2.alsa_raw_enabled = get_config_bool("midi", "music2000_out2_alsa_raw_enabled", 0);
    midi_music2000_out3.alsa_raw_enabled = get_config_bool("midi", "music2000_out3_alsa_raw_enabled", 0);
    midi_music2000_out1.alsa_raw_device  = strdup(get_config_string("midi", "music2000_out1_alsa_raw_device", "default"));
    midi_music2000_out2.alsa_raw_device  = strdup(get_config_string("midi", "music2000_out2_alsa_raw_device", "default"));
    midi_music2000_out3.alsa_raw_device  = strdup(get_config_string("midi", "music2000_out3_alsa_raw_device", "default"));
}

static inline void midi_alsa_save_config(void) {
    set_config_bool("midi", "music4000_alsa_seq_enabled", midi_music4000.alsa_seq_enabled);
    set_config_bool("midi", "music4000_alsa_raw_enabled", midi_music4000.alsa_raw_enabled);
    set_config_string("midi", "music4000_alsa_raw_device", midi_music4000.alsa_raw_device);
    set_config_bool("midi", "music2000_out1_alsa_seq_enabled", midi_music2000_out1.alsa_seq_enabled);
    set_config_bool("midi", "music2000_out2_alsa_seq_enabled", midi_music2000_out2.alsa_seq_enabled);
    set_config_bool("midi", "music2000_out3_alsa_seq_enabled", midi_music2000_out3.alsa_seq_enabled);
    set_config_bool("midi", "music2000_out1_alsa_raw_enabled", midi_music2000_out1.alsa_raw_enabled);
    set_config_bool("midi", "music2000_out2_alsa_raw_enabled", midi_music2000_out2.alsa_raw_enabled);
    set_config_bool("midi", "music2000_out3_alsa_raw_enabled", midi_music2000_out3.alsa_raw_enabled);
    set_config_string("midi", "music2000_out1_alsa_raw_device", midi_music2000_out1.alsa_raw_device);
    set_config_string("midi", "music2000_out2_alsa_raw_device", midi_music2000_out2.alsa_raw_device);
    set_config_string("midi", "music2000_out3_alsa_raw_device", midi_music2000_out3.alsa_raw_device);
}

static inline void midi_alsa_raw_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {
    if (midi->alsa_raw_enabled && midi->alsa_raw_port) {
        log_debug("midi-linux: sending to ALSA raw: status=%02X, len=%d", buffer[0], (int)size);
        if (snd_rawmidi_write(midi->alsa_raw_port, buffer, size) <= 0)
            log_warn("midi-linux: unable to send MIDI byte to ALSA raw");
    }
}

#else
static inline void midi_alsa_seq_init(void) {}
static inline void midi_alsa_raw_init(void) {}
static inline void midi_alsa_load_config(void) {}
static inline void midi_alsa_save_config(void) {}
static void midi_alsa_seq_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {}
static inline void midi_alsa_raw_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {}
#endif

void midi_init(void) {
    if (sound_music5000) {
        midi_jack_init();
        midi_alsa_seq_init();
        midi_alsa_raw_init();
        music2000_init(&midi_music2000_out1, &midi_music2000_out2, &midi_music2000_out3);
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

void midi_send_msg(midi_dev_t *midi, uint8_t *buffer, size_t size) {
    midi_jack_send_msg(midi, buffer, size);
    midi_alsa_seq_send_msg(midi, buffer, size);
    midi_alsa_raw_send_msg(midi, buffer, size);
}
