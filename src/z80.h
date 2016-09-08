void z80_init();
void z80_reset();
void z80_exec();
void z80_close();
uint8_t tube_z80_readmem(uint32_t addr);
void tube_z80_writemem(uint32_t addr, uint8_t byte);
