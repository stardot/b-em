/*B-em v2.2 by Tom Walker
  Disc drive noise*/

#include <stdio.h>
#include "b-em.h"
#include "disc.h"
#include "ddnoise.h"
#include "sound.h"
#include "soundopenal.h"
#include "tapenoise.h"

int ddnoise_vol=3;
int ddnoise_type=0;

static ALLEGRO_SAMPLE *seeksmp[4][2];
static ALLEGRO_SAMPLE *motorsmp[3];

static float ddnoise_mpos = 0;
static int ddnoise_mstat = -1;
static int oldmotoron = 0;

static float ddnoise_spos = 0;
static int ddnoise_sstat = -1;
static int ddnoise_sdir = 0;

ALLEGRO_SAMPLE *find_load_wav(const char *subdir, const char *name) {
    char path[PATH_MAX];

    if (find_dat_file(path, sizeof path, subdir, name, "wav") == 0) {
        log_debug("ddnoise: loading sample %s from %s", name, path);
        return al_load_sample(path);
    }
    return NULL;
}

void ddnoise_init()
{
    const char *subdir;

    if (ddnoise_type) subdir = "ddnoise/35";
    else              subdir = "ddnoise/525";
    seeksmp[0][0] = find_load_wav(subdir, "stepo");
    if (seeksmp[0][0]) {
        seeksmp[0][1] = find_load_wav(subdir, "stepi");
        seeksmp[1][0] = find_load_wav(subdir, "seek1o");
        seeksmp[1][1] = find_load_wav(subdir, "seek1i");
        seeksmp[2][0] = find_load_wav(subdir, "seek2o");
        seeksmp[2][1] = find_load_wav(subdir, "seek2i");
        seeksmp[3][0] = find_load_wav(subdir, "seek3o");
        seeksmp[3][1] = find_load_wav(subdir, "seek3i");
    }
    else
    {
        seeksmp[0][0] = find_load_wav(subdir, "step");
        seeksmp[0][1] = find_load_wav(subdir, "step");
        seeksmp[1][0] = find_load_wav(subdir, "seek");
        seeksmp[1][1] = find_load_wav(subdir, "seek");
        seeksmp[2][0] = find_load_wav(subdir, "seek3");
        seeksmp[2][1] = find_load_wav(subdir, "seek3");
        seeksmp[3][0] = find_load_wav(subdir, "seek2");
        seeksmp[3][1] = find_load_wav(subdir, "seek2");
    }
    motorsmp[0] = find_load_wav(subdir, "motoron");
    motorsmp[1] = find_load_wav(subdir, "motor");
    motorsmp[2] = find_load_wav(subdir, "motoroff");
}

void ddnoise_close()
{
        int c;
        for (c = 0; c < 4; c++)
        {
                if (seeksmp[c][0]) al_destroy_sample(seeksmp[c][0]);
                if (seeksmp[c][1]) al_destroy_sample(seeksmp[c][1]);
                seeksmp[c][0] = seeksmp[c][1] = NULL;
        }
        for (c = 0; c < 3; c++)
        {
                if (motorsmp[c]) al_destroy_sample(motorsmp[c]);
                motorsmp[c] = NULL;
        }
}

static int16_t ddbuffer[BUFLEN_DD];

void ddnoise_seek(int len)
{
//        printf("Seek %i tracks\n",len);
        ddnoise_sdir = (len < 0) ? 1 : 0;
        if (len < 0) len = -len;
        ddnoise_spos = 0;
        if (len == 0)      { ddnoise_sstat = -1; fdc_time = 200; }
        else if (len == 1) { ddnoise_sstat = 0; fdc_time = 140000; }
        else if (len < 7)    ddnoise_sstat = 1;
        else if (len < 30)   ddnoise_sstat = 2;
        else                 ddnoise_sstat = 3;
        if (!sound_ddnoise) fdc_time = 200;
//        log_debug("Start seek!\n");
}

void ddnoise_mix()
{
        int c;
//        if (!f1) f1=x_fopen("f1.pcm","wb");
//        if (!f2) f2=x_fopen("f2.pcm","wb");

        memset(ddbuffer, 0, BUFLEN_DD * 2);
//        fwrite(ddbuffer,BUFLEN_DD*2,1,f1);
        if (motoron && !oldmotoron)
        {
                ddnoise_mstat = 0;
                ddnoise_mpos = 0;
        }
        if (!motoron && oldmotoron)
        {
                ddnoise_mstat = 2;
                ddnoise_mpos = 0;
        }

        if (sound_ddnoise)
        {
                for (c = 0; c < BUFLEN_DD; c++)
                {
                        ddbuffer[c] = 0;
                        if (ddnoise_mstat >= 0)
                        {
                                if (ddnoise_mpos >= al_get_sample_length(motorsmp[ddnoise_mstat]))
                                {
                                        ddnoise_mpos = 0;
                                        if (ddnoise_mstat != 1) ddnoise_mstat++;
                                        if (ddnoise_mstat == 3) ddnoise_mstat = -1;
                                }
                                if (ddnoise_mstat != -1)
                                {
                                        int16_t *data = (int16_t *)al_get_sample_data(motorsmp[ddnoise_mstat]);
                                        ddbuffer[c] += ((int16_t)((data[(int)ddnoise_mpos]) ^ 0x8000) / 2);
                                        ddnoise_mpos += ((float)al_get_sample_frequency(motorsmp[ddnoise_mstat]) / (float) FREQ_DD);
                                }
                        }
                }

                for (c = 0; c < BUFLEN_DD; c++)
                {
                        if (ddnoise_sstat >= 0)
                        {
                                if (ddnoise_spos >= al_get_sample_length(seeksmp[ddnoise_sstat][ddnoise_sdir]))
                                {
                                        if (ddnoise_sstat > 0)
                                           fdc_time = 100;
                                        ddnoise_spos = 0;
                                        ddnoise_sstat = -1;
                                }
                                else
                                {
                                        int16_t *data = (int16_t *)al_get_sample_data(seeksmp[ddnoise_sstat][ddnoise_sdir]);
                                        ddbuffer[c] += ((int16_t)((data[(int)ddnoise_spos]) ^ 0x8000) / 2);
                                        ddnoise_spos += ((float)al_get_sample_frequency(seeksmp[ddnoise_sstat][ddnoise_sdir]) / (float) FREQ_DD);
                                }
                        }
                        ddbuffer[c] = (ddbuffer[c] / 3) * ddnoise_vol;
                }
        }

        tapenoise_mix(ddbuffer);
//        fwrite(ddbuffer,BUFLEN_DD*2,1,f2);
//log_debug("Give buffer... %i %i\n",ddnoise_mstat,ddnoise_sstat);
        al_givebufferdd(ddbuffer);

        oldmotoron=motoron;
}
