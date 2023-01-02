#ifndef __INC_SYSACIA_H
#define __INC_SYSACIA_H

#include "acia.h"

extern ACIA sysacia;
extern int sysacia_tapespeed;
extern FILE *sysacia_fp;

void sysacia_poll(void);
void sysacia_rec_stop(void);
FILE *sysacia_rec_start(const char *filename);

#endif
