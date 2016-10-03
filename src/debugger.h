extern void debug_start();
extern void debug_kill();
extern void debug_end();
extern void debug_read(uint16_t addr);
extern void debug_write(uint16_t addr, uint8_t val);
extern void debugger_do();

extern int readc[65536], writec[65536], fetchc[65536];

extern int debug,debugon;
