/*ARM*/
unsigned long userregs[16],superregs[16],fiqregs[16],irqregs[16];
unsigned long *armregs[16];
int armirq,armfiq;
#define PC ((*armregs[15]&0x3FFFFFC)-8)

void resetarm();
void execarm(int cycles);
void dumparmregs();
int databort;
