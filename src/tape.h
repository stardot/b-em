#ifndef __INC_TAPE_H
#define __INC_TAPE_H

#include "acia.h"

extern char tape_fn[260];
extern int tape_loaded;

void tape_load(char *fn);
void tape_close();
void tape_poll(void);
void tape_receive(ACIA *acia, uint8_t data);

extern int tapelcount,tapellatch;
extern int fasttape;

#endif
