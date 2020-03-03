// 1MHz bus Paula emulations for B-Em
// Copyright (C) 2020 Dominic Beesley
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


#include "paula.h"


FILE *paula_fp;

static bool rec_started;


#define RAM_SIZE				(512*1024)
#define NUM_CHANNELS			4
#define JIM_DEV					0xD0
#define JIM_PAGE				0xFEFC
#define REG_BASE				0x80


static uint8_t jimDev;
static uint16_t jimPage;		//fcfe,fcfd  - big endian


typedef struct
{

    //user facing registers
    INT8	data;
    UINT8	addr_bank;
    UINT16	addr;

    UINT8   period_h_latch;
    UINT16	period;
    UINT16	len;
    bool	act;
    bool	repeat;
    UINT8	vol;
    UINT16	repoff;
    UINT8	peak;

    //internal registers
    bool	act_prev;
    UINT16	samper_ctr;
    INT8	data_next;
    UINT16	sam_ctr;


} CHANNELREGS;

static UINT8	ChipRam[RAM_SIZE];
CHANNELREGS		ChannelRegs[NUM_CHANNELS];
UINT8			ChannelSel;
UINT8			Volume;

#define H1M_STREAM_RATE 31250

#define H1M_PAULA_CLK 3547672
#define H1M_PCLK_LIM  65536
#define H1M_PCLK_A    (int)((((long long)H1M_PCLK_LIM) * ((long long)H1M_PAULA_CLK)) / (long long)H1M_STREAM_RATE)

UINT32 paula_clock_acc = 0; //paula clock acumulator - when this overflows do a Paula period


void paula_reset(void)
{
    jimDev = 0;
    jimPage = 0;
    ChannelSel = 0;
    Volume = 0x3F;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ChannelRegs[i].data = 0;
        ChannelRegs[i].addr = 0;
        ChannelRegs[i].addr_bank = 0;

        ChannelRegs[i].act = false;
        ChannelRegs[i].act_prev = false;
        ChannelRegs[i].repeat = false;

        ChannelRegs[i].len = 0;
        ChannelRegs[i].period_h_latch = 0;
        ChannelRegs[i].period = 0;
        ChannelRegs[i].vol = 0;
        ChannelRegs[i].peak = 0;

        ChannelRegs[i].repoff = 0;

    }

}


void paula_loadstate(FILE *f) {
}


void paula_savestate(FILE *f) {
}

void paula_init()
{
    memset(ChipRam, 0, sizeof(ChipRam));

}

FILE *paula_rec_start(const char *filename)
{
    static const char zeros[] = { 0, 0, 0, 0, 0, 0 };

    FILE *fp = fopen(filename, "wb");
    if (fp) {
        fseek(fp, 44, SEEK_SET);
        fwrite(zeros, 6, 1, fp);
        paula_fp = fp;
        rec_started = false;
    }
    else
        log_error("unable to open %s for writing: %s", filename, strerror(errno));
    return fp;
}

static void fput32le(uint32_t v, FILE *fp)
{
    putc(v & 0xff, fp);
    putc((v >> 8) & 0xff, fp);
    putc((v >> 16) & 0xff, fp);
    putc((v >> 24) & 0xff, fp);
}


void paula_rec_stop(void)
{
    static const char wavfmt[] = {
        0x57, 0x41, 0x56, 0x45, // "WAVE"
        0x66, 0x6D, 0x74, 0x20, // "fmt "
        0x10, 0x00, 0x00, 0x00, // format chunk size
        0x01, 0x00,             // format 1=PCM
        0x01, 0x00,             // channels 2=mono
        0x12, 0x7A, 0x00, 0x00, // sample rate 31250.
        0x24, 0xF4, 0x00, 0x00, // byte rate 6250.
        0x02, 0x00,             // block align 2.
        0x10, 0x00,             // bits per sample 16.
        0x64, 0x61, 0x74, 0x61  // "DATA".
    };

    FILE *fp = paula_fp;
    long size = ftell(fp) - 8;
    fseek(fp, 0, SEEK_SET);
    fwrite("RIFF", 4, 1, fp);
    fput32le(size, fp);
    fwrite(wavfmt, sizeof wavfmt, 1, fp);
    size -= 36;
    fput32le(size, fp);        // data size.
    fclose(fp);
    paula_fp = NULL;
}

