/*B-em v2.2 by Tom Walker
  Tape noise (not very good)*/

#include "b-em.h"
#include <math.h>
#include "ddnoise.h"
#include "tapenoise.h"
#include "sound.h"

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;

static int tpnoisep = 0;
static int tmcount = 0;
static int16_t tapenoise[BUFLEN_DD];

static float swavepos = 0;

static int sinewave[32];

#define PI 3.142

static ALLEGRO_SAMPLE *tsamples[2];

void tapenoise_init(ALLEGRO_EVENT_QUEUE *queue)
{
    ALLEGRO_PATH *dir;
    int c;

    log_debug("tapenoise: tapenoise_init");
    if ((voice = al_create_voice(FREQ_DD, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
        if ((mixer = al_create_mixer(FREQ_DD, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
            if (al_attach_mixer_to_voice(mixer, voice)) {
                if ((stream = al_create_audio_stream(4, BUFLEN_DD, FREQ_DD, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
                    if (al_attach_audio_stream_to_mixer(stream, mixer)) {
                        dir = al_create_path_for_directory("ddnoise");
                        tsamples[0] = find_load_wav(dir, "motoron");
                        tsamples[1] = find_load_wav(dir, "motoroff");
                        al_destroy_path(dir);
                        for (c = 0; c < 32; c++)
                            sinewave[c] = (int)(sin((float)c * ((2.0 * PI) / 32.0)) * 128.0);
                    } else
                        log_error("sound: unable to attach stream to mixer for tape noise");
                } else
                    log_error("sound: unable to create stream for tape noise");
            } else
                log_error("sound: unable to attach mixer to voice for tape noise");
        } else
            log_error("sound: unable to create mixer for tape noise");
    } else
        log_error("sound: unable to create voice for for tape noise");
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

static void send_buffer(void)
{
    int16_t *tapebuffer;
    int c;

    tpnoisep = 0;
    if ((tapebuffer = al_get_audio_stream_fragment(stream))) {

        for (c = 0; c < BUFLEN_DD; c++) {
            tapebuffer[c] = tapenoise[c];
            tapenoise[c] = 0;
        }
        al_set_audio_stream_fragment(stream, tapebuffer);
    } else
        log_debug("tapenoise: overrun");
}

static void add_high(void)
{
    int c;
    float wavediv = (32.0f * 2400.0f) / (float) FREQ_DD;

    tmcount++;
    for (c = 0; c < 368; c++) {
        if (tpnoisep >= BUFLEN_DD)
            send_buffer();
        tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
        swavepos += wavediv;
    }
}

void tapenoise_addhigh(void)
{
    if (sound_tape)
        add_high();
}

static void add_dat(uint8_t dat)
{
    int c, d, e = 0;
    float wavediv = (32.0f * 2400.0f) / (float) FREQ_DD;

    for (c = 0; c < 30; c++) { /*Start bit*/
        if (tpnoisep >= BUFLEN_DD)
            send_buffer();
        tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
        e++;
        swavepos += (wavediv / 2);
    }
    swavepos = fmod(swavepos, 32.0);
    while (swavepos < 32.0) {
        if (tpnoisep >= BUFLEN_DD)
            send_buffer();
        tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
        swavepos += (wavediv / 2);
        e++;
    }
    for (d = 0; d < 8; d++) {
        swavepos = fmod(swavepos, 32.0);
        while (swavepos < 32.0) {
            if (tpnoisep >= BUFLEN_DD)
                send_buffer();
            tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * ((dat & 1) ? 50 : 64);
            if (dat & 1) swavepos += wavediv;
            else         swavepos += (wavediv / 2);
            e++;
        }
        dat >>= 1;
    }
    for ( ;e < 368; e++) { /*Stop bit*/
        if (tpnoisep >= BUFLEN_DD)
            send_buffer();
        tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
        swavepos += (wavediv / 2);
    }
    add_high();
}

void tapenoise_adddat(uint8_t dat)
{
    if (sound_tape)
        add_dat(dat);
}

void tapenoise_motorchange(int stat)
{
    ALLEGRO_SAMPLE *smp;

    log_debug("tapenoise: motorchange, stat=%d", stat);
    if ((stat < 2) && (smp = tsamples[stat]))
        al_play_sample(smp, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
}
