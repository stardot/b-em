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
        void (*format)(int drive, int track, int side, unsigned par2);
        void (*writetrack)(int drive, int track, int side, int density);
        void (*poll)(void);
        void (*abort)(int drive);
        int (*verify)(int drive, int track, int density);
} DRIVE;

extern DRIVE drives[NUM_DRIVES];

extern int curdrive;

void disc_load(int drive, ALLEGRO_PATH *fn);
void disc_close(int drive);
void disc_init(void);
void disc_poll(void);
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);
void disc_writetrack(int drive, int track, int side, int density);
void disc_abort(int drive);
int disc_verify(int drive, int track, int density);

extern int disc_time;

/* FDC status/error indications.
 *
 * These flag values allow a disc format implementation to report one
 * or more statuses back to the FDC emulation.  These bit masks here
 * are as used by the WD1770.  With the exception of "Deleted Data",
 * the i8271 does not allow more than one of these errors to be
 * reported at the same time so the i8271 emulation will convert
 * these flags into a suitable status value.
 */
#define FDC_SUCCESS        0x00
#define FDC_HEADER_CRC_ERR 0x04
#define FDC_DATA_CRC_ERR   0x08
#define FDC_NOT_FOUND      0x10
#define FDC_DELETED_DATA   0x20
#define FDC_WRITE_PROTECT  0x40

extern void (*fdc_callback)(void);
extern void (*fdc_data)(uint8_t dat);
extern void (*fdc_spindown)(void);
extern void (*fdc_finishio)(unsigned flags);
extern int  (*fdc_getdata)(int last);
extern int fdc_time;

extern int motorspin;
extern int motoron;

extern bool defaultwriteprot;
extern ALLEGRO_PATH *discfns[NUM_DRIVES];

extern int writeprot[NUM_DRIVES], fwriteprot[NUM_DRIVES];

#endif
