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
    if (!status)
        gui_allegro_set_eject_text(drive, fn);
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

int oldtrack[NUM_DRIVES] = {0, 0};
void disc_seek(int drive, int track)
{
        if (drives[drive].seek)
            drives[drive].seek(drive, track);
        ddnoise_seek(track - oldtrack[drive]);
        oldtrack[drive] = track;
}

void disc_readsector(int drive, int sector, int track, int side, int density)
{
        if (drives[drive].readsector) {
            autoboot = 0;
            drives[drive].readsector(drive, sector, track, side, density);
        }
        else
           disc_notfound = 10000;
}

void disc_writesector(int drive, int sector, int track, int side, int density)
{
        if (drives[drive].writesector)
           drives[drive].writesector(drive, sector, track, side, density);
        else
           disc_notfound = 10000;
}

void disc_readaddress(int drive, int track, int side, int density)
{
        if (drives[drive].readaddress)
           drives[drive].readaddress(drive, track, side, density);
        else
           disc_notfound = 10000;
}

void disc_format(int drive, int track, int side, int density)
{
        if (drives[drive].format)
           drives[drive].format(drive, track, side, density);
        else
           disc_notfound = 10000;
}

void disc_writetrack(int drive, int track, int side, int density)
{
    if (drives[drive].writetrack)
        drives[drive].writetrack(drive, track, side, density);
    else
        disc_format(drive, track, side, density);
}

void disc_readtrack(int drive, int track, int side, int density)
{
    if (drives[drive].readtrack)
        drives[drive].readtrack(drive, track, side, density);
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

int disc_verify(int drive, int track, int density)
{
        if (drives[drive].verify)
            return drives[drive].verify(drive, track, density);
        else
            return 1;
}
