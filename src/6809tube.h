#ifndef TUBE_6809_INC
#define TUBE_6809_INC

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern void tube_6809_int(int new_irq);
extern uint8_t copro_mc6809nc_read(uint16_t addr);
extern void copro_mc6809nc_write(uint16_t addr, uint8_t data);
extern bool tube_6809_init(FILE *romf);
extern void mc6809nc_reset(void);
extern void mc6809nc_close(void);

#endif
