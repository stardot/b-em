#ifndef __INC_SAVESTATE_H
#define __INC_SAVESTATE_H

extern int      savestate_wantsave, savestate_wantload;
extern char     savestate_name[260];

void            savestate_save();
void            savestate_load();
void            savestate_dosave();
void            savestate_doload();

#endif
