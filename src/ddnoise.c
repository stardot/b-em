/*B-em v2.2 by Tom Walker
  Disc drive noise*/

#include <stdio.h>
#include "b-em.h"
#include "disc.h"
#include "ddnoise.h"
#include "sound.h"
#include "tapenoise.h"

int ddnoise_vol=3;
int ddnoise_type=0;
int ddnoise_ticks = 0;

static ALLEGRO_SAMPLE *seeksmp[4][2];
static ALLEGRO_SAMPLE *motorsmp[3];

static ALLEGRO_SAMPLE_ID seek_smp_id;
static ALLEGRO_SAMPLE_ID motor_smp_id;

ALLEGRO_SAMPLE *find_load_wav(ALLEGRO_PATH *dir, const char *name)
{
    ALLEGRO_PATH *path;
    ALLEGRO_SAMPLE *smp;
    const char *cpath;

    if ((path = find_dat_file(dir, name, ".wav"))) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        if ((smp = al_load_sample(cpath))) {
            log_debug("ddnoise: loaded %s from %s", name, cpath);
            return smp;
        }
        log_error("ddnoise: unable to load %s from %s", name, cpath);
        al_destroy_path(path);
    }
    return NULL;
}

void ddnoise_init(void)
{
    const char *dir;
    ALLEGRO_PATH *subdir;
    static ALLEGRO_SAMPLE *smp;

    if (ddnoise_type) dir = "ddnoise/35";
    else              dir = "ddnoise/525";
    subdir = al_create_path_for_directory(dir);

    if ((smp = find_load_wav(subdir, "stepo"))) {
        seeksmp[0][0] = smp;
        seeksmp[0][1] = find_load_wav(subdir, "stepi");
        seeksmp[1][0] = find_load_wav(subdir, "seek1o");
        seeksmp[1][1] = find_load_wav(subdir, "seek1i");
        seeksmp[2][0] = find_load_wav(subdir, "seek2o");
        seeksmp[2][1] = find_load_wav(subdir, "seek2i");
        seeksmp[3][0] = find_load_wav(subdir, "seek3o");
        seeksmp[3][1] = find_load_wav(subdir, "seek3i");
    } else {
        smp = find_load_wav(subdir, "step");
        seeksmp[0][0] = smp;
        seeksmp[0][1] = smp;
        smp = find_load_wav(subdir, "seek");
        seeksmp[1][0] = smp;
        seeksmp[1][0] = smp;
        smp = find_load_wav(subdir, "seek2");
        seeksmp[2][0] = smp;
        seeksmp[2][1] = smp;
        smp = find_load_wav(subdir, "seek3");
        seeksmp[3][0] = smp;
        seeksmp[3][1] = smp;
    }
    motorsmp[0] = find_load_wav(subdir, "motoron");
    motorsmp[1] = find_load_wav(subdir, "motor");
    motorsmp[2] = find_load_wav(subdir, "motoroff");
}

void ddnoise_close()
{
    ALLEGRO_SAMPLE *smp, *smpo, *smpi;
    int c;

    for (c = 0; c < 4; c++) {
        if ((smpo = seeksmp[c][0])) {
            al_destroy_sample(smpo);
            seeksmp[c][0] = NULL;
        }
        if ((smpi = seeksmp[c][1])) {
            if (smpi != smpo)
                al_destroy_sample(smpo);
            seeksmp[c][1] = NULL;
        }
    }
    for (c = 0; c < 3; c++) {
        if ((smp = motorsmp[c])) {
            al_destroy_sample(motorsmp[c]);
            motorsmp[c] = NULL;
        }
    }
}

static float map_ddnoise_vol(void)
{
    switch(ddnoise_vol)
    {
        case 0:
            return 1.0/3.0;
        case 1:
            return 2.0/3.0;
        default:
            return 1.0;
    }
}

void ddnoise_seek(int len)
{
    ALLEGRO_SAMPLE *smp;
    int ddnoise_sstat = -1;
    int ddnoise_sdir = 0;

    log_debug("ddnoise: seek %i tracks", len);

    fdc_time = 200;
    if (sound_ddnoise && len) {
        if (len < 0) {
            ddnoise_sdir = 1;
            len = -len;
        }
        if (len == 1)
            ddnoise_sstat = 0;
        else if (len < 7)
            ddnoise_sstat = 1;
        else if (len < 30)
            ddnoise_sstat = 2;
        else
            ddnoise_sstat = 3;
        if ((smp = seeksmp[ddnoise_sstat][ddnoise_sdir])) {
            al_stop_sample(&seek_smp_id);
            al_play_sample(smp, map_ddnoise_vol(), 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, &seek_smp_id);
            fdc_time = 64000 * len;
        }
    }
    log_debug("ddnoise: begin seek, fdc_time=%d", fdc_time);
}

void ddnoise_spinup(void)
{
    ALLEGRO_SAMPLE *smp;

    log_debug("ddnoise: spinup");
    if (sound_ddnoise && (smp = motorsmp[0])) {
        al_play_sample(smp, map_ddnoise_vol(), 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
        ddnoise_ticks = (50 * al_get_sample_length(smp)) / al_get_sample_frequency(smp);
        log_debug("ddnoise: head load sample to finish in %d ticks", ddnoise_ticks);
    }
}

void ddnoise_headdown(void)
{
    ALLEGRO_SAMPLE *smp;

    log_debug("ddnoise: head down");
    if (sound_ddnoise && (smp = motorsmp[1]))
        al_play_sample(smp, map_ddnoise_vol(), 0.0, 1.0, ALLEGRO_PLAYMODE_LOOP, &motor_smp_id);
}

void ddnoise_spindown(void)
{
    ALLEGRO_SAMPLE *smp;

    log_debug("ddnoise: spindown");
    if (sound_ddnoise) {
        if ((smp = motorsmp[1])) {
            log_debug("ddnoise: stopping sample");
            al_stop_sample(&motor_smp_id);
        }
        if ((smp = motorsmp[2]))
            al_play_sample(smp, map_ddnoise_vol(), 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
    }
}
