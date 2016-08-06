/*B-em v2.2 by Tom Walker
  Tape noise (not very good)*/

#include <stdio.h>
#include <allegro.h>
#include <math.h>
#include "b-em.h"
#include "ddnoise.h"
#include "tapenoise.h"
#include "sound.h"

static int tpnoisep = 0;
static int tmcount = 0;
static int16_t tapenoise[4410];

static float swavepos = 0;

static int sinewave[32];

#define PI 3.142

static SAMPLE *tsamples[2];

void tapenoise_init()
{
        char path[512], p2[512];
        int c;

        getcwd(p2, 511);
        sprintf(path, "%sddnoise", exedir);
//        printf("path now %s\n",path);
        chdir(path);
        tsamples[0] = safe_load_wav("motoron.wav");
        tsamples[1] = safe_load_wav("motoroff.wav");
        chdir(p2);
        for (c = 0; c < 32; c++)
        {
                sinewave[c] = (int)(sin((float)c * ((2.0 * PI) / 32.0)) * 128.0);
        }
}

void tapenoise_close()
{
        destroy_sample(tsamples[0]);
        destroy_sample(tsamples[1]);
}

void tapenoise_addhigh()
{
        int c;
        float wavediv = (32.0f * 2400.0f) / 44100.0f;
//        bem_debugf("Wavediv %f %i\n",wavediv,tmcount);
        tmcount++;
        for (c = 0; c < 368; c++)
        {
                if (tpnoisep >= 4410) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                swavepos += wavediv;
        }
}

void tapenoise_adddat(uint8_t dat)
{
        int c, d,e = 0;
        float wavediv = (32.0f * 2400.0f) / 44100.0f;
//        swavepos=0;
        for (c = 0; c < 30; c++) /*Start bit*/
        {
                if (tpnoisep >= 4410) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                e++;
                swavepos += (wavediv / 2);
        }
        swavepos = fmod(swavepos, 32.0);
        while (swavepos < 32.0)
        {
                if (tpnoisep >= 4410) return;
                tapenoise[tpnoisep++] = sinewave[((int)swavepos) & 0x1F] * 64;
                swavepos += (wavediv / 2);
                e++;
        }
        for (d = 0; d < 8; d++)
        {
                swavepos = fmod(swavepos, 32.0);
                while (swavepos < 32.0)
                {
                        if (tpnoisep >= 4410) return;
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
                if (tpnoisep >= 4410) return;
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
//        bem_debug("Mix!\n");

        for (c = 0; c < 4410; c++)
        {
                tapebuffer[c] += tapenoise[c];
                tapenoise[c] = 0;
        }

        for (c = 0; c < 4410; c++)
        {
                if (tnoise_sstat >= 0)
                {
                        if (tnoise_spos >= tsamples[tnoise_sstat]->len)
                        {
                                tnoise_spos = 0;
                                tnoise_sstat = -1;
                        }
                        else
                        {
                                tapebuffer[c] += ((int16_t)((((int16_t *)tsamples[tnoise_sstat]->data)[(int)tnoise_spos]) ^ 0x8000) / 4);
                                tnoise_spos += ((float)tsamples[tnoise_sstat]->freq / 44100.0);
                        }
                }
        }
}
