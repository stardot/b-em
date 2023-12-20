// Music 5000 emulation for B-Em
// Copyright (C) 2016 Darren Izzard
//
// based on the Music 5000 code from Beech
//
// Beech - BBC Micro emulator
// Copyright (C) 2015 Darren Izzard
//
// This program is free software; you can redistribute it and / or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301 USA.

// The code has been simplified to reduce the processor overheads of emulation

#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "b-em.h"
#include "main.h"
#include <allegro5/allegro_audio.h>
#include "sound.h"
#include "savestate.h"

#define I_WAVEFORM(n) ((n)*128)
#define I_WFTOP (14*128)

#define I_FREQlo(c)  (*((c)+0x00))
#define I_FREQmid(c) (*((c)+0x10))
#define I_FREQhi(c)  (*((c)+0x20))
#define I_WAVESEL(c) (*((c)+0x50))
#define I_AMP(c)     (*((c)+0x60))
#define I_CTL(c)     (*((c)+0x70))
#define FREQ(c)      (I_FREQlo(c)|(I_FREQmid(c)<<8)|(I_FREQhi(c)<<16))
#define DISABLE(c)   (!!(I_FREQlo(c)&1))
#define AMP(c)       (I_AMP(c))
#define WAVESEL(c)   (I_WAVESEL(c)>>4)
#define CTL(c)       (I_CTL(c))
#define MODULATE(c)  (!!(CTL(c)&0x20))
#define INVERT(c)    (!!(CTL(c)&0x10))
#define PAN(c)       (CTL(c)&0xf)

#define ushort uint16_t
#define byte uint8_t

struct synth {
    int sleft,sright;
    uint32_t phaseRAM[16];
    uint8_t amplitude[16];
    byte ram[0x800];
};

static struct synth m5000, m3000;

static const uint8_t PanArray[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 5, 4, 3, 2, 1 };

size_t buflen_m5 = BUFLEN_M5;
FILE *music5000_fp;

static ALLEGRO_VOICE *music5000_voice;
static ALLEGRO_MIXER *music5000_mixer;
static ALLEGRO_AUDIO_STREAM *music5000_stream;
static bool rec_started;

static ushort antilogtable[128];

static uint16_t *music5000_buf;
static int music5000_bufpos = 0;
static int music5000_time = 0;
static unsigned music5000_freq;

static void synth_reset(struct synth *s)
{
   // Real hardware clears 0x3E00 for 128bytes and random
   // Waveform bytes depending on phaseRAM
   // we only need to clear the disable bytes

    memset(&s->ram[I_WFTOP], 1, 16);
}

void music5000_reset(void)
{
    synth_reset(&m5000);
    synth_reset(&m3000);
}

static void synth_loadstate(struct synth *s, FILE *f)
{
    fread_unlocked(&s->sleft, sizeof s->sleft, 1, f);
    fread_unlocked(&s->sright, sizeof s->sright, 1, f);
    fread_unlocked(s->phaseRAM, sizeof s->phaseRAM, 1, f);
    fread_unlocked(s->ram, sizeof s->ram, 1, f);
}

static void synth_savestate(struct synth *s, FILE *f)
{
    fwrite_unlocked(&s->sleft, sizeof s->sleft, 1, f);
    fwrite_unlocked(&s->sright, sizeof s->sright, 1, f);
    fwrite_unlocked(s->phaseRAM, sizeof s->phaseRAM, 1, f);
    fwrite_unlocked(s->ram, sizeof s->ram, 1, f);
}

void music5000_savestate(FILE *f) {
    if (sound_music5000) {
        putc_unlocked('M', f);
        savestate_save_var(9, f);
        synth_savestate(&m5000, f);
        synth_savestate(&m3000, f);
    } else
        putc_unlocked('m', f);
}

