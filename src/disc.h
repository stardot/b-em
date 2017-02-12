typedef struct
{
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density);
        void (*writesector)(int drive, int sector, int track, int side, int density);
        void (*readaddress)(int drive, int track, int side, int density);
        void (*format)(int drive, int track, int side, int density);
        void (*poll)();
} DRIVE;

extern DRIVE drives[2];

extern int curdrive;

void disc_load(int drive, char *fn);
void disc_new(int drive, char *fn);
void disc_close(int drive);
void disc_init();
void disc_poll();
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);
extern int disc_time;

extern void (*fdc_callback)();
extern void (*fdc_data)(uint8_t dat);
extern void (*fdc_spindown)();
extern void (*fdc_finishread)();
extern void (*fdc_notfound)();
extern void (*fdc_datacrcerror)();
extern void (*fdc_headercrcerror)();
extern void (*fdc_writeprotect)();
extern int  (*fdc_getdata)(int last);
extern int fdc_time;

extern int motorspin;
extern int motoron;

extern int defaultwriteprot;
extern char discfns[2][260];

extern int writeprot[2], fwriteprot[2];