void paula_close(void)
{
    if (paula_fp)
        paula_rec_stop();
}


void paula_write(uint16_t addr, uint8_t val)
{
    if (addr == 0xfcff)
        jimDev = val;
    else {
        if (jimDev == DEVNO_PAULA)
        {
            if (addr == 0xfcfe)
                jimPage = (jimPage & 0xFF00) | val;
            else if (addr == 0xfcfd)
                jimPage = (jimPage & 0x00FF) | ((uint16_t)val << 8);
            else if ((addr & 0xff00) == 0xfd00) {
                //jim
                if ((jimPage == JIM_PAGE) && ((addr & 0x00f0) == REG_BASE))
                {
                    // sound registers
                    //note registers are all exposed big-endian style
                    switch (addr & 0x0F)
                    {
                    case 0:
                        ChannelRegs[ChannelSel].data = val;
                        break;
                    case 1:
                        ChannelRegs[ChannelSel].addr_bank = val;
                        break;
                    case 2:
                        ChannelRegs[ChannelSel].addr = (ChannelRegs[ChannelSel].addr & 0x00FF) | (val << 8);
                        break;
                    case 3:
                        ChannelRegs[ChannelSel].addr = (ChannelRegs[ChannelSel].addr & 0xFF00) | val;
                        break;
                    case 4:
                        ChannelRegs[ChannelSel].period_h_latch = val;
                        break;
                    case 5:
                        ChannelRegs[ChannelSel].period = (ChannelRegs[ChannelSel].period_h_latch << 8) | val;
                        break;
                    case 6:
                        ChannelRegs[ChannelSel].len = (ChannelRegs[ChannelSel].len & 0x00FF) | (val << 8);
                        break;
                    case 7:
                        ChannelRegs[ChannelSel].len = (ChannelRegs[ChannelSel].len & 0xFF00) | val;
                        break;
                    case 8:
                        ChannelRegs[ChannelSel].act = (val & 0x80) != 0;
                        ChannelRegs[ChannelSel].repeat = (val & 0x01) != 0;
                        ChannelRegs[ChannelSel].sam_ctr = 0;
                        break;
                    case 9:
                        ChannelRegs[ChannelSel].vol = val & 0xFC;
                        break;
                    case 10:
                        ChannelRegs[ChannelSel].repoff = (ChannelRegs[ChannelSel].repoff & 0x00FF) | (val << 8);
                        break;
                    case 11:
                        ChannelRegs[ChannelSel].repoff = (ChannelRegs[ChannelSel].repoff & 0xFF00) | val;
                        break;
                    case 12:
                        ChannelRegs[ChannelSel].peak = 0;
                        break;
                    case 14:
                        Volume = val & 0xFC;
                        break;
                    case 15:
                        ChannelSel = val % NUM_CHANNELS;
                        break;
                    }
                }
                else
                {
                    // everything else write chip ram!
                    int chipaddr = (((int)jimPage) << 8) + (((int)addr) & 0xFF);
                    ChipRam[chipaddr % RAM_SIZE] = val;
                }
            }

        }
    }
}

