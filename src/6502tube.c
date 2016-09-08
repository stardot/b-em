/*B-em v2.2 by Tom Walker
  6502 parasite CPU emulation*/

#include <allegro.h>
#include <stdio.h>

#include "b-em.h"
#include "tube.h"
#include "6502tube.h"

#define a tubea
#define x tubex
#define y tubey
#define s tubesp
#define pc tubepc

// define this to trace execution to a file
// #define TRACE_TUBE

static int tube_6502_skipint;
static int tube_6502_oldnmi;

/*6502 registers*/
static uint8_t a, x, y, s;
static uint16_t pc;
static struct {
        int c, z, i, d, v, n;
} tubep;

/*Memory structures*/
/*There is an extra entry to allow for stupid programs (3d grand prix) doing
  something stupid like STA $FFFF,x*/
static uint8_t *tubemem[0x101];
static int tubememstat[0x101];
static uint8_t *tuberam;
static uint8_t tuberom[0x1000];

#ifdef TRACE_TUBE
static FILE *trace_fp;
#endif

static void tube_6502_loadrom()
{
        FILE *f;
        char fn[512];
        append_filename(fn, exedir, "roms/tube/6502Tube.rom", 511);
        f = fopen(fn, "rb");
        fread(tuberom + 0x800, 0x800, 1, f);
        fclose(f);
}

void tube_6502_init_cpu()
{
        int c;
        if (!tuberam)
                tuberam = (uint8_t *) malloc(0x10000);
        memset(tuberam, 0, 0x10000);
        for (c = 0x00; c < 0x100; c++)
                tubemem[c] = (uint8_t *) (tuberam + (c << 8));
        for (c = 0x00; c < 0x100; c++)
                tubememstat[c] = 0;
        for (c = 0xF0; c < 0x100; c++)
                tubememstat[c] = 2;
//        tubememstat[0xFE]=tubememstat[0xFF]=2;
        tubemem[0x100] = tubemem[0];
        tubememstat[0x100] = tubememstat[0];
        tube_6502_loadrom();
#ifdef TRACE_TUBE
        if ((trace_fp = fopen("6502tube.trace", "wb"))) {
                fwrite("6502NMOS", 8, 1, trace_fp);
                time_t secs;
                time(&secs);
                fwrite(&secs, sizeof(secs), 1, trace_fp);
                puts("tube tracing enabled\n");
        } else
                fprintf(stderr, "tube6502: unable to open trace file: %m\n");
#endif
}

void tube_6502_close()
{
#ifdef TRACE_TUBE
        if (trace_fp)
                fclose(trace_fp);
#endif
        if (tuberam)
                free(tuberam);
}

#undef printf
/*static void tubedumpregs()
{
        FILE *f=fopen("tuberam.dmp","wb");
        fwrite(tuberam,65536,1,f);
        fclose(f);
        bem_debug("Tube 65c12 registers :\n");
        bem_debugf("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n",a,x,y,s,pc);
        bem_debugf("Status : %c%c%c%c%c%c\n",(tubep.n)?'N':' ',(tubep.v)?'V':' ',(tubep.d)?'D':' ',(tubep.i)?'I':' ',(tubep.z)?'Z':' ',(tubep.c)?'C':' ');
}*/

static int tuberomin = 1;
void tube_6502_mapoutrom()
{
        tuberomin = 0;
}

#define polltime(c) { tubecycles-=c; }

static uint8_t tubereadmeml(uint16_t addr)
{
        uint8_t temp;
        if ((addr & ~7) == 0xFEF8) {
                temp = tube_parasite_read(addr);
//                bem_debugf("Read tube  %04X %02X %04X\n",addr,temp,pc);
                return temp;
        }
        if ((addr & ~0xFFF) == 0xF000 && tuberomin)
                return tuberom[addr & 0xFFF];
        return tuberam[addr];
}

uint8_t tube_6502_readmem(uint32_t addr) {
    return tubereadmeml(addr & 0xffff);
}

