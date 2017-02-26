#ifndef __INC_SSD_H
#define __INC_SSD_H

void            ssd_init();
void            ssd_load(int drive, char *fn);
void            ssd_close(int drive);
void            dsd_load(int drive, char *fn);
void            ssd_seek(int drive, int track);
void            ssd_readsector(int drive, int sector, int track, int side,
    int density);
void            ssd_writesector(int drive, int sector, int track, int side,
    int density);
void            ssd_readaddress(int drive, int sector, int side, int density);
void            ssd_format(int drive, int sector, int side, int density);
void            ssd_poll();

#endif
