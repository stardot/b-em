#ifndef __INC_SAVESTATE_H
#define __INC_SAVESTATE_H

extern int savestate_wantsave, savestate_wantload;
extern char *savestate_name;

void savestate_save(const char *name);
void savestate_load(const char *name);
void savestate_dosave(void);
void savestate_doload(void);

extern void savestate_save_var(unsigned var, FILE *f);
extern unsigned savestate_load_var(FILE *f);

#endif