int endtimeslice;
static void tubewritememl(uint16_t addr, uint8_t val)
{
//        bem_debugf("Tube writemem %04X %02X %04X\n",addr,val,pc);
        if ((addr & ~7) == 0xFEF8) {
//                bem_debugf("Write tube %04X %02X %04X\n",addr,val,pc);
                tube_parasite_write(addr, val);
                endtimeslice = 1;
                return;
        }
//        if (addr==0xF4 || addr==0xF5) bem_debugf("TUBE PARASITE write %04X %02X\n",addr,val);
        tuberam[addr] = val;
}

void tube_6502_writemem(uint32_t addr, uint8_t byte) {
    tubewritememl(addr & 0xffff, byte);
}

#define readmem(a) ((tubememstat[(a)>>8]==2)?tubereadmeml(a):tubemem[(a)>>8][(a)&0xFF])
#define writemem(a,b) tubewritememl(a,b)
//if (tubememstat[(a)>>8]==0) tubemem[(a)>>8][(a)&0xFF]=b; else if (tubememstat[(a)>>8]==2) tubewritememl(a,b)
#define getw() (readmem(pc)|(readmem(pc+1)<<8)); pc+=2

void tube_6502_reset()
{
        tuberomin = 1;
//memset(tuberam,0,0x10000);
        pc = readmem(0xFFFC) | (readmem(0xFFFD) << 8);
        tubep.i = 1;
        tube_irq = 0;
        tube_6502_skipint = 0;
}

#define setzn(v) tubep.z=!(v); tubep.n=(v)&0x80

#define push(v) tuberam[0x100+(s--)]=v
#define pull()  tuberam[0x100+(++s)]

/*ADC/SBC temp variables*/
static int16_t tempw;
static int tempv, hc, al, ah;
static uint8_t tempb;

#define ADC(temp)       if (!tubep.d)                            \
                        {                                  \
                                tempw=(a+temp+(tubep.c?1:0));        \
                                tubep.v=(!((a^temp)&0x80)&&((a^tempw)&0x80));  \
                                a=tempw&0xFF;                  \
                                tubep.c=tempw&0x100;                  \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                ah=0;        \
                                tempb=a+temp+(tubep.c?1:0);                            \
                                if (!tempb)                                      \
                                   tubep.z=1;                                          \
                                al=(a&0xF)+(temp&0xF)+(tubep.c?1:0);                            \
                                if (al>9)                                        \
                                {                                                \
                                        al-=10;                                  \
                                        al&=0xF;                                 \
                                        ah=1;                                    \
                                }                                                \
                                ah+=((a>>4)+(temp>>4));                             \
                                if (ah&8) tubep.n=1;                                   \
                                tubep.v=(((ah << 4) ^ a) & 128) && !((a ^ temp) & 128);   \
                                tubep.c=0;                                             \
                                if (ah>9)                                        \
                                {                                                \
                                        tubep.c=1;                                     \
                                        ah-=10;                                  \
                                        ah&=0xF;                                 \
                                }                                                \
                                a=(al&0xF)|(ah<<4);                              \
                        }

#define SBC(temp)       if (!tubep.d)                            \
                        {                                  \
                                tempw = a-temp-(tubep.c ? 0 : 1);    \
                                tempv = (signed char)a-(signed char)temp - (tubep.c ? 0 : 1);        \
                                tubep.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);         \
                                tubep.c = tempw >= 0;          \
                                a = tempw & 0xFF;          \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                hc=0;                               \
                                tubep.z=tubep.n=0;                            \
                                if (!((a-temp)-((tubep.c)?0:1)))            \
                                   tubep.z=1;                             \
                                al=(a&15)-(temp&15)-((tubep.c)?0:1);      \
                                if (al&16)                           \
                                {                                   \
                                        al-=6;                      \
                                        al&=0xF;                    \
                                        hc=1;                       \
                                }                                   \
                                ah=(a>>4)-(temp>>4);                \
                                if (hc) ah--;                       \
                                if ((a-(temp+((tubep.c)?0:1)))&0x80)        \
                                   tubep.n=1;                             \
                                tubep.v=(((a-(temp+((tubep.c)?0:1)))^temp)&128)&&((a^temp)&128); \
                                tubep.c=1; \
                                if (ah&16)                           \
                                {                                   \
                                        tubep.c=0; \
                                        ah-=6;                      \
                                        ah&=0xF;                    \
                                }                                   \
                                a=(al&0xF)|((ah&0xF)<<4);                 \
                        }

