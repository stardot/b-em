/*B-em v2.2 by Tom Walker
  Tape noise (not very good)*/
  
/* - overhaul by 'Diminished' */

#include "b-em.h"
#include <math.h>
#include "ddnoise.h"
#include "tapenoise.h"
#include "sound.h"

/*#define DISABLE_RELAY_NOISE*/

#define TAPENOISE_VOLUME (0.25)

/* This is essential if you want it to feel like a real Beeb,
   but it might cause dropouts on things like rPi (works
   OK on my Mac -- YMMV, YOLO etc.): */
#define LOW_LATENCY


#ifdef LOW_LATENCY
#define BUFLEN_TN         (768)
#define FREQ_TN           (FREQ_DD)
#define AUDIO_RINGBUF_LEN ((FREQ_TN * 15) / 100)
#else
/* conservative values, large buffers */
#define BUFLEN_TN         (BUFLEN_DD)
#define FREQ_TN           (FREQ_DD)
#define AUDIO_RINGBUF_LEN ((FREQ_TN * 30) / 100)
#endif

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;

#define PI 3.14159

static ALLEGRO_SAMPLE *tsamples[2];

/* Hmm. We might need to allocate this on the heap;
   it might be about 26K ? */
static int16_t audio_ringbuf[AUDIO_RINGBUF_LEN];

static uint32_t audio_ringbuf_start = 0;
static uint32_t audio_ringbuf_fill = 0;

/* new plan: have two precomputed sine waves, one slightly
   too fast, one slightly too slow; choose one depending on
   whether our audio ring buffer is getting too full or
   too empty.
   
   pick 1208 Hz, because it places us nicely between
   two sine wave lengths (at e.g. 44100 Hz output sampling rate):
   (44100 / 1208) = 36.5 samples per cycle, which gives us
   36 smps/cyc for a "fast" sine wave (1225 Hz), and 37 smps/cyc for
   a "slow" sine wave (1192 Hz).
   
   we only do this during data sections; during leader we can just throw
   away cycles if we're too fast.
*/

#define TAPENOISE_BASEFREQ 1208

#define SLOW_SMPS ((FREQ_TN / TAPENOISE_BASEFREQ) + 1)
#define FAST_SMPS ((FREQ_TN / TAPENOISE_BASEFREQ))

/* each of these represents one or two cycles at the output sample rate,
   so we can just copy it to the output buffer when Allegro wants audio. */
static int16_t sinewave_2400_slow[SLOW_SMPS]; /* a pair of 2400 cycles */
static int16_t sinewave_2400_fast[FAST_SMPS];
static int16_t sinewave_1200_slow[SLOW_SMPS]; /* one single 1200 cycle */
static int16_t sinewave_1200_fast[FAST_SMPS];

static void init_sine_waves(void);
static void ringbuf_play (int16_t *buf);
static void ringbuf_append (int16_t *src, uint32_t len, uint8_t *no_emsgs_inout);

static void init_sine_waves(void) {
    uint32_t n;
    double d;
    double radians_per_sample;
    radians_per_sample = (4.0 * PI) / (double) SLOW_SMPS;
    for (n=0, d=0.0; n < SLOW_SMPS; n++) {
        sinewave_2400_slow[n] = (int) round(sin(d) * TAPENOISE_VOLUME * 32767.0);
        d += radians_per_sample;
    }
    radians_per_sample = (4.0 * PI) / (double) FAST_SMPS;
    for (n=0, d=0.0; n < FAST_SMPS; n++) {
        sinewave_2400_fast[n] = (int) round(sin(d) * TAPENOISE_VOLUME * 32767.0);
        d += radians_per_sample;
    }
    radians_per_sample = (2.0 * PI) / (double) SLOW_SMPS;
    for (n=0, d=0.0; n < SLOW_SMPS; n++) {
        sinewave_1200_slow[n] = (int) round(sin(d) * TAPENOISE_VOLUME * 32767.0);
        d += radians_per_sample;
    }
    radians_per_sample = (2.0 * PI) / (double) FAST_SMPS;
    for (n=0, d=0.0; n < FAST_SMPS; n++) {
        sinewave_1200_fast[n] = (int) round(sin(d) * TAPENOISE_VOLUME * 32767.0);
        d += radians_per_sample;
    }
    memset(audio_ringbuf, 0, sizeof(int16_t) * AUDIO_RINGBUF_LEN);
}

