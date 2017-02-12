void adc_init();
void adc_poll();
uint8_t adc_read(uint16_t addr);
void adc_write(uint16_t addr, uint8_t val);

void adc_savestate(FILE *f);
void adc_loadstate(FILE *f);

extern int adc_time;
