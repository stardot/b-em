void w65816_init();
void w65816_reset();
void w65816_exec();
void w65816_close();
uint8_t readmem65816(uint32_t a);
void writemem65816(uint32_t a, uint8_t v);
