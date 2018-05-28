#ifndef __INC_TUBE_H
#define __INC_TUBE_H

#include "savestate.h"
#include <stdbool.h>

typedef struct {
    const char *name;
    float multipler;
} tube_speed_t;

#define NUM_TUBE_SPEEDS 7
extern tube_speed_t tube_speeds[NUM_TUBE_SPEEDS];
extern int tube_speed_num, tube_multipler;

void tube_reset(void);
bool tube_6502_init(FILE *romf);
bool tube_arm_init(FILE *romf);
bool tube_z80_init(FILE *romf);
bool tube_x86_init(FILE *romf);
bool tube_65816_init(FILE *romf);
bool tube_32016_init(FILE *romf);

uint8_t (*tube_readmem)(uint32_t addr);
void (*tube_writemem)(uint32_t addr, uint8_t byte);
void (*tube_exec)(void);
void (*tube_proc_savestate)(ZFILE *zfp);
void (*tube_proc_loadstate)(ZFILE *zfp);
extern int tubecycles;

uint8_t tube_host_read(uint16_t addr);
void    tube_host_write(uint16_t addr, uint8_t val);
uint8_t tube_parasite_read(uint32_t addr);
void    tube_parasite_write(uint32_t addr, uint8_t val);

extern int tube_irq;

void tube_reset(void);
void tube_updatespeed(void);

void tube_ula_savestate(FILE *f);
void tube_ula_loadstate(FILE *f);

#endif