#ifdef TRACE_TUBE
static INLINE void tube_6502_trace(uint8_t opcode)
{
        uint8_t cyc;

        if (trace_fp) {
                flockfile(trace_fp);
                cyc = tubecycles;
                putc_unlocked(cyc, trace_fp);
                putc_unlocked(pc & 0xff, trace_fp);
                putc_unlocked(pc >> 8, trace_fp);
                putc_unlocked(opcode, trace_fp);
                putc_unlocked(readmem(pc), trace_fp);
                putc_unlocked(readmem(pc + 1), trace_fp);
                putc_unlocked(a, trace_fp);
                putc_unlocked(x, trace_fp);
                putc_unlocked(y, trace_fp);
                putc_unlocked(s, trace_fp);
                uint8_t flags = 0x30;
                if (tubep.n)
                        flags |= 0x80;
                if (tubep.v)
                        flags |= 0x40;
                if (tubep.d)
                        flags |= 0x08;
                if (tubep.i)
                        flags |= 0x04;
                if (tubep.z)
                        flags |= 0x02;
                if (tubep.c)
                        flags |= 0x01;
                putc_unlocked(flags, trace_fp);
                funlockfile(trace_fp);
        }
}
#endif

static uint16_t oldtpc, oldtpc2;

void tube_6502_exec()
{
        uint8_t opcode;
        uint16_t addr;
        uint8_t temp;
        int tempi;
        int8_t offset;
//        tubecycles+=(tubecycs<<1);
//        printf("Tube exec %i %04X\n",tubecycles,pc);
        while (tubecycles > 0) {
                oldtpc2 = oldtpc;
                oldtpc = pc;
                opcode = readmem(pc);
                pc++;
#ifdef TRACE_TUBE
                tube_6502_trace(opcode);
#endif
//                        printf("Tube opcode %02X\n",opcode);
                switch (opcode) {
                case 0x00:
                        /*BRK*/
//                                bem_debugf("Tube BRK at %04X! %04X %04X\n",pc,oldtpc,oldtpc2);
                            pc++;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        temp = 0x30;
                        if (tubep.c)
                                temp |= 1;
                        if (tubep.z)
                                temp |= 2;
                        if (tubep.d)
                                temp |= 8;
                        if (tubep.v)
                                temp |= 0x40;
                        if (tubep.n)
                                temp |= 0x80;
                        push(temp);
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        tubep.i = 1;
                        polltime(7);
                        break;

                case 0x01:      /*ORA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        a |= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x04:      /*TSB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
                        temp |= a;
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x05:      /*ORA zp */
                        addr = readmem(pc);
                        pc++;
                        a |= tuberam[addr];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x06:      /*ASL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x08:
                        /*PHP*/ temp = 0x30;
                        if (tubep.c)
                                temp |= 1;
                        if (tubep.z)
                                temp |= 2;
                        if (tubep.i)
                                temp |= 4;
                        if (tubep.d)
                                temp |= 8;
                        if (tubep.v)
                                temp |= 0x40;
                        if (tubep.n)
                                temp |= 0x80;
                        push(temp);
                        polltime(3);
                        break;

                case 0x09:      /*ORA imm */
                        a |= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x0A:      /*ASL A */
                        tubep.c = a & 0x80;
                        a <<= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x0C:      /*TSB abs */
                        addr = getw();
//                                printf("TSB %04X %02X\n",addr,a);
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
                        temp |= a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x0D:      /*ORA abs */
                        addr = getw();
                        polltime(4);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x0E:      /*ASL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x10:
                        /*BPL*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x11:      /*ORA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x12:      /*ORA () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        a |= readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x14:      /*TRB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x15:      /*ORA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a |= tuberam[(addr + x) & 0xFF];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x16:      /*ASL zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = tuberam[addr];
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x18:
                        /*CLC*/ tubep.c = 0;
                        polltime(2);
                        break;

                case 0x19:      /*ORA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x1A:      /*INC A */
                        a++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x1C:      /*TRB abs */
                        addr = getw();
                        temp = readmem(addr);
                        tubep.z = !(temp & a);  //!(temp&(~a));
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x1D:      /*ORA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        a |= readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x1E:      /*ASL abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        break;

                case 0x20:
                        /*JSR*/ addr = getw();
                        pc--;
                        push(pc >> 8);
                        push(pc);
                        pc = addr;
                        polltime(6);
                        break;

                case 0x21:      /*AND (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        a &= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x24:      /*BIT zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(3);
                        break;

                case 0x25:      /*AND zp */
                        addr = readmem(pc);
                        pc++;
                        a &= tuberam[addr];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x26:      /*ROL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x28:
                        /*PLP*/ temp = pull();
                        tubep.c = temp & 1;
                        tubep.z = temp & 2;
                        tubep.i = temp & 4;
                        tubep.d = temp & 8;
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x29:
                        /*AND*/ a &= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x2A:      /*ROL A */
                        tempi = tubep.c;
                        tubep.c = a & 0x80;
                        a <<= 1;
                        if (tempi)
                                a |= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x2C:      /*BIT abs */
                        addr = getw();
                        temp = readmem(addr);
//                                printf("BIT %04X\n",addr);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x2D:      /*AND abs */
                        addr = getw();
                        polltime(4);
                        a &= readmem(addr);
                        setzn(a);
                        break;

                case 0x2E:      /*ROL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0x30:
                        /*BMI*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x31:      /*AND (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x35:      /*AND zp,x */
                        addr = readmem(pc);
                        pc++;
                        a &= tuberam[(addr + x) & 0xFF];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x36:      /*ROL zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = tuberam[addr];
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x38:
                        /*SEC*/ tubep.c = 1;
                        polltime(2);
                        break;

                case 0x39:      /*AND abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x3A:      /*DEC A */
                        a--;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x3C:      /*BIT abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
//                                printf("BIT abs,x %02X %04X\n",temp,addr);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x3D:      /*AND abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x3E:      /*ROL abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        break;

                case 0x40:
                        /*RTI*/ temp = pull();
                        tubep.c = temp & 1;
                        tubep.z = temp & 2;
                        tubep.i = temp & 4;
                        tubep.d = temp & 8;
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        pc = pull();
                        pc |= (pull() << 8);
                        polltime(6);
                        break;

                case 0x41:      /*EOR (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x45:      /*EOR zp */
                        addr = readmem(pc);
                        pc++;
                        a ^= tuberam[addr];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x46:      /*LSR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x48:
                        /*PHA*/ push(a);
                        polltime(3);
                        break;

                case 0x49:
                        /*EOR*/ a ^= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x4A:      /*LSR A */
                        tubep.c = a & 1;
                        a >>= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x4C:
                        /*JMP*/ addr = getw();
                        pc = addr;
                        polltime(3);
                        break;

                case 0x4D:      /*EOR abs */
                        addr = getw();
                        polltime(4);
                        a ^= readmem(addr);
                        setzn(a);
                        break;

                case 0x4E:      /*LSR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x50:
                        /*BVC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x51:      /*EOR (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x52:      /*EOR () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x55:      /*EOR zp,x */
                        addr = readmem(pc);
                        pc++;
                        a ^= tuberam[(addr + x) & 0xFF];
                        setzn(a);
                        polltime(3);
                        break;

                case 0x56:      /*LSR zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = tuberam[addr];
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x58:
                        /*CLI*/ tubep.i = 0;
                        polltime(2);
                        break;

                case 0x59:      /*EOR abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x5A:
                        /*PHY*/ push(y);
                        polltime(3);
                        break;

                case 0x5D:      /*EOR abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x5E:      /*LSR abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
                        tubep.c = temp & 1;
                        temp >>= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        break;

                case 0x60:
                        /*RTS*/ pc = pull();
                        pc |= (pull() << 8);
                        pc++;
                        polltime(6);
                        break;

                case 0x61:      /*ADC (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        temp = readmem(addr);
                        ADC(temp);
                        polltime(6);
                        break;

                case 0x64:      /*STZ zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr] = 0;
                        polltime(3);
                        break;

                case 0x65:      /*ADC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        ADC(temp);
                        polltime(3);
                        break;

                case 0x66:      /*ROR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x68:
                        /*PLA*/ a = pull();
                        setzn(a);
                        polltime(4);
                        break;

                case 0x69:      /*ADC imm */
                        temp = readmem(pc);
                        pc++;
                        ADC(temp);
                        polltime(2);
                        break;

                case 0x6A:      /*ROR A */
                        tempi = tubep.c;
                        tubep.c = a & 1;
                        a >>= 1;
                        if (tempi)
                                a |= 0x80;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x6C:      /*JMP () */
                        addr = getw();
                        pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(5);
                        break;

                case 0x6D:      /*ADC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        ADC(temp);
                        break;

                case 0x6E:      /*ROR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x70:
                        /*BVS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x71:      /*ADC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        ADC(temp);
                        polltime(5);
                        break;

                case 0x72:      /*ADC () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        temp = readmem(addr);
                        ADC(temp);
                        polltime(5);
                        break;

                case 0x74:      /*STZ zp,x */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + x) & 0xFF] = 0;
                        polltime(3);
                        break;

                case 0x75:      /*ADC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[(addr + x) & 0xFF];
                        ADC(temp);
                        polltime(4);
                        break;

                case 0x76:      /*ROR zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = tuberam[addr];
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        tuberam[addr] = temp;
                        polltime(5);
                        break;

                case 0x78:
                        /*SEI*/ tubep.i = 1;
                        polltime(2);
//                                if (output2) printf("SEI at line %i %04X %02X %02X\n",lines,pc,tuberam[0x103+s],tuberam[0x104+s]);
                        break;

                case 0x79:      /*ADC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        ADC(temp);
                        polltime(4);
                        break;

                case 0x7A:
                        /*PLY*/ y = pull();
                        setzn(y);
                        polltime(4);
                        break;

                case 0x7C:      /*JMP (,x) */
                        addr = getw();
                        addr += x;
                        pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(6);
                        break;

                case 0x7D:      /*ADC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        ADC(temp);
                        polltime(4);
                        break;

                case 0x7E:      /*ROR abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        break;

                case 0x80:
                        /*BRA*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 3;
                        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                temp++;
                        pc += offset;
                        polltime(temp);
                        break;

                case 0x81:      /*STA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        writemem(addr, a);
                        polltime(6);
                        break;

                case 0x84:      /*STY zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr] = y;
                        polltime(3);
                        break;

                case 0x85:      /*STA zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr] = a;
                        polltime(3);
                        break;

                case 0x86:      /*STX zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr] = x;
                        polltime(3);
                        break;

                case 0x88:
                        /*DEY*/ y--;
                        setzn(y);
                        polltime(2);
                        break;

                case 0x89:      /*BIT imm */
                        temp = readmem(pc);
                        pc++;
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(2);
                        break;

                case 0x8A:
                        /*TXA*/ a = x;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x8C:      /*STY abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, y);
                        break;

                case 0x8D:      /*STA abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, a);
                        break;

                case 0x8E:      /*STX abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, x);
                        break;

                case 0x90:
                        /*BCC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x91:      /*STA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8) + y;
                        writemem(addr, a);
                        polltime(6);
                        break;

                case 0x92:      /*STA () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        writemem(addr, a);
                        polltime(6);
                        break;

                case 0x94:      /*STY zp,x */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + x) & 0xFF] = y;
                        polltime(4);
                        break;

                case 0x95:      /*STA zp,x */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + x) & 0xFF] = a;
                        polltime(4);
                        break;

                case 0x96:      /*STX zp,y */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + y) & 0xFF] = x;
                        polltime(4);
                        break;

                case 0x98:
                        /*TYA*/ a = y;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x99:      /*STA abs,y */
                        addr = getw();
                        polltime(4);
                        writemem(addr + y, a);
                        polltime(1);
                        break;

                case 0x9A:
                        /*TXS*/ s = x;
                        polltime(2);
                        break;

                case 0x9C:      /*STZ abs */
                        addr = getw();
                        polltime(3);
                        writemem(addr, 0);
                        polltime(1);
                        break;

                case 0x9D:      /*STA abs,x */
                        addr = getw();
                        polltime(4);
                        writemem(addr + x, a);
                        polltime(1);
                        break;

                case 0x9E:      /*STZ abs,x */
                        addr = getw();
                        addr += x;
                        polltime(4);
                        writemem(addr, 0);
                        polltime(1);
                        break;

                case 0xA0:      /*LDY imm */
                        y = readmem(pc);
                        pc++;
                        setzn(y);
                        polltime(2);
                        break;

                case 0xA1:      /*LDA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        a = readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0xA2:      /*LDX imm */
                        x = readmem(pc);
                        pc++;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xA4:      /*LDY zp */
                        addr = readmem(pc);
                        pc++;
                        y = tuberam[addr];
                        setzn(y);
                        polltime(3);
                        break;

                case 0xA5:      /*LDA zp */
                        addr = readmem(pc);
                        pc++;
                        a = tuberam[addr];
                        setzn(a);
                        polltime(3);
                        break;

                case 0xA6:      /*LDX zp */
                        addr = readmem(pc);
                        pc++;
                        x = tuberam[addr];
                        setzn(x);
                        polltime(3);
                        break;

                case 0xA8:
                        /*TAY*/ y = a;
                        setzn(y);
                        break;

                case 0xA9:      /*LDA imm */
                        a = readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0xAA:
                        /*TAX*/ x = a;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xAC:      /*LDY abs */
                        addr = getw();
                        polltime(4);
                        y = readmem(addr);
                        setzn(y);
                        break;

                case 0xAD:      /*LDA abs */
                        addr = getw();
                        polltime(4);
                        a = readmem(addr);
                        setzn(a);
                        break;

                case 0xAE:      /*LDX abs */
                        addr = getw();
                        polltime(4);
                        x = readmem(addr);
                        setzn(x);
                        break;

                case 0xB0:
                        /*BCS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xB1:      /*LDA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        break;

                case 0xB2:      /*LDA () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        a = readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0xB4:      /*LDY zp,x */
                        addr = readmem(pc);
                        pc++;
                        y = tuberam[(addr + x) & 0xFF];
                        setzn(y);
                        polltime(3);
                        break;

                case 0xB5:      /*LDA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a = tuberam[(addr + x) & 0xFF];
                        setzn(a);
                        polltime(3);
                        break;

                case 0xB6:      /*LDX zp,y */
                        addr = readmem(pc);
                        pc++;
                        x = tuberam[(addr + y) & 0xFF];
                        setzn(x);
                        polltime(3);
                        break;

                case 0xB8:
                        /*CLV*/ tubep.v = 0;
                        polltime(2);
                        break;

                case 0xB9:      /*LDA abs,y */
                        addr = getw();
                        polltime(3);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(1);
                        break;

                case 0xBA:
                        /*TSX*/ x = s;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xBC:      /*LDY abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        y = readmem(addr + x);
                        setzn(y);
                        polltime(4);
                        break;

                case 0xBD:      /*LDA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0xBE:      /*LDX abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        x = readmem(addr + y);
                        setzn(x);
                        polltime(4);
                        break;

                case 0xC0:      /*CPY imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(2);
                        break;

                case 0xC1:      /*CMP (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(6);
                        break;

                case 0xC4:      /*CPY zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(3);
                        break;

                case 0xC5:      /*CMP zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(3);
                        break;

                case 0xC6:      /*DEC zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr]--;
                        setzn(tuberam[addr]);
                        polltime(5);
                        break;

                case 0xC8:
                        /*INY*/ y++;
                        setzn(y);
                        polltime(2);
                        break;

                case 0xC9:      /*CMP imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(2);
                        break;

                case 0xCA:
                        /*DEX*/ x--;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xCC:      /*CPY abs */
                        addr = getw();
                        temp = readmem(addr);
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(4);
                        break;

                case 0xCD:      /*CMP abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        break;

                case 0xCE:      /*DEC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) - 1;
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xD0:
                        /*BNE*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xD1:      /*CMP (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(5);
                        break;

                case 0xD2:      /*CMP () */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(5);
                        break;

                case 0xD5:      /*CMP zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[(addr + x) & 0xFF];
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(3);
                        break;

                case 0xD6:      /*DEC zp,x */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + x) & 0xFF]--;
                        setzn(tuberam[(addr + x) & 0xFF]);
                        polltime(5);
                        break;

                case 0xD8:
                        /*CLD*/ tubep.d = 0;
                        polltime(2);
                        break;

                case 0xD9:      /*CMP abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(4);
                        break;

                case 0xDA:
                        /*PHX*/ push(x);
                        polltime(3);
                        break;

                case 0xDD:      /*CMP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(4);
                        break;

                case 0xDE:      /*DEC abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0xE0:      /*CPX imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(2);
                        break;

                case 0xE1:      /*SBC (,x) *//*This was missed out of every B-em version since 0.6 as it was never used! */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = readmem(temp) | (readmem(temp + 1) << 8);
                        temp = readmem(addr);
                        SBC(temp);
                        polltime(6);
                        break;

                case 0xE4:      /*CPX zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(3);
                        break;

                case 0xE5:      /*SBC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[addr];
                        SBC(temp);
                        polltime(3);
                        break;

                case 0xE6:      /*INC zp */
                        addr = readmem(pc);
                        pc++;
                        tuberam[addr]++;
                        setzn(tuberam[addr]);
                        polltime(5);
                        break;

                case 0xE8:
                        /*INX*/ x++;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xE9:      /*SBC imm */
                        temp = readmem(pc);
                        pc++;
                        SBC(temp);
                        polltime(2);
                        break;

                case 0xEA:
                        /*NOP*/ polltime(2);
                        break;

                case 0xEC:      /*CPX abs */
                        addr = getw();
                        temp = readmem(addr);
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(3);
                        break;

                case 0xED:      /*SBC abs */
                        addr = getw();
                        temp = readmem(addr);
                        SBC(temp);
                        polltime(4);
                        break;

                case 0xEE:      /*INC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) + 1;
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xF0:
                        /*BEQ*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xF1:      /*SBC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = readmem(temp) + (readmem(temp + 1) << 8);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        SBC(temp);
                        polltime(5);
                        break;

                case 0xF5:      /*SBC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = tuberam[(addr + x) & 0xFF];
                        SBC(temp);
                        polltime(3);
                        break;

                case 0xF6:      /*INC zp,x */
                        addr = readmem(pc);
                        pc++;
                        tuberam[(addr + x) & 0xFF]++;
                        setzn(tuberam[(addr + x) & 0xFF]);
                        polltime(5);
                        break;

                case 0xF8:
                        /*SED*/ tubep.d = 1;
                        polltime(2);
                        break;

                case 0xF9:      /*SBC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        SBC(temp);
                        polltime(4);
                        break;

                case 0xFA:
                        /*PLX*/ x = pull();
                        setzn(x);
                        polltime(4);
                        break;

                case 0xFD:      /*SBC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        SBC(temp);
                        polltime(4);
                        break;

                case 0xFE:      /*INC abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr) + 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0x02:
                case 0x22:
                case 0x42:
                case 0x62:
                case 0x82:
                case 0xC2:
                case 0xE2:
                case 0x03:
                case 0x13:
                case 0x23:
                case 0x33:
                case 0x43:
                case 0x53:
                case 0x63:
                case 0x73:
                case 0x83:
                case 0x93:
                case 0xA3:
                case 0xB3:
                case 0xC3:
                case 0xD3:
                case 0xE3:
                case 0xF3:
                case 0x0B:
                case 0x1B:
                case 0x2B:
                case 0x3B:
                case 0x4B:
                case 0x5B:
                case 0x6B:
                case 0x7B:
                case 0x8B:
                case 0x9B:
                case 0xAB:
                case 0xBB:
                case 0xEB:
                case 0xFB:
                case 0x44:
                case 0x54:
                case 0xD4:
                case 0xF4:
                case 0x5C:
                case 0xDC:
                case 0xFC:
                        switch (opcode & 0xF) {
                        case 0xA:
                                break;
                        case 0x0:
                        case 0x2:
                        case 0x3:
                        case 0x4:
                        case 0x7:
                        case 0x9:
                        case 0xB:
                                pc++;
                                break;
                        case 0xC:
                        case 0xE:
                        case 0xF:
                                pc += 2;
                                break;
                        }
                        break;
//                                default:
//                                allegro_exit();
//                                printf("Error : Bad tube 65c02 opcode %02X\n",opcode);
//                                pc--;
//                                dumpregs();
//                                printf("Current ROM %02X\n",currom);
//                                exit(-1);
                }
                if ((tube_irq & 2) && !tube_6502_oldnmi) {
                        push(pc >> 8);
                        push(pc & 0xFF);
                        temp = 0x20;
                        if (tubep.c)
                                temp |= 1;
                        if (tubep.z)
                                temp |= 2;
                        if (tubep.i)
                                temp |= 4;
                        if (tubep.d)
                                temp |= 8;
                        if (tubep.v)
                                temp |= 0x40;
                        if (tubep.n)
                                temp |= 0x80;
                        push(temp);
                        pc = readmem(0xFFFA) | (readmem(0xFFFB) << 8);
                        tubep.i = 1;
                        polltime(7);
                }
                tube_6502_oldnmi = tube_irq & 2;
                if (((tube_irq & 1) && !tubep.i && !tube_6502_skipint)
                    || tube_6502_skipint == 2) {
//                                if (skipint==2) printf("interrupt\n");
                        tube_6502_skipint = 0;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        temp = 0x20;
                        if (tubep.c)
                                temp |= 1;
                        if (tubep.z)
                                temp |= 2;
                        if (tubep.i)
                                temp |= 4;
                        if (tubep.d)
                                temp |= 8;
                        if (tubep.v)
                                temp |= 0x40;
                        if (tubep.n)
                                temp |= 0x80;
                        push(temp);
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        tubep.i = 1;
                        polltime(7);
//                                printf("Interrupt line %i %i %02X %02X %02X %02X\n",interrupt,lines,sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier,uservia.ier,uservia.ifr);
                }
                if ((tube_irq & 1) && !tubep.i && tube_6502_skipint) {
                        tube_6502_skipint = 2;
//                                printf("skipint=2\n");
                }
                if (endtimeslice) {
                        endtimeslice = 0;
                        return;
                }
/*                        if (tubeoutput==2) bem_debugf("%04X : %02X %02X %02X\n",pc,a,x,y);
                        if (tubetimetolive)
                        {
                                tubetimetolive--;
                                if (!tubetimetolive)
                                {
                                        tubeoutput=1;
                                }
                        }*/
        }
}
