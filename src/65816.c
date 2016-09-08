/*B-em v2.2 by Tom Walker
  65816 parasite CPU emulation
  Originally from Snem, with some bugfixes*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"
#include "tube.h"
#include "65816.h"

#define printf bem_debug

static uint8_t *w65816ram,*w65816rom;
/*Registers*/
typedef union
{
        uint16_t w;
        struct
        {
                uint8_t l,h;
        } b;
} reg;

static reg w65816a,w65816x,w65816y,w65816s;
static uint32_t pbr,dbr;
static uint16_t w65816pc,dp;

static int wins=0;

static struct
{
        int c,z,i,d,b,v,n,m,ex,e; /*X renamed to EX due to #define conflict*/
} w65816p;

static int inwai;
/*Opcode table*/
static void (*opcodes[256][5])();

/*CPU modes : 0 = X1M1
              1 = X1M0
              2 = X0M1
              3 = X0M0
              4 = emulation*/
static int cpumode;

/*Current opcode*/
static uint8_t w65816opcode;

#define a w65816a
#define x w65816x
#define y w65816y
#define s w65816s
#define pc w65816pc
#define p w65816p

#define cycles tubecycles
#define opcode w65816opcode

static int def=1,divider=0,banking=0,banknum=0;
static uint32_t w65816mask=0xFFFF;

uint8_t readmem65816(uint32_t a)
{
        uint8_t temp;
        a&=w65816mask;
        cycles--;
        if ((a&~7)==0xFEF8)
        {
                temp=tube_parasite_read(a);
/*                if (a==0xFEFB && temp==0xD)
                {
                        dtimes++;
                        if (dtimes==2) toutput=1;
                }*/
                //bem_debugf("Read TUBE  %04X %02X %04X\n",a,temp,pc);
                return temp;
        }
        if ((a&0x78000)==0x8000 && (def || (banking&8))) return w65816rom[a&0x7FFF];
        if ((a&0x78000)==0x4000 && !def && (banking&1)) return w65816ram[(a&0x3FFF)|((banknum&7)<<14)];
        if ((a&0x78000)==0x8000 && !def && (banking&2)) return w65816ram[(a&0x3FFF)|(((banknum>>3)&7)<<14)];
        return w65816ram[a];
}

static uint16_t readmemw65816(uint32_t a)
{
        a&=w65816mask;
        return readmem65816(a)|(readmem65816(a+1)<<8);
//        cycles-=2;
//        bem_debugf("Reading %08X %i %08X %08X\n",a,tuberomin,w65816rom,w65816ram);
//        if ((a&~0xFFF)==0xF000 && tuberomin) return w65816rom[a&0xFFF]|(w65816rom[(a+1)&0xFFF]<<8);
//        if (a<0x10000) return w65816ram[a]|(w65816ram[a+1]<<8);
}

int endtimeslice;
void writemem65816(uint32_t a, uint8_t v)
{
        a&=w65816mask;
//        if (a==0xFF) bem_debugf("Write 00FF %02X %04X %i\n",v,pc,wins);
        cycles--;
        if ((a&~7)==0xFEF0)
        {
//                printf("Write control %04X %02X\n",a,v);
                switch (v&7)
                {
                        case 0: case 1: def=v&1; /*bem_debugf("Default now %i\n",def); */break;
                        case 2: case 3: divider=(divider>>1)|((v&1)<<3); /*bem_debugf("Divider now %i\n",divider);*/ break;
                        case 4: case 5: banking=(banking>>1)|((v&1)<<3); /*bem_debugf("Banking now %i\n",banking);*/ break;
                        case 6: case 7: banknum=(banknum>>1)|((v&1)<<5); /*bem_debugf("Banknum now %i\n",banknum);*/ break;
                }
                if (def || !(banking&4)) w65816mask=0xFFFF;
                else                     w65816mask=0x7FFFF;
//                bem_debugf("Mask now %08X\n",w65816mask);
                return;
        }
        if ((a&~7)==0xFEF8)
        {
//                bem_debugf("Write TUBE %04X %02X %04X\n",a,v,pc);
                tube_parasite_write(a,v);
                endtimeslice=1;
                return;
        }
        if ((a&0x78000)==0x4000 && !def && (banking&1)) { w65816ram[(a&0x3FFF)|((banknum&7)<<14)]=v; return; }
        if ((a&0x78000)==0x8000 && !def && (banking&2)) { w65816ram[(a&0x3FFF)|(((banknum>>3)&7)<<14)]=v; return; }
//        if (a>0xF000) bem_debugf("Write %04X %02X %04X\n",a,v,pc);
//        if (a==0xF7FF && v==0xFF) toutput=1;
        w65816ram[a]=v;
}

static void writememw65816(uint32_t a, uint16_t v)
{
        a&=w65816mask;
        writemem65816(a,v);
        writemem65816(a+1,v>>8);
//        cycles-=2;
//        if (a<0x10000)
//        {
//                w65816ram[a]=v&0xFF;
//                w65816ram[a+1]=v>>8;
//        }
}

#define readmem(a)     readmem65816(a)
#define readmemw(a)    readmemw65816(a)
#define writemem(a,v)  writemem65816(a,v)
#define writememw(a,v) writememw65816(a,v)

#define clockspc(c)

static void updatecpumode();
static int inwai=0;
/*Temporary variables*/
static uint32_t addr;

/*Addressing modes*/
static uint32_t absolute()
{
        uint32_t temp=readmemw(pbr|pc); pc+=2;
        return temp|dbr;
}

static uint32_t absolutex()
{
        uint32_t temp=(readmemw(pbr|pc))+x.w+dbr; pc+=2;
//        if ((temp&0xFFFF)>0x2200 && (temp&0xFFFF)<0x8000) printf("ABSX %04X %06X\n",x.w,temp);
//        if (output) printf("ABSX 0000,%04X - %06X\n",x.w,temp);
//        if (output) printf("Addr %06X\n",temp);
        return temp;
}

static uint32_t absolutey()
{
        uint32_t temp=(readmemw(pbr|pc))+y.w+dbr; pc+=2;
        return temp;
}

static uint32_t absolutelong()
{
        uint32_t temp=readmemw(pbr|pc); pc+=2;
        temp|=(readmem(pbr|pc)<<16); pc++;
        return temp;
}

static uint32_t absolutelongx()
{
        uint32_t temp=(readmemw(pbr|pc))+x.w; pc+=2;
        temp+=(readmem(pbr|pc)<<16); pc++;
//        printf("abslx %06X %04X\n",temp,x.w);
        return temp;
}

static uint32_t zeropage() /*It's actually direct page, but I'm used to calling it zero page*/
{
        uint32_t temp=readmem(pbr|pc); pc++;
        temp+=dp;
        if (dp&0xFF) { cycles--; clockspc(6); }
        return temp&0xFFFF;
}

static uint32_t zeropagex()
{
        uint32_t temp=readmem(pbr|pc)+x.w; pc++;
        if (p.e) temp&=0xFF;
        temp+=dp;
        if (dp&0xFF) { cycles--; clockspc(6); }
        return temp&0xFFFF;
}

static uint32_t zeropagey()
{
        uint32_t temp=readmem(pbr|pc)+y.w; pc++;
        if (p.e) temp&=0xFF;
        temp+=dp;
        if (dp&0xFF) { cycles--; clockspc(6); }
        return temp&0xFFFF;
}

static uint32_t stack()
{
        uint32_t temp=readmem(pbr|pc); pc++;
        temp+=s.w;
        return temp&0xFFFF;
}

static uint32_t indirect()
{
        uint32_t temp=(readmem(pbr|pc)+dp)&0xFFFF; pc++;
        return (readmemw(temp))+dbr;
}

static uint32_t indirectx()
{
        uint32_t temp=(readmem(pbr|pc)+dp+x.w)&0xFFFF; pc++;
        return (readmemw(temp))+dbr;
}
static uint32_t jindirectx() /*JSR (,x) uses PBR instead of DBR, and 2 byte address insted of 1 + dp*/
{
        uint32_t temp=(readmem(pbr|pc)+(readmem((pbr|pc)+1)<<8)+x.w)+pbr; pc+=2;
//        printf("Temp %06X\n",temp);
        return temp;
}

static uint32_t indirecty()
{
        uint32_t temp=(readmem(pbr|pc)+dp)&0xFFFF; pc++;
        return (readmemw(temp))+y.w+dbr;
}
static uint32_t sindirecty()
{
        uint32_t temp=(readmem(pbr|pc)+s.w)&0xFFFF; pc++;
        return (readmemw(temp))+y.w+dbr;
}

static uint32_t indirectl()
{
                uint32_t temp, addr;
        temp=(readmem(pbr|pc)+dp)&0xFFFF; pc++;
        addr=readmemw(temp)|(readmem(temp+2)<<16);
//        printf("IND %06X\n",addr);
        return addr;
}

static uint32_t indirectly()
{
                uint32_t temp, addr;
        temp=(readmem(pbr|pc)+dp)&0xFFFF; pc++;
        addr=(readmemw(temp)|(readmem(temp+2)<<16))+y.w;
//        if (pc==0xFDC9) printf("INDy %04X %06X\n",temp,addr);
//        if (output) printf("INDy %06X %02X %06X\n",addr,opcode,pbr|pc);
        return addr;
}

/*Flag setting*/
#define setzn8(v)  p.z=!(v); p.n=(v)&0x80
#define setzn16(v) p.z=!(v); p.n=(v)&0x8000

/*ADC/SBC macros*/
#define ADC8()  tempw=a.b.l+temp+((p.c)?1:0);                          \
                p.v=(!((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));       \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw&0x100;

#define ADC16() templ=a.w+tempw+((p.c)?1:0);                           \
                p.v=(!((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));      \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ&0x10000;

#define ADCBCD8()                                                       \
                tempw=(a.b.l&0xF)+(temp&0xF)+(p.c?1:0);                 \
                if (tempw>9)                                            \
                {                                                       \
                        tempw+=6;                                       \
                }                                                       \
                tempw+=((a.b.l&0xF0)+(temp&0xF0));                      \
                if (tempw>0x9F)                                         \
                {                                                       \
                        tempw+=0x60;                                    \
                }                                                       \
                p.v=(!((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));       \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw>0xFF;                                         \
                cycles--; clockspc(6);

#define ADCBCD16()                                                      \
                templ=(a.w&0xF)+(tempw&0xF)+(p.c?1:0);                  \
                if (templ>9)                                            \
                {                                                       \
                        templ+=6;                                       \
                }                                                       \
                templ+=((a.w&0xF0)+(tempw&0xF0));                       \
                if (templ>0x9F)                                         \
                {                                                       \
                        templ+=0x60;                                    \
                }                                                       \
                templ+=((a.w&0xF00)+(tempw&0xF00));                     \
                if (templ>0x9FF)                                        \
                {                                                       \
                        templ+=0x600;                                   \
                }                                                       \
                templ+=((a.w&0xF000)+(tempw&0xF000));                   \
                if (templ>0x9FFF)                                       \
                {                                                       \
                        templ+=0x6000;                                  \
                }                                                       \
                p.v=(!((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));      \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ>0xFFFF;                                       \
                cycles--; clockspc(6);

#define SBC8()  tempw=a.b.l-temp-((p.c)?0:1);                          \
                p.v=(((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));        \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw<=0xFF;

#define SBC16() templ=a.w-tempw-((p.c)?0:1);                           \
                p.v=(((a.w^tempw)&(a.w^templ))&0x8000);                 \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ<=0xFFFF;

#define SBCBCD8()                                                       \
                tempw=(a.b.l&0xF)-(temp&0xF)-(p.c?0:1);                 \
                if (tempw>9)                                            \
                {                                                       \
                        tempw-=6;                                       \
                }                                                       \
                tempw+=((a.b.l&0xF0)-(temp&0xF0));                      \
                if (tempw>0x9F)                                         \
                {                                                       \
                        tempw-=0x60;                                    \
                }                                                       \
                p.v=(((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));        \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw<=0xFF;                                        \
                cycles--; clockspc(6);

#define SBCBCD16()                                                      \
                templ=(a.w&0xF)-(tempw&0xF)-(p.c?0:1);                  \
                if (templ>9)                                            \
                {                                                       \
                        templ-=6;                                       \
                }                                                       \
                templ+=((a.w&0xF0)-(tempw&0xF0));                       \
                if (templ>0x9F)                                         \
                {                                                       \
                        templ-=0x60;                                    \
                }                                                       \
                templ+=((a.w&0xF00)-(tempw&0xF00));                     \
                if (templ>0x9FF)                                        \
                {                                                       \
                        templ-=0x600;                                   \
                }                                                       \
                templ+=((a.w&0xF000)-(tempw&0xF000));                   \
                if (templ>0x9FFF)                                       \
                {                                                       \
                        templ-=0x6000;                                  \
                }                                                       \
                p.v=(((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));       \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ<=0xFFFF;                                      \
                cycles--; clockspc(6);

/*Instructions*/
static void inca8()
{
        readmem(pbr|pc);
        a.b.l++;
        setzn8(a.b.l);
}
static void inca16()
{
        readmem(pbr|pc);
        a.w++;
        setzn16(a.w);
}
static void inx8()
{
        readmem(pbr|pc);
        x.b.l++;
        setzn8(x.b.l);
}
static void inx16()
{
        readmem(pbr|pc);
        x.w++;
        setzn16(x.w);
}
static void iny8()
{
        readmem(pbr|pc);
        y.b.l++;
        setzn8(y.b.l);
}
static void iny16()
{
        readmem(pbr|pc);
        y.w++;
        setzn16(y.w);
}

static void deca8()
{
        readmem(pbr|pc);
        a.b.l--;
        setzn8(a.b.l);
}
static void deca16()
{
        readmem(pbr|pc);
        a.w--;
        setzn16(a.w);
}
static void dex8()
{
        readmem(pbr|pc);
        x.b.l--;
        setzn8(x.b.l);
}
static void dex16()
{
        readmem(pbr|pc);
        x.w--;
        setzn16(x.w);
}
static void dey8()
{
        readmem(pbr|pc);
        y.b.l--;
        setzn8(y.b.l);
}
static void dey16()
{
        readmem(pbr|pc);
        y.w--;
        setzn16(y.w);
}

/*INC group*/
static void incZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp++;
        setzn8(temp);
        writemem(addr,temp);
}
static void incZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp++;
        setzn16(temp);
        writememw(addr,temp);
}

static void incZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp++;
        setzn8(temp);
        writemem(addr,temp);
}
static void incZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp++;
        setzn16(temp);
        writememw(addr,temp);
}

static void incAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp++;
        setzn8(temp);
        writemem(addr,temp);
}
static void incAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp++;
        setzn16(temp);
        writememw(addr,temp);
}

static void incAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp++;
        setzn8(temp);
        writemem(addr,temp);
}
static void incAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp++;
        setzn16(temp);
        writememw(addr,temp);
}

/*DEC group*/
static void decZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp--;
        setzn8(temp);
        writemem(addr,temp);
//        if (output && addr==4) printf("DEC 4 %02X %i %i\n",temp,p.z,p.n);
}
static void decZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp--;
        setzn16(temp);
        writememw(addr,temp);
}

