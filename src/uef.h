#ifndef __INC_UEF_H
#define __INC_UEF_H

void uef_load(const char *fn);
void uef_close(void);
void uef_poll(void);
void uef_findfilenames(void);

extern int uef_toneon;

#endif
