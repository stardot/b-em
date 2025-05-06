#ifndef __INC_DISC_H
#define __INC_DISC_H

#define NUM_DRIVES 2

enum {
    DISC_FLAG_MFM  = 0x01,
    DISC_FLAG_DELD = 0x02,
    DISC_FLAG_GAPS = 0x04
};

typedef struct
{
    /*
     * The following function pointers defines the interface between this disc
     * odule and the modules that implement specifc disc file formats.
     */
    void (*close)(int drive);
    void (*seek)(int drive, int track);
    void (*readsector)(int drive, int sector, int track, int side, unsigned flags);
    void (*writesector)(int drive, int sector, int track, int side, unsigned flags);
    void (*readaddress)(int drive, int side, unsigned flags);
    void (*format)(int drive, int side, unsigned par2);
    void (*writetrack)(int drive, int side, unsigned flags);
    void (*readtrack)(int drive, int side, unsigned flags);
    void (*poll)(void);
    void (*abort)(int drive);
    void (*spinup)(int drive);
    void (*spindown)(int drive);
    int (*verify)(int drive, int track, unsigned flags);
    /*
     * The following fields hold the state for each drive.
     */
    ALLEGRO_PATH *discfn;
    unsigned writeprot:1;
    unsigned fwriteprot:1;
    unsigned newdisk:1;
    int curtrack;
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
void disc_seek0(int drive, uint32_t step_time, uint32_t settle_time);
void disc_seekrelative(int drive, int tracks, uint32_t step_time, uint32_t settle_time);
void disc_readsector(int drive, int sector, int track, int side, unsigned flags);
void disc_writesector(int drive, int sector, int track, int side, unsigned flags);
void disc_readaddress(int drive, int side, unsigned flags);
void disc_format(int drive, int side, unsigned flags);
void disc_writetrack(int drive, int side, unsigned flags);
void disc_readtrack(int drive, int side, unsigned flags);
void disc_abort(int drive);
int disc_verify(int drive, int track, unsigned flags);

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