void music5000_init(int speed)
{
    if (sound_music5000) {
        unsigned new_freq = FREQ_M5;
        if (speed < num_emu_speeds)
            new_freq *= emu_speeds[speed].multiplier;
        if (new_freq != music5000_freq) {
            ALLEGRO_VOICE *new_voice = al_create_voice(new_freq, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2);
            if (new_voice) {
                ALLEGRO_MIXER *new_mixer = al_create_mixer(new_freq, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2);
                if (new_mixer) {
                    if (al_attach_mixer_to_voice(new_mixer, new_voice)) {
                        ALLEGRO_AUDIO_STREAM *new_stream = al_create_audio_stream(8, buflen_m5, new_freq, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2);
                        if (new_stream) {
                            if (al_attach_audio_stream_to_mixer(new_stream, new_mixer)) {
                                void *new_frag = al_get_audio_stream_fragment(new_stream);
                                if (new_frag) {
                                    if (music5000_freq == 0) { // Not previously running.
                                        for (int n = 0; n < 128; n++) {
                                            //12-bit antilog as per AM6070 datasheet
                                            int S = n & 15, C = n >> 4;
                                            antilogtable[n] = (ushort)(2 * (pow(2.0, C)*(S + 16.5) - 16.5));
                                        }
                                        music5000_reset();
                                    }
                                    if (music5000_stream)
                                        al_destroy_audio_stream(music5000_stream);
                                    music5000_stream = new_stream;
                                    if (music5000_mixer)
                                        al_destroy_mixer(music5000_mixer);
                                    music5000_mixer = new_mixer;
                                    if (music5000_voice)
                                        al_destroy_voice(music5000_voice);
                                    music5000_voice = new_voice;
                                    music5000_freq = new_freq;
                                    return;
                                }
                                else
                                    log_error("sound: unable to get buffer fragment for Music 5000");
                            }
                            else
                                log_error("sound: unable to attach stream to mixer for Music 5000");
                            al_destroy_audio_stream(new_stream);
                        }
                        else
                            log_error("sound: unable to create stream for Music 5000 at %uHz", new_freq);
                    }
                    else
                        log_error("sound: unable to attach mixer to voice for Music 5000");
                    al_destroy_mixer(new_mixer);
                }
                else
                    log_error("sound: unable to create mixer for Music 5000 at %uHz", new_freq);
                al_destroy_voice(new_voice);
            }
            else
                log_error("sound: unable to create voice for Music 5000 at %uHz", new_freq);
        }
    }
}

FILE *music5000_rec_start(const char *filename)
{
    static const char zeros[] = { 0, 0, 0, 0, 0, 0 };

    FILE *fp = fopen(filename, "wb");
    if (fp) {
        fseek(fp, 44, SEEK_SET);
        fwrite_unlocked(zeros, 6, 1, fp);
        music5000_fp = fp;
        rec_started = false;
    }
    else
        log_error("unable to open %s for writing: %s", filename, strerror(errno));
    return fp;
}

static void fput32le(uint32_t v, FILE *fp)
{
    putc_unlocked(v & 0xff, fp);
    putc_unlocked((v >> 8) & 0xff, fp);
    putc_unlocked((v >> 16) & 0xff, fp);
    putc_unlocked((v >> 24) & 0xff, fp);
}

void music5000_rec_stop(void)
{
    static const char wavfmt[] = {
        0x57, 0x41, 0x56, 0x45, // "WAVE"
        0x66, 0x6D, 0x74, 0x20, // "fmt "
        0x10, 0x00, 0x00, 0x00, // format chunk size
        0x01, 0x00,             // format 1=PCM
        0x02, 0x00,             // channels 2=stereo
        0x1B, 0xB7, 0x00, 0x00, // sample rate.
        0xA2, 0x4A, 0x04, 0x00, // byte rate.
        0x06, 0x00,             // block align.
        0x18, 0x00,             // bits per sample.
        0x64, 0x61, 0x74, 0x61  // "DATA".
    };

    FILE *fp = music5000_fp;
    long size = ftell(fp) - 8;
    fseek(fp, 0, SEEK_SET);
    fwrite_unlocked("RIFF", 4, 1, fp);
    fput32le(size, fp);
    fwrite_unlocked(wavfmt, sizeof wavfmt, 1, fp);
    size -= 36;
    fput32le(size, fp);        // data size.
    fclose(fp);
    music5000_fp = NULL;
}