static void decZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp--;
        setzn8(temp);
        writemem(addr,temp);
}
static void decZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp--;
        setzn16(temp);
        writememw(addr,temp);
}

static void decAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp--;
        setzn8(temp);
        writemem(addr,temp);
}
static void decAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp--;
        setzn16(temp);
        writememw(addr,temp);
}

static void decAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        temp--;
        setzn8(temp);
        writemem(addr,temp);
}
static void decAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        temp--;
        setzn16(temp);
        writememw(addr,temp);
}

/*Flag group*/
static void clc()
{
        readmem(pbr|pc);
        p.c=0;
}
static void cld()
{
        readmem(pbr|pc);
        p.d=0;
}
static void cli()
{
        readmem(pbr|pc);
        p.i=0;
}
static void clv()
{
        readmem(pbr|pc);
        p.v=0;
}

static void sec()
{
        readmem(pbr|pc);
        p.c=1;
}
static void sed()
{
        readmem(pbr|pc);
        p.d=1;
}
static void sei()
{
        readmem(pbr|pc);
        p.i=1;
}

static void xce()
{
        int temp=p.c;
        p.c=p.e;
        p.e=temp;
        readmem(pbr|pc);
        updatecpumode();
}

static void sep()
{
        uint8_t temp=readmem(pbr|pc); pc++;
        if (temp&1) p.c=1;
        if (temp&2) p.z=1;
        if (temp&4) p.i=1;
        if (temp&8) p.d=1;
        if (temp&0x40) p.v=1;
        if (temp&0x80) p.n=1;
        if (!p.e)
        {
                if (temp&0x10) p.ex=1;
                if (temp&0x20) p.m=1;
                updatecpumode();
        }
}

static void rep65816()
{
        uint8_t temp=readmem(pbr|pc); pc++;
        if (temp&1) p.c=0;
        if (temp&2) p.z=0;
        if (temp&4) p.i=0;
        if (temp&8) p.d=0;
        if (temp&0x40) p.v=0;
        if (temp&0x80) p.n=0;
        if (!p.e)
        {
                if (temp&0x10) p.ex=0;
                if (temp&0x20) p.m=0;
                updatecpumode();
        }
}

/*Transfer group*/
static void tax8()
{
        readmem(pbr|pc);
        x.b.l=a.b.l;
        setzn8(x.b.l);
}
static void tay8()
{
        readmem(pbr|pc);
        y.b.l=a.b.l;
        setzn8(y.b.l);
}
static void txa8()
{
        readmem(pbr|pc);
        a.b.l=x.b.l;
        setzn8(a.b.l);
}
static void tya8()
{
        readmem(pbr|pc);
        a.b.l=y.b.l;
        setzn8(a.b.l);
}
static void tsx8()
{
        readmem(pbr|pc);
        x.b.l=s.b.l;
        setzn8(x.b.l);
}
static void txs8()
{
        readmem(pbr|pc);
        s.b.l=x.b.l;
//        setzn8(s.b.l);
}
static void txy8()
{
        readmem(pbr|pc);
        y.b.l=x.b.l;
        setzn8(y.b.l);
}
static void tyx8()
{
        readmem(pbr|pc);
        x.b.l=y.b.l;
        setzn8(x.b.l);
}

static void tax16()
{
        readmem(pbr|pc);
        x.w=a.w;
        setzn16(x.w);
}
static void tay16()
{
        readmem(pbr|pc);
        y.w=a.w;
        setzn16(y.w);
}
static void txa16()
{
        readmem(pbr|pc);
        a.w=x.w;
        setzn16(a.w);
}
static void tya16()
{
        readmem(pbr|pc);
        a.w=y.w;
        setzn16(a.w);
}
static void tsx16()
{
        readmem(pbr|pc);
        x.w=s.w;
        setzn16(x.w);
}
static void txs16()
{
        readmem(pbr|pc);
        s.w=x.w;
//        setzn16(s.w);
}
static void txy16()
{
        readmem(pbr|pc);
        y.w=x.w;
        setzn16(y.w);
}
static void tyx16()
{
        readmem(pbr|pc);
        x.w=y.w;
        setzn16(x.w);
}

/*LDX group*/
static void ldxImm8()
{
        x.b.l=readmem(pbr|pc); pc++;
        setzn8(x.b.l);
}
static void ldxZp8()
{
        addr=zeropage();
        x.b.l=readmem(addr);
        setzn8(x.b.l);
}
static void ldxZpy8()
{
        addr=zeropagey();
        x.b.l=readmem(addr);
        setzn8(x.b.l);
}
static void ldxAbs8()
{
        addr=absolute();
        x.b.l=readmem(addr);
        setzn8(x.b.l);
}
static void ldxAbsy8()
{
        addr=absolutey();
        x.b.l=readmem(addr);
        setzn8(x.b.l);
}

static void ldxImm16()
{
        x.w=readmemw(pbr|pc); pc+=2;
        setzn16(x.w);
}
static void ldxZp16()
{
        addr=zeropage();
        x.w=readmemw(addr);
        setzn16(x.w);
}
static void ldxZpy16()
{
        addr=zeropagey();
        x.w=readmemw(addr);
        setzn16(x.w);
}
static void ldxAbs16()
{
        addr=absolute();
        x.w=readmemw(addr);
        setzn16(x.w);
}
static void ldxAbsy16()
{
        addr=absolutey();
        x.w=readmemw(addr);
        setzn16(x.w);
}

/*LDY group*/
static void ldyImm8()
{
        y.b.l=readmem(pbr|pc); pc++;
        setzn8(y.b.l);
}
static void ldyZp8()
{
        addr=zeropage();
        y.b.l=readmem(addr);
        setzn8(y.b.l);
}
static void ldyZpx8()
{
        addr=zeropagex();
        y.b.l=readmem(addr);
        setzn8(y.b.l);
}
static void ldyAbs8()
{
        addr=absolute();
        y.b.l=readmem(addr);
        setzn8(y.b.l);
}
static void ldyAbsx8()
{
        addr=absolutex();
        y.b.l=readmem(addr);
        setzn8(y.b.l);
}

static void ldyImm16()
{
        y.w=readmemw(pbr|pc); pc+=2;
        setzn16(y.w);
}
static void ldyZp16()
{
        addr=zeropage();
        y.w=readmemw(addr);
        setzn16(y.w);
}
static void ldyZpx16()
{
        addr=zeropagex();
        y.w=readmemw(addr);
        setzn16(y.w);
}
static void ldyAbs16()
{
        addr=absolute();
        y.w=readmemw(addr);
        setzn16(y.w);
}
static void ldyAbsx16()
{
        addr=absolutex();
        y.w=readmemw(addr);
        setzn16(y.w);
}

