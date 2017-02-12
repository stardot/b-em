void debug_start();
void debug_kill();
void debug_end();
void debug_read(uint16_t addr);
void debug_write(uint16_t addr, uint8_t val);
void debugger_do();


extern int readc[65536], writec[65536], fetchc[65536];

extern int debug,debugon;
