#ifndef __INC_SYSACIA_H
#define __INC_SYSACIA_H

#include "acia.h"

extern ACIA sysacia;
extern int sysacia_tapespeed;

void sysacia_poll(void);

#endif