void tapenoise_init(ALLEGRO_EVENT_QUEUE *queue)
{
    ALLEGRO_PATH *dir;

    log_debug("tapenoise: tapenoise_init");
    if ((voice = al_create_voice(FREQ_TN,
                                 ALLEGRO_AUDIO_DEPTH_INT16,
                                 ALLEGRO_CHANNEL_CONF_1))) {
        if ((mixer = al_create_mixer(FREQ_TN,
                                     ALLEGRO_AUDIO_DEPTH_INT16,
                                     ALLEGRO_CHANNEL_CONF_1))) {
            if (al_attach_mixer_to_voice(mixer, voice)) {
                if ((stream = al_create_audio_stream(4,
                                                     BUFLEN_TN,
                                                     FREQ_TN,
                                                     ALLEGRO_AUDIO_DEPTH_INT16,
                                                     ALLEGRO_CHANNEL_CONF_1))) {
                    if (al_attach_audio_stream_to_mixer(stream, mixer)) {
                        dir = al_create_path_for_directory("ddnoise");
                        tsamples[0] = find_load_wav(dir, "motoron");
                        tsamples[1] = find_load_wav(dir, "motoroff");
                        al_destroy_path(dir);
                        init_sine_waves();
                    } else
                        log_error("sound: unable to attach stream to mixer for tape noise");
                } else
                    log_error("sound: unable to create stream for tape noise");
            } else
                log_error("sound: unable to attach mixer to voice for tape noise");
        } else
            log_error("sound: unable to create mixer for tape noise");
    } else
        log_error("sound: unable to create voice for tape noise");
}

void tapenoise_close()
{
    ALLEGRO_SAMPLE *smp;

    log_debug("tapenoise: tapenoise_close");
    if ((smp = tsamples[0]))
        al_destroy_sample(smp);
    if ((smp = tsamples[1]))
        al_destroy_sample(smp);
}

static uint8_t currently_fast = 0;

static uint8_t is_ringbuf_filling (uint8_t now_fast,
                                   uint8_t hysteresis,
                                   uint32_t fill) {
    if (hysteresis) {
        /* "gentle" speed compensation */
        if ( ( ! now_fast ) && (fill >= ((3 * AUDIO_RINGBUF_LEN) / 4))) {
            // buffer is filling up; switch to fast mode
            return 1;
        } else if (now_fast && (fill <= ((1 * AUDIO_RINGBUF_LEN) / 4))) {
            // buffer is emptying; switch to slow mode
            return 0;
        }
    } else {
        /* "twitchy" speed compensation */
        if (fill >= (AUDIO_RINGBUF_LEN / 2)) {
            return 1;
        } else {
            return 0;
        }
    }
    return now_fast; /* if no change */
}

/* receive 1/1200 seconds' worth of data;
   (this is usually one bit, but not if 300 baud) */
