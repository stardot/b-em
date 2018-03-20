/*B-em v2.2 by Tom Walker
  Internal SN sound chip emulation*/

#include "b-em.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "via.h"
#include "uservia.h"

uint8_t sn_freqhi[4], sn_freqlo[4];
uint8_t sn_vol[4];
uint8_t sn_noise;
static uint16_t sn_shift;
static int lasttone;
static int sn_count[4], sn_stat[4];
uint32_t sn_latch[4];

static int sn_rect_pos = 0,sn_rect_dir = 0;

int curwave = 0;

static float volslog[16] =
{
	0.00000f, 0.59715f, 0.75180f, 0.94650f,
        1.19145f, 1.50000f, 1.88835f, 2.37735f,
        2.99295f, 3.76785f, 4.74345f, 5.97165f,
        7.51785f, 9.46440f, 11.9194f, 15.0000f
};

/*static int volume_table[16] = {
  32767, 26028, 20675, 16422, 13045, 10362,  8231,  6568,
   5193,  4125,  3277,  2603,  2067,  1642,  1304,     0
};*/

static int16_t snwaves[5][32] =
{
        {
	         127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
                -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127

        },
        {
	        -120, -112, -104, -96, -88, -80, -72, -64, -56, -48, -40, -32, -24, -16,  -8,   0,
	           8,   16,   24,  32,  40,  48,  56,  64,  72,  80,  88,  96, 104, 112, 120, 127
        },
        {
                 16,  32,  48,  64,  80,  96,  112,  128,  112,  96,  80,  64,  48,  32,  16, 0,
                -16, -32, -48, -64, -80, -96, -112, -128, -112, -96, -80, -64, -48, -32, -16, 0
        },
        {
                  0,  24,  48,  70,  89,  105,  117,  124,  126,  124,  117,  105,  89,  70,  48,  24,
                  0, -24, -48, -70, -89, -105, -117, -124, -126, -124, -117, -105, -90, -70, -48, -25
        }
};

static void sn_updaterectwave(int d)
{
        int c;

        for (c = 0; c < d; c++) snwaves[4][c] =  127;
        for ( ;c < 32; c++)     snwaves[4][c] = -127;
}




void sn_fillbuf(int16_t *buffer, int len)
{
        int c, d;
        static int sidcount = 0;

        for (d = 0; d < len; d++)
        {
                for (c = 0; c < 3; c++)
                {
                        c++;
                        if (sn_latch[c] > 256) buffer[d] += (int16_t) (snwaves[curwave][sn_stat[c]] * volslog[sn_vol[c]]);
                        else                   buffer[d] += (int16_t) (volslog[sn_vol[c]] * 127);
                        
                        sn_count[c] -= 8192;
                        while ((int)sn_count[c] < 0  && sn_latch[c])
                        {
                                sn_count[c] += sn_latch[c];
                                sn_stat[c]++;
                                sn_stat[c] &= 31;
                        }
                        c--;
                }
                if (!(sn_noise & 4))
                {
                        if (curwave == 4) buffer[d] += (snwaves[4][sn_stat[0] & 31] * volslog[sn_vol[0]]);
                        else              buffer[d] += (((sn_shift & 1) ^ 1) * 127 * volslog[sn_vol[0]] * 2);
                }
                else    buffer[d] += (((sn_shift & 1) ^ 1) * 127 * volslog[sn_vol[0]] * 2);

                sn_count[0] -= 512;
                while ((int)sn_count[0] < 0 && sn_latch[0])
                {
                        sn_count[0] += (sn_latch[0] * 2);
                        if (!(sn_noise & 4))
                        {
                                if (sn_shift & 1) sn_shift |= 0x8000;
                                sn_shift >>= 1;
                        }
                        else
                        {
                                if ((sn_shift & 1) ^ ((sn_shift >> 1) & 1)) sn_shift |= 0x8000;
                                sn_shift >>= 1;
                        }
                        sn_stat[0]++;
                }
                if (!(sn_noise & 4))
                {
                        while (sn_stat[0] >= 30) sn_stat[0] -= 30;
                }
                else
                   sn_stat[0] &= 32767;
//                buffer[d] += (lpt_dac * 32);
                
                sidcount++;
                if (sidcount == 624)
                {
                        sidcount = 0;
                        if (!sn_rect_dir)
                        {
                                sn_rect_pos++;
                                if (sn_rect_pos == 30) sn_rect_dir = 1;
                        }
                        else
                        {
                                sn_rect_pos--;
                                if (sn_rect_pos == 1) sn_rect_dir = 0;
                        }
                        sn_updaterectwave(sn_rect_pos);
                }
        }
}

