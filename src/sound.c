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
static float iir(float NewSample)
{
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

static void sound_rec_float(float *buf)
{
    if (sound_rec.fp) {
        if (!sound_rec.rec_started) {
            for (int c = 0; c < BUFLEN_SO; ++c) {
                if (buf[c]) {
                    sound_rec.rec_started = true;
                    break;
                }
            }
        }
        if (sound_rec.rec_started) {
            unsigned char tmp[BUFLEN_SO * 2];
            unsigned char *ptr = tmp;
            for (int c = 0; c < BUFLEN_SO; ++c) {
                int value = 32767 * buf[c];
                *ptr++ = value;
                *ptr++ = value >> 8;
            }
            fwrite(tmp, sizeof(tmp), 1, sound_rec.fp);
        }
    }
}

static void sound_rec_int(short *buf)
{
    if (sound_rec.fp) {
        if (!sound_rec.rec_started) {
            for (int c = 0; c < BUFLEN_SO; ++c) {
                if (buf[c]) {
                    sound_rec.rec_started = true;
                    break;
                }
            }
        }
        if (sound_rec.rec_started) {
            unsigned char tmp[BUFLEN_SO * 2];
            unsigned char *ptr = tmp;
            for (int c = 0; c < BUFLEN_SO; ++c) {
                int value = buf[c];
                *ptr++ = value;
                *ptr++ = value >> 8;
            }
            fwrite(tmp, sizeof(tmp), 1, sound_rec.fp);
        }
    }
}

static void sound_poll_all(void)
{
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

        for (int c = 0; c < 8/2; c++) {
            sound_buffer[sound_pos + c] += temp_buffer[0];
            sound_buffer[sound_pos + c + 4] += temp_buffer[1];
        }
        // skip forward 8 mono samples
        sound_pos += 8;
        if (sound_pos == BUFLEN_SO) {
            float *buf = al_get_audio_stream_fragment(stream);
            if (buf) {
                if (sound_filter) {
                    for (int c = 0; c < BUFLEN_SO; c++)
                        buf[c] = iir((float)sound_buffer[c] / 32767.0);
                    sound_rec_float(buf);
                } else {
                    for (int c = 0; c < BUFLEN_SO; c++)
                        buf[c] = (float)sound_buffer[c] / 32767.0;
                    sound_rec_int(sound_buffer);
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

bool sound_start_rec(sound_rec_t *rec, const char *filename)
{
    static const char zeros[] = { 0, 0, 0, 0, 0, 0 };

    FILE *fp = fopen(filename, "wb");
    if (fp) {
        unsigned bytes_samp = (rec->bits_samp + 7) / 8;
        unsigned block_align = bytes_samp * rec->channels;
        /* skip past the WAVE header. */
        fseek(fp, 44, SEEK_SET);
        /* Write an initial sample of zero */
        fwrite_unlocked(zeros, block_align, 1, fp);
        rec->fp = fp;
        rec->rec_started = false;
        return true;
    }
    else {
        log_error("unable to open %s for writing: %s", filename, strerror(errno));
        return false;
    }
}

static const unsigned char hdr_tmpl[] = {
    0x52, 0x49, 0x46, 0x46, // RIFF
    0x00, 0x00, 0x00, 0x00, // file size.
    0x57, 0x41, 0x56, 0x45, // "WAVE"
    0x66, 0x6D, 0x74, 0x20, // "fmt "
    0x10, 0x00, 0x00, 0x00  // format chunk size
};

static void put16le(unsigned value, unsigned char *addr)
{
    addr[0] = value;
    addr[1] = value >> 8;
}

static void put32le(unsigned value, unsigned char *addr)
{
    addr[0] = value;
    addr[1] = value >> 8;
    addr[2] = value >> 16;
    addr[3] = value >> 24;
}

void sound_stop_rec(sound_rec_t *rec)
{
    FILE *fp = rec->fp;
    long size = ftell(fp) - 8;
    unsigned samp_rate = rec->samp_rate;
    unsigned bits_samp = rec->bits_samp;
    unsigned bytes_samp = (bits_samp + 7) / 8;
    unsigned channels = rec->channels;
    unsigned byte_rate = samp_rate * bytes_samp;
    unsigned block_align = bytes_samp * channels;
    unsigned char hdr[44];
    memcpy(hdr, hdr_tmpl, sizeof(hdr_tmpl));
    put32le(size, hdr+4);
    put16le(rec->wav_type, hdr+20);
    put16le(channels, hdr+22);
    put32le(samp_rate, hdr+24);
    put32le(byte_rate, hdr+28);
    put16le(block_align, hdr+32);
    put16le(bits_samp, hdr+34);
    hdr[36] = 0x64; // data
    hdr[37] = 0x61;
    hdr[38] = 0x74;
    hdr[39] = 0x61;
    size -= 36;
    put32le(size, hdr+40);
    fseek(fp, 0, SEEK_SET);
    fwrite_unlocked(hdr, sizeof(hdr), 1, fp);
    fclose(fp);
    rec->fp = NULL;
    rec->rec_started = false;
}

sound_rec_t sound_rec = {
    NULL,    // fp
    false,   // rec_started
    "Record SN76489 to file",
    1,       // WAVE type
    1,       // channels
    FREQ_SO, // sample rate
    16       // bits/sample
};

void sound_close(void)
{
    if (sound_rec.fp)
        sound_stop_rec(&sound_rec);
    if (stream)
        al_destroy_audio_stream(stream);
    if (mixer)
        al_destroy_mixer(mixer);
    if (voice)
        al_destroy_voice(voice);
}
