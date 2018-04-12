#ifndef __INC_SAVESTATE_H
#define __INC_SAVESTATE_H

#include <stdio.h>

typedef struct _sszfile ZFILE;

extern int savestate_wantsave, savestate_wantload;
extern char *savestate_name;

void savestate_save(const char *name);
void savestate_load(const char *name);
void savestate_dosave(void);
void savestate_doload(void);

void savestate_zread(ZFILE *zfp, void *dest, size_t size);
void savestate_zwrite(ZFILE *zfp, void *src, size_t size);

extern void savestate_save_var(unsigned var, FILE *f);
extern void savestate_save_str(const char *str, FILE *f);
extern unsigned savestate_load_var(FILE *f);
extern char *savestate_load_str(FILE *f);

#endif
