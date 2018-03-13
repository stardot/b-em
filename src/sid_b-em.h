#ifndef __INC_SID_B_EM_H
#define __INC_SID_B_EM_H

#ifdef __cplusplus
extern "C" {
#endif

void    sid_init(void);
void    sid_reset(void);
void    sid_settype(int resamp, int model);
uint8_t sid_read(uint16_t addr);
void    sid_write(uint16_t addr, uint8_t val);
void sid_fillbuf(int16_t *buf, int len);

extern int cursid;
extern int sidmethod;

#ifdef __cplusplus
}
#endif
#endif
