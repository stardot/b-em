#ifndef __INC_ADF_H
#define __INC_ADF_H

void adf_init();
void adf_load(int drive, char *fn);
void adf_close(int drive);
void adl_load(int drive, char *fn);
void adl_loadex(int drive, char *fn, int sectors, int size, int dblstep);

#endif
