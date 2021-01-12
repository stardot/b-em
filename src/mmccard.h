#ifndef MMCCARD_INC
#define MMCCARD_INC

extern uint8_t mmccard_read(void);
extern void mmccard_write(uint8_t byte);
extern void mmccard_load(char *filename);
extern void mmccard_eject(void);

extern char *mmccard_fn;

#endif
