#ifndef __INC_65816_H
#define __INC_65816_H

#include "6502debug.h"
#include "savestate.h"

typedef struct
{
    int c,z,i,d,b,v,n,m,ex,e; /*X renamed to EX due to #define conflict*/
} w65816p_t;

extern w65816p_t w65816p;

bool w65816_init(FILE *romf);
void w65816_reset(void);
void w65816_exec(void);
void w65816_close(void);
void w65816_savestate(ZFILE *zfp);
void w65816_loadstate(ZFILE *zfp);
uint8_t readmem65816(uint32_t a);
void writemem65816(uint32_t a, uint8_t v);

extern cpu_debug_t tube65816_cpu_debug;

#endif
