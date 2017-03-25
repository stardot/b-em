#ifndef __INC_65816_H
#define __INC_65816_H

#include "6502debug.h"

typedef struct 
{
    int c,z,i,d,b,v,n,m,ex,e; /*X renamed to EX due to #define conflict*/
} w65816p_t;

extern w65816p_t w65816p;

void w65816_init();
void w65816_reset();
void w65816_exec();
void w65816_close();
uint8_t readmem65816(uint32_t a);
void writemem65816(uint32_t a, uint8_t v);
#endif
