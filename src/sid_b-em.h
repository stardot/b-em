#ifdef __cplusplus
extern "C" {
#endif

void    sid_init();
void    sid_reset();
void    sid_settype(int resamp, int model);
uint8_t sid_read(uint16_t addr);
void    sid_write(uint16_t addr, uint8_t val);
void sid_fillbuf(int16_t *buf, int len);

extern int cursid;
extern int sidmethod;

#ifdef __cplusplus
}
#endif
