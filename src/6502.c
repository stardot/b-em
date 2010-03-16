/*B-em v2.0 by Tom Walker
  6502/65c02 host CPU emulation*/

#include <stdio.h>
#include "b-em.h"

int mainins;
uint8_t acccon;
int tubecycle;

#define polltime(c) { cycles-=(c); \
                      sysvia.t1c-=(c);  if (!(sysvia.acr&0x20))  sysvia.t2c-=(c);  if (sysvia.t1c<-3  || sysvia.t2c<-3)  updatesystimers();  \
                      uservia.t1c-=(c); if (!(uservia.acr&0x20)) uservia.t2c-=(c); if (uservia.t1c<-3 || uservia.t2c<-3) updateusertimers(); \
                      pollvideo(c); \
                      otherstuffcount-=(c); \
                      if (motoron) \
                      { \
                                if (fdctime) { fdctime-=(c); if (fdctime<=0) fdccallback(); } \
                                disctime-=(c); if (disctime<=0) { disctime+=16; disc_poll(); } \
                      } \
                      tubecycle+=(c); \
                    }

int cycles;
int otherstuffcount=0;
int romsel;
int ram4k,ram8k,ram12k,ram20k;

int FEslowdown[8]={1,0,1,1,0,0,1,0};
int RAMbank[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int swram[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

uint8_t *memlook[2][256];
int memstat[2][256];
int vis20k=0;

extern int scrx,scry;
uint8_t readmem(uint16_t addr)
{
        uint8_t temp;
        if (debugon) debugread(addr);
        if (pc==addr) fetchc[addr]=31;
        else          readc[addr]=31;
        if (memstat[vis20k][addr>>8]) return memlook[vis20k][addr>>8][addr];
/*        if (addr<0x3000) return ram[addr];
        if (addr<0x8000 && (RAMbank[pc>>12] || ram20k)) return ram[addr|0x8000];
        if (addr<0x8000) return ram[addr];
        if (addr<0x9000 && ram4k) return ram[addr];
        if (addr<0xB000 && ram12k) return ram[addr];
        if ((addr&~0x1FFF)==0xC000 && ram8k) return ram[addr-0x3000];
        if (addr<0xC000) return rom[(addr&0x3FFF)+romsel];
        if (addr<0xFC00 || addr>=0xFF00) return os[addr&0x3FFF];*/
        if (addr<0xFE00 || FEslowdown[(addr>>5)&7]) { if (cycles&1) {polltime(2);} else { polltime(1); } }
        switch (addr&~3)
        {
                case 0xFC20: case 0xFC24: case 0xFC28: case 0xFC2C:
                case 0xFC30: case 0xFC34: case 0xFC38: case 0xFC3C:
//                case 0xFCE0: case 0xFCE4: case 0xFCE8: case 0xFCEC:
//                case 0xFCF0: case 0xFCF4: case 0xFCF8: case 0xFCFC:
                return readsid(addr);
                case 0xFE00: case 0xFE04: return readcrtc(addr);
                case 0xFE08: case 0xFE0C: return readacia(addr);
                case 0xFE18: if (MASTER) return readadc(addr); break;
                case 0xFE24: case 0xFE28: if (MASTER) return read1770(addr); break;
                case 0xFE34: if (MASTER) return acccon; break;
                case 0xFE40: case 0xFE44: case 0xFE48: case 0xFE4C:
                case 0xFE50: case 0xFE54: case 0xFE58: case 0xFE5C:
                temp=readsysvia(addr);
//                if (pc<0x8000) rpclog("Read SYS VIA %04X %02X %04X\n",addr,(addr==0xFE4d)?temp&~2:temp,pc);
                return temp;
                case 0xFE60: case 0xFE64: case 0xFE68: case 0xFE6C:
                case 0xFE70: case 0xFE74: case 0xFE78: case 0xFE7C:
                temp=readuservia(addr);
//                if (pc<0x8000) rpclog("Read USR VIA %04X %02X %04X\n",addr,temp,pc);
                return temp;
                case 0xFE80: case 0xFE84: case 0xFE88: case 0xFE8C:
                case 0xFE90: case 0xFE94: case 0xFE98: case 0xFE9C:
//                        break;
                if (!MASTER)
                {
                        if (WD1770) return read1770(addr);
                        return read8271(addr);
                }
                break;
                case 0xFEC0: case 0xFEC4: case 0xFEC8: case 0xFECC:
                case 0xFED0: case 0xFED4: case 0xFED8: case 0xFEDC:
                if (!MASTER) return readadc(addr);
                break;
                case 0xFEE0: case 0xFEE4: case 0xFEE8: case 0xFEEC:
                case 0xFEF0: case 0xFEF4: case 0xFEF8: case 0xFEFC:
                return readtubehost(addr);
        }
        return addr>>8;
}

void writemem(uint16_t addr, uint8_t val)
{
        int c;
//        if (addr>=0x3000 && addr<0x8000) rpclog("Write %04X %04X %02X\n",addr,pc,val);
//        if ((addr&0xFFE0)==0xFFE0) printf("Write %04X %04X %02X\n",addr,pc,val);
//        if (pc>=0x1000 && pc<0x2000 && addr>0xFF && (addr<0x1000 || addr>=0x2000) && ((addr&~0xFF)!=0xFC00))
//           printf("Write %04X %04X %02X\n",addr,pc,val);
//        if (addr>=0xD400 && addr<0xD420) printf("Write %04X %04X %02X\n",addr,pc,val);
//        if (addr>=0xFC00 && addr<0xFCFF) printf("Write %04X %04X %02X\n",addr,pc,val);
//        if (addr==0x31F) printf("Write 31F %02X %04X\n",val,pc);
        if (debugon) debugwrite(addr,val);
//        if (addr==0x1A) rpclog("Write 1A %04X %02X %02X\n",pc,val,ram[0x27]);
        writec[addr]=31;
//        if (addr==(0x7C28+4)) printf("Write %04X %02X %i %i\n",addr,val,scry,scrx);
        if (memstat[vis20k][addr>>8]==1) { memlook[vis20k][addr>>8][addr]=val; return; }
/*        if (addr<0x3000) { ram[addr]=val; return; }
        if (addr<0x8000 && (RAMbank[pc>>12] || ram20k)) { ram[addr|0x8000]=val; return; }
        if (addr<0x8000) { ram[addr]=val; return; }
        if (addr<0x9000 && ram4k) { ram[addr]=val; return; }
        if (addr<0xB000 && ram12k) { ram[addr]=val; return; }
        if ((addr&~0x1FFF)==0xC000 && ram8k) { ram[addr-0x3000]=val; return; }
        if (addr<0xC000 && swram[romsel>>14]) { rom[(addr&0x3FFF)+romsel]=val; return; }*/
        if (addr<0xFC00 || addr>=0xFF00) return;
        if (addr<0xFE00 || FEslowdown[(addr>>5)&7]) { if (cycles&1) {polltime(2);} else { polltime(1); } }
        switch (addr&~3)
        {
                case 0xFC20: case 0xFC24: case 0xFC28: case 0xFC2C:
                case 0xFC30: case 0xFC34: case 0xFC38: case 0xFC3C:
//                        rpclog("Write SID %04X %02X\n",addr,val);
                writesid(addr,val);
                break;
                case 0xFCE0: case 0xFCE4: case 0xFCE8: case 0xFCEC:
                case 0xFCF0: case 0xFCF4: case 0xFCF8: case 0xFCFC:
//                        rpclog("Write Bad SID %04X %02X\n",addr,val);
                        break;
                case 0xFE00: case 0xFE04: writecrtc(addr,val); break;
                case 0xFE08: case 0xFE0C: writeacia(addr,val); break;
                case 0xFE10: case 0xFE14: writeserial(addr,val); break;
                case 0xFE18: if (MASTER) writeadc(addr,val); break;
                case 0xFE20: writeula(addr,val); break;
                case 0xFE24: if (MASTER) write1770(addr,val); else writeula(addr,val); break;
                case 0xFE28: if (MASTER) write1770(addr,val); break;
                case 0xFE30:
                for (c=128;c<192;c++) memlook[0][c]=memlook[1][c]=&rom[(val&15)<<14]-0x8000;
                for (c=128;c<192;c++) memstat[0][c]=memstat[1][c]=swram[val&15]?1:2;
                romsel=(val&15)<<14;
                ram4k=((val&0x80) && MASTER);
                ram12k=((val&0x80) && BPLUS);
                RAMbank[0xA]=ram12k;
                if (ram4k)
                {
                        for (c=128;c<144;c++) memlook[0][c]=memlook[1][c]=ram;
                        for (c=128;c<144;c++) memstat[0][c]=memstat[1][c]=1;
                }
                if (ram12k)
                {
                        for (c=128;c<176;c++) memlook[0][c]=memlook[1][c]=ram;
                        for (c=128;c<176;c++) memstat[0][c]=memstat[1][c]=1;
                }
                break;
                case 0xFE34:
                if (BPLUS)
                {
                        acccon=val;
                        vidbank=(val&0x80)<<8;
                        if (val&0x80) RAMbank[0xC]=RAMbank[0xD]=1;
                        else          RAMbank[0xC]=RAMbank[0xD]=0;
                }
                if (MASTER)
                {
                        printf("Write %04X %02X %i %i\n",addr,val,scry,scrx);
                        acccon=val;
                        ram8k=(val&8);
                        ram20k=(val&4);
                        vidbank=(val&1)?0x8000:0;
                        if (val&2) RAMbank[0xC]=RAMbank[0xD]=1;
                        else       RAMbank[0xC]=RAMbank[0xD]=0;
                        for (c=48;c<128;c++) memlook[0][c]=ram+((ram20k)?32768:0);
                        if (ram8k)
                        {
                                for (c=192;c<224;c++) memlook[0][c]=memlook[1][c]=ram-0x3000;
                                for (c=192;c<224;c++) memstat[0][c]=memstat[1][c]=1;
                        }
                        else
                        {
                                for (c=192;c<224;c++) memlook[0][c]=memlook[1][c]=os-0xC000;
                                for (c=192;c<224;c++) memstat[0][c]=memstat[1][c]=2;
                        }
//                        printf("FE34 write %02X %04X\n",val,pc);
                }
                break;
                case 0xFE40: case 0xFE44: case 0xFE48: case 0xFE4C:
                case 0xFE50: case 0xFE54: case 0xFE58: case 0xFE5C:
                        if (opcode!=0x8c && opcode!=0x8d && opcode!=0x8e && opcode!=0x2e && opcode!=0x6e && opcode!=0xce && opcode!=0xee && pc<0x8000) printf("Write SYS VIA %04X %04X %02X %02X\n",pc,addr,val,opcode);
                writesysvia(addr,val);
                break;
                case 0xFE60: case 0xFE64: case 0xFE68: case 0xFE6C:
                case 0xFE70: case 0xFE74: case 0xFE78: case 0xFE7C:
                        if (opcode!=0x8c && opcode!=0x8d && opcode!=0x8e && opcode!=0x2e && opcode!=0x6e && opcode!=0xce && opcode!=0xee && pc<0x8000) printf("Write USR VIA %04X %04X %02X %02X\n",pc,addr,val,opcode);
                writeuservia(addr,val);
                break;
                case 0xFE80: case 0xFE84: case 0xFE88: case 0xFE8C:
                case 0xFE90: case 0xFE94: case 0xFE98: case 0xFE9C:
//                        break;
                if (!MASTER)
                {
                        if (WD1770) write1770(addr,val);
                        else        write8271(addr,val);
                }
                break;
                case 0xFEC0: case 0xFEC4: case 0xFEC8: case 0xFECC:
                case 0xFED0: case 0xFED4: case 0xFED8: case 0xFEDC:
                if (!MASTER) writeadc(addr,val);
                break;
                case 0xFEE0: case 0xFEE4: case 0xFEE8: case 0xFEEC:
                case 0xFEF0: case 0xFEF4: case 0xFEF8: case 0xFEFC:
                writetubehost(addr,val);
                break;
        }
}

uint8_t a,x,y,s;
uint16_t pc;
PREG p;
int nmi,oldnmi,nmilock,interrupt,takeint;

uint16_t pc3,oldpc,oldoldpc;
uint8_t opcode;

void reset6502()
{
        int c;
        for (c=0;c<16;c++) RAMbank[c]=0;
        for (c=0;c<128;c++)   memstat[0][c]=memstat[1][c]=1;
        for (c=128;c<256;c++) memstat[0][c]=memstat[1][c]=2;
        for (c=0;c<128;c++)   memlook[0][c]=memlook[1][c]=ram;
        if (MODELA)
        {
                for (c=0;c<64;c++) memlook[0][c]=memlook[1][c]=ram+16384;
        }
        for (c=48;c<128;c++)  memlook[1][c]=ram+32768;
        for (c=128;c<192;c++) memlook[0][c]=memlook[1][c]=rom-0x8000;
        for (c=192;c<256;c++) memlook[0][c]=memlook[1][c]=os-0xC000;
        memstat[0][0xFC]=memstat[0][0xFD]=memstat[0][0xFE]=0;
        memstat[1][0xFC]=memstat[1][0xFD]=memstat[1][0xFE]=0;

        cycles=0;
        ram4k=ram8k=ram12k=ram20k=0;
        pc=readmem(0xFFFC)|(readmem(0xFFFD)<<8);
        p.i=1;
        nmi=oldnmi=nmilock=0;
        output=0;
        tubecycle=tubecycles=0;
//        tubecycs=tubelinecycs=0;
}

void dumpregs()
{
        rpclog("6502 registers :\n");
        rpclog("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n",a,x,y,s,pc);
        rpclog("Status : %c%c%c%c%c%c\n",(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ');
        rpclog("ROMSEL %02X\n",romsel>>14);
}

inline uint16_t getsw()
{
        uint16_t temp=readmem(pc); pc++;
        temp|=(readmem(pc)<<8); pc++;
        return temp;
}
#define getw() getsw()

//#define getw() (readmem(pc)|(readmem(pc+1)<<8)); pc+=2

#define setzn(v) p.z=!(v); p.n=(v)&0x80

#define push(v) writemem(0x100+(s--),v)
#define pull()  readmem(0x100+(++s))

/*ADC/SBC temp variables*/
static uint16_t tempw;
static int tempv,hc,al,ah;
static uint8_t tempb;

#define ADC(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=(a+temp+(p.c?1:0));        \
                                p.v=(!((a^temp)&0x80)&&((a^tempw)&0x80));  \
                                a=tempw&0xFF;                  \
                                p.c=tempw&0x100;                  \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                ah=0;        \
                                tempb=a+temp+(p.c?1:0);                            \
                                if (!tempb)                                      \
                                   p.z=1;                                          \
                                al=(a&0xF)+(temp&0xF)+(p.c?1:0);                            \
                                if (al>9)                                        \
                                {                                                \
                                        al-=10;                                  \
                                        al&=0xF;                                 \
                                        ah=1;                                    \
                                }                                                \
                                ah+=((a>>4)+(temp>>4));                             \
                                if (ah&8) p.n=1;                                   \
                                p.v=(((ah << 4) ^ a) & 128) && !((a ^ temp) & 128);   \
                                p.c=0;                                             \
                                if (ah>9)                                        \
                                {                                                \
                                        p.c=1;                                     \
                                        ah-=10;                                  \
                                        ah&=0xF;                                 \
                                }                                                \
                                a=(al&0xF)|(ah<<4);                              \
                        }

#define SBC(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=a-(temp+(p.c?0:1));    \
                                tempv=(short)a-(short)(temp+(p.c?0:1));            \
                                p.v=((a^(temp+(p.c?0:1)))&(a^(uint8_t)tempv)&0x80); \
                                p.c=tempv>=0;\
                                a=tempw&0xFF;              \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                hc=0;                               \
                                p.z=p.n=0;                            \
                                if (!((a-temp)-((p.c)?0:1)))            \
                                   p.z=1;                             \
                                al=(a&15)-(temp&15)-((p.c)?0:1);      \
                                if (al&16)                           \
                                {                                   \
                                        al-=6;                      \
                                        al&=0xF;                    \
                                        hc=1;                       \
                                }                                   \
                                ah=(a>>4)-(temp>>4);                \
                                if (hc) ah--;                       \
                                if ((a-(temp+((p.c)?0:1)))&0x80)        \
                                   p.n=1;                             \
                                p.v=(((a-(temp+((p.c)?0:1)))^temp)&128)&&((a^temp)&128); \
                                p.c=1; \
                                if (ah&16)                           \
                                {                                   \
                                        p.c=0; \
                                        ah-=6;                      \
                                        ah&=0xF;                    \
                                }                                   \
                                a=(al&0xF)|((ah&0xF)<<4);                 \
                        }

#define ADCc(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=(a+temp+(p.c?1:0));        \
                                p.v=(!((a^temp)&0x80)&&((a^tempw)&0x80));  \
                                a=tempw&0xFF;                  \
                                p.c=tempw&0x100;                  \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                ah=0;        \
                                tempb=a+temp+(p.c?1:0);                            \
                                al=(a&0xF)+(temp&0xF)+(p.c?1:0);                            \
                                if (al>9)                                        \
                                {                                                \
                                        al-=10;                                  \
                                        al&=0xF;                                 \
                                        ah=1;                                    \
                                }                                                \
                                ah+=((a>>4)+(temp>>4));                             \
                                p.v=(((ah << 4) ^ a) & 128) && !((a ^ temp) & 128);   \
                                p.c=0;                                             \
                                if (ah>9)                                        \
                                {                                                \
                                        p.c=1;                                     \
                                        ah-=10;                                  \
                                        ah&=0xF;                                 \
                                }                                                \
                                a=(al&0xF)|(ah<<4);                              \
                                setzn(a); \
                                polltime(1); \
                        }

#define SBCc(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=a-(temp+(p.c?0:1));    \
                                tempv=(short)a-(short)(temp+(p.c?0:1));            \
                                p.v=((a^(temp+(p.c?0:1)))&(a^(uint8_t)tempv)&0x80); \
                                p.c=tempv>=0;\
                                a=tempw&0xFF;              \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                hc=0;                               \
                                p.z=p.n=0;                            \
                                al=(a&15)-(temp&15)-((p.c)?0:1);      \
                                if (al&16)                           \
                                {                                   \
                                        al-=6;                      \
                                        al&=0xF;                    \
                                        hc=1;                       \
                                }                                   \
                                ah=(a>>4)-(temp>>4);                \
                                if (hc) ah--;                       \
                                p.v=(((a-(temp+((p.c)?0:1)))^temp)&128)&&((a^temp)&128); \
                                p.c=1; \
                                if (ah&16)                           \
                                {                                   \
                                        p.c=0; \
                                        ah-=6;                      \
                                        ah&=0xF;                    \
                                }                                   \
                                a=(al&0xF)|((ah&0xF)<<4);                 \
                                setzn(a); \
                                polltime(1); \
                        }

