#ifndef __INC_TAPE_H
#define __INC_TAPE_H

extern char     tape_fn[260];
extern int      tape_loaded;

void            tape_load(char *fn);
void            tape_close();

extern int      tapelcount, tapellatch;
extern int      fasttape;

#endif
