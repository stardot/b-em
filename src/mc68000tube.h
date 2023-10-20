#ifndef MC68000_TUBE_INC
#define MC68000_TUBE_INC

#include <stdbool.h>

#define MC68000_RAM_SIZE 0x800000
#define MC68000_ROM_SIZE 0x8000

extern bool tube_68000_init(void *rom);
extern void tube_68000_rst(void);

extern cpu_debug_t mc68000_cpu_debug;

#endif