void sn_init()
{
        int c;
//        for (c = 0; c < 16; c++)
//            volslog[c] = (float)volume_table[15 - c] / 2048.0;

        for (c = 0; c < 32; c++)
            snwaves[3][c] -= 128;


        sn_latch[0] = sn_latch[1] = sn_latch[2] = sn_latch[3] = 0x3FF << 6;
        sn_vol[0] = 0;
        sn_vol[1] = sn_vol[2] = sn_vol[3] = 8;
        srand(time(NULL));
        sn_count[0] = 0;
        sn_count[1] = (rand()&0x3FF)<<6;
        sn_count[2] = (rand()&0x3FF)<<6;
        sn_count[3] = (rand()&0x3FF)<<6;
        sn_noise = 3;
        sn_shift = 0x4000;
}

static uint8_t firstdat;
void sn_write(uint8_t data)
{
        int freq;

        if (data & 0x80)
        {
                firstdat = data;
                switch (data & 0x70)
                {
                    case 0:
                        sn_freqlo[3] = data & 0xF;
                        sn_latch[3] = (sn_freqlo[3] | (sn_freqhi[3] << 4)) << 6;
                        lasttone = 3;
                        break;
                    case 0x10:
                        data &= 0xF;
                        sn_vol[3] = 0xF - data;
                        break;
                    case 0x20:
                        sn_freqlo[2] = data & 0xF;
                        sn_latch[2] = (sn_freqlo[2] | (sn_freqhi[2] << 4)) << 6;
                        lasttone = 2;
                        break;
                    case 0x30:
                        data &= 0xF;
                        sn_vol[2] = 0xF - data;
                        break;
                    case 0x40:
                        sn_freqlo[1] = data & 0xF;
                        sn_latch[1] = (sn_freqlo[1] | (sn_freqhi[1] << 4)) << 6;
                        lasttone = 1;
                        break;
                    case 0x50:
                        data &= 0xF;
                        sn_vol[1] = 0xF - data;
                        break;
                    case 0x60:
                        sn_shift = 0x4000;
                        if ((data & 3) != (sn_noise & 3)) sn_count[0] = 0;
                        sn_noise = data & 0xF;
                        if ((data & 3) == 3) sn_latch[0] = sn_latch[1];
                        else                 sn_latch[0] = 0x400 << (data & 3);
                        break;
                    case 0x70:
                        data &= 0xF;
                        sn_vol[0] = 0xF - data;
                        break;
                }
        }
        else
        {
                if ((firstdat & 0x70) == 0x60)
                {
                        sn_shift = 0x4000;
                        if ((data & 3) != (sn_noise & 3)) sn_count[0] = 0;
                        sn_noise = data & 0xF;
                        if ((data & 3) == 3) sn_latch[0] = sn_latch[1];
                        else                 sn_latch[0] = 0x400 << (data & 3);
                        return;
                }
                sn_freqhi[lasttone] = data & 0x3F;
                freq=sn_freqlo[lasttone] | (sn_freqhi[lasttone] << 4);
                if ((sn_noise & 3) == 3 && lasttone == 1)
                   sn_latch[0] = freq << 6;
                sn_latch[lasttone] = freq << 6;
                sn_count[lasttone] = 0;
        }
}



void sn_savestate(FILE *f)
{
        fwrite(sn_latch, 16, 1, f);
        fwrite(sn_count, 16, 1, f);
        fwrite(sn_stat,  16, 1, f);
        fwrite(sn_vol,   4,  1, f);
        putc(sn_noise, f);
        putc(sn_shift, f); putc(sn_shift >> 8, f);
}

void sn_loadstate(FILE *f)
{
        fread(sn_latch, 16, 1, f);
        fread(sn_count, 16, 1, f);
        fread(sn_stat,  16, 1, f);
        fread(sn_vol,   4,  1, f);
        sn_noise = getc(f);
        sn_shift = getc(f); sn_shift |= getc(f) << 8;
}
