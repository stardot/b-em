#ifndef __INC_UEF_H
#define __INC_UEF_H

void uef_load(char *fn);
void uef_close();
void uef_poll();
void uef_findfilenames();

extern int uef_toneon;

#endif
