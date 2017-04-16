#ifndef __INC_SSD_H
#define __INC_SSD_H

void ssd_init();
void ssd_load(int drive, char *fn);
void ssd_close(int drive);
void dsd_load(int drive, char *fn);

#endif
