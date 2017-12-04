#include <allegro.h>
#include "b-em.h"
#include "music4000.h"
#include "sound.h"

extern int quited;

#define NUM_BLOCKS 8

static uint8_t block = 0;
static uint8_t matrix[NUM_BLOCKS];
//static int count1 = 0;
//static int match = 0;
//uint8_t mask = 0x01;

void music4000_shift(int value) {
    if (value == 0)
        block = 0;
    else if (block < NUM_BLOCKS)
        block++;
    //log_debug("m4000: block is now #%d", block);
    //count1++;
        
}

uint8_t music4000_read(void) {
    int value = matrix[block];
    /*uint8_t value = 0xff;
    if (block == 0) {
        if (key[KEY_1_PAD])
            value &= ~0x01;
        if (key[KEY_2_PAD])
            value &= ~0x02;
        if (key[KEY_3_PAD])
            value &= ~0x04;
        if (key[KEY_4_PAD])
            value &= ~0x08;
        if (key[KEY_5_PAD])
            value &= ~0x10;
        if (key[KEY_6_PAD])
            value &= ~0x20;
        if (key[KEY_7_PAD])
            value &= ~0x40;
        if (key[KEY_8_PAD])
            value &= ~0x80;
    }
        
    //uint8_t value;
    // = matrix[block];
    if (count1++ > 1000) {
        mask << = 1;
        count2++;
        if (count2 > 8) {
            match++;
            if (match > 8)
                match = 0;
            count2 = 0;
            mask = 1;
        }
        count1 = 0;
    }
    * */
    //log_debug("m4000: read returns %02X", value);
    return value;
}

static void do_note(int note, int vel, int onoff) {
    uint8_t key_num, key_block, key_mask;

    if (note >= 36 && note < 98) {
        if (vel == 0)
            onoff = 0;
        key_num = note - 36;
        key_block = key_num/8;
        key_mask = 0x01 << (key_num - (key_block * 8));
        log_debug("m4000: note=%d, key=%d, block=%d, mask=%02X", note, key_num, key_block, key_mask);
        if (onoff)
            matrix[key_block] &= ~key_mask;
        else
            matrix[key_block] |= key_mask;
    } else
        log_debug("m4000: note %d off keyboard", note);
}

void music4000_note_on(int note, int vel) {
    do_note(note, vel, 1);
}

void music4000_note_off(int note, int vel) {
    do_note(note, vel, 0);
}

#ifdef WIN32

void music4000_init(void) {
    if (sound_music5000)
        log_warn("music4000: not implemented yet on Windows");
}

#else

#ifdef ALSA_MIDI

#include <pthread.h>
#include <alsa/asoundlib.h>

static int midi_port = -1;
static pthread_t midi_thread;

static void *midi_run(void *arg) {
    snd_seq_t *midi_seq = NULL;
    snd_seq_event_t *ev;
    int err;
    
    if ((err = snd_seq_open(&midi_seq, "default", SND_SEQ_OPEN_INPUT, 0)))
        log_error("m4000: unable to create MIDI sequencer: %s", strerror(errno));
    else {
        snd_seq_set_client_name(midi_seq, "B-Em");
        log_debug("m4000: midi client created");
        midi_port = snd_seq_create_simple_port(midi_seq, "b-em:in", SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_SOFTWARE|SND_SEQ_PORT_TYPE_SYNTHESIZER);
        if (midi_port < 0)
            log_error("m4000: unable to create MIDI port: %s", strerror(errno));
        else {
            log_debug("m4000: midi port created");
            while (!quited) {
                log_debug("m4000: waiting for MIDI event");
                if (snd_seq_event_input(midi_seq, &ev) == 0) {
                    log_debug("m4000: got MIDI event");
                    switch(ev->type) {
                        case SND_SEQ_EVENT_NOTEON:
                            log_debug("m4000: note on, tick=%d, note=%d, vel=%d", ev->time.tick, ev->data.note.note, ev->data.note.velocity); 
                            music4000_note_on(ev->data.note.note, ev->data.note.velocity);
                            break;
                        case SND_SEQ_EVENT_NOTEOFF:
                            log_debug("m4000: note off, tick=%d, note=%d, evl=%d", ev->time.tick, ev->data.note.note, ev->data.note.velocity); 
                            music4000_note_off(ev->data.note.note, ev->data.note.velocity);
                    }
                }
            }
            log_debug("m4000: midi thread finishing");
        }
        snd_seq_close(midi_seq);
    }
    return NULL;
}

void music4000_init(void) {
    int err;

    if (sound_music5000) {
        if ((err = pthread_create(&midi_thread, NULL, midi_run, NULL)) != 0) {
            log_error("m4000: unable to create MIDI thread: %s", strerror(err));
        }
    }
}

#else

#include <jack/jack.h>
#include <jack/midiport.h>

static pthread_t jack_midi_thread;
static jack_client_t *jack_client;
static jack_port_t *jack_port;

static int process(jack_nframes_t nframes, void *arg) {
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
                log_debug("m4000: jack midi note off, note=%d, vel=%d", md[1], md[2]);
                music4000_note_off(md[1], md[2]);
                break;
            case 0x90:
                log_debug("m4000: jack midi note on, note=%d, vel=%d", md[1], md[2]);
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
        log_debug("m4000: jack client open");
        if ((err = jack_set_process_callback(jack_client, process, NULL)) == 0) {
            log_debug("m4000: jack process callback set");
            if ((jack_port = jack_port_register(jack_client, "midi-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput|JackPortIsTerminal, 0))) {
                log_debug("m4000: jack midi port open");
                if ((err = jack_activate(jack_client)) == 0) {
                    log_debug("m4000: jack active");
                    for (;;)
                        pause();
                }
                else
                    log_error("m4000: unable to activate jack client: %d", err);
                jack_port_unregister(jack_client, jack_port);
            } else
                log_error("m4000: unable to register jack midi port: %d", err);
        } else
            log_error("m4000: unable to set jack callback, err=%d", err);
        jack_client_close(jack_client);
            
    } else
        log_error("m4000: unable to register as jack client, status=%x", status);
    return NULL;
}

void music4000_init(void) {
    int err;

    if (sound_music5000) {
        if ((err = pthread_create(&jack_midi_thread, NULL, jack_midi_run, NULL)) != 0) {
            log_error("m4000: unable to create MIDI thread: %s", strerror(err));
        }
    }
}

#endif
#endif

void music4000_reset(void) {
    int i;

    for (i = 0; i < NUM_BLOCKS; i++)
        matrix[i] = 0xff;
}
