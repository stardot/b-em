#ifndef __INC_DEBUGGER_H
#define __INC_DEBUGGER_H

extern void debug_start(void);
extern void debug_kill(void);
extern void debug_end(void);
extern void debug_toggle_core(void);
extern void debug_toggle_tube(void);

extern int readc[65536], writec[65536], fetchc[65536];

extern int debug_core,debug_tube,debug_step;

#endif

