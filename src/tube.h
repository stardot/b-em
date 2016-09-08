void tube_reset();
void tube_6502_init();
void tube_arm_init();
void tube_z80_init();
void tube_x86_init();
void tube_65816_init();
void tube_32016_init();

uint8_t (*tube_readmem)(uint32_t addr);
void (*tube_writemem)(uint32_t addr, uint8_t byte);
void (*tube_exec)();
extern int tubecycles;

uint8_t tube_host_read(uint16_t addr);
void    tube_host_write(uint16_t addr, uint8_t val);
uint8_t tube_parasite_read(uint32_t addr);
void    tube_parasite_write(uint32_t addr, uint8_t val);

extern int tube_shift;
extern int tube_6502_speed;

extern int tube_irq;

void tube_reset();
void tube_updatespeed();
