#ifndef __INC_DISC_H
#define __INC_DISC_H

#define NUM_DRIVES 2

typedef struct
{
    /*
     * The following function pointers defines the interface between this disc
     * odule and the modules that implement specifc disc file formats.
     */
    void (*close)(int drive);
    void (*seek)(int drive, int track);
    void (*readsector)(int drive, int sector, int track, int side, int density);
    void (*writesector)(int drive, int sector, int track, int side, int density);
    void (*readaddress)(int drive, int track, int side, int density);
    void (*format)(int drive, int track, int side, unsigned par2);
    void (*writetrack)(int drive, int track, int side, int density);
    void (*readtrack)(int drive, int track, int side, int density);
    void (*poll)(void);
    void (*abort)(int drive);
    void (*spinup)(int drive);
    void (*spindown)(int drive);
    int (*verify)(int drive, int track, int density);
    /*
     * The following fields hold the state for each drive.
     */
    ALLEGRO_PATH *discfn;
    unsigned writeprot:1;
    unsigned fwriteprot:1;
} DRIVE;

extern DRIVE drives[NUM_DRIVES];

extern int curdrive;

/*
 * The following functions are the external interface to this disc module.
 */

int disc_load(int drive, ALLEGRO_PATH *fn);
void disc_close(int drive);
void disc_init(void);
void disc_poll(void);
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);
void disc_writetrack(int drive, int track, int side, int density);
void disc_readtrack(int drive, int track, int side, int density);
void disc_abort(int drive);
int disc_verify(int drive, int track, int density);

extern int disc_time;

/*
 * The following functions are callback to the FDC that may be made by
 * either this module or any of the disc file format modules when various
 * onditions occur.
 */

extern void (*fdc_callback)(void);
extern void (*fdc_data)(uint8_t dat);
extern void (*fdc_spindown)(void);
extern void (*fdc_finishread)(bool deleted);
extern void (*fdc_notfound)(void);
extern void (*fdc_datacrcerror)(bool deleted);
extern void (*fdc_headercrcerror)(void);
extern void (*fdc_writeprotect)(void);
extern int  (*fdc_getdata)(int last);
extern int fdc_time;

extern int motorspin;
extern int motoron;

extern bool defaultwriteprot;

#endif
