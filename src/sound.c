/*B-em v2.2 by Tom Walker
  Internal SN sound chip emulation*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "via.h"
#include "uservia.h"
#include "music5000.h"
#include "paula.h"

bool sound_internal = false, sound_beebsid = false, sound_dac = false;
bool sound_ddnoise = false, sound_tape = false;
bool sound_music5000 = false, sound_filter = false;
bool sound_paula = false;

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;

static int sound_pos = 0;
static int sound_sn_pos = 0;

static short sound_buffer[BUFLEN_SO];

static int sound_sn76489_cycles = 0, sound_poll_cycles = 0;

#define NCoef 2
static float iir(float NewSample) {
    static const float ACoef[NCoef+1] = {
        0.9844825527642453,
        -1.9689651055284907,
        0.9844825527642453
    };

    static const float BCoef[NCoef+1] = {
        1,
        -1.9687243044104659,
        0.9692059066465155
    };

    static float y[NCoef+1]; //output samples
    static float x[NCoef+1]; //input samples
    int n;

    //shift the old samples
    for(n=NCoef; n>0; n--) {
       x[n] = x[n-1];
       y[n] = y[n-1];
    }

    //Calculate the new output
    x[0] = NewSample;
    y[0] = ACoef[0] * x[0];
    for(n=1; n<=NCoef; n++)
        y[0] += ACoef[n] * x[n] - BCoef[n] * y[n];

    return y[0];
}

static void sound_poll_all(void)
{
    float *buf;
    int c;

    if ((sound_internal || sound_beebsid) && stream) {
        int16_t temp_buffer[2] = {0};

        if (sound_beebsid)
            sid_fillbuf(temp_buffer, 2);
        if (sound_paula)
            paula_fillbuf(temp_buffer, 2);
        if (sound_dac) {
            temp_buffer[0] += (((int)lpt_dac - 0x80) * 32);
            temp_buffer[1] += (((int)lpt_dac - 0x80) * 32);
        }

        for (c = 0; c < 8/2; c++) {
            sound_buffer[sound_pos + c] += temp_buffer[0];
            sound_buffer[sound_pos + c + 4] += temp_buffer[1];
        }
        // skip forward 8 mono samples
        sound_pos += 8;
        if (sound_pos == BUFLEN_SO) {
            if ((buf = al_get_audio_stream_fragment(stream))) {
                if (sound_filter) {
                    for (c = 0; c < BUFLEN_SO; c++)
                        buf[c] = iir((float)sound_buffer[c] / 32767.0);
                } else {
                    for (c = 0; c < BUFLEN_SO; c++)
                        buf[c] = (float)sound_buffer[c] / 32767.0;
                }
                al_set_audio_stream_fragment(stream, buf);
                al_set_audio_stream_playing(stream, true);
            } else
                log_debug("sound: overrun");
            sound_pos = 0;
            sound_sn_pos = 0;
            memset(sound_buffer, 0, sizeof(sound_buffer));
        }
    }
}

void sound_poll(int cycles)
{
    sound_sn76489_cycles -= cycles;
    if (sound_sn76489_cycles < 0)
    {
        sound_sn76489_cycles += 16;

        if (sound_internal)
            sn_fillbuf(&sound_buffer[sound_sn_pos], 1);

        sound_sn_pos++;

        sound_poll_cycles -= 16;
        if (sound_poll_cycles < 0)
        {
            sound_poll_cycles += 128;
            sound_poll_all();
        }
    }
}

static ALLEGRO_VOICE *sound_create_voice(void)
{
    ALLEGRO_VOICE *voice;

    if ((voice = al_create_voice(FREQ_SO, ALLEGRO_AUDIO_DEPTH_FLOAT32, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with standard sound freq, float depth");
        return voice;
    }
    if ((voice = al_create_voice(FREQ_SO, ALLEGRO_AUDIO_DEPTH_INT24, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with standard sound freq, 24bit depth");
        return voice;
    }
    if ((voice = al_create_voice(FREQ_SO, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with standard sound freq, 16bit depth");
        return voice;
    }
    if ((voice = al_create_voice(FREQ_DD, ALLEGRO_AUDIO_DEPTH_FLOAT32, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with ddnoise freq, float depth");
        return voice;
    }
    if ((voice = al_create_voice(FREQ_DD, ALLEGRO_AUDIO_DEPTH_INT24, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with ddnoise freq, 24bit depth");
        return voice;
    }
    if ((voice = al_create_voice(FREQ_DD, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1))) {
        log_debug("sound: created voice with ddnoise freq, 16bit depth");
        return voice;
    }
    return NULL;
}

void sound_init(void)
{
    if ((voice = sound_create_voice())) {
        if ((mixer = al_create_mixer(FREQ_SO, ALLEGRO_AUDIO_DEPTH_FLOAT32, ALLEGRO_CHANNEL_CONF_1))) {
            if (al_attach_mixer_to_voice(mixer, voice)) {
                if ((stream = al_create_audio_stream(4, BUFLEN_SO, FREQ_SO, ALLEGRO_AUDIO_DEPTH_FLOAT32, ALLEGRO_CHANNEL_CONF_1))) {
                    if (!al_attach_audio_stream_to_mixer(stream, mixer))
                        log_error("sound: unable to attach stream to mixer for internal/SID/DAC sound");
                } else
                    log_error("sound: unable to create stream for internal/SID/DAC sound");
            } else
                log_error("sound: unable to attach mixer to voice for internal/SID/DAC sound");
        } else
            log_error("sound: unable to create mixer for internal/SID/DAC sound");
    } else
        log_error("sound: unable to create voice for internal/SID/DAC sound");
}

void sound_close(void)
{
    if (stream)
        al_destroy_audio_stream(stream);
    if (mixer)
        al_destroy_mixer(mixer);
    if (voice)
        al_destroy_voice(voice);
}
