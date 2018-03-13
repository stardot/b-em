#ifndef __INC_DISC_H
#define __INC_DISC_H

#define NUM_DRIVES 2

typedef struct
{
        void (*close)(int drive);
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density);
        void (*writesector)(int drive, int sector, int track, int side, int density);
        void (*readaddress)(int drive, int track, int side, int density);
        void (*format)(int drive, int track, int side, int density);
        void (*poll)(void);
        void (*abort)(int drive);
        int (*verify)(int drive, int track, int density);
} DRIVE;

extern DRIVE drives[NUM_DRIVES];

extern int curdrive;

void disc_load(int drive, ALLEGRO_PATH *fn);
void disc_new(int drive, ALLEGRO_PATH *fn);
void disc_close(int drive);
void disc_init(void);
void disc_poll(void);
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);
void disc_abort(int drive);
int disc_verify(int drive, int track, int density);

extern int disc_time;

extern void (*fdc_callback)(void);
extern void (*fdc_data)(uint8_t dat);
extern void (*fdc_spindown)(void);
extern void (*fdc_finishread)(void);
extern void (*fdc_notfound)(void);
extern void (*fdc_datacrcerror)(void);
extern void (*fdc_headercrcerror)(void);
extern void (*fdc_writeprotect)(void);
extern int  (*fdc_getdata)(int last);
extern int fdc_time;

extern int motorspin;
extern int motoron;

extern int defaultwriteprot;
extern ALLEGRO_PATH *discfns[NUM_DRIVES];

extern int writeprot[NUM_DRIVES], fwriteprot[NUM_DRIVES];

#endif
