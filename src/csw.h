#ifndef __INC__CSW_H
#define __INC__CSW_H

void csw_load(const char *fn);
void csw_close(void);
void csw_poll(void);
void csw_findfilenames(void);

extern int csw_ena;
extern int csw_toneon;

#endif
