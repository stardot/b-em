void tube_6502_init_cpu();
void tube_6502_reset();
void tube_6502_exec();
void tube_6502_close();
void tube_6502_mapoutrom();
uint8_t tube_6502_readmem(uint32_t addr);
void tube_6502_writemem(uint32_t addr, uint8_t byte);