void music5000_close(void)
{
    if (music5000_fp)
        music5000_rec_stop();
    if (music5000_stream) {
        al_destroy_audio_stream(music5000_stream);
        music5000_stream = NULL;
    }
    if (music5000_mixer) {
        al_destroy_mixer(music5000_mixer);
        music5000_mixer = NULL;
    }
    if (music5000_voice) {
        al_destroy_voice(music5000_voice);
        music5000_voice = NULL;
    }
    music5000_freq = 0;
}

void music5000_loadstate(FILE *f) {
    int ch, pc;

    if ((ch = getc_unlocked(f)) != EOF) {
        if (ch == 'M') {
            if (!sound_music5000) {
                sound_music5000 = true;
                music5000_init(emuspeed);
            }
            pc = savestate_load_var(f);
            if (pc == 9) {
                synth_loadstate(&m5000, f);
                synth_loadstate(&m3000, f);
            }
            else
                log_warn("music5000: old, unsupported Music 5000 state");
        } else if (ch == 'm') {
            if (sound_music5000) {
                music5000_close();
                sound_music5000 = false;
            }
        }
        else
            log_warn("music5000: invalid Music 5000 state from savestate file");
    }
}

static uint8_t page = 0;

static void ram_write(struct synth *s, uint16_t addr, uint8_t val)
{
    if ((addr & 0xff00) == 0xfd00) {
        addr = (page << 8) | (addr & 0xFF);
        if ((addr & 0x0f00) == 0x0e00) {
            //control RAM
            s->ram[I_WFTOP + (addr & 0xff)] = val;
        } else {
            //waveform RAM
            int wavepage = (addr & 0x0e00) >> 9;
            ushort adr = I_WAVEFORM(wavepage * 2) + (addr & 0xff);
            //if (adr >= I_WFTOP) {
            //  __debugbreak();
            //}
            s->ram[adr] = val;
        }
    }
}

void music5000_write(uint16_t addr, uint8_t val)
{
    if (addr == 0xfcff)
        page = val;
    else {
        uint8_t msn = page & 0xf0;
        if (msn == 0x30)
            ram_write(&m5000, addr, val);
        else if (msn == 0x50)
            ram_write(&m3000, addr, val);
    }
}

