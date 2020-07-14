/*B-em v2.2 by Tom Walker
  Disc support*/

#include "b-em.h"
#include "gui-allegro.h"
#include "fdi.h"
#include "disc.h"
#include "sdf.h"
#include "imd.h"

#include "ddnoise.h"

DRIVE drives[2];

int curdrive = 0;

ALLEGRO_PATH *discfns[2] = { NULL, NULL };
bool defaultwriteprot = false;
int writeprot[NUM_DRIVES], fwriteprot[NUM_DRIVES];

int fdc_time;
int disc_time;

int motorspin;
int motoron;

void (*fdc_callback)();
void (*fdc_data)(uint8_t dat);
void (*fdc_spindown)();
void (*fdc_finishio)();
int  (*fdc_getdata)(int last);

void disc_load(int drive, ALLEGRO_PATH *fn)
{
    const char *ext;
    const char *cpath;

    if (!fn)
        return;
    gui_allegro_set_eject_text(drive, fn);
    cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
    if ((ext = al_get_path_extension(fn))) {
        if (*ext == '.')
            ext++;
        if (strcasecmp(ext, "fdi") == 0) {
            log_debug("Loading %i: %s as FDI", drive, cpath);
            fdi_load(drive, cpath);
            return;
        }
        if (strcasecmp(ext, "imd") == 0) {
            log_debug("Loading %i: %s as IMD", drive, cpath);
            imd_load(drive, cpath);
            return;
        }
    }
    sdf_load(drive, cpath, ext);
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

void disc_init()
{
        drives[0].poll = drives[1].poll = 0;
        drives[0].seek = drives[1].seek = 0;
        drives[0].readsector = drives[1].readsector = 0;
        curdrive = 0;
}

void disc_poll()
{
        if (drives[curdrive].poll) drives[curdrive].poll();
        if (disc_notfound)
        {
                disc_notfound--;
                if (!disc_notfound)
                   fdc_finishio(FDC_NOT_FOUND);
        }
}

int oldtrack[2] = {0, 0};
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