/*LDA group*/
static void ldaImm8()
{
        a.b.l=readmem(pbr|pc); pc++;
        setzn8(a.b.l);
}
static void ldaZp8()
{
        addr=zeropage();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaZpx8()
{
        addr=zeropagex();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaSp8()
{
        addr=stack();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaSIndirecty8()
{
        addr=sindirecty();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaAbs8()
{
        addr=absolute();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaAbsx8()
{
        addr=absolutex();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaAbsy8()
{
        addr=absolutey();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaLong8()
{
        addr=absolutelong();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaLongx8()
{
        addr=absolutelongx();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaIndirect8()
{
        addr=indirect();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaIndirectx8()
{
        addr=indirectx();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaIndirecty8()
{
        addr=indirecty();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaIndirectLong8()
{
        addr=indirectl();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}
static void ldaIndirectLongy8()
{
        addr=indirectly();
        a.b.l=readmem(addr);
        setzn8(a.b.l);
}

static void ldaImm16()
{
        a.w=readmemw(pbr|pc); pc+=2;
        setzn16(a.w);
}
static void ldaZp16()
{
        addr=zeropage();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaZpx16()
{
        addr=zeropagex();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaSp16()
{
        addr=stack();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaSIndirecty16()
{
        addr=sindirecty();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaAbs16()
{
        addr=absolute();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaAbsx16()
{
        addr=absolutex();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaAbsy16()
{
        addr=absolutey();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaLong16()
{
        addr=absolutelong();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaLongx16()
{
        addr=absolutelongx();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaIndirect16()
{
        addr=indirect();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaIndirectx16()
{
        addr=indirectx();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaIndirecty16()
{
        addr=indirecty();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaIndirectLong16()
{
        addr=indirectl();
        a.w=readmemw(addr);
        setzn16(a.w);
}
static void ldaIndirectLongy16()
{
        addr=indirectly();
        a.w=readmemw(addr);
        setzn16(a.w);
}

/*STA group*/
static void staZp8()
{
        addr=zeropage();
        writemem(addr,a.b.l);
}
static void staZpx8()
{
        addr=zeropagex();
        writemem(addr,a.b.l);
}
static void staAbs8()
{
        addr=absolute();
        writemem(addr,a.b.l);
}
static void staAbsx8()
{
        addr=absolutex();
        writemem(addr,a.b.l);
}
static void staAbsy8()
{
        addr=absolutey();
        writemem(addr,a.b.l);
}
static void staLong8()
{
        addr=absolutelong();
        writemem(addr,a.b.l);
}
static void staLongx8()
{
        addr=absolutelongx();
//        bem_debugf("Addr %06X\n",addr);
        writemem(addr,a.b.l);
}
static void staIndirect8()
{
        addr=indirect();
        writemem(addr,a.b.l);
}
static void staIndirectx8()
{
        addr=indirectx();
        writemem(addr,a.b.l);
}
static void staIndirecty8()
{
        addr=indirecty();
        writemem(addr,a.b.l);
}
static void staIndirectLong8()
{
        addr=indirectl();
        writemem(addr,a.b.l);
}
static void staIndirectLongy8()
{
        addr=indirectly();
        writemem(addr,a.b.l);
}
static void staSp8()
{
        addr=stack();
        writemem(addr,a.b.l);
}
static void staSIndirecty8()
{
        addr=sindirecty();
        writemem(addr,a.b.l);
}

static void staZp16()
{
        addr=zeropage();
        writememw(addr,a.w);
}
static void staZpx16()
{
        addr=zeropagex();
        writememw(addr,a.w);
}
static void staAbs16()
{
        addr=absolute();
        writememw(addr,a.w);
}
static void staAbsx16()
{
        addr=absolutex();
        writememw(addr,a.w);
}
static void staAbsy16()
{
        addr=absolutey();
        writememw(addr,a.w);
}
static void staLong16()
{
        addr=absolutelong();
        writememw(addr,a.w);
}
static void staLongx16()
{
        addr=absolutelongx();
        writememw(addr,a.w);
//        printf("Written %06X %04X %04X\n",addr,a.w,readmemw(addr));
}
static void staIndirect16()
{
        addr=indirect();
        writememw(addr,a.w);
}
static void staIndirectx16()
{
        addr=indirectx();
        writememw(addr,a.w);
}
static void staIndirecty16()
{
        addr=indirecty();
        writememw(addr,a.w);
}
static void staIndirectLong16()
{
        addr=indirectl();
        writememw(addr,a.w);
}
static void staIndirectLongy16()
{
        addr=indirectly();
        writememw(addr,a.w);
}
static void staSp16()
{
        addr=stack();
        writememw(addr,a.w);
}
static void staSIndirecty16()
{
        addr=sindirecty();
        writememw(addr,a.w);
}

/*STX group*/
static void stxZp8()
{
        addr=zeropage();
        writemem(addr,x.b.l);
}
static void stxZpy8()
{
        addr=zeropagey();
        writemem(addr,x.b.l);
}
static void stxAbs8()
{
        addr=absolute();
        writemem(addr,x.b.l);
}

static void stxZp16()
{
        addr=zeropage();
        writememw(addr,x.w);
}
static void stxZpy16()
{
        addr=zeropagey();
        writememw(addr,x.w);
}
static void stxAbs16()
{
        addr=absolute();
        writememw(addr,x.w);
}

/*STY group*/
static void styZp8()
{
        addr=zeropage();
        writemem(addr,y.b.l);
}
static void styZpx8()
{
        addr=zeropagex();
        writemem(addr,y.b.l);
}
static void styAbs8()
{
        addr=absolute();
        writemem(addr,y.b.l);
}

static void styZp16()
{
        addr=zeropage();
        writememw(addr,y.w);
}
static void styZpx16()
{
        addr=zeropagex();
        writememw(addr,y.w);
}
static void styAbs16()
{
        addr=absolute();
        writememw(addr,y.w);
}

/*STZ group*/
static void stzZp8()
{
        addr=zeropage();
        writemem(addr,0);
}
static void stzZpx8()
{
        addr=zeropagex();
        writemem(addr,0);
}
static void stzAbs8()
{
        addr=absolute();
        writemem(addr,0);
}
static void stzAbsx8()
{
        addr=absolutex();
        writemem(addr,0);
}

static void stzZp16()
{
        addr=zeropage();
        writememw(addr,0);
}
static void stzZpx16()
{
        addr=zeropagex();
        writememw(addr,0);
}
static void stzAbs16()
{
        addr=absolute();
        writememw(addr,0);
}
static void stzAbsx16()
{
        addr=absolutex();
        writememw(addr,0);
}

/*ADC group*/
static void adcImm8()
{
        uint16_t tempw;
        uint8_t temp=readmem(pbr|pc); pc++;
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcZp8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcZpx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcSp8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=stack();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcAbs8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcAbsx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcAbsy8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutey();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcLong8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutelong();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcLongx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutelongx();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcIndirect8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirect();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcIndirectx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectx();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcIndirecty8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirecty();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcsIndirecty8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=sindirecty();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcIndirectLong8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectl();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}
static void adcIndirectLongy8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectly();
        temp=readmem(addr);
        if (p.d) { ADCBCD8(); } else { ADC8(); }
}

static void adcImm16()
{
        uint32_t templ;
        uint16_t tempw;
        tempw=readmemw(pbr|pc); pc+=2;
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcZp16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=zeropage();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcZpx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=zeropagex();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcSp16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=stack();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcAbs16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolute();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcAbsx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutex();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcAbsy16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutey();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcLong16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutelong();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcLongx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutelongx();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcIndirect16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirect();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcIndirectx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectx();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcIndirecty16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirecty();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcsIndirecty16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=sindirecty();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcIndirectLong16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectl();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}
static void adcIndirectLongy16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectly();
        tempw=readmemw(addr);
        if (p.d) { ADCBCD16(); } else { ADC16(); }
}

/*SBC group*/
static void sbcImm8()
{
        uint16_t tempw;
        uint8_t temp=readmem(pbr|pc); pc++;
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcZp8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcZpx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcSp8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=stack();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcAbs8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcAbsx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcAbsy8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutey();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcLong8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutelong();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcLongx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=absolutelongx();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcIndirect8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirect();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcIndirectx8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectx();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcIndirecty8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirecty();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcsIndirecty8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=sindirecty();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcIndirectLong8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectl();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}
static void sbcIndirectLongy8()
{
        uint16_t tempw;
        uint8_t temp;
        addr=indirectly();
        temp=readmem(addr);
        if (p.d) { SBCBCD8(); } else { SBC8(); }
}

static void sbcImm16()
{
        uint32_t templ;
        uint16_t tempw;
        tempw=readmemw(pbr|pc); pc+=2;
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcZp16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=zeropage();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcZpx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=zeropagex();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcSp16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=stack();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcAbs16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolute();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcAbsx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutex();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcAbsy16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutey();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcLong16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutelong();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcLongx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=absolutelongx();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcIndirect16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirect();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcIndirectx16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectx();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcIndirecty16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirecty();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcsIndirecty16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=sindirecty();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcIndirectLong16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectl();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}
static void sbcIndirectLongy16()
{
        uint32_t templ;
        uint16_t tempw;
        addr=indirectly();
        tempw=readmemw(addr);
        if (p.d) { SBCBCD16(); } else { SBC16(); }
}

/*EOR group*/
static void eorImm8()
{
        a.b.l^=readmem(pbr|pc); pc++;
        setzn8(a.b.l);
}
static void eorZp8()
{
        addr=zeropage();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorZpx8()
{
        addr=zeropagex();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorSp8()
{
        addr=stack();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorAbs8()
{
        addr=absolute();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorAbsx8()
{
        addr=absolutex();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorAbsy8()
{
        addr=absolutey();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorLong8()
{
        addr=absolutelong();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorLongx8()
{
        addr=absolutelongx();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorIndirect8()
{
        addr=indirect();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorIndirectx8()
{
        addr=indirectx();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorIndirecty8()
{
        addr=indirecty();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorsIndirecty8()
{
        addr=sindirecty();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorIndirectLong8()
{
        addr=indirectl();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}
static void eorIndirectLongy8()
{
        addr=indirectly();
        a.b.l^=readmem(addr);
        setzn8(a.b.l);
}

static void eorImm16()
{
        a.w^=readmemw(pbr|pc); pc+=2;
        setzn16(a.w);
}
static void eorZp16()
{
        addr=zeropage();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorZpx16()
{
        addr=zeropagex();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorSp16()
{
        addr=stack();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorAbs16()
{
        addr=absolute();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorAbsx16()
{
        addr=absolutex();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorAbsy16()
{
        addr=absolutey();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorLong16()
{
        addr=absolutelong();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorLongx16()
{
        addr=absolutelongx();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorIndirect16()
{
        addr=indirect();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorIndirectx16()
{
        addr=indirectx();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorIndirecty16()
{
        addr=indirecty();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorsIndirecty16()
{
        addr=sindirecty();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorIndirectLong16()
{
        addr=indirectl();
        a.w^=readmemw(addr);
        setzn16(a.w);
}
static void eorIndirectLongy16()
{
        addr=indirectly();
        a.w^=readmemw(addr);
        setzn16(a.w);
}

/*AND group*/
static void andImm8()
{
        a.b.l&=readmem(pbr|pc); pc++;
        setzn8(a.b.l);
}
static void andZp8()
{
        addr=zeropage();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andZpx8()
{
        addr=zeropagex();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andSp8()
{
        addr=stack();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andAbs8()
{
        addr=absolute();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andAbsx8()
{
        addr=absolutex();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andAbsy8()
{
        addr=absolutey();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andLong8()
{
        addr=absolutelong();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andLongx8()
{
        addr=absolutelongx();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andIndirect8()
{
        addr=indirect();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andIndirectx8()
{
        addr=indirectx();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andIndirecty8()
{
        addr=indirecty();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andsIndirecty8()
{
        addr=sindirecty();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andIndirectLong8()
{
        addr=indirectl();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}
static void andIndirectLongy8()
{
        addr=indirectly();
        a.b.l&=readmem(addr);
        setzn8(a.b.l);
}

static void andImm16()
{
        a.w&=readmemw(pbr|pc); pc+=2;
        setzn16(a.w);
}
static void andZp16()
{
        addr=zeropage();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andZpx16()
{
        addr=zeropagex();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andSp16()
{
        addr=stack();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andAbs16()
{
        addr=absolute();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andAbsx16()
{
        addr=absolutex();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andAbsy16()
{
        addr=absolutey();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andLong16()
{
        addr=absolutelong();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andLongx16()
{
        addr=absolutelongx();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andIndirect16()
{
        addr=indirect();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andIndirectx16()
{
        addr=indirectx();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andIndirecty16()
{
        addr=indirecty();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andsIndirecty16()
{
        addr=sindirecty();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andIndirectLong16()
{
        addr=indirectl();
        a.w&=readmemw(addr);
        setzn16(a.w);
}
static void andIndirectLongy16()
{
        addr=indirectly();
        a.w&=readmemw(addr);
        setzn16(a.w);
}

/*ORA group*/
static void oraImm8()
{
        a.b.l|=readmem(pbr|pc); pc++;
        setzn8(a.b.l);
}
static void oraZp8()
{
        addr=zeropage();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraZpx8()
{
        addr=zeropagex();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraSp8()
{
        addr=stack();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraAbs8()
{
        addr=absolute();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraAbsx8()
{
        addr=absolutex();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraAbsy8()
{
        addr=absolutey();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraLong8()
{
        addr=absolutelong();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraLongx8()
{
        addr=absolutelongx();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraIndirect8()
{
        addr=indirect();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraIndirectx8()
{
        addr=indirectx();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraIndirecty8()
{
        addr=indirecty();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void orasIndirecty8()
{
        addr=sindirecty();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraIndirectLong8()
{
        addr=indirectl();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}
static void oraIndirectLongy8()
{
        addr=indirectly();
        a.b.l|=readmem(addr);
        setzn8(a.b.l);
}

static void oraImm16()
{
        a.w|=readmemw(pbr|pc); pc+=2;
        setzn16(a.w);
}
static void oraZp16()
{
        addr=zeropage();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraZpx16()
{
        addr=zeropagex();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraSp16()
{
        addr=stack();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraAbs16()
{
        addr=absolute();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraAbsx16()
{
        addr=absolutex();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraAbsy16()
{
        addr=absolutey();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraLong16()
{
        addr=absolutelong();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraLongx16()
{
        addr=absolutelongx();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraIndirect16()
{
        addr=indirect();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraIndirectx16()
{
        addr=indirectx();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraIndirecty16()
{
        addr=indirecty();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void orasIndirecty16()
{
        addr=sindirecty();
        a.w|=readmem(addr);
        setzn16(a.w);
}
static void oraIndirectLong16()
{
        addr=indirectl();
        a.w|=readmemw(addr);
        setzn16(a.w);
}
static void oraIndirectLongy16()
{
        addr=indirectly();
        a.w|=readmemw(addr);
        setzn16(a.w);
}

/*BIT group*/
static void bitImm8()
{
        uint8_t temp=readmem(pbr|pc); pc++;
        p.z=!(temp&a.b.l);
}
static void bitImm16()
{
        uint16_t temp=readmemw(pbr|pc); pc+=2;
        p.z=!(temp&a.w);
}

static void bitZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        p.z=!(temp&a.b.l);
        p.v=temp&0x40;
        p.n=temp&0x80;
}
static void bitZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        p.z=!(temp&a.w);
        p.v=temp&0x4000;
        p.n=temp&0x8000;
}

static void bitZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        p.z=!(temp&a.b.l);
        p.v=temp&0x40;
        p.n=temp&0x80;
}
static void bitZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        p.z=!(temp&a.w);
        p.v=temp&0x4000;
        p.n=temp&0x8000;
}

static void bitAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        p.z=!(temp&a.b.l);
        p.v=temp&0x40;
        p.n=temp&0x80;
}
static void bitAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        p.z=!(temp&a.w);
        p.v=temp&0x4000;
        p.n=temp&0x8000;
}

static void bitAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        p.z=!(temp&a.b.l);
        p.v=temp&0x40;
        p.n=temp&0x80;
}
static void bitAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        p.z=!(temp&a.w);
        p.v=temp&0x4000;
        p.n=temp&0x8000;
}

/*CMP group*/
static void cmpImm8()
{
        uint8_t temp;
        temp=readmem(pbr|pc); pc++;
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpSp8()
{
        uint8_t temp;
        addr=stack();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpAbsy8()
{
        uint8_t temp;
        addr=absolutey();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpLong8()
{
        uint8_t temp;
        addr=absolutelong();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpLongx8()
{
        uint8_t temp;
        addr=absolutelongx();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpIndirect8()
{
        uint8_t temp;
        addr=indirect();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpIndirectx8()
{
        uint8_t temp;
        addr=indirectx();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpIndirecty8()
{
        uint8_t temp;
        addr=indirecty();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpsIndirecty8()
{
        uint8_t temp;
        addr=sindirecty();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpIndirectLong8()
{
        uint8_t temp;
        addr=indirectl();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}
static void cmpIndirectLongy8()
{
        uint8_t temp;
        addr=indirectly();
        temp=readmem(addr);
        setzn8(a.b.l-temp);
        p.c=(a.b.l>=temp);
}

static void cmpImm16()
{
        uint16_t temp;
        temp=readmemw(pbr|pc); pc+=2;
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpSp16()
{
        uint16_t temp;
        addr=stack();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpAbsy16()
{
        uint16_t temp;
        addr=absolutey();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpLong16()
{
        uint16_t temp;
        addr=absolutelong();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpLongx16()
{
        uint16_t temp;
        addr=absolutelongx();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpIndirect16()
{
        uint16_t temp;
        addr=indirect();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpIndirectx16()
{
        uint16_t temp;
        addr=indirectx();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpIndirecty16()
{
        uint16_t temp;
        addr=indirecty();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpsIndirecty16()
{
        uint16_t temp;
        addr=sindirecty();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpIndirectLong16()
{
        uint16_t temp;
        addr=indirectl();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}
static void cmpIndirectLongy16()
{
        uint16_t temp;
        addr=indirectly();
        temp=readmemw(addr);
        setzn16(a.w-temp);
        p.c=(a.w>=temp);
}

/*Stack Group*/
static void phb()
{
        readmem(pbr|pc);
        writemem(s.w,dbr>>16); s.w--;
//        printf("PHB %04X\n",s.w);
}
static void phbe()
{
        readmem(pbr|pc);
        writemem(s.w,dbr>>16); s.b.l--;
}

static void phk()
{
        readmem(pbr|pc);
        writemem(s.w,pbr>>16); s.w--;
//        printf("PHK %04X\n",s.w);
}
static void phke()
{
        readmem(pbr|pc);
        writemem(s.w,pbr>>16); s.b.l--;
}

static void pea()
{
        addr=readmemw(pbr|pc); pc+=2;
        writemem(s.w,addr>>8);   s.w--;
        writemem(s.w,addr&0xFF); s.w--;
//        printf("PEA %04X\n",s.w);
}

static void pei()
{
        addr=indirect();
        writemem(s.w,addr>>8);   s.w--;
        writemem(s.w,addr&0xFF); s.w--;
        //printf("PEI %04X\n",s.w);
}

static void per()
{
        addr=readmemw(pbr|pc); pc+=2;
        addr+=pc;
        writemem(s.w,addr>>8);   s.w--;
        writemem(s.w,addr&0xFF); s.w--;
        //printf("PER %04X\n",s.w);
}

static void phd()
{
        writemem(s.w,dp>>8);   s.w--;
        writemem(s.w,dp&0xFF); s.w--;
        //printf("PHD %04X\n",s.w);
}
static void pld()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        dp=readmem(s.w);
        s.w++; dp|=(readmem(s.w)<<8);
        //printf("PLD %04X\n",s.w);
}

static void pha8()
{
        readmem(pbr|pc);
        writemem(s.w,a.b.l); s.w--;
        //printf("PHA %04X\n",s.w);
}
static void pha16()
{
        readmem(pbr|pc);
        writemem(s.w,a.b.h); s.w--;
        writemem(s.w,a.b.l); s.w--;
        //printf("PHA %04X\n",s.w);
}

static void phx8()
{
        readmem(pbr|pc);
        writemem(s.w,x.b.l); s.w--;
        //printf("PHX %04X\n",s.w);
}
static void phx16()
{
        readmem(pbr|pc);
        writemem(s.w,x.b.h); s.w--;
        writemem(s.w,x.b.l); s.w--;
        //printf("PHX %04X\n",s.w);
}

static void phy8()
{
        readmem(pbr|pc);
        writemem(s.w,y.b.l); s.w--;
        //printf("PHY %04X\n",s.w);
}
static void phy16()
{
        readmem(pbr|pc);
        writemem(s.w,y.b.h); s.w--;
        writemem(s.w,y.b.l); s.w--;
        //printf("PHY %04X\n",s.w);
}

static void pla8()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        a.b.l=readmem(s.w);
        setzn8(a.b.l);
        //printf("PLA %04X\n",s.w);
}
static void pla16()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        a.b.l=readmem(s.w);
        s.w++; a.b.h=readmem(s.w);
        setzn16(a.w);
        //printf("PLA %04X\n",s.w);
}

static void plx8()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        x.b.l=readmem(s.w);
        setzn8(x.b.l);
        //printf("PLX %04X\n",s.w);
}
static void plx16()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        x.b.l=readmem(s.w);
        s.w++; x.b.h=readmem(s.w);
        setzn16(x.w);
        //printf("PLX %04X\n",s.w);
}

static void ply8()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        y.b.l=readmem(s.w);
        setzn8(y.b.l);
        //printf("PLY %04X\n",s.w);
}
static void ply16()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        y.b.l=readmem(s.w);
        s.w++; y.b.h=readmem(s.w);
        setzn16(y.w);
        //printf("PLY %04X\n",s.w);
}

static void plb()
{
        readmem(pbr|pc);
        s.w++; cycles--; clockspc(6);
        dbr=readmem(s.w)<<16;
        //printf("PLB %04X\n",s.w);
}
static void plbe()
{
        readmem(pbr|pc);
        s.b.l++; cycles--; clockspc(6);
        dbr=readmem(s.w)<<16;
}

static void plp()
{
        uint8_t temp=readmem(s.w+1); s.w++;
        p.c=temp&1;
        p.z=temp&2;
        p.i=temp&4;
        p.d=temp&8;
        p.ex=temp&0x10;
        p.m=temp&0x20;
        p.v=temp&0x40;
        p.n=temp&0x80;
        cycles-=2; clockspc(12);
        updatecpumode();
        //printf("PLP %04X\n",s.w);
}
static void plpe()
{
        uint8_t temp;
        s.b.l++; temp=readmem(s.w);
        p.c=temp&1;
        p.z=temp&2;
        p.i=temp&4;
        p.d=temp&8;
        p.v=temp&0x40;
        p.n=temp&0x80;
        cycles-=2; clockspc(12);
}

static void php()
{
        uint8_t temp=(p.c)?1:0;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        if (p.ex) temp|=0x10;
        if (p.m) temp|=0x20;
        readmem(pbr|pc);
        writemem(s.w,temp); s.w--;
        //printf("PHP %04X\n",s.w);
}
static void phpe()
{
        uint8_t temp=(p.c)?1:0;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        temp|=0x30;
        readmem(pbr|pc);
        writemem(s.w,temp); s.b.l--;
}

/*CPX group*/
static void cpxImm8()
{
        uint8_t temp=readmem(pbr|pc); pc++;
        setzn8(x.b.l-temp);
        p.c=(x.b.l>=temp);
}
static void cpxImm16()
{
        uint16_t temp=readmemw(pbr|pc); pc+=2;
        setzn16(x.w-temp);
        p.c=(x.w>=temp);
}

static void cpxZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        setzn8(x.b.l-temp);
        p.c=(x.b.l>=temp);
}
static void cpxZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        setzn16(x.w-temp);
        p.c=(x.w>=temp);
}

static void cpxAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        setzn8(x.b.l-temp);
        p.c=(x.b.l>=temp);
}
static void cpxAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        setzn16(x.w-temp);
        p.c=(x.w>=temp);
}

/*CPY group*/
static void cpyImm8()
{
        uint8_t temp=readmem(pbr|pc); pc++;
        setzn8(y.b.l-temp);
        p.c=(y.b.l>=temp);
}
static void cpyImm16()
{
        uint16_t temp=readmemw(pbr|pc); pc+=2;
        setzn16(y.w-temp);
        p.c=(y.w>=temp);
}

static void cpyZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        setzn8(y.b.l-temp);
        p.c=(y.b.l>=temp);
}
static void cpyZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        setzn16(y.w-temp);
        p.c=(y.w>=temp);
}

static void cpyAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        setzn8(y.b.l-temp);
        p.c=(y.b.l>=temp);
}
static void cpyAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        setzn16(y.w-temp);
        p.c=(y.w>=temp);
}

/*Branch group*/
static void bcc()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (!p.c)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void bcs()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (p.c)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void beq()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
//        if (setzf>0) p.z=0;
//        if (setzf<0) p.z=1;
//        setzf=0;
        if (p.z)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void bne()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
//        if (pc==0x8D44) printf("BNE %i %i ",setzf,p.z);
//        if (setzf>0) p.z=1;
//        if (setzf<0) p.z=0;
//        setzf=0;
//        if (pc==0x8D44) printf("%i\n",p.z);
//        if (skipz) //printf("skipz ");
        if (!p.z)// && !skipz)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
//        if (skipz) //printf("%04X\n",pc);
//        skipz=0;
}
static void bpl()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (!p.n)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void bmi()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (p.n)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void bvc()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (!p.v)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}
static void bvs()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        if (p.v)
        {
                pc+=temp;
                cycles--; clockspc(6);
        }
}

static void bra()
{
        int8_t temp=(int8_t)readmem(pbr|pc); pc++;
        pc+=temp;
        cycles--; clockspc(6);
}
static void brl()
{
        uint16_t temp=readmemw(pbr|pc); pc+=2;
        pc+=temp;
        cycles--; clockspc(6);
}

/*Jump group*/
static void jmp()
{
        addr=readmemw(pbr|pc);
        pc=addr;
}

static void jmplong()
{
        addr=readmemw(pbr|pc)|(readmem((pbr|pc)+2)<<16);
        pc=addr&0xFFFF;
        pbr=addr&0xFF0000;
}

static void jmpind()
{
        addr=readmemw(pbr|pc);
//        bem_debugf("Addr %04X\n",addr);
        pc=readmemw(addr);
//        bem_debugf("PC %04X\n",addr);
}

static void jmpindx()
{
        addr=(readmemw(pbr|pc))+x.w+pbr;
//        //printf("Read %06X\n",addr);
        pc=readmemw(addr);
}

static void jmlind()
{
        addr=readmemw(pbr|pc);
        pc=readmemw(addr);
        pbr=readmem(addr+2)<<16;
}

static void jsr()
{
        addr=readmemw(pbr|pc);  pc++;
        readmem(pbr|pc);
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF); s.w--;
        pc=addr;
        //printf("JSR %04X\n",s.w);
}
static void jsre()
{
        addr=readmemw(pbr|pc);  pc++;
        readmem(pbr|pc);
        writemem(s.w,pc>>8);   s.b.l--;
        writemem(s.w,pc&0xFF); s.b.l--;
        pc=addr;
}

static void jsrIndx()
{
        addr=jindirectx(); pc--;
//        //printf("Addr %06X\n",addr);
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF); s.w--;
        pc=readmemw(addr);
        //printf("JSR %04X\n",s.w);
//        //printf("PC %04X\n",pc);
}
static void jsrIndxe()
{
        addr=jindirectx(); pc--;
        writemem(s.w,pc>>8);   s.b.l--;
        writemem(s.w,pc&0xFF); s.b.l--;
        pc=readmemw(addr);
}

static void jsl()
{
        uint8_t temp;
        addr=readmemw(pbr|pc);  pc+=2;
        temp=readmem(pbr|pc);
        writemem(s.w,pbr>>16); s.w--;
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF); s.w--;
//        bem_debugf("JSL %06X\n",pbr|pc);
        pc=addr;
        pbr=temp<<16;
        //printf("JSL %04X\n",s.w);
}

static void jsle()
{
        uint8_t temp;
        addr=readmemw(pbr|pc);  pc+=2;
        temp=readmem(pbr|pc);
        writemem(s.w,pbr>>16); s.b.l--;
        writemem(s.w,pc>>8);   s.b.l--;
        writemem(s.w,pc&0xFF); s.b.l--;
//        bem_debugf("JSLe %06X\n",pbr|pc);
        pc=addr;
        pbr=temp<<16;
}

static void rtl()
{
        cycles-=3; clockspc(18);
        pc=readmemw(s.w+1); s.w+=2;
        pbr=readmem(s.w+1)<<16; s.w++;
//        bem_debugf("RTL %06X\n",pbr|pc);
        pc++;
        //printf("RTL %04X\n",s.w);
}
static void rtle()
{
        cycles-=3; clockspc(18);
        s.b.l++; pc=readmem(s.w);
        s.b.l++; pc|=(readmem(s.w)<<8);
        s.b.l++; pbr=readmem(s.w)<<16;
        pc++;
}

static void rts()
{
        cycles-=3; clockspc(18);
        pc=readmemw(s.w+1); s.w+=2;
        pc++;
        //printf("RTS %04X\n",s.w);
}
static void rtse()
{
        cycles-=3; clockspc(18);
        s.b.l++; pc=readmem(s.w);
        s.b.l++; pc|=(readmem(s.w)<<8);
        pc++;
}

static void rti()
{
        uint8_t temp;
        cycles--; s.w++; clockspc(6);
        temp=readmem(s.w);
        p.c=temp&1;
        p.z=temp&2;
        p.i=temp&4;
        p.d=temp&8;
        p.ex=temp&0x10;
        p.m=temp&0x20;
        p.v=temp&0x40;
        p.n=temp&0x80;
        s.w++; pc=readmem(s.w);       //  //printf("%04X -> %02X\n",s.w,pc);
        s.w++; pc|=(readmem(s.w)<<8);  // //printf("%04X -> %02X\n",s.w,pc>>8);
        s.w++; pbr=readmem(s.w)<<16;    ////printf("%04X -> %02X\n",s.w,pbr>>16);
        updatecpumode();
}

static void rtie()
{
        uint8_t temp;
        cycles--; s.b.l++; clockspc(6);
        temp=readmem(s.w);
        p.c=temp&1;
        p.z=temp&2;
        p.i=temp&4;
        p.d=temp&8;
        p.ex=p.m=0;
        p.v=temp&0x40;
        p.n=temp&0x80;
        s.b.l++; pc=readmem(s.w);       //  //printf("%04X -> %02X\n",s.w,pc);
        s.b.l++; pc|=(readmem(s.w)<<8);  // //printf("%04X -> %02X\n",s.w,pc>>8);
        updatecpumode();
}

/*Shift group*/
static void asla8()
{
        readmem(pbr|pc);
        p.c=a.b.l&0x80;
        a.b.l<<=1;
        setzn8(a.b.l);
}
static void asla16()
{
        readmem(pbr|pc);
        p.c=a.w&0x8000;
        a.w<<=1;
        setzn16(a.w);
}

static void aslZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&0x80;
        temp<<=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void aslZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&0x8000;
        temp<<=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void aslZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&0x80;
        temp<<=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void aslZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&0x8000;
        temp<<=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void aslAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&0x80;
        temp<<=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void aslAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&0x8000;
        temp<<=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void aslAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&0x80;
        temp<<=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void aslAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&0x8000;
        temp<<=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void lsra8()
{
        readmem(pbr|pc);
        p.c=a.b.l&1;
        a.b.l>>=1;
        setzn8(a.b.l);
}
static void lsra16()
{
        readmem(pbr|pc);
        p.c=a.w&1;
        a.w>>=1;
        setzn16(a.w);
}

static void lsrZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void lsrZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void lsrZpx8()
{
        uint8_t temp;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void lsrZpx16()
{
        uint16_t temp;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void lsrAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void lsrAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void lsrAbsx8()
{
        uint8_t temp;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void lsrAbsx16()
{
        uint16_t temp;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        p.c=temp&1;
        temp>>=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void rola8()
{
        readmem(pbr|pc);
        addr=p.c;
        p.c=a.b.l&0x80;
        a.b.l<<=1;
        if (addr) a.b.l|=1;
        setzn8(a.b.l);
}
static void rola16()
{
        readmem(pbr|pc);
        addr=p.c;
        p.c=a.w&0x8000;
        a.w<<=1;
        if (addr) a.w|=1;
        setzn16(a.w);
}

static void rolZp8()
{
        uint8_t temp;
        int tempc;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x80;
        temp<<=1;
        if (tempc) temp|=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void rolZp16()
{
        uint16_t temp;
        int tempc;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x8000;
        temp<<=1;
        if (tempc) temp|=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void rolZpx8()
{
        uint8_t temp;
        int tempc;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x80;
        temp<<=1;
        if (tempc) temp|=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void rolZpx16()
{
        uint16_t temp;
        int tempc;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x8000;
        temp<<=1;
        if (tempc) temp|=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void rolAbs8()
{
        uint8_t temp;
        int tempc;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x80;
        temp<<=1;
        if (tempc) temp|=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void rolAbs16()
{
        uint16_t temp;
        int tempc;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x8000;
        temp<<=1;
        if (tempc) temp|=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void rolAbsx8()
{
        uint8_t temp;
        int tempc;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x80;
        temp<<=1;
        if (tempc) temp|=1;
        setzn8(temp);
        writemem(addr,temp);
}
static void rolAbsx16()
{
        uint16_t temp;
        int tempc;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&0x8000;
        temp<<=1;
        if (tempc) temp|=1;
        setzn16(temp);
        writememw(addr,temp);
}

static void rora8()
{
        readmem(pbr|pc);
        addr=p.c;
        p.c=a.b.l&1;
        a.b.l>>=1;
        if (addr) a.b.l|=0x80;
        setzn8(a.b.l);
}
static void rora16()
{
        readmem(pbr|pc);
        addr=p.c;
        p.c=a.w&1;
        a.w>>=1;
        if (addr) a.w|=0x8000;
        setzn16(a.w);
}

static void rorZp8()
{
        uint8_t temp;
        int tempc;
        addr=zeropage();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x80;
        setzn8(temp);
        writemem(addr,temp);
}
static void rorZp16()
{
        uint16_t temp;
        int tempc;
        addr=zeropage();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x8000;
        setzn16(temp);
        writememw(addr,temp);
}

static void rorZpx8()
{
        uint8_t temp;
        int tempc;
        addr=zeropagex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x80;
        setzn8(temp);
        writemem(addr,temp);
}
static void rorZpx16()
{
        uint16_t temp;
        int tempc;
        addr=zeropagex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x8000;
        setzn16(temp);
        writememw(addr,temp);
}

static void rorAbs8()
{
        uint8_t temp;
        int tempc;
        addr=absolute();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x80;
        setzn8(temp);
        writemem(addr,temp);
}
static void rorAbs16()
{
        uint16_t temp;
        int tempc;
        addr=absolute();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x8000;
        setzn16(temp);
        writememw(addr,temp);
}

static void rorAbsx8()
{
        uint8_t temp;
        int tempc;
        addr=absolutex();
        temp=readmem(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x80;
        setzn8(temp);
        writemem(addr,temp);
}
static void rorAbsx16()
{
        uint16_t temp;
        int tempc;
        addr=absolutex();
        temp=readmemw(addr);
        cycles--; clockspc(6);
        tempc=p.c;
        p.c=temp&1;
        temp>>=1;
        if (tempc) temp|=0x8000;
        setzn16(temp);
        writememw(addr,temp);
}

/*Misc group*/
static void xba()
{
        readmem(pbr|pc);
        a.w=(a.w>>8)|(a.w<<8);
        setzn8(a.b.l);
}
static void nop()
{
        cycles--; clockspc(6);
}

static void tcd()
{
        readmem(pbr|pc);
        dp=a.w;
        setzn16(dp);
}

static void tdc()
{
        readmem(pbr|pc);
        a.w=dp;
        setzn16(a.w);
}

static void tcs()
{
        readmem(pbr|pc);
        s.w=a.w;
}

static void tsc()
{
        readmem(pbr|pc);
        a.w=s.w;
        setzn16(a.w);
}

static void trbZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        p.z=!(a.b.l&temp);
        temp&=~a.b.l;
        cycles--; clockspc(6);
        writemem(addr,temp);
}
static void trbZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        p.z=!(a.w&temp);
        temp&=~a.w;
        cycles--; clockspc(6);
        writememw(addr,temp);
}

static void trbAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        p.z=!(a.b.l&temp);
        temp&=~a.b.l;
        cycles--; clockspc(6);
        writemem(addr,temp);
}
static void trbAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        p.z=!(a.w&temp);
        temp&=~a.w;
        cycles--; clockspc(6);
        writememw(addr,temp);
}

static void tsbZp8()
{
        uint8_t temp;
        addr=zeropage();
        temp=readmem(addr);
        p.z=!(a.b.l&temp);
        temp|=a.b.l;
        cycles--; clockspc(6);
        writemem(addr,temp);
}
static void tsbZp16()
{
        uint16_t temp;
        addr=zeropage();
        temp=readmemw(addr);
        p.z=!(a.w&temp);
        temp|=a.w;
        cycles--; clockspc(6);
        writememw(addr,temp);
}

static void tsbAbs8()
{
        uint8_t temp;
        addr=absolute();
        temp=readmem(addr);
        p.z=!(a.b.l&temp);
        temp|=a.b.l;
        cycles--; clockspc(6);
        writemem(addr,temp);
}
static void tsbAbs16()
{
        uint16_t temp;
        addr=absolute();
        temp=readmemw(addr);
        p.z=!(a.w&temp);
        temp|=a.w;
        cycles--; clockspc(6);
        writememw(addr,temp);
}

static void wai()
{
        readmem(pbr|pc);
        inwai=1;
        pc--;
//        printf("WAI %06X\n",pbr|pc);
}

static void mvp()
{
        uint8_t temp;
        dbr=(readmem(pbr|pc))<<16; pc++;
        addr=(readmem(pbr|pc))<<16; pc++;
        temp=readmem(addr|x.w);
        writemem(dbr|y.w,temp);
        x.w--;
        y.w--;
        a.w--;
        if (a.w!=0xFFFF) pc-=3;
        cycles-=2; clockspc(12);
}

static void mvn()
{
        uint8_t temp;
        dbr=(readmem(pbr|pc))<<16; pc++;
        addr=(readmem(pbr|pc))<<16; pc++;
        temp=readmem(addr|x.w);
        writemem(dbr|y.w,temp);
        x.w++;
        y.w++;
        a.w--;
        if (a.w!=0xFFFF) pc-=3;
        cycles-=2; clockspc(12);
}

static void op_brk()
{
        uint8_t temp=0;
        pc++;
        writemem(s.w,pbr>>16); s.w--;
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF);  s.w--;
        if (p.c) temp|=1;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.ex) temp|=0x10;
        if (p.m) temp|=0x20;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        writemem(s.w,temp);    s.w--;
        pc=readmemw(0xFFE6);
        pbr=0;
        p.i=1;
        p.d=0;
}

static void brke()
{
        uint8_t temp=0;
        pc++;
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF);  s.w--;
        if (p.c) temp|=1;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        writemem(s.w,temp|0x30);    s.w--;
        pc=readmemw(0xFFFE);
        pbr=0;
        p.i=1;
        p.d=0;
}

static void cop()
{
        uint8_t temp=0;
        pc++;
        writemem(s.w,pbr>>16); s.w--;
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF);  s.w--;
        if (p.c) temp|=1;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.ex) temp|=0x10;
        if (p.m) temp|=0x20;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        writemem(s.w,temp);    s.w--;
        pc=readmemw(0xFFE4);
        pbr=0;
        p.i=1;
        p.d=0;
}

static void cope()
{
        uint8_t temp=0;
        pc++;
        writemem(s.w,pc>>8);   s.w--;
        writemem(s.w,pc&0xFF);  s.w--;
        if (p.c) temp|=1;
        if (p.z) temp|=2;
        if (p.i) temp|=4;
        if (p.d) temp|=8;
        if (p.v) temp|=0x40;
        if (p.n) temp|=0x80;
        writemem(s.w,temp);    s.w--;
        pc=readmemw(0xFFF4);
        pbr=0;
        p.i=1;
        p.d=0;
}

static void wdm()
{
        readmem(pc); pc++;
}

static void stp() /*No point emulating this properly as the external support circuitry isn't there*/
{
        pc--;
        cycles-=600;
}

/*Functions*/
void w65816_reset()
{
        def=1;
                if (def || (banking&4)) w65816mask=0xFFFF;
                else                    w65816mask=0x7FFFF;
//        tuberomin=1;
        pbr=dbr=0;
        s.w=0x1FF;
        cpumode=4;
        p.e=1;
        p.i=1;
        pc=readmemw(0xFFFC);
        a.w=x.w=y.w=0;
        p.ex=p.m=1;
        cycles=0;
        //skipz=0;
//        printf("Reset to %04X\n",pc);
//        exit(-1);
}

/*static void w65816_dumpregs()
{
        int c;
        FILE *f=fopen("65816.dmp","wb");

        for (c=0;c<65536;c++) putc(readmem(c),f);
        fclose(f);
        printf("65816 regs :\n");
        printf("A=%04X X=%04X Y=%04X S=%04X\n",a.w,x.w,y.w,s.w);
        printf("PC=%06X DBR=%02X DP=%04X\n",pc|pbr,dbr>>24,dp);
        printf("%c %c %c %i %i\n",(p.e)?'E':' ',(p.ex)?'X':' ',(p.m)?'M':' ',cpumode,wins);
        //printf("89272=%02X\n",readmem(0x89272));
}*/

static void badopcode()
{
//        FILE *f=fopen("rom.dmp","wb");
//        printf("Bad opcode %02X\n",opcode);
//        pc--;
//        dumpregs65816();
        //printf("%02X %06X\n",readmem(0x3F8A82),rom[((0x3F8A82>>16)*0x8000)+(((0x3F8A82>>12)&3)*0x2000)+(0x3F8A82&0x1FFF)]);
//        fwrite(rom,2048*1024,1,f);
//        fclose(f);
//        exit(-1);
}

static void makeopcodetable65816()
{
        int c,d;
        for (c=0;c<256;c++)
        {
                for (d=0;d<5;d++)
                {
                        opcodes[c][d]=badopcode;
                }
        }
        /*LDA group*/
        opcodes[0xA9][0]=opcodes[0xA9][2]=opcodes[0xA9][4]=ldaImm8;
        opcodes[0xA9][1]=opcodes[0xA9][3]=ldaImm16;
        opcodes[0xA5][0]=opcodes[0xA5][2]=opcodes[0xA5][4]=ldaZp8;
        opcodes[0xA5][1]=opcodes[0xA5][3]=ldaZp16;
        opcodes[0xB5][0]=opcodes[0xB5][2]=opcodes[0xB5][4]=ldaZpx8;
        opcodes[0xB5][1]=opcodes[0xB5][3]=ldaZpx16;
        opcodes[0xA3][0]=opcodes[0xA3][2]=opcodes[0xA3][4]=ldaSp8;
        opcodes[0xA3][1]=opcodes[0xA3][3]=ldaSp16;
        opcodes[0xB3][0]=opcodes[0xB3][2]=opcodes[0xB3][4]=ldaSIndirecty8;
        opcodes[0xB3][1]=opcodes[0xB3][3]=ldaSIndirecty16;
        opcodes[0xAD][0]=opcodes[0xAD][2]=opcodes[0xAD][4]=ldaAbs8;
        opcodes[0xAD][1]=opcodes[0xAD][3]=ldaAbs16;
        opcodes[0xBD][0]=opcodes[0xBD][2]=opcodes[0xBD][4]=ldaAbsx8;
        opcodes[0xBD][1]=opcodes[0xBD][3]=ldaAbsx16;
        opcodes[0xB9][0]=opcodes[0xB9][2]=opcodes[0xB9][4]=ldaAbsy8;
        opcodes[0xB9][1]=opcodes[0xB9][3]=ldaAbsy16;
        opcodes[0xAF][0]=opcodes[0xAF][2]=opcodes[0xAF][4]=ldaLong8;
        opcodes[0xAF][1]=opcodes[0xAF][3]=ldaLong16;
        opcodes[0xBF][0]=opcodes[0xBF][2]=opcodes[0xBF][4]=ldaLongx8;
        opcodes[0xBF][1]=opcodes[0xBF][3]=ldaLongx16;
        opcodes[0xB2][0]=opcodes[0xB2][2]=opcodes[0xB2][4]=ldaIndirect8;
        opcodes[0xB2][1]=opcodes[0xB2][3]=ldaIndirect16;
        opcodes[0xA1][0]=opcodes[0xA1][2]=opcodes[0xA1][4]=ldaIndirectx8;
        opcodes[0xA1][1]=opcodes[0xA1][3]=ldaIndirectx16;
        opcodes[0xB1][0]=opcodes[0xB1][2]=opcodes[0xB1][4]=ldaIndirecty8;
        opcodes[0xB1][1]=opcodes[0xB1][3]=ldaIndirecty16;
        opcodes[0xA7][0]=opcodes[0xA7][2]=opcodes[0xA7][4]=ldaIndirectLong8;
        opcodes[0xA7][1]=opcodes[0xA7][3]=ldaIndirectLong16;
        opcodes[0xB7][0]=opcodes[0xB7][2]=opcodes[0xB7][4]=ldaIndirectLongy8;
        opcodes[0xB7][1]=opcodes[0xB7][3]=ldaIndirectLongy16;
        /*LDX group*/
        opcodes[0xA2][0]=opcodes[0xA2][1]=opcodes[0xA2][4]=ldxImm8;
        opcodes[0xA2][2]=opcodes[0xA2][3]=ldxImm16;
        opcodes[0xA6][0]=opcodes[0xA6][1]=opcodes[0xA6][4]=ldxZp8;
        opcodes[0xA6][2]=opcodes[0xA6][3]=ldxZp16;
        opcodes[0xB6][0]=opcodes[0xB6][1]=opcodes[0xB6][4]=ldxZpy8;
        opcodes[0xB6][2]=opcodes[0xB6][3]=ldxZpy16;
        opcodes[0xAE][0]=opcodes[0xAE][1]=opcodes[0xAE][4]=ldxAbs8;
        opcodes[0xAE][2]=opcodes[0xAE][3]=ldxAbs16;
        opcodes[0xBE][0]=opcodes[0xBE][1]=opcodes[0xBE][4]=ldxAbsy8;
        opcodes[0xBE][2]=opcodes[0xBE][3]=ldxAbsy16;
        /*LDY group*/
        opcodes[0xA0][0]=opcodes[0xA0][1]=opcodes[0xA0][4]=ldyImm8;
        opcodes[0xA0][2]=opcodes[0xA0][3]=ldyImm16;
        opcodes[0xA4][0]=opcodes[0xA4][1]=opcodes[0xA4][4]=ldyZp8;
        opcodes[0xA4][2]=opcodes[0xA4][3]=ldyZp16;
        opcodes[0xB4][0]=opcodes[0xB4][1]=opcodes[0xB4][4]=ldyZpx8;
        opcodes[0xB4][2]=opcodes[0xB4][3]=ldyZpx16;
        opcodes[0xAC][0]=opcodes[0xAC][1]=opcodes[0xAC][4]=ldyAbs8;
        opcodes[0xAC][2]=opcodes[0xAC][3]=ldyAbs16;
        opcodes[0xBC][0]=opcodes[0xBC][1]=opcodes[0xBC][4]=ldyAbsx8;
        opcodes[0xBC][2]=opcodes[0xBC][3]=ldyAbsx16;

        /*STA group*/
        opcodes[0x85][0]=opcodes[0x85][2]=opcodes[0x85][4]=staZp8;
        opcodes[0x85][1]=opcodes[0x85][3]=staZp16;
        opcodes[0x95][0]=opcodes[0x95][2]=opcodes[0x95][4]=staZpx8;
        opcodes[0x95][1]=opcodes[0x95][3]=staZpx16;
        opcodes[0x8D][0]=opcodes[0x8D][2]=opcodes[0x8D][4]=staAbs8;
        opcodes[0x8D][1]=opcodes[0x8D][3]=staAbs16;
        opcodes[0x9D][0]=opcodes[0x9D][2]=opcodes[0x9D][4]=staAbsx8;
        opcodes[0x9D][1]=opcodes[0x9D][3]=staAbsx16;
        opcodes[0x99][0]=opcodes[0x99][2]=opcodes[0x99][4]=staAbsy8;
        opcodes[0x99][1]=opcodes[0x99][3]=staAbsy16;
        opcodes[0x8F][0]=opcodes[0x8F][2]=opcodes[0x8F][4]=staLong8;
        opcodes[0x8F][1]=opcodes[0x8F][3]=staLong16;
        opcodes[0x9F][0]=opcodes[0x9F][2]=opcodes[0x9F][4]=staLongx8;
        opcodes[0x9F][1]=opcodes[0x9F][3]=staLongx16;
        opcodes[0x92][0]=opcodes[0x92][2]=opcodes[0x92][4]=staIndirect8;
        opcodes[0x92][1]=opcodes[0x92][3]=staIndirect16;
        opcodes[0x81][0]=opcodes[0x81][2]=opcodes[0x81][4]=staIndirectx8;
        opcodes[0x81][1]=opcodes[0x81][3]=staIndirectx16;
        opcodes[0x91][0]=opcodes[0x91][2]=opcodes[0x91][4]=staIndirecty8;
        opcodes[0x91][1]=opcodes[0x91][3]=staIndirecty16;
        opcodes[0x87][0]=opcodes[0x87][2]=opcodes[0x87][4]=staIndirectLong8;
        opcodes[0x87][1]=opcodes[0x87][3]=staIndirectLong16;
        opcodes[0x97][0]=opcodes[0x97][2]=opcodes[0x97][4]=staIndirectLongy8;
        opcodes[0x97][1]=opcodes[0x97][3]=staIndirectLongy16;
        opcodes[0x83][0]=opcodes[0x83][2]=opcodes[0x83][4]=staSp8;
        opcodes[0x83][1]=opcodes[0x83][3]=staSp16;
        opcodes[0x93][0]=opcodes[0x93][2]=opcodes[0x93][4]=staSIndirecty8;
        opcodes[0x93][1]=opcodes[0x93][3]=staSIndirecty16;
        /*STX group*/
        opcodes[0x86][0]=opcodes[0x86][1]=opcodes[0x86][4]=stxZp8;
        opcodes[0x86][2]=opcodes[0x86][3]=stxZp16;
        opcodes[0x96][0]=opcodes[0x96][1]=opcodes[0x96][4]=stxZpy8;
        opcodes[0x96][2]=opcodes[0x96][3]=stxZpy16;
        opcodes[0x8E][0]=opcodes[0x8E][1]=opcodes[0x8E][4]=stxAbs8;
        opcodes[0x8E][2]=opcodes[0x8E][3]=stxAbs16;
        /*STY group*/
        opcodes[0x84][0]=opcodes[0x84][1]=opcodes[0x84][4]=styZp8;
        opcodes[0x84][2]=opcodes[0x84][3]=styZp16;
        opcodes[0x94][0]=opcodes[0x94][1]=opcodes[0x94][4]=styZpx8;
        opcodes[0x94][2]=opcodes[0x94][3]=styZpx16;
        opcodes[0x8C][0]=opcodes[0x8C][1]=opcodes[0x8C][4]=styAbs8;
        opcodes[0x8C][2]=opcodes[0x8C][3]=styAbs16;
        /*STZ group*/
        opcodes[0x64][0]=opcodes[0x64][2]=opcodes[0x64][4]=stzZp8;
        opcodes[0x64][1]=opcodes[0x64][3]=stzZp16;
        opcodes[0x74][0]=opcodes[0x74][2]=opcodes[0x74][4]=stzZpx8;
        opcodes[0x74][1]=opcodes[0x74][3]=stzZpx16;
        opcodes[0x9C][0]=opcodes[0x9C][2]=opcodes[0x9C][4]=stzAbs8;
        opcodes[0x9C][1]=opcodes[0x9C][3]=stzAbs16;
        opcodes[0x9E][0]=opcodes[0x9E][2]=opcodes[0x9E][4]=stzAbsx8;
        opcodes[0x9E][1]=opcodes[0x9E][3]=stzAbsx16;

        opcodes[0x3A][0]=opcodes[0x3A][2]=opcodes[0x3A][4]=deca8;
        opcodes[0x3A][1]=opcodes[0x3A][3]=deca16;
        opcodes[0xCA][0]=opcodes[0xCA][1]=opcodes[0xCA][4]=dex8;
        opcodes[0xCA][2]=opcodes[0xCA][3]=dex16;
        opcodes[0x88][0]=opcodes[0x88][1]=opcodes[0x88][4]=dey8;
        opcodes[0x88][2]=opcodes[0x88][3]=dey16;
        opcodes[0x1A][0]=opcodes[0x1A][2]=opcodes[0x1A][4]=inca8;
        opcodes[0x1A][1]=opcodes[0x1A][3]=inca16;
        opcodes[0xE8][0]=opcodes[0xE8][1]=opcodes[0xE8][4]=inx8;
        opcodes[0xE8][2]=opcodes[0xE8][3]=inx16;
        opcodes[0xC8][0]=opcodes[0xC8][1]=opcodes[0xC8][4]=iny8;
        opcodes[0xC8][2]=opcodes[0xC8][3]=iny16;

        /*INC group*/
        opcodes[0xE6][0]=opcodes[0xE6][2]=opcodes[0xE6][4]=incZp8;
        opcodes[0xE6][1]=opcodes[0xE6][3]=incZp16;
        opcodes[0xF6][0]=opcodes[0xF6][2]=opcodes[0xF6][4]=incZpx8;
        opcodes[0xF6][1]=opcodes[0xF6][3]=incZpx16;
        opcodes[0xEE][0]=opcodes[0xEE][2]=opcodes[0xEE][4]=incAbs8;
        opcodes[0xEE][1]=opcodes[0xEE][3]=incAbs16;
        opcodes[0xFE][0]=opcodes[0xFE][2]=opcodes[0xFE][4]=incAbsx8;
        opcodes[0xFE][1]=opcodes[0xFE][3]=incAbsx16;

        /*DEC group*/
        opcodes[0xC6][0]=opcodes[0xC6][2]=opcodes[0xC6][4]=decZp8;
        opcodes[0xC6][1]=opcodes[0xC6][3]=decZp16;
        opcodes[0xD6][0]=opcodes[0xD6][2]=opcodes[0xD6][4]=decZpx8;
        opcodes[0xD6][1]=opcodes[0xD6][3]=decZpx16;
        opcodes[0xCE][0]=opcodes[0xCE][2]=opcodes[0xCE][4]=decAbs8;
        opcodes[0xCE][1]=opcodes[0xCE][3]=decAbs16;
        opcodes[0xDE][0]=opcodes[0xDE][2]=opcodes[0xDE][4]=decAbsx8;
        opcodes[0xDE][1]=opcodes[0xDE][3]=decAbsx16;

        /*AND group*/
        opcodes[0x29][0]=opcodes[0x29][2]=opcodes[0x29][4]=andImm8;
        opcodes[0x29][1]=opcodes[0x29][3]=andImm16;
        opcodes[0x25][0]=opcodes[0x25][2]=opcodes[0x25][4]=andZp8;
        opcodes[0x25][1]=opcodes[0x25][3]=andZp16;
        opcodes[0x35][0]=opcodes[0x35][2]=opcodes[0x35][4]=andZpx8;
        opcodes[0x35][1]=opcodes[0x35][3]=andZpx16;
        opcodes[0x23][0]=opcodes[0x23][2]=opcodes[0x23][4]=andSp8;
        opcodes[0x23][1]=opcodes[0x23][3]=andSp16;
        opcodes[0x2D][0]=opcodes[0x2D][2]=opcodes[0x2D][4]=andAbs8;
        opcodes[0x2D][1]=opcodes[0x2D][3]=andAbs16;
        opcodes[0x3D][0]=opcodes[0x3D][2]=opcodes[0x3D][4]=andAbsx8;
        opcodes[0x3D][1]=opcodes[0x3D][3]=andAbsx16;
        opcodes[0x39][0]=opcodes[0x39][2]=opcodes[0x39][4]=andAbsy8;
        opcodes[0x39][1]=opcodes[0x39][3]=andAbsy16;
        opcodes[0x2F][0]=opcodes[0x2F][2]=opcodes[0x2F][4]=andLong8;
        opcodes[0x2F][1]=opcodes[0x2F][3]=andLong16;
        opcodes[0x3F][0]=opcodes[0x3F][2]=opcodes[0x3F][4]=andLongx8;
        opcodes[0x3F][1]=opcodes[0x3F][3]=andLongx16;
        opcodes[0x32][0]=opcodes[0x32][2]=opcodes[0x32][4]=andIndirect8;
        opcodes[0x32][1]=opcodes[0x32][3]=andIndirect16;
        opcodes[0x21][0]=opcodes[0x21][2]=opcodes[0x21][4]=andIndirectx8;
        opcodes[0x21][1]=opcodes[0x21][3]=andIndirectx16;
        opcodes[0x31][0]=opcodes[0x31][2]=opcodes[0x31][4]=andIndirecty8;
        opcodes[0x31][1]=opcodes[0x31][3]=andIndirecty16;
        opcodes[0x33][0]=opcodes[0x33][2]=opcodes[0x33][4]=andsIndirecty8;
        opcodes[0x33][1]=opcodes[0x33][3]=andsIndirecty16;
        opcodes[0x27][0]=opcodes[0x27][2]=opcodes[0x27][4]=andIndirectLong8;
        opcodes[0x27][1]=opcodes[0x27][3]=andIndirectLong16;
        opcodes[0x37][0]=opcodes[0x37][2]=opcodes[0x37][4]=andIndirectLongy8;
        opcodes[0x37][1]=opcodes[0x37][3]=andIndirectLongy16;

        /*EOR group*/
        opcodes[0x49][0]=opcodes[0x49][2]=opcodes[0x49][4]=eorImm8;
        opcodes[0x49][1]=opcodes[0x49][3]=eorImm16;
        opcodes[0x45][0]=opcodes[0x45][2]=opcodes[0x45][4]=eorZp8;
        opcodes[0x45][1]=opcodes[0x45][3]=eorZp16;
        opcodes[0x55][0]=opcodes[0x55][2]=opcodes[0x55][4]=eorZpx8;
        opcodes[0x55][1]=opcodes[0x55][3]=eorZpx16;
        opcodes[0x43][0]=opcodes[0x43][2]=opcodes[0x43][4]=eorSp8;
        opcodes[0x43][1]=opcodes[0x43][3]=eorSp16;
        opcodes[0x4D][0]=opcodes[0x4D][2]=opcodes[0x4D][4]=eorAbs8;
        opcodes[0x4D][1]=opcodes[0x4D][3]=eorAbs16;
        opcodes[0x5D][0]=opcodes[0x5D][2]=opcodes[0x5D][4]=eorAbsx8;
        opcodes[0x5D][1]=opcodes[0x5D][3]=eorAbsx16;
        opcodes[0x59][0]=opcodes[0x59][2]=opcodes[0x59][4]=eorAbsy8;
        opcodes[0x59][1]=opcodes[0x59][3]=eorAbsy16;
        opcodes[0x4F][0]=opcodes[0x4F][2]=opcodes[0x4F][4]=eorLong8;
        opcodes[0x4F][1]=opcodes[0x4F][3]=eorLong16;
        opcodes[0x5F][0]=opcodes[0x5F][2]=opcodes[0x5F][4]=eorLongx8;
        opcodes[0x5F][1]=opcodes[0x5F][3]=eorLongx16;
        opcodes[0x52][0]=opcodes[0x52][2]=opcodes[0x52][4]=eorIndirect8;
        opcodes[0x52][1]=opcodes[0x52][3]=eorIndirect16;
        opcodes[0x41][0]=opcodes[0x41][2]=opcodes[0x41][4]=eorIndirectx8;
        opcodes[0x41][1]=opcodes[0x41][3]=eorIndirectx16;
        opcodes[0x51][0]=opcodes[0x51][2]=opcodes[0x51][4]=eorIndirecty8;
        opcodes[0x51][1]=opcodes[0x51][3]=eorIndirecty16;
        opcodes[0x53][0]=opcodes[0x53][2]=opcodes[0x53][4]=eorsIndirecty8;
        opcodes[0x53][1]=opcodes[0x53][3]=eorsIndirecty16;
        opcodes[0x47][0]=opcodes[0x47][2]=opcodes[0x47][4]=eorIndirectLong8;
        opcodes[0x47][1]=opcodes[0x47][3]=eorIndirectLong16;
        opcodes[0x57][0]=opcodes[0x57][2]=opcodes[0x57][4]=eorIndirectLongy8;
        opcodes[0x57][1]=opcodes[0x57][3]=eorIndirectLongy16;

        /*ORA group*/
        opcodes[0x09][0]=opcodes[0x09][2]=opcodes[0x09][4]=oraImm8;
        opcodes[0x09][1]=opcodes[0x09][3]=oraImm16;
        opcodes[0x05][0]=opcodes[0x05][2]=opcodes[0x05][4]=oraZp8;
        opcodes[0x05][1]=opcodes[0x05][3]=oraZp16;
        opcodes[0x15][0]=opcodes[0x15][2]=opcodes[0x15][4]=oraZpx8;
        opcodes[0x15][1]=opcodes[0x15][3]=oraZpx16;
        opcodes[0x03][0]=opcodes[0x03][2]=opcodes[0x03][4]=oraSp8;
        opcodes[0x03][1]=opcodes[0x03][3]=oraSp16;
        opcodes[0x0D][0]=opcodes[0x0D][2]=opcodes[0x0D][4]=oraAbs8;
        opcodes[0x0D][1]=opcodes[0x0D][3]=oraAbs16;
        opcodes[0x1D][0]=opcodes[0x1D][2]=opcodes[0x1D][4]=oraAbsx8;
        opcodes[0x1D][1]=opcodes[0x1D][3]=oraAbsx16;
        opcodes[0x19][0]=opcodes[0x19][2]=opcodes[0x19][4]=oraAbsy8;
        opcodes[0x19][1]=opcodes[0x19][3]=oraAbsy16;
        opcodes[0x0F][0]=opcodes[0x0F][2]=opcodes[0x0F][4]=oraLong8;
        opcodes[0x0F][1]=opcodes[0x0F][3]=oraLong16;
        opcodes[0x1F][0]=opcodes[0x1F][2]=opcodes[0x1F][4]=oraLongx8;
        opcodes[0x1F][1]=opcodes[0x1F][3]=oraLongx16;
        opcodes[0x12][0]=opcodes[0x12][2]=opcodes[0x12][4]=oraIndirect8;
        opcodes[0x12][1]=opcodes[0x12][3]=oraIndirect16;
        opcodes[0x01][0]=opcodes[0x01][2]=opcodes[0x01][4]=oraIndirectx8;
        opcodes[0x01][1]=opcodes[0x01][3]=oraIndirectx16;
        opcodes[0x11][0]=opcodes[0x11][2]=opcodes[0x11][4]=oraIndirecty8;
        opcodes[0x11][1]=opcodes[0x11][3]=oraIndirecty16;
        opcodes[0x13][0]=opcodes[0x13][2]=opcodes[0x13][4]=orasIndirecty8;
        opcodes[0x13][1]=opcodes[0x13][3]=orasIndirecty16;
        opcodes[0x07][0]=opcodes[0x07][2]=opcodes[0x07][4]=oraIndirectLong8;
        opcodes[0x07][1]=opcodes[0x07][3]=oraIndirectLong16;
        opcodes[0x17][0]=opcodes[0x17][2]=opcodes[0x17][4]=oraIndirectLongy8;
        opcodes[0x17][1]=opcodes[0x17][3]=oraIndirectLongy16;

        /*ADC group*/
        opcodes[0x69][0]=opcodes[0x69][2]=opcodes[0x69][4]=adcImm8;
        opcodes[0x69][1]=opcodes[0x69][3]=adcImm16;
        opcodes[0x65][0]=opcodes[0x65][2]=opcodes[0x65][4]=adcZp8;
        opcodes[0x65][1]=opcodes[0x65][3]=adcZp16;
        opcodes[0x75][0]=opcodes[0x75][2]=opcodes[0x75][4]=adcZpx8;
        opcodes[0x75][1]=opcodes[0x75][3]=adcZpx16;
        opcodes[0x63][0]=opcodes[0x63][2]=opcodes[0x63][4]=adcSp8;
        opcodes[0x63][1]=opcodes[0x63][3]=adcSp16;
        opcodes[0x6D][0]=opcodes[0x6D][2]=opcodes[0x6D][4]=adcAbs8;
        opcodes[0x6D][1]=opcodes[0x6D][3]=adcAbs16;
        opcodes[0x7D][0]=opcodes[0x7D][2]=opcodes[0x7D][4]=adcAbsx8;
        opcodes[0x7D][1]=opcodes[0x7D][3]=adcAbsx16;
        opcodes[0x79][0]=opcodes[0x79][2]=opcodes[0x79][4]=adcAbsy8;
        opcodes[0x79][1]=opcodes[0x79][3]=adcAbsy16;
        opcodes[0x6F][0]=opcodes[0x6F][2]=opcodes[0x6F][4]=adcLong8;
        opcodes[0x6F][1]=opcodes[0x6F][3]=adcLong16;
        opcodes[0x7F][0]=opcodes[0x7F][2]=opcodes[0x7F][4]=adcLongx8;
        opcodes[0x7F][1]=opcodes[0x7F][3]=adcLongx16;
        opcodes[0x72][0]=opcodes[0x72][2]=opcodes[0x72][4]=adcIndirect8;
        opcodes[0x72][1]=opcodes[0x72][3]=adcIndirect16;
        opcodes[0x61][0]=opcodes[0x61][2]=opcodes[0x61][4]=adcIndirectx8;
        opcodes[0x61][1]=opcodes[0x61][3]=adcIndirectx16;
        opcodes[0x71][0]=opcodes[0x71][2]=opcodes[0x71][4]=adcIndirecty8;
        opcodes[0x71][1]=opcodes[0x71][3]=adcIndirecty16;
        opcodes[0x73][0]=opcodes[0x73][2]=opcodes[0x73][4]=adcsIndirecty8;
        opcodes[0x73][1]=opcodes[0x73][3]=adcsIndirecty16;
        opcodes[0x67][0]=opcodes[0x67][2]=opcodes[0x67][4]=adcIndirectLong8;
        opcodes[0x67][1]=opcodes[0x67][3]=adcIndirectLong16;
        opcodes[0x77][0]=opcodes[0x77][2]=opcodes[0x77][4]=adcIndirectLongy8;
        opcodes[0x77][1]=opcodes[0x77][3]=adcIndirectLongy16;

        /*SBC group*/
        opcodes[0xE9][0]=opcodes[0xE9][2]=opcodes[0xE9][4]=sbcImm8;
        opcodes[0xE9][1]=opcodes[0xE9][3]=sbcImm16;
        opcodes[0xE5][0]=opcodes[0xE5][2]=opcodes[0xE5][4]=sbcZp8;
        opcodes[0xE5][1]=opcodes[0xE5][3]=sbcZp16;
        opcodes[0xE3][0]=opcodes[0xE3][2]=opcodes[0xE3][4]=sbcSp8;
        opcodes[0xE3][1]=opcodes[0xE3][3]=sbcSp16;
        opcodes[0xF5][0]=opcodes[0xF5][2]=opcodes[0xF5][4]=sbcZpx8;
        opcodes[0xF5][1]=opcodes[0xF5][3]=sbcZpx16;
        opcodes[0xED][0]=opcodes[0xED][2]=opcodes[0xED][4]=sbcAbs8;
        opcodes[0xED][1]=opcodes[0xED][3]=sbcAbs16;
        opcodes[0xFD][0]=opcodes[0xFD][2]=opcodes[0xFD][4]=sbcAbsx8;
        opcodes[0xFD][1]=opcodes[0xFD][3]=sbcAbsx16;
        opcodes[0xF9][0]=opcodes[0xF9][2]=opcodes[0xF9][4]=sbcAbsy8;
        opcodes[0xF9][1]=opcodes[0xF9][3]=sbcAbsy16;
        opcodes[0xEF][0]=opcodes[0xEF][2]=opcodes[0xEF][4]=sbcLong8;
        opcodes[0xEF][1]=opcodes[0xEF][3]=sbcLong16;
        opcodes[0xFF][0]=opcodes[0xFF][2]=opcodes[0xFF][4]=sbcLongx8;
        opcodes[0xFF][1]=opcodes[0xFF][3]=sbcLongx16;
        opcodes[0xF2][0]=opcodes[0xF2][2]=opcodes[0xF2][4]=sbcIndirect8;
        opcodes[0xF2][1]=opcodes[0xF2][3]=sbcIndirect16;
        opcodes[0xE1][0]=opcodes[0xE1][2]=opcodes[0xE1][4]=sbcIndirectx8;
        opcodes[0xE1][1]=opcodes[0xE1][3]=sbcIndirectx16;
        opcodes[0xF1][0]=opcodes[0xF1][2]=opcodes[0xF1][4]=sbcIndirecty8;
        opcodes[0xF1][1]=opcodes[0xF1][3]=sbcIndirecty16;
        opcodes[0xF3][0]=opcodes[0xF3][2]=opcodes[0xF3][4]=sbcsIndirecty8;
        opcodes[0xF3][1]=opcodes[0xF3][3]=sbcsIndirecty16;
        opcodes[0xE7][0]=opcodes[0xE7][2]=opcodes[0xE7][4]=sbcIndirectLong8;
        opcodes[0xE7][1]=opcodes[0xE7][3]=sbcIndirectLong16;
        opcodes[0xF7][0]=opcodes[0xF7][2]=opcodes[0xF7][4]=sbcIndirectLongy8;
        opcodes[0xF7][1]=opcodes[0xF7][3]=sbcIndirectLongy16;

        /*Transfer group*/
        opcodes[0xAA][0]=opcodes[0xAA][1]=opcodes[0xAA][4]=tax8;
        opcodes[0xAA][2]=opcodes[0xAA][3]=tax16;
        opcodes[0xA8][0]=opcodes[0xA8][1]=opcodes[0xA8][4]=tay8;
        opcodes[0xA8][2]=opcodes[0xA8][3]=tay16;
        opcodes[0x8A][0]=opcodes[0x8A][2]=opcodes[0x8A][4]=txa8;
        opcodes[0x8A][1]=opcodes[0x8A][3]=txa16;
        opcodes[0x98][0]=opcodes[0x98][2]=opcodes[0x98][4]=tya8;
        opcodes[0x98][1]=opcodes[0x98][3]=tya16;
        opcodes[0x9B][0]=opcodes[0x9B][1]=opcodes[0x9B][4]=txy8;
        opcodes[0x9B][2]=opcodes[0x9B][3]=txy16;
        opcodes[0xBB][0]=opcodes[0xBB][1]=opcodes[0xBB][4]=tyx8;
        opcodes[0xBB][2]=opcodes[0xBB][3]=tyx16;
        opcodes[0xBA][0]=opcodes[0xBA][1]=opcodes[0xBA][4]=tsx8;
        opcodes[0xBA][2]=opcodes[0xBA][3]=tsx16;
        opcodes[0x9A][0]=opcodes[0x9A][1]=opcodes[0x9A][4]=txs8;
        opcodes[0x9A][2]=opcodes[0x9A][3]=txs16;

        /*Flag Group*/
        opcodes[0x18][0]=opcodes[0x18][1]=opcodes[0x18][2]=
                         opcodes[0x18][3]=opcodes[0x18][4]=clc;
        opcodes[0xD8][0]=opcodes[0xD8][1]=opcodes[0xD8][2]=
                         opcodes[0xD8][3]=opcodes[0xD8][4]=cld;
        opcodes[0x58][0]=opcodes[0x58][1]=opcodes[0x58][2]=
                         opcodes[0x58][3]=opcodes[0x58][4]=cli;
        opcodes[0xB8][0]=opcodes[0xB8][1]=opcodes[0xB8][2]=
                         opcodes[0xB8][3]=opcodes[0xB8][4]=clv;
        opcodes[0x38][0]=opcodes[0x38][1]=opcodes[0x38][2]=
                         opcodes[0x38][3]=opcodes[0x38][4]=sec;
        opcodes[0xF8][0]=opcodes[0xF8][1]=opcodes[0xF8][2]=
                         opcodes[0xF8][3]=opcodes[0xF8][4]=sed;
        opcodes[0x78][0]=opcodes[0x78][1]=opcodes[0x78][2]=
                         opcodes[0x78][3]=opcodes[0x78][4]=sei;
        opcodes[0xFB][0]=opcodes[0xFB][1]=opcodes[0xFB][2]=
                         opcodes[0xFB][3]=opcodes[0xFB][4]=xce;
        opcodes[0xE2][0]=opcodes[0xE2][1]=opcodes[0xE2][2]=
                         opcodes[0xE2][3]=opcodes[0xE2][4]=sep;
        opcodes[0xC2][0]=opcodes[0xC2][1]=opcodes[0xC2][2]=
                         opcodes[0xC2][3]=opcodes[0xC2][4]=rep65816;

        /*Stack group*/
        opcodes[0x8B][0]=opcodes[0x8B][1]=opcodes[0x8B][2]=
                                          opcodes[0x8B][3]=phb;
        opcodes[0x8B][4]=phbe;
        opcodes[0x4B][0]=opcodes[0x4B][1]=opcodes[0x4B][2]=
                                          opcodes[0x4B][3]=phk;
        opcodes[0x4B][4]=phke;
        opcodes[0xAB][0]=opcodes[0xAB][1]=opcodes[0xAB][2]=
                                          opcodes[0xAB][3]=plb;
        opcodes[0xAB][4]=plbe;
        opcodes[0x08][0]=opcodes[0x08][1]=opcodes[0x08][2]=
                                          opcodes[0x08][3]=php;
        opcodes[0x08][4]=phpe;
        opcodes[0x28][0]=opcodes[0x28][1]=opcodes[0x28][2]=
                                          opcodes[0x28][3]=plp;
        opcodes[0x28][4]=plpe;
        opcodes[0x48][0]=opcodes[0x48][2]=opcodes[0x48][4]=pha8;
        opcodes[0x48][1]=opcodes[0x48][3]=pha16;
        opcodes[0xDA][0]=opcodes[0xDA][1]=opcodes[0xDA][4]=phx8;
        opcodes[0xDA][2]=opcodes[0xDA][3]=phx16;
        opcodes[0x5A][0]=opcodes[0x5A][1]=opcodes[0x5A][4]=phy8;
        opcodes[0x5A][2]=opcodes[0x5A][3]=phy16;
        opcodes[0x68][0]=opcodes[0x68][2]=opcodes[0x68][4]=pla8;
        opcodes[0x68][1]=opcodes[0x68][3]=pla16;
        opcodes[0xFA][0]=opcodes[0xFA][1]=opcodes[0xFA][4]=plx8;
        opcodes[0xFA][2]=opcodes[0xFA][3]=plx16;
        opcodes[0x7A][0]=opcodes[0x7A][1]=opcodes[0x7A][4]=ply8;
        opcodes[0x7A][2]=opcodes[0x7A][3]=ply16;
        opcodes[0xD4][0]=opcodes[0xD4][1]=opcodes[0xD4][2]=
                         opcodes[0xD4][3]=opcodes[0xD4][4]=pei;
        opcodes[0xF4][0]=opcodes[0xF4][1]=opcodes[0xF4][2]=
                         opcodes[0xF4][3]=opcodes[0xF4][4]=pea;
        opcodes[0x62][0]=opcodes[0x62][1]=opcodes[0x62][2]=
                         opcodes[0x62][3]=opcodes[0x62][4]=per;
        opcodes[0x0B][0]=opcodes[0x0B][1]=opcodes[0x0B][2]=
                         opcodes[0x0B][3]=opcodes[0x0B][4]=phd;
        opcodes[0x2B][0]=opcodes[0x2B][1]=opcodes[0x2B][2]=
                         opcodes[0x2B][3]=opcodes[0x2B][4]=pld;

        /*CMP group*/
        opcodes[0xC9][0]=opcodes[0xC9][2]=opcodes[0xC9][4]=cmpImm8;
        opcodes[0xC9][1]=opcodes[0xC9][3]=cmpImm16;
        opcodes[0xC5][0]=opcodes[0xC5][2]=opcodes[0xC5][4]=cmpZp8;
        opcodes[0xC5][1]=opcodes[0xC5][3]=cmpZp16;
        opcodes[0xC3][0]=opcodes[0xC3][2]=opcodes[0xC3][4]=cmpSp8;
        opcodes[0xC3][1]=opcodes[0xC3][3]=cmpSp16;
        opcodes[0xD5][0]=opcodes[0xD5][2]=opcodes[0xD5][4]=cmpZpx8;
        opcodes[0xD5][1]=opcodes[0xD5][3]=cmpZpx16;
        opcodes[0xCD][0]=opcodes[0xCD][2]=opcodes[0xCD][4]=cmpAbs8;
        opcodes[0xCD][1]=opcodes[0xCD][3]=cmpAbs16;
        opcodes[0xDD][0]=opcodes[0xDD][2]=opcodes[0xDD][4]=cmpAbsx8;
        opcodes[0xDD][1]=opcodes[0xDD][3]=cmpAbsx16;
        opcodes[0xD9][0]=opcodes[0xD9][2]=opcodes[0xD9][4]=cmpAbsy8;
        opcodes[0xD9][1]=opcodes[0xD9][3]=cmpAbsy16;
        opcodes[0xCF][0]=opcodes[0xCF][2]=opcodes[0xCF][4]=cmpLong8;
        opcodes[0xCF][1]=opcodes[0xCF][3]=cmpLong16;
        opcodes[0xDF][0]=opcodes[0xDF][2]=opcodes[0xDF][4]=cmpLongx8;
        opcodes[0xDF][1]=opcodes[0xDF][3]=cmpLongx16;
        opcodes[0xD2][0]=opcodes[0xD2][2]=opcodes[0xD2][4]=cmpIndirect8;
        opcodes[0xD2][1]=opcodes[0xD2][3]=cmpIndirect16;
        opcodes[0xC1][0]=opcodes[0xC1][2]=opcodes[0xC1][4]=cmpIndirectx8;
        opcodes[0xC1][1]=opcodes[0xC1][3]=cmpIndirectx16;
        opcodes[0xD1][0]=opcodes[0xD1][2]=opcodes[0xD1][4]=cmpIndirecty8;
        opcodes[0xD1][1]=opcodes[0xD1][3]=cmpIndirecty16;
        opcodes[0xD3][0]=opcodes[0xD3][2]=opcodes[0xD3][4]=cmpsIndirecty8;
        opcodes[0xD3][1]=opcodes[0xD3][3]=cmpsIndirecty16;
        opcodes[0xC7][0]=opcodes[0xC7][2]=opcodes[0xC7][4]=cmpIndirectLong8;
        opcodes[0xC7][1]=opcodes[0xC7][3]=cmpIndirectLong16;
        opcodes[0xD7][0]=opcodes[0xD7][2]=opcodes[0xD7][4]=cmpIndirectLongy8;
        opcodes[0xD7][1]=opcodes[0xD7][3]=cmpIndirectLongy16;

        /*CPX group*/
        opcodes[0xE0][0]=opcodes[0xE0][1]=opcodes[0xE0][4]=cpxImm8;
        opcodes[0xE0][2]=opcodes[0xE0][3]=cpxImm16;
        opcodes[0xE4][0]=opcodes[0xE4][1]=opcodes[0xE4][4]=cpxZp8;
        opcodes[0xE4][2]=opcodes[0xE4][3]=cpxZp16;
        opcodes[0xEC][0]=opcodes[0xEC][1]=opcodes[0xEC][4]=cpxAbs8;
        opcodes[0xEC][2]=opcodes[0xEC][3]=cpxAbs16;

        /*CPY group*/
        opcodes[0xC0][0]=opcodes[0xC0][1]=opcodes[0xC0][4]=cpyImm8;
        opcodes[0xC0][2]=opcodes[0xC0][3]=cpyImm16;
        opcodes[0xC4][0]=opcodes[0xC4][1]=opcodes[0xC4][4]=cpyZp8;
        opcodes[0xC4][2]=opcodes[0xC4][3]=cpyZp16;
        opcodes[0xCC][0]=opcodes[0xCC][1]=opcodes[0xCC][4]=cpyAbs8;
        opcodes[0xCC][2]=opcodes[0xCC][3]=cpyAbs16;

        /*Branch group*/
        opcodes[0x90][0]=opcodes[0x90][1]=opcodes[0x90][2]=
                         opcodes[0x90][3]=opcodes[0x90][4]=bcc;
        opcodes[0xB0][0]=opcodes[0xB0][1]=opcodes[0xB0][2]=
                         opcodes[0xB0][3]=opcodes[0xB0][4]=bcs;
        opcodes[0xF0][0]=opcodes[0xF0][1]=opcodes[0xF0][2]=
                         opcodes[0xF0][3]=opcodes[0xF0][4]=beq;
        opcodes[0xD0][0]=opcodes[0xD0][1]=opcodes[0xD0][2]=
                         opcodes[0xD0][3]=opcodes[0xD0][4]=bne;
        opcodes[0x80][0]=opcodes[0x80][1]=opcodes[0x80][2]=
                         opcodes[0x80][3]=opcodes[0x80][4]=bra;
        opcodes[0x82][0]=opcodes[0x82][1]=opcodes[0x82][2]=
                         opcodes[0x82][3]=opcodes[0x82][4]=brl;
        opcodes[0x10][0]=opcodes[0x10][1]=opcodes[0x10][2]=
                         opcodes[0x10][3]=opcodes[0x10][4]=bpl;
        opcodes[0x30][0]=opcodes[0x30][1]=opcodes[0x30][2]=
                         opcodes[0x30][3]=opcodes[0x30][4]=bmi;
        opcodes[0x50][0]=opcodes[0x50][1]=opcodes[0x50][2]=
                         opcodes[0x50][3]=opcodes[0x50][4]=bvc;
        opcodes[0x70][0]=opcodes[0x70][1]=opcodes[0x70][2]=
                         opcodes[0x70][3]=opcodes[0x70][4]=bvs;

        /*Jump group*/
        opcodes[0x4C][0]=opcodes[0x4C][1]=opcodes[0x4C][2]=
                         opcodes[0x4C][3]=opcodes[0x4C][4]=jmp;
        opcodes[0x5C][0]=opcodes[0x5C][1]=opcodes[0x5C][2]=
                         opcodes[0x5C][3]=opcodes[0x5C][4]=jmplong;
        opcodes[0x6C][0]=opcodes[0x6C][1]=opcodes[0x6C][2]=
                         opcodes[0x6C][3]=opcodes[0x6C][4]=jmpind;
        opcodes[0x7C][0]=opcodes[0x7C][1]=opcodes[0x7C][2]=
                         opcodes[0x7C][3]=opcodes[0x7C][4]=jmpindx;
        opcodes[0xDC][0]=opcodes[0xDC][1]=opcodes[0xDC][2]=
                         opcodes[0xDC][3]=opcodes[0xDC][4]=jmlind;
        opcodes[0x20][0]=opcodes[0x20][1]=opcodes[0x20][2]=
                                          opcodes[0x20][3]=jsr;
        opcodes[0x20][4]=jsre;
        opcodes[0xFC][0]=opcodes[0xFC][1]=opcodes[0xFC][2]=
                                          opcodes[0xFC][3]=jsrIndx;
        opcodes[0xFC][4]=jsrIndxe;
        opcodes[0x60][0]=opcodes[0x60][1]=opcodes[0x60][2]=
                                          opcodes[0x60][3]=rts;
        opcodes[0x60][4]=rtse;
        opcodes[0x6B][0]=opcodes[0x6B][1]=opcodes[0x6B][2]=
                                          opcodes[0x6B][3]=rtl;
        opcodes[0x6B][4]=rtle;
        opcodes[0x40][0]=opcodes[0x40][1]=opcodes[0x40][2]=
                                          opcodes[0x40][3]=rti;
        opcodes[0x40][4]=rtie;
        opcodes[0x22][0]=opcodes[0x22][1]=opcodes[0x22][2]=
                                          opcodes[0x22][3]=jsl;
        opcodes[0x22][4]=jsle;

        /*Shift group*/
        opcodes[0x0A][0]=opcodes[0x0A][2]=opcodes[0x0A][4]=asla8;
        opcodes[0x0A][1]=opcodes[0x0A][3]=asla16;
        opcodes[0x06][0]=opcodes[0x06][2]=opcodes[0x06][4]=aslZp8;
        opcodes[0x06][1]=opcodes[0x06][3]=aslZp16;
        opcodes[0x16][0]=opcodes[0x16][2]=opcodes[0x16][4]=aslZpx8;
        opcodes[0x16][1]=opcodes[0x16][3]=aslZpx16;
        opcodes[0x0E][0]=opcodes[0x0E][2]=opcodes[0x0E][4]=aslAbs8;
        opcodes[0x0E][1]=opcodes[0x0E][3]=aslAbs16;
        opcodes[0x1E][0]=opcodes[0x1E][2]=opcodes[0x1E][4]=aslAbsx8;
        opcodes[0x1E][1]=opcodes[0x1E][3]=aslAbsx16;

        opcodes[0x4A][0]=opcodes[0x4A][2]=opcodes[0x4A][4]=lsra8;
        opcodes[0x4A][1]=opcodes[0x4A][3]=lsra16;
        opcodes[0x46][0]=opcodes[0x46][2]=opcodes[0x46][4]=lsrZp8;
        opcodes[0x46][1]=opcodes[0x46][3]=lsrZp16;
        opcodes[0x56][0]=opcodes[0x56][2]=opcodes[0x56][4]=lsrZpx8;
        opcodes[0x56][1]=opcodes[0x56][3]=lsrZpx16;
        opcodes[0x4E][0]=opcodes[0x4E][2]=opcodes[0x4E][4]=lsrAbs8;
        opcodes[0x4E][1]=opcodes[0x4E][3]=lsrAbs16;
        opcodes[0x5E][0]=opcodes[0x5E][2]=opcodes[0x5E][4]=lsrAbsx8;
        opcodes[0x5E][1]=opcodes[0x5E][3]=lsrAbsx16;

        opcodes[0x2A][0]=opcodes[0x2A][2]=opcodes[0x2A][4]=rola8;
        opcodes[0x2A][1]=opcodes[0x2A][3]=rola16;
        opcodes[0x26][0]=opcodes[0x26][2]=opcodes[0x26][4]=rolZp8;
        opcodes[0x26][1]=opcodes[0x26][3]=rolZp16;
        opcodes[0x36][0]=opcodes[0x36][2]=opcodes[0x36][4]=rolZpx8;
        opcodes[0x36][1]=opcodes[0x36][3]=rolZpx16;
        opcodes[0x2E][0]=opcodes[0x2E][2]=opcodes[0x2E][4]=rolAbs8;
        opcodes[0x2E][1]=opcodes[0x2E][3]=rolAbs16;
        opcodes[0x3E][0]=opcodes[0x3E][2]=opcodes[0x3E][4]=rolAbsx8;
        opcodes[0x3E][1]=opcodes[0x3E][3]=rolAbsx16;

        opcodes[0x6A][0]=opcodes[0x6A][2]=opcodes[0x6A][4]=rora8;
        opcodes[0x6A][1]=opcodes[0x6A][3]=rora16;
        opcodes[0x66][0]=opcodes[0x66][2]=opcodes[0x66][4]=rorZp8;
        opcodes[0x66][1]=opcodes[0x66][3]=rorZp16;
        opcodes[0x76][0]=opcodes[0x76][2]=opcodes[0x76][4]=rorZpx8;
        opcodes[0x76][1]=opcodes[0x76][3]=rorZpx16;
        opcodes[0x6E][0]=opcodes[0x6E][2]=opcodes[0x6E][4]=rorAbs8;
        opcodes[0x6E][1]=opcodes[0x6E][3]=rorAbs16;
        opcodes[0x7E][0]=opcodes[0x7E][2]=opcodes[0x7E][4]=rorAbsx8;
        opcodes[0x7E][1]=opcodes[0x7E][3]=rorAbsx16;

        /*BIT group*/
        opcodes[0x89][0]=opcodes[0x89][2]=opcodes[0x89][4]=bitImm8;
        opcodes[0x89][1]=opcodes[0x89][3]=bitImm16;
        opcodes[0x24][0]=opcodes[0x24][2]=opcodes[0x24][4]=bitZp8;
        opcodes[0x24][1]=opcodes[0x24][3]=bitZp16;
        opcodes[0x34][0]=opcodes[0x34][2]=opcodes[0x34][4]=bitZpx8;
        opcodes[0x34][1]=opcodes[0x34][3]=bitZpx16;
        opcodes[0x2C][0]=opcodes[0x2C][2]=opcodes[0x2C][4]=bitAbs8;
        opcodes[0x2C][1]=opcodes[0x2C][3]=bitAbs16;
        opcodes[0x3C][0]=opcodes[0x3C][2]=opcodes[0x3C][4]=bitAbsx8;
        opcodes[0x3C][1]=opcodes[0x3C][3]=bitAbsx16;

        /*Misc group*/
        opcodes[0x00][0]=opcodes[0x00][1]=opcodes[0x00][2]=
                         opcodes[0x00][3]=op_brk;
        opcodes[0x00][4]=brke;
        opcodes[0x02][0]=opcodes[0x02][1]=opcodes[0x02][2]=
                         opcodes[0x02][3]=cop;
        opcodes[0x02][4]=cope;
        opcodes[0xEB][0]=opcodes[0xEB][1]=opcodes[0xEB][2]=
                         opcodes[0xEB][3]=opcodes[0xEB][4]=xba;
        opcodes[0xEA][0]=opcodes[0xEA][1]=opcodes[0xEA][2]=
                         opcodes[0xEA][3]=opcodes[0xEA][4]=nop;
        opcodes[0x5B][0]=opcodes[0x5B][1]=opcodes[0x5B][2]=
                         opcodes[0x5B][3]=opcodes[0x5B][4]=tcd;
        opcodes[0x7B][0]=opcodes[0x7B][1]=opcodes[0x7B][2]=
                         opcodes[0x7B][3]=opcodes[0x7B][4]=tdc;
        opcodes[0x1B][0]=opcodes[0x1B][1]=opcodes[0x1B][2]=
                         opcodes[0x1B][3]=opcodes[0x1B][4]=tcs;
        opcodes[0x3B][0]=opcodes[0x3B][1]=opcodes[0x3B][2]=
                         opcodes[0x3B][3]=opcodes[0x3B][4]=tsc;
        opcodes[0xCB][0]=opcodes[0xCB][1]=opcodes[0xCB][2]=
                         opcodes[0xCB][3]=opcodes[0xCB][4]=wai;
        opcodes[0x44][0]=opcodes[0x44][1]=opcodes[0x44][2]=
                         opcodes[0x44][3]=opcodes[0x44][4]=mvp;
        opcodes[0x54][0]=opcodes[0x54][1]=opcodes[0x54][2]=
                         opcodes[0x54][3]=opcodes[0x54][4]=mvn;
        opcodes[0x04][0]=opcodes[0x04][2]=opcodes[0x04][4]=tsbZp8;
        opcodes[0x04][1]=opcodes[0x04][3]=tsbZp16;
        opcodes[0x0C][0]=opcodes[0x0C][2]=opcodes[0x0C][4]=tsbAbs8;
        opcodes[0x0C][1]=opcodes[0x0C][3]=tsbAbs16;
        opcodes[0x14][0]=opcodes[0x14][2]=opcodes[0x14][4]=trbZp8;
        opcodes[0x14][1]=opcodes[0x14][3]=trbZp16;
        opcodes[0x1C][0]=opcodes[0x1C][2]=opcodes[0x1C][4]=trbAbs8;
        opcodes[0x1C][1]=opcodes[0x1C][3]=trbAbs16;
        opcodes[0x42][0]=opcodes[0x42][1]=opcodes[0x42][2]=
                         opcodes[0x42][3]=opcodes[0x42][4]=wdm;
        opcodes[0xDB][0]=opcodes[0xDB][1]=opcodes[0xDB][2]=
                         opcodes[0xDB][3]=opcodes[0xDB][4]=stp;
}

void w65816_init()
{
        FILE *f;
        char fn[512];
        if (!w65816rom) w65816rom=malloc(0x8000);
        if (!w65816ram) w65816ram=malloc(0x80000);
                append_filename(fn,exedir,"roms/tube/ReCo6502ROM_816",511);
                f=fopen(fn,"rb");
                fread(w65816rom,0x8000,1,f);
        fclose(f);
        makeopcodetable65816();
}

void w65816_close()
{
        if (w65816ram) free(w65816ram);
        if (w65816rom) free(w65816rom);
}

static void updatecpumode()
{
        if (p.e)
        {
                cpumode=4;
                x.b.h=y.b.h=0;
        }
        else
        {
                cpumode=0;
                if (!p.m) cpumode|=1;
                if (!p.ex) cpumode|=2;
                if (p.ex) x.b.h=y.b.h=0;
        }
}

static void nmi65816()
{
        uint8_t temp=0;
//        printf("NMI %i %i %i\n",p.i,inwai,irqenable);
        readmem(pbr|pc);
        cycles--; clockspc(6);
        if (inwai) pc++;
        inwai=0;
        if (!p.e)
        {
//                //printf("%02X -> %04X\n",pbr>>16,s.w);
                writemem(s.w,pbr>>16); s.w--;
//                //printf("%02X -> %04X\n",pc>>8,s.w);
                writemem(s.w,pc>>8);   s.w--;
//                //printf("%02X -> %04X\n",pc&0xFF,s.w);
                writemem(s.w,pc&0xFF);  s.w--;
                if (p.c) temp|=1;
                if (p.z) temp|=2;
                if (p.i) temp|=4;
                if (p.d) temp|=8;
                if (p.ex) temp|=0x10;
                if (p.m) temp|=0x20;
                if (p.v) temp|=0x40;
                if (p.n) temp|=0x80;
//                //printf("%02X -> %04X\n",temp,s.w);
                writemem(s.w,temp);    s.w--;
                pc=readmemw(0xFFEA);
                pbr=0;
                p.i=1;
                p.d=0;
//                printf("NMI\n");
        }
        else
        {
                writemem(s.w,pc>>8);   s.b.l--;
                writemem(s.w,pc&0xFF);  s.b.l--;
                if (p.c) temp|=1;
                if (p.z) temp|=2;
                if (p.i) temp|=4;
                if (p.d) temp|=8;
                if (p.v) temp|=0x40;
                if (p.n) temp|=0x80;
                writemem(s.w,temp|0x30);    s.b.l--;
                pc=readmemw(0xFFFA);
                pbr=0;
                p.i=1;
                p.d=0;
//                printf("Emulation mode NMI\n");
//                dumpregs();
//                exit(-1);
        }
}

static int toutput=0;
static void irq65816()
{
        uint8_t temp=0;
//        printf("IRQ %i %i %i\n",p.i,inwai,irqenable);
        readmem(pbr|pc);
        cycles--; clockspc(6);
        if (inwai && p.i)
        {
                pc++;
                inwai=0;
                return;
        }
        if (inwai) pc++;
        inwai=0;
        if (!p.e)
        {
                writemem(s.w,pbr>>16); s.w--;
                writemem(s.w,pc>>8);   s.w--;
                writemem(s.w,pc&0xFF);  s.w--;
                if (p.c) temp|=1;
                if (p.z) temp|=2;
                if (p.i) temp|=4;
                if (p.d) temp|=8;
                if (p.ex) temp|=0x10;
                if (p.m) temp|=0x20;
                if (p.v) temp|=0x40;
                if (p.n) temp|=0x80;
                writemem(s.w,temp);    s.w--;
                pc=readmemw(0xFFEE);
                pbr=0;
                p.i=1;
                p.d=0;
//                printf("IRQ\n");
        }
        else
        {
                writemem(s.w,pc>>8);     s.b.l--;
                writemem(s.w,pc&0xFF);   s.b.l--;
                if (p.c) temp|=1;
                if (p.z) temp|=2;
                if (p.i) temp|=4;
                if (p.d) temp|=8;
                if (p.v) temp|=0x40;
                if (p.n) temp|=0x80;
                writemem(s.w,temp|0x20); s.b.l--;
                pc=readmemw(0xFFFE);
                pbr=0;
                p.i=1;
                p.d=0;
//                bem_debugf("Emulation mode IRQ %04X\n",pc);
//                toutput=1;
//                dumpregs();
//                exit(-1);
        }
}

static int woldnmi=0;
static uint16_t toldpc;
void w65816_exec()
{
        while (tubecycles>0)
        {
                opcode=readmem(pbr|pc); pc++;
                if (toutput) bem_debugf("%i : %02X:%04X %04X %02X %i %04X  %04X %04X %04X\n",wins,pbr,pc-1,toldpc,opcode,cycles,s.b.l,a.w,x.w,y.w);
                toldpc=pc-1;
                opcodes[opcode][cpumode]();
//                if (pc==0xffee) toutput=1;
                wins++;
                if ((tube_irq&2) && !woldnmi)  nmi65816();
                else if ((tube_irq&1) && !p.i) irq65816();
                woldnmi=tube_irq&2;
//                if (pc==0x10C) toutput=1;
/*                if (pc==0xfff7)
                {
                        dumpregs65816();
                        exit(-1);
                }*/
//                if (pc==0xCB63) toutput=1;
//                if (wins==4236000) toutput=1;
//                if (wins==4236050) toutput=0;
        }
}