static void update_channels(struct synth *s)
{
    int sleft = 0;
    int sright = 0;
    static uint8_t modulate = 0;

    for (int i = 0; i < 16; i++) {
        uint8_t * c = s->ram + I_WFTOP + modulate + i;
        {
            int c4d, sign, sample;
            unsigned int sum = (DISABLE(c)?0:s->phaseRAM[i]) + FREQ(c);
            s->phaseRAM[i] = sum & 0xffffff;
            // c4d is used for "Synchronization" e.g. the "Wha" instrument
            c4d = sum & (1<<24);
            sample = s->ram[I_WAVEFORM(WAVESEL(c))|(s->phaseRAM[i] >> 17)];
            // only if there is a carry ( waveform crossing do we update the amplitude)
            if (c4d)
                s->amplitude[i] = AMP(c);

            // The amplitude operates in the log domain
            // - sam holds the wave table output which is 1 bit sign and 7 bit magnitude
            // - amp holds the amplitude which is 1 bit sign and 8 bit magnitude (0x00 being quite, 0x7f being loud)
            // The real hardware combines these in a single 8 bit adder, as we do here
            //
            // Consider a positive wav value (sign bit = 1)
            //       wav: (0x80 -> 0xFF) + amp: (0x00 -> 0x7F) => (0x80 -> 0x7E)
            // values in the range 0x80...0xff are very small are clamped to zero
            //
            // Consider a negative wav vale (sign bit = 0)
            //       wav: (0x00 -> 0x7F) + amp: (0x00 -> 0x7F) => (0x00 -> 0xFE)
            // values in the range 0x00...0x7f are very small are clamped to zero
            //
            // In both cases:
            // - zero clamping happens when the sign bit stays the same
            // - the 7-bit result is in bits 0..6
            //
            // Note:
            // - this only works if the amp < 0x80
            // - amp >= 0x80 causes clamping at the high points of the waveform
            // - this behavior matches the FPGA implementation, and we think the original hardware

            sign = sample & 0x80;
            sample += s->amplitude[i];
            modulate = (( MODULATE(c) && (!!(sign) || !!(c4d)))? 128:0);
            if ((sign ^ sample) & 0x80) {
                // sign bits being different is the normal case
                sample &= 0x7f;
            }
            else {
                // sign bits being the same indicates underflow so clamp to zero
                sample = 0;
            }

            // in the real hardware, inversion does not affect modulation
            if (INVERT(c)) {
                sign ^= 0x80;
            }
            //sam is now an 7-bit log value
            sample =  antilogtable[sample];
            if ((sign)) {
                // sign being zero is negative
                sample =-sample;
            }
            //sam is now a 14-bit linear sample
            uint8_t pan = PanArray[PAN(c)];

            // Apply panning. Divide by 6 taken out of the loop as a common subexpression
            sleft  += ((sample*pan));
            sright += ((sample*(6 - pan)));
        }
    }
    s->sleft  = sleft / 6;
    s->sright = sright / 6;
}

static void fput_samples(FILE *fp, int sl, int sr)
{
    if (fp && (rec_started || sl || sr)) {
        char bytes[6];
        bytes[0] = sl << 6;
        bytes[1] = sl >> 2;
        bytes[2] = sl >> 10;
        bytes[3] = sr << 6;
        bytes[4] = sr >> 2;
        bytes[5] = sr >> 10;
        fwrite_unlocked(bytes, 6, 1, fp);
        rec_started = true;
    }
}

typedef struct _m5000_fcoeff {
    double biquada[3];
    double biquadb[3];
    double gain;
} m5000_fcoeff;

#define NBQ 2

static const m5000_fcoeff m500_filters[2] = {
    { // original.
        {0.6545294918791053,-1.503352371060256,-0.640959826975052},
        {1,2,1},
        147.38757472209932
    },
    { // 16Khz.
        {0.40854522701892754,0.7646729121849647,0.29507476655875625},
        {1,2,1},
        2.8424433902604673
    }
};

static double xyv_l[]={0,0,0,0,0,0,0,0,0};
static double xyv_r[]={0,0,0,0,0,0,0,0,0};
int music5000_fno;

static double applyfilter(const m5000_fcoeff *fcp, double *xyv, double v)
{
	int i,b,xp=0,yp=3,bqp=0;
	double out=v/fcp->gain;
	for (i=8; i>0; i--) {xyv[i]=xyv[i-1];}
	for (b=0; b<NBQ; b++)
	{
		int len=(b==NBQ-1)?1:2;
		xyv[xp]=out;
		for(i=0; i<len; i++) { out+=xyv[xp+len-i]*fcp->biquadb[bqp+i]-xyv[yp+len-i]*fcp->biquada[bqp+i]; }
		bqp+=len;
		xyv[yp]=out;
		xp=yp; yp+=len+1;
	}
	return out;
}

