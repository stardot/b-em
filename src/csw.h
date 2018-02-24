#ifndef __INC__CSW_H
#define __INC__CSW_H

void csw_load(const char *fn);
void csw_close();
void csw_poll();
void csw_findfilenames();

extern int csw_ena;
extern int csw_toneon;

#endif
