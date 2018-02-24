/*B-em v2.2 by Tom Walker
  Tape noise (not very good)*/

#include <stdio.h>
#include <math.h>
#include "b-em.h"
#include "ddnoise.h"
#include "tapenoise.h"
#include "sound.h"
#include "soundopenal.h"

static int tpnoisep = 0;
static int tmcount = 0;
static int16_t tapenoise[BUFLEN_DD];

static float swavepos = 0;

static int sinewave[32];

#define PI 3.142

static ALLEGRO_SAMPLE *tsamples[2];

void tapenoise_init()
{
        int c;

        tsamples[0] = find_load_wav("ddnoise", "motoron");
        tsamples[1] = find_load_wav("ddnoise", "motoroff");
        for (c = 0; c < 32; c++)
        {
                sinewave[c] = (int)(sin((float)c * ((2.0 * PI) / 32.0)) * 128.0);
        }
}

void tapenoise_close()
{
        al_destroy_sample(tsamples[0]);
        al_destroy_sample(tsamples[1]);
}

void tapenoise_addhigh()
{
        int c;
        float wavediv = (32.0f * 2400.0f) / (float) FREQ_DD;
//        log_debug("Wavediv %f %i\n",wavediv,tmcount);
        tmcount++;
        for (c = 0; c < 368; c++)
        {
                if (tpnoisep >= BUFLEN_DD) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                swavepos += wavediv;
        }
}

void tapenoise_adddat(uint8_t dat)
{
        int c, d,e = 0;
        float wavediv = (32.0f * 2400.0f) / (float) FREQ_DD;
//        swavepos=0;
        for (c = 0; c < 30; c++) /*Start bit*/
        {
                if (tpnoisep >= BUFLEN_DD) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                e++;
                swavepos += (wavediv / 2);
        }
        swavepos = fmod(swavepos, 32.0);
        while (swavepos < 32.0)
        {
                if (tpnoisep >= BUFLEN_DD) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                swavepos += (wavediv / 2);
                e++;
        }
        for (d = 0; d < 8; d++)
        {
                swavepos = fmod(swavepos, 32.0);
                while (swavepos < 32.0)
                {
                        if (tpnoisep >= BUFLEN_DD) return;
                        tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * ((dat & 1) ? 50 : 64);
                        if (dat & 1) swavepos += wavediv;
                        else         swavepos += (wavediv / 2);
                        e++;
                }
                dat >>= 1;
        }
//        swavepos=0;
        for ( ;e < 368; e++) /*Stop bit*/
        {
                if (tpnoisep >= BUFLEN_DD) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                swavepos += (wavediv / 2);
        }

        tapenoise_addhigh();
}

static int tnoise_sstat = -1, tnoise_spos;

void tapenoise_motorchange(int stat)
{
        tnoise_sstat = stat ^ 1;
        tnoise_spos = 0;
}

void tapenoise_mix(int16_t *tapebuffer)
{
        int c;
        tpnoisep = 0;
        if (!sound_tape) return;
//        log_debug("Mix!\n");

        for (c = 0; c < BUFLEN_DD; c++)
        {
                tapebuffer[c] += tapenoise[c];
                tapenoise[c] = 0;
        }

        for (c = 0; c < BUFLEN_DD; c++)
        {
                if (tnoise_sstat >= 0)
                {
                        if (tnoise_spos >= al_get_sample_length(tsamples[tnoise_sstat]))
                        {
                                tnoise_spos = 0;
                                tnoise_sstat = -1;
                        }
                        else
                        {
                                int16_t *data = (int16_t *)al_get_sample_data(tsamples[tnoise_sstat]);
                                tapebuffer[c] += ((int16_t)((data[(int)tnoise_spos]) ^ 0x8000) / 4);
                                tnoise_spos += ((float)al_get_sample_frequency(tsamples[tnoise_sstat]) / (float) FREQ_DD);
                        }
                }
        }
}
