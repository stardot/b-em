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

#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include "sound.h"
#include "savestate.h"

#define I_WAVEFORM(n) ((n)*128)
#define I_WFTOP (14*128)

#define I_CHAN(c) ((((c)&0x1e)>>1)+(((c)&0x01)<<7)+I_WFTOP)
#define I_FREQ(c) (I_CHAN(c))
#define I_WAVESEL(c) (I_CHAN(c)+0x50)
#define I_AMP(c) (I_CHAN(c)+0x60)
#define I_CTL(c) (I_CHAN(c)+0x70)
#define FREQ(c) ((RAM[I_FREQ(c)+0x20]<<16)|(RAM[I_FREQ(c)+0x10]<<8)|(RAM[I_FREQ(c)]&0x7e))
#define DISABLE(c) (!!(RAM[I_FREQ(c)]&1))
#define AMP(c) (RAM[I_AMP(c)])
#define WAVESEL(c) (RAM[I_WAVESEL(c)]>>4)
#define CTL(c) (RAM[I_CTL(c)])
#define MODULATE(c) (!!(CTL(c)&0x20))
#define INVERT(c) (!!(CTL(c)&0x10))
#define PAN(c) (CTL(c)&0xf)

#define ushort uint16_t
#define byte uint8_t

size_t buflen_m5 = BUFLEN_M5;

static ALLEGRO_VOICE *voice;
static ALLEGRO_MIXER *mixer;
static ALLEGRO_AUDIO_STREAM *stream;

static ushort antilogtable[128];
static byte RAM[2048];
static int phaseRAM[16];

static short sleft[16], sright[16];

static int channel,pc;
static int modulate;
static int disable;
static short sam;
static byte sign;
static byte c4d;

void music5000_reset(void)
{
	memset(RAM, 0, 2048);
	memset(phaseRAM, 0, 16 * sizeof(int));
	memset(sleft, 0, 16 * sizeof(short));
	memset(sright, 0, 16 * sizeof(short));
	pc = 0;
	channel = 0;
	modulate = 0;
	disable = 0;
	sam = 0;
}

void music5000_loadstate(FILE *f) {
    int ch;

    if ((ch = getc(f)) != EOF) {
        if (ch == 'M') {
            sound_music5000 = true;
            pc = savestate_load_var(f);
            channel = savestate_load_var(f);
            modulate = savestate_load_var(f);
            disable = savestate_load_var(f);
            sam = savestate_load_var(f);
            fread(RAM, sizeof RAM, 1, f);
            fread(phaseRAM, sizeof phaseRAM, 1, f);
            fread(sleft, sizeof sleft, 1, f);
            fread(sright, sizeof sright, 1, f);
        } else if (ch == 'm')
            sound_music5000 = false;
        else
            log_warn("music5000: invalid Music 5000 state from savestate file");
    }
}

void music5000_savestate(FILE *f) {
    if (sound_music5000) {
        putc('M', f);
        savestate_save_var(pc, f);
        savestate_save_var(channel, f);
        savestate_save_var(modulate, f);
        savestate_save_var(disable, f);
        savestate_save_var(sam, f);
        fwrite(RAM, sizeof RAM, 1, f);
        fwrite(phaseRAM, sizeof phaseRAM, 1, f);
        fwrite(sleft, sizeof sleft, 1, f);
        fwrite(sright, sizeof sright, 1, f);
    } else
        putc('m', f);
}

void music5000_init(ALLEGRO_EVENT_QUEUE *queue)
{
	int n;

    if ((voice = al_create_voice(FREQ_M5, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2))) {
        if ((mixer = al_create_mixer(FREQ_M5, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2))) {
            if (al_attach_mixer_to_voice(mixer, voice)) {
                if ((stream = al_create_audio_stream(4, BUFLEN_M5, FREQ_M5, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_2))) {
                    if (al_attach_audio_stream_to_mixer(stream, mixer)) {
                        al_register_event_source(queue, al_get_audio_stream_event_source(stream));
                        for (n = 0; n < 128; n++) {
                            //12-bit antilog as per AM6070 datasheet
                            int S = n & 15, C = n >> 4;
                            antilogtable[n] = (ushort)(2 * (pow(2.0, C)*(S + 16.5) - 16.5));
                        }
                        music5000_reset();
                    } else
                        log_error("sound: unable to attach stream to mixer for Music 5000");
                } else
                    log_error("sound: unable to create stream for Music 5000");
            } else
                log_error("sound: unable to attach mixer to voice for Music 5000");
        } else
            log_error("sound: unable to create mixer for Music 5000");
    } else
        log_error("sound: unable to create voice for Music 5000");

}

void music5000_write(uint16_t addr, uint8_t val)
{
	static uint8_t page = 0;
	if (addr == 0xfcff) {
		page = val;
	} else if (((page & 0xf0) == 0x30) && ((addr & 0xff00) == 0xfd00)) {
		addr = (page << 8) | (addr & 0xFF);
		if ((addr & 0x0f00) == 0x0e00) {
			//control RAM
			RAM[I_WFTOP + (addr & 0xff)] = val;
		} else {
			//waveform RAM
			int wavepage = (addr & 0x0e00) >> 9;
			ushort adr = I_WAVEFORM(wavepage * 2) + (addr & 0xff);
			//if (adr >= I_WFTOP) {
			//	__debugbreak();
			//}
			RAM[adr] = val;
		}
	}
}

