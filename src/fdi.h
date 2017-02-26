#ifndef __INC_FDI_H
#define __INC_FDI_H

void            fdi_init();
void            fdi_load(int drive, char *fn);
void            fdi_close(int drive);
void            fdi_seek(int drive, int track);
void            fdi_readsector(int drive, int sector, int track, int side,
    int density);
void            fdi_writesector(int drive, int sector, int track, int side,
    int density);
void            fdi_readaddress(int drive, int sector, int side, int density);
void            fdi_format(int drive, int sector, int side, int density);
void            fdi_poll();

#endif
