/*B-em v2.2 by Tom Walker
  Disc support*/

#include "b-em.h"
#include "gui-allegro.h"
#include "fdi.h"
#include "hfe.h"
#include "disc.h"
#include "sdf.h"
#include "imd.h"

#include "ddnoise.h"

DRIVE drives[NUM_DRIVES];

int curdrive = 0;

bool defaultwriteprot = false;

int fdc_time;
int disc_time;

int motorspin;
int motoron;

void (*fdc_callback)();
void (*fdc_data)(uint8_t dat);
void (*fdc_spindown)();
void (*fdc_finishread)(bool deleted);
void (*fdc_notfound)();
void (*fdc_datacrcerror)(bool deleted);
void (*fdc_headercrcerror)();
void (*fdc_writeprotect)();
int  (*fdc_getdata)(int last);

int disc_load(int drive, ALLEGRO_PATH *fn)
{
    int status = -1;
    if (fn) {
        const char *cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
        if (cpath) {
            const char *ext = al_get_path_extension(fn);
            if (ext) {
                if (*ext == '.')
                    ext++;
                if (strcasecmp(ext, "fdi") == 0) {
                    log_debug("Loading %i: %s as FDI", drive, cpath);
                    status = fdi_load(drive, cpath);
                }
                else if (strcasecmp(ext, "hfe") == 0) {
                    log_debug("Loading %i: %s as HFE", drive, cpath);
                    status =  hfe_load(drive, cpath);
                }
                else if (strcasecmp(ext, "imd") == 0) {
                    log_debug("Loading %i: %s as IMD", drive, cpath);
                    status = imd_load(drive, cpath);
                }
                else
                    status = sdf_load(drive, cpath, ext);
            }
            else
                status = sdf_load(drive, cpath, ext);
        }
    }
    if (!status) {
        drives[drive].newdisk = 1;
        gui_allegro_set_eject_text(drive, fn);
    }
    else
        gui_allegro_set_eject_text(drive, NULL);
    return status;
}

void disc_close(int drive)
{
        if (drives[drive].close)
            drives[drive].close(drive);
        // Force the drive to spin down (i.e. become not-ready) when the disk is unloaded
        // This prevents the file system (e.g DFS) caching the old disk catalogue
        if (fdc_spindown)
            fdc_spindown();

}

int disc_notfound=0;

static void disc_init_drive(DRIVE *drive)
{
    drive->close       = NULL;
    drive->seek        = NULL;
    drive->readsector  = NULL;
    drive->writesector = NULL;
    drive->readaddress = NULL;
    drive->format      = NULL;
    drive->writetrack  = NULL;
    drive->readtrack   = NULL;
    drive->poll        = NULL;
    drive->abort       = NULL;
    drive->spinup      = NULL;
    drive->spindown    = NULL;
    drive->verify      = NULL;
}

void disc_init()
{
    disc_init_drive(&drives[0]);
    disc_init_drive(&drives[1]);
    curdrive = 0;
}

void disc_poll()
{
        if (drives[curdrive].poll) drives[curdrive].poll();
        if (disc_notfound)
        {
                disc_notfound--;
                if (!disc_notfound)
                   fdc_notfound();
        }
}

void disc_seek0(int drive, uint32_t step_time, uint32_t settle_time)
{
    DRIVE *dp = &drives[drive];
    if (dp->curtrack) {
        if (dp->seek)
            dp->seek(drive, 0);
        ddnoise_seek(-dp->curtrack);
        fdc_time = dp->curtrack * step_time + settle_time;
        log_debug("disc: drive %d: seek track zero, steps=%d, step_time=%'u, settle_time=%'u, fdc_time=%'d", drive, dp->curtrack, step_time, settle_time, fdc_time);
        dp->curtrack = 0;
    }
    else if (dp->newdisk) {
        log_debug("disc: drive %d: seek track zero, new disk", drive);
        if (dp->seek)
            dp->seek(drive, 0);
        dp->newdisk = 0;
        fdc_time = 200;
    }
    else {
        log_debug("disc: drive %d: seek track zero, already there", drive);
        fdc_time = 200;
    }
}

void disc_seekrelative(int drive, int tracks, uint32_t step_time, uint32_t settle_time)
{
    if (tracks) {
        DRIVE *dp = &drives[drive];
        int newtrack = dp->curtrack + tracks;
        if (newtrack < 0) {
            log_warn("disc: drive %d: attempt to seek out beyond track zero", drive);
            newtrack = 0;
        }
        ddnoise_seek(tracks);
        dp->curtrack = newtrack;
        fdc_time = ((tracks < 0) ? -tracks : tracks) * step_time + settle_time;
        log_debug("disc: drive %d: seek of %+d tracks, step_time=%'u, settle_time=%'u, fdc_time=%'d", drive, tracks, step_time, settle_time, fdc_time);
        if (dp->seek)
            dp->seek(drive, newtrack);
    }
    else {
        fdc_time = 200;
        log_debug("disc: drive %d: seek of zero tracks", drive);
    }
}

void disc_readsector(int drive, int sector, int track, int side, unsigned flags)
{
        if (drives[drive].readsector) {
            autoboot = 0;
            drives[drive].readsector(drive, sector, track, side, flags);
        }
        else
           disc_notfound = 10000;
}

void disc_writesector(int drive, int sector, int track, int side, unsigned flags)
{
        if (drives[drive].writesector)
           drives[drive].writesector(drive, sector, track, side, flags);
        else
           disc_notfound = 10000;
}

void disc_readaddress(int drive, int side, unsigned flags)
{
    if (drives[drive].readaddress)
       drives[drive].readaddress(drive, side, flags);
    else
       disc_notfound = 10000;
}

void disc_format(int drive, int side, unsigned flags)
{
    if (drives[drive].format)
       drives[drive].format(drive, side, flags);
    else
       fdc_writeprotect();
}

void disc_writetrack(int drive, int side, unsigned flags)
{
    if (drives[drive].writetrack)
        drives[drive].writetrack(drive, side, flags);
    else
        fdc_writeprotect();
}

void disc_readtrack(int drive, int side, unsigned flags)
{
    if (drives[drive].readtrack)
        drives[drive].readtrack(drive, side, flags);
    else
       disc_notfound = 10000;
}

void disc_abort(int drive)
{
        if (drives[drive].abort)
           drives[drive].abort(drive);
        else
           disc_notfound = 10000;
}

int disc_verify(int drive, int track, unsigned flags)
{
        if (drives[drive].verify)
            return drives[drive].verify(drive, track, flags);
        else
            return 1;
}
