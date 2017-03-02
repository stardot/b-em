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

static ushort antilogtable[128];
static byte RAM[2048];
static int phaseRAM[16];

static short sleft[16], sright[16];

static int channel,pc;
static int modulate;
static int disable;
static short sam;
static byte sign;



void music5000_reset()
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

void music5000_init()
{
   int n;
	for (n = 0; n < 128; n++) {
		//12-bit antilog as per AM6070 datasheet
		int S = n & 15, C = n >> 4;
		antilogtable[n] = (ushort)(2 * (pow(2.0, C)*(S + 16.5) - 16.5));
	}

	music5000_reset();
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

void music5000_update_6MHz()
{
	int c = (channel << 1) + (modulate ? 1 : 0);
	switch (pc) {
	case 0:
		disable = DISABLE(c);
		break;
	case 1:	break;
	case 2:
		phaseRAM[channel] = (phaseRAM[channel] + FREQ(c)) & 0xffffff;
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
		if (INVERT(c)) {
			sign ^= 0x80;
		}
		modulate = MODULATE(c) && !!(sign);
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
		//apply panning
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
	// sleft/right is (-8191..8191) i.e. 13 bits
	// summing 16 gives a 17 bit output
	int t;
	int sl = 0, sr = 0;
	for (t = 0; t < 16; t++) {
		sl += sleft[t];
		sr += sright[t];
	}
	// divide by 2 to get a 16 bit output
	*left = (int16_t) (sl / 2);
	*right = (int16_t) (sr / 2);
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

