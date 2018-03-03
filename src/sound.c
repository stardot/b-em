/*B-em v2.2 by Tom Walker
  Internal SN sound chip emulation*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "via.h"
#include "uservia.h"
#include "soundopenal.h"
#include "music5000.h"

int sound_internal = 0, sound_beebsid = 0, sound_dac = 0, sound_ddnoise = 0, sound_tape = 0, sound_music5000 = 0;
int sound_filter = 0;

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;

static int sound_pos = 0;
static short sound_buffer[BUFLEN_SO];

static int m5_pos = 0;
static short m5_buffer[BUFLEN_M5 * 2];

#define NCoef 4
static float iir(float NewSample) {
    float ACoef[NCoef+1] = {
        0.30631912757971225000,
        0.00000000000000000000,
        -0.61263825515942449000,
        0.00000000000000000000,
        0.30631912757971225000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.86772356053227330000,
        1.08459167506874430000,
        -0.37711292573951394000,
        0.17253125052500490000
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

void sound_poll(void)
{
    float *buf;
    int c;

#if 0
        if (sound_music5000) {
                // every 64us Music 5000 must provide 3 stereo samples (46.875KHz)
                music5000_fillbuf( m5_buffer + m5_pos, 3);
                // skip forward 3 stereo samples
                m5_pos += 6;
                // buflen_m5 is in units of samples, not integers
                if ((m5_pos >> 1) == buflen_m5)
                {
                        m5_pos = 0;
                        al_givebufferm5(m5_buffer);
                }
        }
#endif
    if (sound_beebsid)
        sid_fillbuf(sound_buffer + sound_pos, 2);
    if (sound_internal)
        sn_fillbuf(sound_buffer + sound_pos, 2);
    if (sound_dac) {
        sound_buffer[sound_pos]     += (((int)lpt_dac - 0x80) * 32);
        sound_buffer[sound_pos + 1] += (((int)lpt_dac - 0x80) * 32);
    }

    // skip forward 2 mono samples
    sound_pos += 2;
    if (sound_pos == BUFLEN_SO) {
        if ((buf = al_get_audio_stream_fragment(stream))) {
            if (sound_filter) {
                for (c = 0; c < BUFLEN_SO; c++)
                    buf[c] = iir((float)sound_buffer[c] / 32767.0);
            } else {
                for (c = 0; c < BUFLEN_SO; c++)
                    buf[c] = (float)sound_buffer[c] / 32767.0;
            }
            sound_pos = 0;
            al_set_audio_stream_fragment(stream, buf);
            memset(sound_buffer, 0, sizeof(sound_buffer));
        } else
            log_debug("sound: overrun");
    }
}

void sound_init(void)
{
    if ((voice = al_create_voice(FREQ_SO, ALLEGRO_AUDIO_DEPTH_FLOAT32, ALLEGRO_CHANNEL_CONF_1))) {
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
