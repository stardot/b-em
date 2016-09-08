/*ARM*/
//uint32_t *usrregs[16],userregs[16],superregs[16],fiqregs[16],irqregs[16];
//uint32_t armregs[16];
//int armirq,armfiq;
//#define PC ((armregs[15])&0x3FFFFFC)

//void dumparmregs();
//int databort;

void arm_init();
void arm_reset();
void arm_exec();
void arm_close();
uint8_t readarmb(uint32_t addr);
void writearmb(uint32_t addr, uint8_t val);
