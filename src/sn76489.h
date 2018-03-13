#ifndef __INC_SN74689_H
#define __INC_SN74689_H

void sn_init(void);
void sn_fillbuf(int16_t *buffer, int len);
void sn_write(uint8_t data);
void sn_savestate(FILE *f);
void sn_loadstate(FILE *f);

extern uint8_t sn_freqhi[4],sn_freqlo[4];
extern uint8_t sn_vol[4];
extern uint8_t sn_noise;
extern uint32_t sn_latch[4];

extern int curwave;

#endif
