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
static int phaseRAM[32];

static short sleft[16], sright[16];

static int channel,pc;
static int modulate;
static int disable;
static short sam;



void music5000_reset()
{
	memset(RAM, 0, 2048);
	memset(phaseRAM, 0, 32 * sizeof(int));
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
	int c = channel + (modulate ? 1 : 0);
	switch (pc) {
	case 0:
		disable = DISABLE(c);
		break;
	case 1:	break;
	case 2:
		phaseRAM[c] = (phaseRAM[c] + FREQ(c)) & 0xffffff;
		break;
	case 3:	break;
	case 4:	break;
	case 5:
	{
		byte ramp = phaseRAM[c] >> 17;
		byte ws = WAVESEL(c);
		ushort iwf = I_WAVEFORM(ws) + ramp;
		sam = RAM[iwf];
		break;
	}
	case 6:
	{
		byte amp = AMP(c);
		sam = (sam & 0x80) | (short)(((sam & 0x7f)*amp) / 0x80);
		break;
	}
	case 7:
	{
		if (INVERT(c)) {
			sam ^= 0x80;
		}
		modulate = MODULATE(c) && !!(sam & 0x80);
		//sam is now an 8-bit log value
		if (sam & 0x80) {
			sam = -antilogtable[sam & 0x7f];
		}
		else {
			sam = antilogtable[sam];
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
		sleft[c / 2] = disable ? 0 : ((sam*pan) / 6);
		sright[c / 2] = disable ? 0 : ((sam*(6 - pan)) / 6);
		break;
	}
	}
	pc++;
	if (pc == 8) {
		pc = 0;
		channel += 2;
		if (channel == 32) {
			channel = 0;
		}
	}
}

void music5000_get_sample(int *left, int *right)
{
   int t;
	int sl = 0, sr = 0;
	for (t = 0; t < 16; t++) {
		sl += sleft[t] / 2;
		sr += sright[t] / 2;
	}
	*left = sl;
	*right = sr;
}

// Music 5000 runs at a sample rate of 6MHz / 128 = 46875
// openal is configured to run at 31250
// 46875 / 31250 = 3 / 2
// So 3 music 5000 samples need to be interpolated down to 2 openal samples
// Or we need to tweak the Music 5000 frequencies somehow

// This code is called by sound_poll() which asks for 2 samples (i.e. len = 2)
void music5000_fillbuf(int16_t *buffer, int len) {
   int i;
   int sample;
   int lefts[3], rights[3];

   if (len != 2) {
      printf("music5000_fillbuf must be called with len=2!\r\n");
      return;
   }

   for (sample = 0; sample < 3; sample++) {
      for (i = 0; i < 128; i++) {
         music5000_update_6MHz();
      }
      music5000_get_sample(&lefts[sample], &rights[sample]);
   }
   // Crude 3:2 interpolation, will probably sound awful!
   // 000011112222333344445555
   //   AAAA  BBBB  CCCC  DDDD

   sample = (lefts[0] + lefts[1] + rights[0] + rights[1]) / 4;
   *buffer++ += (int16_t) sample;

   sample = (lefts[2] + rights[2]) / 2 ;
   *buffer++ += (int16_t) sample;

}
