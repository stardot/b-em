#ifndef __INC_DEBUGGER_H
#define __INC_DEBUGGER_H

extern void debug_start(const char *exec_fn, uint8_t spawn_memview); /* TOHv4: spawn_memview */
extern void debug_kill(void);
extern void debug_end(void);
void debug_toggle_core(uint8_t spawn_memview); /* TOHv4: spawn_memview */
extern void debug_toggle_tube(void);
extern void debug_paste(const char *str, void (*paste_start)(char *str));

extern int readc[65536], writec[65536], fetchc[65536];

extern int debug_core,debug_tube,debug_step;

#endif

