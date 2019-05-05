#ifndef __INC_ADC_H
#define __INC_ADC_H

void adc_init(void);
void adc_poll(void);
uint8_t adc_read(uint16_t addr);
void adc_write(uint16_t addr, uint8_t val);

void adc_savestate(FILE *f);
void adc_loadstate(FILE *f);

extern int adc_time;

#endif