void tapenoise_send_1200 (char tone_1200th, uint8_t *no_emsgs_inout) {

    int16_t *sine1200, *sine2400;
    uint32_t num_smps;
    uint8_t hysteresis;
    
#ifdef SPEED_COMPENSATION_HYSTERESIS
    hysteresis = 1;
#else
    hysteresis = 0;
#endif

    currently_fast = is_ringbuf_filling (currently_fast,
                                         hysteresis,
                                         audio_ringbuf_fill);
    
    /* During leader sections, we can do speed compensation
       just by throwing away cycles. This sounds less
       distracting, hence this condition for adding the
       cycle to the ring buffer. During data blocks, we
       can't do this and we have to pick a fast/slow sine wave
       and suffer the wobble of speed changes instead, but
       the 1200/2400 switching will mask the audible effect. */
    if (('L' != tone_1200th) || ! currently_fast) {
    
        if (currently_fast) {
            num_smps = FAST_SMPS;
            sine1200 = sinewave_1200_fast;
            sine2400 = sinewave_2400_fast;
        } else {
            num_smps = SLOW_SMPS;
            sine1200 = sinewave_1200_slow;
            sine2400 = sinewave_2400_slow;
        }

        if ('0' == tone_1200th) {
            ringbuf_append(sine1200, num_smps, no_emsgs_inout);
        } else if (('1' == tone_1200th) || ('L' == tone_1200th)) {
            ringbuf_append(sine2400, num_smps, no_emsgs_inout);
        } else {
            ringbuf_append(NULL, num_smps, no_emsgs_inout);
        }
        
    }
    
}


void tapenoise_poll (void) {
    int16_t *tapebuffer;
    if (0 == audio_ringbuf_fill) {
        return;
    }
    /* now output an audio chunk from the start of the ring buffer */
    if (NULL != (tapebuffer = al_get_audio_stream_fragment(stream))) {
        ringbuf_play(tapebuffer);
        al_set_audio_stream_fragment(stream, tapebuffer);
    }
}



/*#define TAPE_NOISE_RINGBUF_EMSGS*/

static void ringbuf_append (int16_t *src, uint32_t len, uint8_t *no_emsgs_inout) {
    uint32_t n, pos;
    if ( ! sound_tape ) {
        audio_ringbuf_fill = 0;
        return;
    }
    if ((audio_ringbuf_fill + len) >= AUDIO_RINGBUF_LEN) {
        if ( ! *no_emsgs_inout ) {
#ifdef TAPE_NOISE_RINGBUF_EMSGS
            log_warn("tapenoise: audio ring buffer is full\n");
#endif
            *no_emsgs_inout = 1; /* suppress further error message spam */
        }
        return;
    }
    *no_emsgs_inout = 0; /* re-enable future error messages */
    pos = audio_ringbuf_start + audio_ringbuf_fill;
    for (n=0; n < len; n++, pos++) {
        int16_t i;
        if (pos >= AUDIO_RINGBUF_LEN) {
            pos -= AUDIO_RINGBUF_LEN;
        }
        if (src != NULL) {
            i = src[n];
        } else {
            i = 0;
        }
        audio_ringbuf[pos] = i;
    }
    audio_ringbuf_fill += len;
}



static void ringbuf_play (int16_t *buf) {
    uint32_t n;
    
    /* remember, BUFLEN_TN is the Allegro output buffer length;
       AUDIO_RINGBUF_LEN is the internal ring buffer length. */
    
    for (n=0; (n < BUFLEN_TN) && (audio_ringbuf_fill > 0); n++) {
        /* implement ring buffer wrappery */
        if (audio_ringbuf_start >= AUDIO_RINGBUF_LEN) {
            audio_ringbuf_start = 0;
        }
        buf[n] = audio_ringbuf[audio_ringbuf_start];
        audio_ringbuf_start++;
        audio_ringbuf_fill--;
    }
    
    /* if fill was not complete, finish the job with zeros */
    if (n < BUFLEN_TN) {
        memset(buf + n, 0, sizeof(int16_t) * (BUFLEN_TN - n));
        /* only warn if buffer isn't completely empty
           (avoids endless error message spam) */
        if (n != 0) {
#ifdef TAPE_NOISE_RINGBUF_EMSGS
            log_warn("tapenoise: audio ring buffer is empty\n");
#endif
            return;
        }
    }
    
}


void tapenoise_motorchange(int stat)
{
    ALLEGRO_SAMPLE *smp;

    log_debug("tapenoise: motorchange, stat=%d", stat);
    /* tape overhaul v2: added sound_tape_relay gate */
    if (sound_tape_relay && (stat < 2) && (smp = tsamples[stat]))
        al_play_sample(smp, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
}