int getpd() { return p.d; }

void exec6502()
{
        uint16_t addr;
        uint8_t temp;
        int tempi;
        int8_t offset;
        int c;
        cycles+=40000;
        while (cycles>0)
        {
                pc3=oldoldpc;
                oldoldpc=oldpc;
                oldpc=pc;
//                if (skipint==1) skipint=0;
                vis20k=RAMbank[pc>>12];
                opcode=readmem(pc);
                if (debugon) dodebugger();
                pc++;
                switch (opcode)
                {
                                case 0x00: /*BRK*/
//                                printf("BRK! at %04X\n",pc);
                                pc++;
                                push(pc>>8);
                                push(pc&0xFF);
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.d) temp|=8; if (p.v) temp|=0x40;
                                if (p.n) temp|=0x80;
                                push(temp);
                                pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                                p.i=1;
                                polltime(7);
                                takeint=0;
                                break;

                                case 0x01: /*ORA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x03: /*Undocumented - SLO (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x04: /*Undocumented - NOP zp*/
                                addr=readmem(pc); pc++;
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x05: /*ORA zp*/
                                addr=readmem(pc); pc++;
                                a|=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x06: /*ASL zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x07: /*Undocumented - SLO zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x08: /*PHP*/
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.i) temp|=4; if (p.d) temp|=8;
                                if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                                push(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x09: /*ORA imm*/
                                a|=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0A: /*ASL A*/
                                p.c=a&0x80;
                                a<<=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0B: /*Undocumented - ANC imm*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                p.c=p.n;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0C: /*Undocumented - NOP abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0D: /*ORA abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x0E: /*ASL abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                break;

                                case 0x0F: /*Undocumented - SLO abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x10: /*BPL*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp-1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x11: /*ORA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x13: /*Undocumented - SLO (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr+y,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x14: /*Undocumented - NOP zp,x*/
                                addr=readmem(pc); pc++;
                                readmem((addr+x)&0xFF);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x15: /*ORA zp,x*/
                                addr=readmem(pc); pc++;
                                a|=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x16: /*ASL zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x17: /*Undocumented - SLO zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x18: /*CLC*/
                                p.c=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x19: /*ORA abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x1A: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x1B: /*Undocumented - SLO abs,y*/
                                addr=getw()+y;
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x1C: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                readmem(addr);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x1D: /*ORA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x1E: /*ASL abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr);
                                writemem(addr,temp);
                                p.c=temp&0x80;
                                temp<<=1;
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                break;

                                case 0x1F: /*Undocumented - SLO abs,x*/
                                addr=getw()+x;
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                a|=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x20: /*JSR*/
                                addr=getw(); pc--;
                                push(pc>>8);
                                push(pc);
                                pc=addr;
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x21: /*AND (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a&=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x23: /*Undocumented - RLA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                a&=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x24: /*BIT zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x25: /*AND zp*/
                                addr=readmem(pc); pc++;
                                a&=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x26: /*ROL zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x27: /*Undocumented - RLA zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                a&=temp;
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x28: /*PLP*/
                                temp=pull();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                break;

                                case 0x29: /*AND*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x2A: /*ROL A*/
                                tempi=p.c;
                                p.c=a&0x80;
                                a<<=1;
                                if (tempi) a|=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x2B: /*Undocumented - ANC imm*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                p.c=p.n;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x2C: /*BIT abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                break;

                                case 0x2D: /*AND abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a&=readmem(addr);
                                setzn(a);
                                break;

                                case 0x2E: /*ROL abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                takeint=((interrupt&128) && !p.i);// takeint=1;
                                polltime(1);
                                if (!takeint) takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0x2F: /*Undocumented - RLA abs*/
                                addr=getw();  /*Found in The Hobbit*/
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                a&=temp;
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x30: /*BMI*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x31: /*AND (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x33: /*Undocumented - RLA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                polltime(1);
                                writemem(addr+y,temp);
                                a&=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x34: /*Undocumented - NOP zp,x*/
                                addr=readmem(pc); pc++;
                                readmem((addr+x)&0xFF);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x35: /*AND zp,x*/
                                addr=readmem(pc); pc++;
                                a&=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x36: /*ROL zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x37: /*Undocumented - RLA zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                a&=temp;
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x38: /*SEC*/
                                p.c=1;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x39: /*AND abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3A: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3B: /*Undocumented - RLA abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                polltime(1);
                                writemem(addr+y,temp);
                                a&=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3C: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                readmem(addr+x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3D: /*AND abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                a&=readmem(addr);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3E: /*ROL abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr);
                                writemem(addr,temp);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3F: /*Undocumented - RLA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+x);
                                polltime(1);
                                writemem(addr+x,temp);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                polltime(1);
                                writemem(addr+x,temp);
                                a&=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x40: /*RTI*/
                                temp=pull();
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                pc=pull();
                                pc|=(pull()<<8);
                                polltime(6);
//                                nmilock=0;
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x41: /*EOR (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a^=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x43: /*Undocumented - SRE (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x44: /*Undocumented - NOP zp*/
                                addr=readmem(pc); pc++;
                                readmem(addr);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x45: /*EOR zp*/
                                addr=readmem(pc); pc++;
                                a^=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x46: /*LSR zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x47: /*Undocumented - SRE zp*/
                                addr=readmem(pc); pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x48: /*PHA*/
                                push(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x49: /*EOR imm*/
                                a^=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4A: /*LSR A*/
                                p.c=a&1;
                                a>>=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4B: /*Undocumented - ASR imm*/
                                a&=readmem(pc); pc++;
                                p.c=a&1;
                                a>>=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4C: /*JMP*/
                                addr=getw();
                                pc=addr;
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4D: /*EOR abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a^=readmem(addr);
                                setzn(a);
                                break;

                                case 0x4E: /*LSR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                takeint=((interrupt&128) && !p.i);// takeint=1;
                                polltime(1);
                                if (!takeint) takeint=(interrupt && !p.i);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                break;

                                case 0x4F: /*Undocumented - SRE abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x50: /*BVC*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x51: /*EOR (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x53: /*Undocumented - SRE (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr+y,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x54: /*Undocumented - NOP zp,x*/
                                addr=readmem(pc); pc++;
                                readmem((addr+x)&0xFF);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x55: /*EOR zp,x*/
                                addr=readmem(pc); pc++;
                                a^=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x56: /*LSR zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x57: /*Undocumented - SRE zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x58: /*CLI*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                p.i=0;
                                break;

                                case 0x59: /*EOR abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5A: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5B: /*Undocumented - SRE abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr+y,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5C: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                polltime(4);
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) { readmem((addr&0xFF00)|((addr+x)&0xFF)); polltime(1); }
                                readmem(addr+x);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5D: /*EOR abs,x*/
                                addr=getw();
                                polltime(4);
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) { readmem((addr&0xFF00)|((addr+x)&0xFF)); polltime(1); }
                                addr+=x;
                                a^=readmem(addr);
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5E: /*LSR abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr);
                                writemem(addr,temp);
                                p.c=temp&1;
                                temp>>=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5F: /*Undocumented - SRE abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+x);
                                polltime(1);
                                writemem(addr+x,temp);
                                p.c=temp&1;
                                temp>>=1;
                                polltime(1);
                                writemem(addr+x,temp);
                                a^=temp;
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x60: /*RTS*/
                                pc=pull();
                                pc|=(pull()<<8);
                                pc++;
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x61: /*ADC (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                ADC(temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x63: /*Undocumented - RRA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x64: /*Undocumented - NOP zp*/
                                addr=readmem(pc); pc++;
                                readmem(addr);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x65: /*ADC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                ADC(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x66: /*ROR zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x67: /*Undocumented - RRA zp*/
                                addr=readmem(pc); pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x68: /*PLA*/
                                a=pull();
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x69: /*ADC imm*/
                                temp=readmem(pc); pc++;
                                ADC(temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6A: /*ROR A*/
                                tempi=p.c;
                                p.c=a&1;
                                a>>=1;
                                if (tempi) a|=0x80;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6B: /*Undocumented - ARR*/
                                a&=readmem(pc); pc++;
                                tempi=p.c;
                                if (p.d) /*This instruction is just as broken on a real 6502 as it is here*/
                                {
                                        p.v=((a>>6)^(a>>7)); /*V set if bit 6 changes in ROR*/
                                        a>>=1;
                                        if (tempi) a|=0x80;
                                        setzn(tempi);
                                        p.c=0;
                                        if ((a&0xF)+(a&1)>5) a=(a&0xF0)+((a&0xF)+6); /*Do broken fixup*/
                                        if ((a&0xF0)+(a&0x10)>0x50) { a+=0x60; p.c=1; }
                                }
                                else
                                {       /*V & C flag behaviours in 64doc.txt are backwards*/
                                        p.v=((a>>6)^(a>>7)); /*V set if bit 6 changes in ROR*/
                                        a>>=1;
                                        if (tempi) a|=0x80;
                                        setzn(a);
                                        p.c=a&0x40;
                                }
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6C: /*JMP ()*/
                                addr=getw();
                                if ((addr&0xFF)==0xFF) pc=readmem(addr)|(readmem(addr-0xFF)<<8);
                                else                   pc=readmem(addr)|(readmem(addr+1)<<8);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6D: /*ADC abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                ADC(temp);
                                break;

                                case 0x6E: /*ROR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                if ((interrupt&128) && !p.i) takeint=1;
                                polltime(1);
                                if (interrupt && !p.i) takeint=1;
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                break;

                                case 0x6F: /*Undocumented - RRA abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x70: /*BVS*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x71: /*ADC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADC(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x73: /*Undocumented - RRA (,y)*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x74: /*Undocumented - NOP zp,x*/
                                addr=readmem(pc); pc++;
                                readmem((addr+x)&0xFF);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x75: /*ADC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                ADC(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x76: /*ROR zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x77: /*Undocumented - RRA zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x78: /*SEI*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                p.i=1;
                                break;

                                case 0x79: /*ADC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADC(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7A: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7B: /*Undocumented - RRA abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr+y,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7D: /*ADC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                temp=readmem(addr);
                                ADC(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7E: /*ROR abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr);
                                writemem(addr,temp);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7F: /*Undocumented - RRA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+x);
                                polltime(1);
                                writemem(addr+x,temp);
                                temp>>=1;
                                if (p.c) temp|=0x80;
                                polltime(1);
                                writemem(addr+x,temp);
                                ADC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x80: /*Undocumented - NOP imm*/
                                readmem(pc); pc++;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x81: /*STA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                writemem(addr,a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x82: /*Undocumented - NOP imm*/ /*Should sometimes lock up the machine*/
                                readmem(pc); pc++;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x83: /*Undocumented - SAX (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                writemem(addr,a&x);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x84: /*STY zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x85: /*STA zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x86: /*STX zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x87: /*Undocumented - SAX zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,a&x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x88: /*DEY*/
                                y--;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x89: /*Undocumented - NOP imm*/
                                readmem(pc); pc++;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x8A: /*TXA*/
                                a=x;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x8B: /*Undocumented - ANE*/
                                temp=readmem(pc); pc++;
                                a=(a|0xEE)&x&temp; /*Internal parameter always 0xEE on BBC, always 0xFF on Electron*/
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x8C: /*STY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,y);
                                break;

                                case 0x8D: /*STA abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,a);
                                break;

                                case 0x8E: /*STX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,x);
                                break;

                                case 0x8F: /*Undocumented - SAX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,a&x);
                                break;

                                case 0x90: /*BCC*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp-1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x91: /*STA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8)+y;
                                writemem(addr,a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x93: /*Undocumented - SHA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                writemem(addr+y,a&x&((addr>>8)+1));
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x94: /*STY zp,x*/
                                addr=readmem(pc); pc++;
                                writemem((addr+x)&0xFF,y);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x95: /*STA zp,x*/
                                addr=readmem(pc); pc++;
                                writemem((addr+x)&0xFF,a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x96: /*STX zp,y*/
                                addr=readmem(pc); pc++;
                                writemem((addr+y)&0xFF,x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x97: /*Undocumented - SAX zp,y*/
                                addr=readmem(pc); pc++;
                                writemem((addr+y)&0xFF,a&x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x98: /*TYA*/
                                a=y;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x99: /*STA abs,y*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+y)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+y,a);
                                break;

                                case 0x9A: /*TXS*/
                                s=x;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x9B: /*Undocumented - SHS abs,y*/
                                addr=getw();
                                readmem((addr&0xFF00)+((addr+y)&0xFF));
                                writemem(addr+y,a&x&((addr>>8)+1));
                                s=a&x;
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x9C: /*Undocumented - SHY abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)+((addr+x)&0xFF));
                                writemem(addr+x,y&((addr>>8)+1));
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x9D: /*STA abs,x*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+x,a);
                                break;

                                case 0x9E: /*Undocumented - SHX abs,y*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+y,x&((addr>>8)+1));
                                break;

                                case 0x9F: /*Undocumented - SHA abs,y*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+y,a&x&((addr>>8)+1));
                                break;

                                case 0xA0: /*LDY imm*/
                                y=readmem(pc); pc++;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA1: /*LDA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA2: /*LDX imm*/
                                x=readmem(pc); pc++;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA3: /*Undocumented - LAX (,y)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a=x=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA4: /*LDY zp*/
                                addr=readmem(pc); pc++;
                                y=readmem(addr);
                                setzn(y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA5: /*LDA zp*/
                                addr=readmem(pc); pc++;
                                a=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA6: /*LDX zp*/
                                addr=readmem(pc); pc++;
                                x=readmem(addr);
                                setzn(x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA7: /*Undocumented - LAX zp*/
                                addr=readmem(pc); pc++;
                                a=x=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA8: /*TAY*/
                                y=a;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA9: /*LDA imm*/
                                a=readmem(pc); pc++;
                                setzn(a);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0xAA: /*TAX*/
                                x=a;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xAB: /*Undocumented - LAX*/
                                temp=readmem(pc); pc++;
                                a=x=((a|0xEE)&temp); /*WAAAAY more complicated than this, but it varies from machine to machine anyway*/
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xAC: /*LDY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                y=readmem(addr);
                                setzn(y);
                                break;

                                case 0xAD: /*LDA abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a=readmem(addr);
                                setzn(a);
                                break;

                                case 0xAE: /*LDX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                x=readmem(addr);
                                setzn(x);
                                break;

                                case 0xAF: /*LAX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a=x=readmem(addr);
                                setzn(a);
                                break;

                                case 0xB0: /*BCS*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB1: /*LDA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB3: /*LAX (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=x=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB4: /*LDY zp,x*/
                                addr=readmem(pc); pc++;
                                y=readmem((addr+x)&0xFF);
                                setzn(y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB5: /*LDA zp,x*/
                                addr=readmem(pc); pc++;
                                a=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB6: /*LDX zp,y*/
                                addr=readmem(pc); pc++;
                                x=readmem((addr+y)&0xFF);
                                setzn(x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB7: /*LAX zp,y*/
                                addr=readmem(pc); pc++;
                                a=x=readmem((addr+y)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB8: /*CLV*/
                                p.v=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB9: /*LDA abs,y*/
                                addr=getw();
                                polltime(3);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBA: /*TSX*/
                                x=s;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBB: /*Undocumented - LAS abs,y*/
                                addr=getw();
                                polltime(3);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=x=s=s&readmem(addr+y); /*No, really!*/
                                setzn(a);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBC: /*LDY abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                y=readmem(addr+x);
                                setzn(y);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBD: /*LDA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBE: /*LDX abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                x=readmem(addr+y);
                                setzn(x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBF: /*LAX abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=x=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC0: /*CPY imm*/
                                temp=readmem(pc); pc++;
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC1: /*CMP (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC2: /*Undocumented - NOP imm*/ /*Should sometimes lock up the machine*/
                                readmem(pc); pc++;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC3: /*Undocumented - DCP (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp--;
                                polltime(1);
                                writemem(addr,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC4: /*CPY zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC5: /*CMP zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC6: /*DEC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC7: /*Undocumented - DCP zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC8: /*INY*/
                                y++;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC9: /*CMP imm*/
                                temp=readmem(pc); pc++;
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xCA: /*DEX*/
                                x--;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xCB: /*Undocumented - SBX imm*/
                                temp=readmem(pc); pc++;
                                setzn((a&x)-temp);
                                p.c=((a&x)>=temp);
                                x=(a&x)-temp;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xCC: /*CPY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(y-temp);
                                p.c=(y>=temp);
                                break;

                                case 0xCD: /*CMP abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                break;

                                case 0xCE: /*DEC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)-1;
                                polltime(1);
//                                takeint=(interrupt && !p.i);
                                writemem(addr,temp+1);
                                takeint=((interrupt&128) && !p.i);// takeint=1;
                                polltime(1);
                                if (!takeint) takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xCF: /*Undocumented - DCP abs*/
                                addr=getw();
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD0: /*BNE*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD1: /*CMP (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD3: /*Undocumented - DCP (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr)-1;
                                polltime(1);
                                writemem(addr,temp+1);
                                polltime(1);
                                writemem(addr,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD4: /*Undocumented - NOP zp,x*/
                                addr=readmem(pc); pc++;
                                readmem((addr+x)&0xFF);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD5: /*CMP zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD6: /*DEC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF)-1;
                                setzn(temp);
                                writemem((addr+x)&0xFF,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD7: /*Undocumented - DCP zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD8: /*CLD*/
                                p.d=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD9: /*CMP abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDA: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDB: /*Undocumented - DCP abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+y)-1;
                                polltime(1);
                                writemem(addr+y,temp+1);
                                polltime(1);
                                writemem(addr+y,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDC: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                readmem(addr+x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDD: /*CMP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDE: /*DEC abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr)-1;
                                writemem(addr,temp+1);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDF: /*Undocumented - DCP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                polltime(4);
                                temp=readmem(addr+x)-1;
                                polltime(1);
                                writemem(addr+x,temp+1);
                                polltime(1);
                                writemem(addr+x,temp);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE0: /*CPX imm*/
                                temp=readmem(pc); pc++;
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE1: /*SBC (,x)*/ /*This was missed out of every B-em version since 0.6 as it was never used!*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                SBC(temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE2: /*Undocumented - NOP imm*/ /*Should sometimes lock up the machine*/
                                readmem(pc); pc++;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE3: /*Undocumented - ISB (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE4: /*CPX zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE5: /*SBC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                SBC(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE6: /*INC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr)+1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE7: /*Undocumented - ISB zp*/
                                addr=readmem(pc); pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE8: /*INX*/
                                x++;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE9: /*SBC imm*/
                                temp=readmem(pc); pc++;
                                SBC(temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xEA: /*NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xEB: /*Undocumented - SBC imm*/
                                temp=readmem(pc); pc++;
                                SBC(temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xEC: /*CPX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(x-temp);
                                p.c=(x>=temp);
                                break;

                                case 0xED: /*SBC abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                SBC(temp);
                                break;

                                case 0xEE: /*INC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)+1;
                                polltime(1);
                                writemem(addr,temp-1);
                                if ((interrupt&128) && !p.i) takeint=1;
                                polltime(1);
                                if (interrupt && !p.i) takeint=1;
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xEF: /*Undocumented - ISB abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF0: /*BEQ*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
//                                        if (pc<0x8000) printf("%04X %02X\n",(pc&0xFF00)^((pc+offset)&0xFF00),temp);
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF1: /*SBC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBC(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF3: /*Undocumented - ISB (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                polltime(5);
                                temp=readmem(addr+y);
                                polltime(1);
                                writemem(addr+y,temp);
                                temp++;
                                polltime(1);
                                writemem(addr+y,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF4: /*Undocumented - NOP zpx*/
                                addr=readmem(pc); pc++;
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF5: /*SBC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                SBC(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF6: /*INC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF)+1;
                                writemem((addr+x)&0xFF,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF7: /*Undocumented - ISB zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                polltime(3);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF8: /*SED*/
                                p.d=1;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF9: /*SBC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBC(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFA: /*Undocumented - NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFB: /*Undocumented - ISB abs,y*/
                                addr=getw()+y;
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFC: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                readmem(addr+x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFD: /*SBC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                SBC(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFE: /*INC abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr)+1;
                                writemem(addr,temp-1);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFF: /*Undocumented - ISB abs,x*/
                                addr=getw()+x;
                                polltime(5);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                temp++;
                                polltime(1);
                                writemem(addr,temp);
                                SBC(temp);
                                takeint=(interrupt && !p.i);
                                break;

#if 0
                                case 0x02: /*TFS opcode - OSFSC*/
/*                                if (uefena)
                                {
                                        pc+=2;
                                }
                                else
                                {*/
                                printf("OSFSC!\n");
                                        c=OSFSC();
                                        if (c==6||c==8||c==0||c==5)
                                        {
                                                pc=pull();
                                                pc+=(pull()<<8)+1;
                                        }
                                        if (c==0x80)
                                        {
                                                temp=ram[pc++];
                                                p.c=(a>=temp);
                                                setzn(a-temp);
                                        }
//                                }*/
//                                pc+=2;
                                break;

                                case 0x92: /*TFS opcode - OSFILE*/
/*                                if (uefena)
                                {
                                        pc+=2;
                                }
                                else
                                {*/
                                printf("OSFILE!\n");
                                        a=OSFILE();
                                        if (a==0x80)
                                        {
                                                push(a);
                                        }
                                        else if (a!=0x7F)
                                        {
                                                pc=pull();
                                                pc+=(pull()<<8)+1;
                                        }
//                                }*/
//                                pc+=2;
                                break;
#endif
                                default:   /*Halt!*/
                                pc--;      /*PC never moves on*/
                                takeint=0; /*Interrupts never occur*/
                                oldnmi=1;  /*NMIs never occur*/
                                polltime(100000); /*Actually lasts forever, but the above code keeps executing HLT forever*/
                                break;
//                                printf("Found bad opcode %02X\n",opcode);
/*                                switch (opcode&0xF)
                                {
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
                                        pc+=2;
                                        break;
                                }*/
                }

//                output=(pc<0x172B && pc>=0x167F);
/*                if (pc==0x7112)
                {
                        output=1;
                        dumpregs();
                        dumpram();
                        exit(-1);
                }*/
/*                if (pc==0x6041) output=1;
                if (pc==0x6195)
                {
                        dumpregs();
                        dumpram();
                        exit(-1);
                }*/
//                if (pc==0xCD7A) printf("CD7A from %04X\n",oldpc);
//                if (pc==0xC565) printf("C565 from %04X\n",oldpc);
                if (output)
                {
//                        #undef printf
                        rpclog("A=%02X X=%02X Y=%02X S=%02X PC=%04X %c%c%c%c%c%c op=%02X %02X%02X\n",a,x,y,s,pc,(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ',opcode,ram[0x78],ram[0x79]);
                }
//                if (pc==0x400) output=1;
                if (timetolive)
                {
                        timetolive--;
                        if (!timetolive) output=0;
                }
                if (takeint)
                {
                        interrupt&=~128;
                        takeint=0;
//                        skipint=0;
                        push(pc>>8);
                        push(pc&0xFF);
                        temp=0x20;
                        if (p.c) temp|=1; if (p.z) temp|=2;
                        if (p.i) temp|=4; if (p.d) temp|=8;
                        if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                        push(temp);
                        pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                        p.i=1;
                        polltime(7);
//                        printf("INT\n");
                }
                interrupt&=~128;

                if (otherstuffcount<=0)
                {
                        otherstuffcount+=128;
                        logvols();
                        if (!tapelcount)
                        {
                                pollacia();
                                tapelcount=tapellatch;
                        }
                        tapelcount--;
                        if (motorspin)
                        {
                                motorspin--;
                                if (!motorspin) fdcspindown();
                        }
                }
                if (tubeexec && tubecycle)
                {
                        tubecycles+=(tubecycle<<tubeshift);
                        if (tubecycles>3) tubeexec();
                        tubecycle=0;
                }

                if (nmi && !oldnmi)
                {
                        push(pc>>8);
                        push(pc&0xFF);
                        temp=0x20;
                        if (p.c) temp|=1; if (p.z) temp|=2;
                        if (p.i) temp|=4; if (p.d) temp|=8;
                        if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                        push(temp);
                        pc=readmem(0xFFFA)|(readmem(0xFFFB)<<8);
                        p.i=1;
                        polltime(7);
                        nmi=0;
//                        nmilock=1;
//                        printf("NMI\n");
                }
                oldnmi=nmi;
        }
}

void exec65c02()
{
        uint16_t addr;
        uint8_t temp;
        int tempi;
        int8_t offset;
        int c;
        cycles+=40000;
        while (cycles>0)
        {
                pc3=oldoldpc;
                oldoldpc=oldpc;
                oldpc=pc;
                if (debugon) dodebugger();
//                if (skipint==1) skipint=0;
                vis20k=RAMbank[pc>>12];
                opcode=readmem(pc); pc++;
                switch (opcode)
                {
                                case 0x00: /*BRK*/
//                                printf("BRK! %04X\n",pc);
//                                if (output==2)
//                                {
//                                        rpclog("BRK! %04X %04X %04X %04X\n",pc,oldpc,oldoldpc,pc3);
//                                        dumpregs();
//                                        if (output) exit(-1);
//                                        exit(-1);
//                                }
                                pc++;
                                push(pc>>8);
                                push(pc&0xFF);
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.d) temp|=8; if (p.v) temp|=0x40;
                                if (p.n) temp|=0x80;
                                push(temp);
                                pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                                p.i=1;
                                p.d=0;
                                polltime(7);
                                takeint=0;
                                break;

                                case 0x01: /*ORA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x04: /*TSB zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.z=!(temp&a);
                                temp|=a;
                                writemem(addr,temp);
                                polltime(5);
                                break;

                                case 0x05: /*ORA zp*/
                                addr=readmem(pc); pc++;
                                a|=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x06: /*ASL zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x08: /*PHP*/
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.i) temp|=4; if (p.d) temp|=8;
                                if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                                push(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x09: /*ORA imm*/
                                a|=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0A: /*ASL A*/
                                p.c=a&0x80;
                                a<<=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0B: /*ANC imm*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                p.c=p.n;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x0C: /*TSB abs*/
                                addr=getw();
                                temp=readmem(addr);
                                p.z=!(temp&a);
                                temp|=a;
                                writemem(addr,temp);
                                polltime(6);
                                break;

                                case 0x0D: /*ORA abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x0E: /*ASL abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                readmem(addr);
                                polltime(1);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                break;

                                case 0x10: /*BPL*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp-1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x11: /*ORA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x12: /*ORA ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                a|=readmem(addr);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x14: /*TRB zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.z=!(temp&a);
                                temp&=~a;
                                writemem(addr,temp);
                                polltime(5);
                                break;

                                case 0x15: /*ORA zp,x*/
                                addr=readmem(pc); pc++;
                                a|=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x16: /*ASL zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x18: /*CLC*/
                                p.c=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x19: /*ORA abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x1A: /*INC A*/
                                a++;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x1C: /*TRB abs*/
                                addr=getw();
                                temp=readmem(addr);
                                p.z=!(temp&a);
                                temp&=~a;
                                writemem(addr,temp);
                                polltime(6);
                                break;

                                case 0x1D: /*ORA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a|=readmem(addr);
                                setzn(a);
                                break;

                                case 0x1E: /*ASL abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                tempw=((addr&0xFF00)^((addr+x)&0xFF00))?1:0;
                                addr+=x;
                                temp=readmem(addr);
                                readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6+tempw);
                                break;

                                case 0x20: /*JSR*/
                                addr=getw(); pc--;
                                push(pc>>8);
                                push(pc);
                                pc=addr;
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x21: /*AND (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a&=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x24: /*BIT zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x25: /*AND zp*/
                                addr=readmem(pc); pc++;
                                a&=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x26: /*ROL zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x28: /*PLP*/
                                temp=pull();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                break;

                                case 0x29: /*AND*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x2A: /*ROL A*/
                                tempi=p.c;
                                p.c=a&0x80;
                                a<<=1;
                                if (tempi) a|=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x2C: /*BIT abs*/
                                addr=getw();
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                break;

                                case 0x2D: /*AND abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a&=readmem(addr);
                                setzn(a);
                                break;

                                case 0x2E: /*ROL abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0x30: /*BMI*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x31: /*AND (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x32: /*AND ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                a&=readmem(addr);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x35: /*AND zp,x*/
                                addr=readmem(pc); pc++;
                                a&=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x36: /*ROL zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x38: /*SEC*/
                                p.c=1;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x39: /*AND abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3A: /*DEC A*/
                                a--;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x3C: /*BIT abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                polltime(4);
                                break;

                                case 0x3D: /*AND abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                a&=readmem(addr);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x3E: /*ROL abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                tempw=((addr&0xFF00)^((addr+x)&0xFF00))?1:0;
                                addr+=x;
                                temp=readmem(addr);
                                readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6+tempw);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x40: /*RTI*/
                                temp=pull();
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                pc=pull();
                                pc|=(pull()<<8);
                                polltime(6);
//                                nmilock=0;
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x41: /*EOR (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a^=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x45: /*EOR zp*/
                                addr=readmem(pc); pc++;
                                a^=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x46: /*LSR zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x48: /*PHA*/
                                push(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x49: /*EOR imm*/
                                a^=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4A: /*LSR A*/
                                p.c=a&1;
                                a>>=1;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4C: /*JMP*/
                                addr=getw();
                                pc=addr;
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x4D: /*EOR abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a^=readmem(addr);
                                setzn(a);
                                break;

                                case 0x4E: /*LSR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                readmem(addr);
                                polltime(1);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                break;

                                case 0x50: /*BVC*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x51: /*EOR (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x52: /*EOR ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                a^=readmem(addr);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x55: /*EOR zp,x*/
                                addr=readmem(pc); pc++;
                                a^=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x56: /*LSR zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x58: /*CLI*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                p.i=0;
                                break;

                                case 0x59: /*EOR abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5A: /*PHY*/
                                push(y);
                                polltime(3);
                                break;

                                case 0x5D: /*EOR abs,x*/
                                addr=getw();
                                polltime(4);
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) { readmem((addr&0xFF00)|((addr+x)&0xFF)); polltime(1); }
                                addr+=x;
                                a^=readmem(addr);
                                setzn(a);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x5E: /*LSR abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                tempw=((addr&0xFF00)^((addr+x)&0xFF00))?1:0;
                                addr+=x;
                                temp=readmem(addr);
                                readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6+tempw);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x60: /*RTS*/
                                pc=pull();
                                pc|=(pull()<<8);
                                pc++;
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x61: /*ADC (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                ADCc(temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x64: /*STZ zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,0);
                                polltime(3);
                                break;

                                case 0x65: /*ADC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                ADCc(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x66: /*ROR zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x68: /*PLA*/
                                a=pull();
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x69: /*ADC imm*/
                                temp=readmem(pc); pc++;
                                ADCc(temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6A: /*ROR A*/
                                tempi=p.c;
                                p.c=a&1;
                                a>>=1;
                                if (tempi) a|=0x80;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6C: /*JMP ()*/
                                addr=getw();
                                pc=readmem(addr)|(readmem(addr+1)<<8);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x6D: /*ADC abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                ADCc(temp);
                                break;

                                case 0x6E: /*ROR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                readmem(addr);
                                if ((interrupt&128) && !p.i) takeint=1;
                                polltime(1);
                                if (interrupt && !p.i) takeint=1;
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                break;

                                case 0x70: /*BVS*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x71: /*ADC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADCc(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x72: /*ADC ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                ADCc(temp);
                                polltime(5);
                                break;

                                case 0x74: /*STZ zp,x*/
                                addr=readmem(pc); pc++;
                                writemem((addr+x)&0xFF,0);
                                polltime(3);
                                break;

                                case 0x75: /*ADC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                ADCc(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x76: /*ROR zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x78: /*SEI*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                p.i=1;
                                break;

                                case 0x79: /*ADC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADCc(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7A: /*PLY*/
                                y=pull();
                                setzn(y);
                                polltime(4);
                                break;

                                case 0x7C: /*JMP (,x)*/
                                addr=getw(); addr+=x;
                                pc=readmem(addr)|(readmem(addr+1)<<8);
                                polltime(6);
                                break;

                                case 0x7D: /*ADC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                addr+=x;
                                temp=readmem(addr);
                                ADCc(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x7E: /*ROR abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                tempw=((addr&0xFF00)^((addr+x)&0xFF00))?1:0;
                                addr+=x;
                                temp=readmem(addr);
                                readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6+tempw);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x80: /*BRA*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=3;
                                if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                pc+=offset;
                                polltime(temp);
                                break;

                                case 0x81: /*STA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                writemem(addr,a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x84: /*STY zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x85: /*STA zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x86: /*STX zp*/
                                addr=readmem(pc); pc++;
                                writemem(addr,x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x88: /*DEY*/
                                y--;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x89: /*BIT imm*/
                                temp=readmem(pc); pc++;
                                p.z=!(a&temp);
                                polltime(2);
                                break;

                                case 0x8A: /*TXA*/
                                a=x;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x8C: /*STY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,y);
                                break;

                                case 0x8D: /*STA abs*/
                                addr=getw();
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                writemem(addr,a);
                                break;

                                case 0x8E: /*STX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,x);
                                break;

                                case 0x90: /*BCC*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp-1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0x91: /*STA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8)+y;
                                writemem(addr,a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x92: /*STA ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                writemem(addr,a);
                                polltime(6);
                                break;

                                case 0x94: /*STY zp,x*/
                                addr=readmem(pc); pc++;
                                writemem((addr+x)&0xFF,y);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x95: /*STA zp,x*/
                                addr=readmem(pc); pc++;
                                writemem((addr+x)&0xFF,a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x96: /*STX zp,y*/
                                addr=readmem(pc); pc++;
                                writemem((addr+y)&0xFF,x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x98: /*TYA*/
                                a=y;
                                setzn(a);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x99: /*STA abs,y*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+y)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+y,a);
                                break;

                                case 0x9A: /*TXS*/
                                s=x;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0x9C: /*STZ abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                writemem(addr,0);
                                break;

                                case 0x9D: /*STA abs,x*/
                                addr=getw();
                                polltime(4);
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr+x,a);
                                break;

                                case 0x9E: /*STZ abs,x*/
                                addr=getw(); addr+=x;
                                polltime(4);
                                writemem(addr,0);
                                polltime(1);
                                break;

                                case 0xA0: /*LDY imm*/
                                y=readmem(pc); pc++;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA1: /*LDA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a=readmem(addr);
                                setzn(a);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA2: /*LDX imm*/
                                x=readmem(pc); pc++;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA4: /*LDY zp*/
                                addr=readmem(pc); pc++;
                                y=readmem(addr);
                                setzn(y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA5: /*LDA zp*/
                                addr=readmem(pc); pc++;
                                a=readmem(addr);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA6: /*LDX zp*/
                                addr=readmem(pc); pc++;
                                x=readmem(addr);
                                setzn(x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA8: /*TAY*/
                                y=a;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xA9: /*LDA imm*/
                                a=readmem(pc); pc++;
                                setzn(a);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                polltime(1);
                                break;

                                case 0xAA: /*TAX*/
                                x=a;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xAC: /*LDY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                y=readmem(addr);
                                setzn(y);
                                break;

                                case 0xAD: /*LDA abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                a=readmem(addr);
                                setzn(a);
                                break;

                                case 0xAE: /*LDX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                x=readmem(addr);
                                setzn(x);
                                break;

                                case 0xB0: /*BCS*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB1: /*LDA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB2: /*LDA ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                a=readmem(addr);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0xB4: /*LDY zp,x*/
                                addr=readmem(pc); pc++;
                                y=readmem((addr+x)&0xFF);
                                setzn(y);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB5: /*LDA zp,x*/
                                addr=readmem(pc); pc++;
                                a=readmem((addr+x)&0xFF);
                                setzn(a);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB6: /*LDX zp,y*/
                                addr=readmem(pc); pc++;
                                x=readmem((addr+y)&0xFF);
                                setzn(x);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB8: /*CLV*/
                                p.v=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xB9: /*LDA abs,y*/
                                addr=getw();
                                polltime(3);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBA: /*TSX*/
                                x=s;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBC: /*LDY abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                y=readmem(addr+x);
                                setzn(y);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBD: /*LDA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xBE: /*LDX abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                x=readmem(addr+y);
                                setzn(x);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC0: /*CPY imm*/
                                temp=readmem(pc); pc++;
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC1: /*CMP (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC4: /*CPY zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC5: /*CMP zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC6: /*DEC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC8: /*INY*/
                                y++;
                                setzn(y);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xC9: /*CMP imm*/
                                temp=readmem(pc); pc++;
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xCA: /*DEX*/
                                x--;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xCB: /*WAI*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                if (!takeint) pc--;
                                break;

                                case 0xCC: /*CPY abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(y-temp);
                                p.c=(y>=temp);
                                break;

                                case 0xCD: /*CMP abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                break;

                                case 0xCE: /*DEC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)-1;
                                polltime(1);
//                                takeint=(interrupt && !p.i);
                                readmem(addr);
                                takeint=((interrupt&128) && !p.i);// takeint=1;
                                polltime(1);
                                if (!takeint) takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xD0: /*BNE*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (!p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD1: /*CMP (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD2: /*CMP ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                break;

                                case 0xD5: /*CMP zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD6: /*DEC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF)-1;
                                writemem((addr+x)&0xFF,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD8: /*CLD*/
                                p.d=0;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xD9: /*CMP abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDA: /*PHX*/
                                push(x);
                                polltime(3);
                                break;

                                case 0xDD: /*CMP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xDE: /*DEC abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr)-1;
                                readmem(addr);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE0: /*CPX imm*/
                                temp=readmem(pc); pc++;
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE1: /*SBC (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                SBC(temp);
                                polltime(6);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE4: /*CPX zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE5: /*SBC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr);
                                SBCc(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE6: /*INC zp*/
                                addr=readmem(pc); pc++;
                                temp=readmem(addr)+1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE8: /*INX*/
                                x++;
                                setzn(x);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xE9: /*SBC imm*/
                                temp=readmem(pc); pc++;
                                SBCc(temp);
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xEA: /*NOP*/
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xEC: /*CPX abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                setzn(x-temp);
                                p.c=(x>=temp);
                                break;

                                case 0xED: /*SBC abs*/
                                addr=getw();
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                temp=readmem(addr);
                                SBCc(temp);
                                break;

                                case 0xEE: /*INC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)+1;
                                polltime(1);
                                readmem(addr);
                                polltime(1);
                                takeint=(interrupt && !p.i);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xF0: /*BEQ*/
                                offset=(int8_t)readmem(pc); pc++;
                                temp=2;
                                if (p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
//                                        if (pc<0x8000) printf("%04X %02X\n",(pc&0xFF00)^((pc+offset)&0xFF00),temp);
                                        pc+=offset;
                                }
                                polltime(temp);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF1: /*SBC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBCc(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF2: /*SBC ()*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                SBCc(temp);
                                polltime(5);
                                break;

                                case 0xF5: /*SBC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF);
                                SBCc(temp);
                                polltime(3);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF6: /*INC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=readmem((addr+x)&0xFF)+1;
                                writemem((addr+x)&0xFF,temp);
                                setzn(temp);
                                polltime(5);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF8: /*SED*/
                                p.d=1;
                                polltime(2);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xF9: /*SBC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBCc(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFA: /*PLX*/
                                x=pull();
                                setzn(x);
                                polltime(4);
                                break;

                                case 0xFD: /*SBC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                SBCc(temp);
                                polltime(4);
                                takeint=(interrupt && !p.i);
                                break;

                                case 0xFE: /*INC abs,x*/
                                addr=getw();
                                readmem((addr&0xFF00)|((addr+x)&0xFF));
                                addr+=x;
                                temp=readmem(addr)+1;
                                readmem(addr);
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                takeint=(interrupt && !p.i);
                                break;

                                default:
                                switch (opcode&0xF)
                                {
                                        case 2: pc++; polltime(2); break;
                                        case 3: case 0xB: polltime(1); break;
                                        case 4: pc++; if (opcode==0x44) { polltime(3); } else { polltime(4); } break;
                                        case 0xC: pc+=2; if (opcode==0x5C) { polltime(8); } else { polltime(4); } break;
                                        case 7: case 0xF: pc++; polltime(2); break;
                                }
                                takeint=(interrupt && !p.i);
                                break;

                                printf("Found bad opcode %02X\n",opcode);
                                dumpregs();
                                dumpram();
                                exit(-1);
                }
/*                if (pc==0x6A6) printf("A=%i %02X\n",a,a);
                if (pc==0xAEA1)
                {
                        output++;
                        printf("AEA1 %i\n",output);
                }*/
/*                if (pc==0x400)
                {
                         output=2;
                         dumpregs();
                         dumpram();
                         exit(-1);
                        }
                if (pc==0x605)
                {
                        dumpregs();
                        dumpram();
                        exit(-1);
                }*/
/*                if (pc==0x8E70 && (romsel>>14)<8)
                {
                        output=2;
//                        dumpregs();
//                        dumpram();
//                        exit(-1);
                }*/
//if (pc==0x63C) output=2;
//                if (pc==0x5D2) output++;
/*                if (output==2)
                {
                        dumpregs();
                        exit(-1);
                }*/
/*                if (pc==0x2740)
                {
                        if (x==7) output=1;
                        printf("RUNSP %i\n",x);
                }
                if (pc==0x27A3) output=0;
//                if (pc==0x2D04) output=1;
//                if (pc<0x2D04 || pc>0x2D38) output=0;*/
                if (output)
                {
                        rpclog("A=%02X X=%02X Y=%02X S=%02X PC=%04X %c%c%c%c%c%c op=%02X %02X%02X %02X%02X %02X %i %08X\n",a,x,y,s,pc,(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ',opcode,ram[0x03],ram[0x02],ram[0x7F],ram[0x7E],ram[0x7D],mainins,memlook[pc>>8]);
                }
                mainins++;
                if (timetolive)
                {
                        timetolive--;
                        if (!timetolive) output=0;
                }
                if (takeint)
                {
                        interrupt&=~128;
                        takeint=0;
//                        skipint=0;
                        push(pc>>8);
                        push(pc&0xFF);
                        temp=0x20;
                        if (p.c) temp|=1; if (p.z) temp|=2;
                        if (p.i) temp|=4; if (p.d) temp|=8;
                        if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                        push(temp);
                        pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                        p.i=1;
                        p.d=0;
                        polltime(7);
//                        rpclog("INT\n");
//                        printf("Interrupt - %02X %02X\n",sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier);
//                        printf("INT\n");
                }
                interrupt&=~128;
                if (tubeexec && tubecycle)
                {
//                        rpclog("tubeexec %i %i %i\n",tubecycles,tubecycle,tubeshift);
                        tubecycles+=(tubecycle<<tubeshift);
                        if (tubecycles>3) tubeexec();
                        tubecycle=0;
                }

                if (otherstuffcount<=0)
                {
                        otherstuffcount+=128;
//                        sidline();
                        logvols();
                        if (!tapelcount)
                        {
                                pollacia();
                                tapelcount=tapellatch;
                        }
                        tapelcount--;
                        if (motorspin)
                        {
                                motorspin--;
                                if (!motorspin) fdcspindown();
                        }
                }
                if (nmi && !oldnmi)
                {
                        push(pc>>8);
                        push(pc&0xFF);
                        temp=0x20;
                        if (p.c) temp|=1; if (p.z) temp|=2;
                        if (p.i) temp|=4; if (p.d) temp|=8;
                        if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                        push(temp);
                        pc=readmem(0xFFFA)|(readmem(0xFFFB)<<8);
                        p.i=1;
                        polltime(7);
                        nmi=0;
//                        nmilock=1;
                        p.d=0;
//                        rpclog("NMI\n");
//                        printf("NMI\n");
                }
                oldnmi=nmi;
        }
}