bool paula_read(uint16_t addr, uint8_t *val)
{
    if (jimDev == DEVNO_PAULA)
    {
        if (addr == 0xfcff) {
            *val = ~jimDev;
            return true;
        }
        else if (addr == 0xfcfe) {
            *val = (UINT8)jimPage;
            return true;
        }
        else if (addr == 0xfcfd) {
            *val = jimPage >> 8;
            return true;
        }
        else if ((addr & 0xff00) == 0xfd00) {
            //jim
            if ((jimPage == JIM_PAGE) && ((addr & 0x00f0) == REG_BASE))
            {
                //note registers are all exposed big-endian style
                switch (addr & 0x0F)
                {
                case 0:
                    *val = ChannelRegs[ChannelSel].data;
                    break;
                case 1:
                    *val = ChannelRegs[ChannelSel].addr_bank;
                    break;
                case 2:
                    *val = ChannelRegs[ChannelSel].addr >> 8;
                    break;
                case 3:
                    *val = (UINT8)ChannelRegs[ChannelSel].addr;
                    break;
                case 4:
                    *val = ChannelRegs[ChannelSel].period >> 8;
                    break;
                case 5:
                    *val = (UINT8)ChannelRegs[ChannelSel].period;
                    break;
                case 6:
                    *val = ChannelRegs[ChannelSel].len >> 8;
                    break;
                case 7:
                    *val = (UINT8)ChannelRegs[ChannelSel].len;
                    break;
                case 8:
                    *val = (ChannelRegs[ChannelSel].act ? 0x80 : 0x00) | (ChannelRegs[ChannelSel].repeat ? 0x01 : 0x00);
                    break;
                case 9:
                    *val = ChannelRegs[ChannelSel].vol;
                    break;
                case 10:
                    *val = ChannelRegs[ChannelSel].repoff >> 8;
                    break;
                case 11:
                    *val = (UINT8)ChannelRegs[ChannelSel].repoff;
                    break;
                case 12:
                    *val = ChannelRegs[ChannelSel].peak;
                    break;
                case 14:
                    *val = Volume; //as of 2019/11/23 real paula returns sel here - is a bug to be fixed
                    break;
                case 15:
                    *val = ChannelSel;
                    break;
                default:
                    *val = 255;
                    break;
                }
            }
            else
            {
                // everything else write chip ram!
                int chipaddr = (((int)jimPage) << 8) + (((int)addr) & 0xFF);
                *val = ChipRam[chipaddr % RAM_SIZE];
            }
            return true;
        }
        else
            return false;
    }
    else
        return false;
}

static void fetch_mem(CHANNELREGS *curchan) {


    //this is a very rough approximation of what really happens in terms of prioritisation
    //but should be close enough - normally data accesses that clash will be queued up
    //by intcon and could take many 8M cycles but here we just always deliver the data!
    int addr = (((int)curchan->addr_bank) << 16) + (int)curchan->addr + (int)curchan->sam_ctr;
    curchan->data_next = ChipRam[addr % RAM_SIZE];

    if (curchan->sam_ctr == curchan->len)
    {
        if (curchan->repeat)
        {
            curchan->sam_ctr = curchan->repoff;
        }
        else
        {
            curchan->act = false;
            curchan->sam_ctr = 0;
        }
    }
    else
    {
        curchan->sam_ctr++;
    }

}



static void paula_update_3_5MHz()
{

    //do this once every 3.5MhZ

    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        CHANNELREGS *curchan = &ChannelRegs[i];

        if (!curchan->act) {
            curchan->samper_ctr = 0;
        }
        else {
            if (!curchan->act_prev)
            {
                fetch_mem(curchan);
                curchan->samper_ctr = curchan->period;
            }
            else if (curchan->samper_ctr == 0)
            {
                curchan->data = curchan->data_next;
                fetch_mem(curchan);
                curchan->samper_ctr = curchan->period;
            }
            else
                curchan->samper_ctr--;
        }

        curchan->act_prev = curchan->act;
    }

}

static void fput_samples(FILE *fp, int16_t s)
{
    char bytes[2];
    bytes[0] = s;
    bytes[1] = s >> 8;
    fwrite(bytes, 2, 1, fp);
    rec_started = true;
}


static int16_t paula_get_sample()
{
    int16_t sample = 0;

    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        int snd_dat = (int)ChannelRegs[i].data * (int)(ChannelRegs[i].vol >> 2);
        sample += snd_dat;
        int snd_mag = ((snd_dat > 0) ? snd_dat : -snd_dat) >> 6;
        if (snd_mag > ChannelRegs[i].peak)
            ChannelRegs[i].peak = snd_mag;
    }

    return sample;
}

// use sound rate of 31250
void paula_fillbuf(int16_t *buffer, int len) {
    int i;
    int sample;
    int16_t *bufptr = buffer;
    for (sample = 0; sample < len; sample++) {
        paula_clock_acc += H1M_PCLK_A;
        while (paula_clock_acc > H1M_PCLK_LIM) {
            paula_update_3_5MHz();
            paula_clock_acc -= H1M_PCLK_LIM;
        }
        int16_t s = paula_get_sample();

        if (paula_fp)
            fput_samples(paula_fp, s);
        *bufptr++ += s;
    }
}