static void music5000_get_sample(const m5000_fcoeff *fcp)
{
    int clip;
    static int divisor = 1;

    update_channels(&m5000);
    update_channels(&m3000);

#ifdef LOG_LEVELS
    static int count = 0;
    static int min_l = INT_MAX;
    static int max_l = INT_MIN;
    static int min_r = INT_MAX;
    static int max_r = INT_MIN;
    static double sum_squares_l = 0.0;
    static double sum_squares_r = 0.0;
    static int window = FREQ_M5 * 30;
#endif

    int sl = m5000.sleft  + m3000.sleft;
    int sr = m5000.sright + m3000.sright;
    if (fcp) {
        sl = applyfilter(fcp, xyv_l, (double)sl);
        sr = applyfilter(fcp, xyv_r, (double)sr);
    }
    
    fput_samples(music5000_fp, sl, sr);

#ifdef LOG_LEVELS
    if (sl < min_l) {
        min_l = sl;
    }
    if (sl > max_l) {
        max_l = sl;
    }
    if (sr < min_r) {
        min_r = sr;
    }
    if (sr > max_r) {
        max_r = sr;
    }
    sum_squares_l += sl * sl;
    sum_squares_r += sr * sr;
    count++;
    if (count == window) {
        int rms_l = (int) (sqrt(sum_squares_l / window));
        int rms_r = (int) (sqrt(sum_squares_r / window));
        printf("Music 5000: L:%6d..%5d (rms %5d) R:%6d..%5d (rms %5d)\n", min_l, max_l, rms_l, min_r, max_r, rms_r);
        min_l = INT_MAX;
        max_l = INT_MIN;
        min_r = INT_MAX;
        max_r = INT_MIN;
        sum_squares_l = 0;
        sum_squares_r = 0;
        count = 0;
    }
#endif

    // Worst case we should divide by 4 to get 18 bits down to 16 bits.
    // But this does loose dynamic range.
    //
    // Even loud tracks like In Concert by Pilgrim Beat rarely use
    // the full 18 bits:
    //
    //   L:-25086..26572 (rms  3626) R:-23347..21677 (rms  3529)
    //   L:-25795..31677 (rms  3854) R:-22592..21373 (rms  3667)
    //   L:-20894..20989 (rms  1788) R:-22221..17949 (rms  1367)
    //
    // So lets try a crude adaptive clipping system, and see what feedback we get!
    sl /= divisor;
    sr /= divisor;
    clip = 0;
    if (sl < SHRT_MIN) {
        sl = SHRT_MIN;
        clip = 1;
    }
    if (sl > SHRT_MAX) {
        sl = SHRT_MAX;
        clip = 1;
    }
    if (sr < SHRT_MIN) {
        sr = SHRT_MIN;
        clip = 1;
    }
    if (sr > SHRT_MAX) {
        sr = SHRT_MAX;
        clip = 1;
    }
    if (clip) {
        divisor *= 2;
        log_warn("Music 5000 clipped, reducing gain by 3dB (divisor now %d)", divisor);
    }

    music5000_buf[music5000_bufpos++] = sl;
    music5000_buf[music5000_bufpos++] = sr;
}

void music5000_poll(int cycles)
{
    if (sound_music5000) {
        music5000_time -= cycles;
        if (music5000_time < 0) {
            if (!music5000_buf) {
                music5000_buf = al_get_audio_stream_fragment(music5000_stream);
                log_debug("music5000: late buffer allocation %s", music5000_buf ? "worked" : "failed");
            }
            if (music5000_buf) {
                const m5000_fcoeff *fcp = music5000_fno < 0 ? NULL : &m500_filters[music5000_fno];
                music5000_get_sample(fcp);
                music5000_get_sample(fcp);
                music5000_get_sample(fcp);
                if (music5000_bufpos >= (buflen_m5*2)) {
                    al_set_audio_stream_fragment(music5000_stream, music5000_buf);
                    al_set_audio_stream_playing(music5000_stream, true);
                    music5000_buf = al_get_audio_stream_fragment(music5000_stream);
                    music5000_bufpos = 0;
                }
            }
            music5000_time += 128;
        }
    }
}

bool music5000_ok(void)
{
    if (sound_music5000) {
        unsigned frags = al_get_available_audio_stream_fragments(music5000_stream);
        if (frags < 2)
            return false;
        if (frags >= 8)
            log_debug("music5000: underrun");
    }
    return true;
}