void music5000_update_6MHz(void)
{
	int c = (channel << 1) + (modulate ? 1 : 0);
	switch (pc) {
	case 0:
		disable = DISABLE(c);
		break;
	case 1:	break;
	case 2:
		// In the real hardware the disable bit works by forcing the
		// phase accumulator to zero.
		if (disable) {
			phaseRAM[channel] = 0;
			c4d = 0;
		} else {
			unsigned int sum = phaseRAM[channel] + FREQ(c);
			phaseRAM[channel] = sum & 0xffffff;
			// c4d is used for "Synchronization" e.g. the "Wha" instrument
			c4d = sum >> 24;
		}
		break;
	case 3:	break;
	case 4:	break;
	case 5:
	{
		byte ramp = phaseRAM[channel] >> 17;
		byte ws = WAVESEL(c);
		ushort iwf = I_WAVEFORM(ws) + ramp;
		sam = RAM[iwf];
		break;
	}
	case 6:
	{
		// The amplitude operates in the log domain
		// - sam holds the wave table output which is 1 bit sign and 7 bit magnitude
		// - amp holds the amplitude which is 1 bit sign and 8 bit magnitude (0x00 being quite, 0x7f being loud)
		// The real hardware combites these in a single 8 bit adder, as we do here
		//
		// Consider a positive wav value (sign bit = 1)
		//		 wav: (0x80 -> 0xFF) + amp: (0x00 -> 0x7F) => (0x80 -> 0x7E)
		// values in the range 0x80...0xff are very small are clamped to zero
		//
		// Consider a negative wav vale (sign bit = 0)
		//		 wav: (0x00 -> 0x7F) + amp: (0x00 -> 0x7F) => (0x00 -> 0xFE)
		// values in the range 0x00...0x7f are very small are clamped to zero
		//
		// In both cases:
		// - zero clamping happens when the sign bit stays the same
		// - the 7-bit result is in bits 0..6
		//
		// Note:
		// - this only works if the amp < 0x80
		// - amp >= 0x80 causes clamping at the high points of the waveform
		// - this behaviour matches the FPGA implematation, and we think the original hardware
		byte amp = AMP(c);
		sign = sam & 0x80;
		sam += amp;
		if ((sign ^ sam) & 0x80) {
			// sign bits being different is the normal case
			sam &= 0x7f;
		} else {
			// sign bits being the same indicates underflow so clamp to zero
			sam = 0;
		}
		break;
	}
	case 7:
	{
		modulate = MODULATE(c) && (!!(sign) || !!(c4d));
		// in the real hardware, inversion does not affect modulation
		if (INVERT(c)) {
			sign ^= 0x80;
		}
		//sam is now an 8-bit log value
		if (sign) {
			// sign being 1 is positive
			sam = antilogtable[sam];
		}
		else {
			// sign being zero is negative
			sam = -antilogtable[sam];
		}
		//sam is now a 12-bit linear sample
		byte pan = 0;
		switch (PAN(c)) {
		case 8:	case 9:	case 10:	pan = 6;	break;
		case 11: pan = 5;	break;
		case 12: pan = 4;	break;
		case 13: pan = 3;	break;
		case 14: pan = 2;	break;
		case 15: pan = 1;	break;
		}
		// Apply panning. In the real hardware, a disabled channel is not actually
		// forced to zero, but this seems harmless so leave in for now.
		sleft[channel] = disable ? 0 : ((sam*pan) / 6);
		sright[channel] = disable ? 0 : ((sam*(6 - pan)) / 6);
		break;
	}
	}
	pc++;
	if (pc == 8) {
		pc = 0;
		channel ++;
		channel &= 15;
	}
}

void music5000_get_sample(int16_t *left, int16_t *right)
{
	int clip;
	static int divisor = 1;

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

	// the range of sleft/right is (-8191..8191) i.e. 14 bits
	// so summing 16 channels gives an 18 bit output
	int t;
	int sl = 0, sr = 0;
	for (t = 0; t < 16; t++) {
		sl += sleft[t];
		sr += sright[t];
	}
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

	*left = (int16_t) sl;
	*right = (int16_t) sr;
}

// Music 5000 runs at a sample rate of 6MHz / 128 = 46875
void music5000_fillbuf(int16_t *buffer, int len) {
	int i;
	int sample;
	for (sample = 0; sample < len; sample++) {
		for (i = 0; i < 128; i++) {
			music5000_update_6MHz();
		}
		music5000_get_sample(buffer, buffer + 1);
		buffer += 2;
	}
}

void music5000_streamfrag(void)
{
    int16_t *buf;

    // This function is called when a audio stream fragment available
    // event is received in the main event handling loop but the event
    // does not specify for which stream a new fragment has become
    // available so we need to check if it is this one!

    if (sound_music5000) {
        if ((buf = al_get_audio_stream_fragment(stream))) {
            music5000_fillbuf(buf, BUFLEN_M5);
            al_set_audio_stream_fragment(stream, buf);
            al_set_audio_stream_playing(stream, true);
        }
    }
}
