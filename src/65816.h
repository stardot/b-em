#ifndef __INC_65816_H
#define __INC_65816_H

#include "6502debug.h"
#include "savestate.h"

typedef struct
{
    int c,z,i,d,b,v,n,m,ex,e; /*X renamed to EX due to #define conflict*/
} w65816p_t;

extern w65816p_t w65816p;

bool w65816_init(void *rom);
void w65816_reset(void);
void w65816_exec(void);
void w65816_close(void);

extern cpu_debug_t tube65816_cpu_debug;

#endif
