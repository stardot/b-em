/*B-em v2.2 by Tom Walker
  80186 emulation
  Originally from PCem
  A few bits of 80286 emulation hanging around also*/
#include <stdio.h>
#include <stdint.h>
#include <allegro.h>
#include "b-em.h"
#include "x86.h"
#include "tube.h"

static int x86ins=0;

#define loadcs(seg) CS=seg; cs=seg<<4

static void loadseg(uint16_t val, x86seg *seg)
{
        seg->seg=val;
        seg->base=val<<4;
}

#define readmembl(a)    (((a)<0xE0000)?x86ram[(a)]:readmemblx86(a))
#define writemembl(a,b) x86ram[(a)&0xFFFFF]=b;
#define readmemwl(s,a) ((((s)+(a))<0xE0000)?*(uint16_t *)(&x86ram[(s)+(a)]):readmemwlx86(s,a))
#define writememwl(s,a,b) *(uint16_t *)(&x86ram[((s)+(a))&0xFFFFF])=b;

#define pc x86pc

static void x86dumpregs();

static uint32_t old8,old82,old83;
static uint16_t oldcs;

static int tempc;
static uint16_t getword();
static uint8_t opcode;
static int noint=0;

static uint8_t readmemblx86(uint32_t addr);
static uint16_t readmemwlx86(uint32_t seg, uint32_t addr);

static uint8_t *x86ram,*x86rom;

static int ssegs;

uint8_t x86_readmem(uint32_t addr) {
    return readmembl(addr);
}

void x86_writemem(uint32_t addr, uint8_t byte) {
    writemembl(addr, byte);
}

/*EA calculation*/

/*R/M - bits 0-2 - R/M   bits 3-5 - Reg   bits 6-7 - mod
  From 386 programmers manual :
r8(/r)                     AL    CL    DL    BL    AH    CH    DH    BH
r16(/r)                    AX    CX    DX    BX    SP    BP    SI    DI
r32(/r)                    EAX   ECX   EDX   EBX   ESP   EBP   ESI   EDI
/digit (Opcode)            0     1     2     3     4     5     6     7
REG =                      000   001   010   011   100   101   110   111
  ┌───Address
disp8 denotes an 8-bit displacement following the ModR/M byte, to be
sign-extended and added to the index. disp16 denotes a 16-bit displacement
following the ModR/M byte, to be added to the index. Default segment
register is SS for the effective addresses containing a BP index, DS for
other effective addresses.
            ──┐ ┌Mod R/M┐ ┌────────ModR/M Values in Hexadecimal────────┐

[BX + SI]            000   00    08    10    18    20    28    30    38
[BX + DI]            001   01    09    11    19    21    29    31    39
[BP + SI]            010   02    0A    12    1A    22    2A    32    3A
[BP + DI]            011   03    0B    13    1B    23    2B    33    3B
[SI]             00  100   04    0C    14    1C    24    2C    34    3C
[DI]                 101   05    0D    15    1D    25    2D    35    3D
disp16               110   06    0E    16    1E    26    2E    36    3E
[BX]                 111   07    0F    17    1F    27    2F    37    3F

[BX+SI]+disp8        000   40    48    50    58    60    68    70    78
[BX+DI]+disp8        001   41    49    51    59    61    69    71    79
[BP+SI]+disp8        010   42    4A    52    5A    62    6A    72    7A
[BP+DI]+disp8        011   43    4B    53    5B    63    6B    73    7B
[SI]+disp8       01  100   44    4C    54    5C    64    6C    74    7C
[DI]+disp8           101   45    4D    55    5D    65    6D    75    7D
[BP]+disp8           110   46    4E    56    5E    66    6E    76    7E
[BX]+disp8           111   47    4F    57    5F    67    6F    77    7F

[BX+SI]+disp16       000   80    88    90    98    A0    A8    B0    B8
[BX+DI]+disp16       001   81    89    91    99    A1    A9    B1    B9
[BX+SI]+disp16       010   82    8A    92    9A    A2    AA    B2    BA
[BX+DI]+disp16       011   83    8B    93    9B    A3    AB    B3    BB
[SI]+disp16      10  100   84    8C    94    9C    A4    AC    B4    BC
[DI]+disp16          101   85    8D    95    9D    A5    AD    B5    BD
[BP]+disp16          110   86    8E    96    9E    A6    AE    B6    BE
[BX]+disp16          111   87    8F    97    9F    A7    AF    B7    BF

EAX/AX/AL            000   C0    C8    D0    D8    E0    E8    F0    F8
ECX/CX/CL            001   C1    C9    D1    D9    E1    E9    F1    F9
EDX/DX/DL            010   C2    CA    D2    DA    E2    EA    F2    FA
EBX/BX/BL            011   C3    CB    D3    DB    E3    EB    F3    FB
ESP/SP/AH        11  100   C4    CC    D4    DC    E4    EC    F4    FC
EBP/BP/CH            101   C5    CD    D5    DD    E5    ED    F5    FD
ESI/SI/DH            110   C6    CE    D6    DE    E6    EE    F6    FE
EDI/DI/BH            111   C7    CF    D7    DF    E7    EF    F7    FF

mod = 11 - register
      10 - address + 16 bit displacement
      01 - address + 8 bit displacement
      00 - address

reg = If mod=11,  (depending on data size, 16 bits/8 bits, 32 bits=extend 16 bit registers)
      0=AX/AL   1=CX/CL   2=DX/DL   3=BX/BL
      4=SP/AH   5=BP/CH   6=SI/DH   7=DI/BH

      Otherwise, LSB selects SI/DI (0=SI), NMSB selects BX/BP (0=BX), and MSB
      selects whether BX/BP are used at all (0=used).

      mod=00 is an exception though
      6=16 bit displacement only
      7=[BX]

      Usage varies with instructions.

      MOV AL,BL has ModR/M as C3, for example.
      mod=11, reg=0, r/m=3
      MOV uses reg as dest, and r/m as src.
      reg 0 is AL, reg 3 is BL

      If BP or SP are in address calc, seg is SS, else DS
*/

static int x86cycles=0;
#define cycles x86cycles

static uint32_t easeg,eaaddr;
static int rm,reg,mod,rmdat;

static uint16_t zero=0;
static uint16_t *mod1add[2][8];
static uint32_t *mod1seg[8];

static void makemod1table()
{
        mod1add[0][0]=&BX; mod1add[0][1]=&BX; mod1add[0][2]=&BP; mod1add[0][3]=&BP;
        mod1add[0][4]=&SI; mod1add[0][5]=&DI; mod1add[0][6]=&BP; mod1add[0][7]=&BX;
        mod1add[1][0]=&SI; mod1add[1][1]=&DI; mod1add[1][2]=&SI; mod1add[1][3]=&DI;
        mod1add[1][4]=&zero; mod1add[1][5]=&zero; mod1add[1][6]=&zero; mod1add[1][7]=&zero;
        mod1seg[0]=&ds; mod1seg[1]=&ds; mod1seg[2]=&ss; mod1seg[3]=&ss;
        mod1seg[4]=&ds; mod1seg[5]=&ds; mod1seg[6]=&ss; mod1seg[7]=&ds;
}

static void fetcheal()
{
                if (!mod && rm==6) { eaaddr=getword(); easeg=ds; }
                else
                {
                        switch (mod)
                        {
                                case 0: eaaddr=0; break;
                                case 1: eaaddr=(uint16_t)(signed char)readmembl(cs+pc); pc++; break;
                                case 2: eaaddr=getword(); break;
                        }
                        eaaddr+=(*mod1add[0][rm])+(*mod1add[1][rm]);
                        easeg=*mod1seg[rm];
                        eaaddr&=0xFFFF;
                }
}

static inline uint8_t geteab()
{
        if (mod==3)
           return (rm&4)?regs[rm&3].b.h:regs[rm&3].b.l;
        cycles-=3;
        return readmembl(easeg+eaaddr);
}

static inline uint16_t geteaw()
{
        if (mod==3)
           return regs[rm].w;
        cycles-=3;
        return readmemwl(easeg,eaaddr);
}

static inline uint16_t geteaw2()
{
        if (mod==3)
           return regs[rm].w;
        cycles-=2;
        return readmemwl(easeg,(eaaddr+2)&0xFFFF);
}

static inline void seteab(uint8_t val)
{
        if (mod==3)
        {
                if (rm&4) regs[rm&3].b.h=val;
                else      regs[rm&3].b.l=val;
        }
        else
        {
                cycles-=2;
                writemembl(easeg+eaaddr,val);
        }
}

static inline void seteaw(uint16_t val)
{
        if (mod==3)
           regs[rm].w=val;
        else
        {
                cycles-=2;
                writememwl(easeg,eaaddr,val);
        }
}

#define getr8(r)   ((r&4)?regs[r&3].b.h:regs[r&3].b.l)

#define setr8(r,v) if (r&4) regs[r&3].b.h=v; \
                   else     regs[r&3].b.l=v;


/*Flags*/
static uint8_t znptable8[256];
static uint16_t znptable16[65536];

static void x86makeznptable()
{
        int c,d;
        for (c=0;c<256;c++)
        {
                d=0;
                if (c&1) d++;
                if (c&2) d++;
                if (c&4) d++;
                if (c&8) d++;
                if (c&16) d++;
                if (c&32) d++;
                if (c&64) d++;
                if (c&128) d++;
                if (d&1)
                   znptable8[c]=0;
                else
                   znptable8[c]=P_FLAG;
                if (!c) znptable8[c]|=Z_FLAG;
                if (c&0x80) znptable8[c]|=N_FLAG;
        }
        for (c=0;c<65536;c++)
        {
                d=0;
                if (c&1) d++;
                if (c&2) d++;
                if (c&4) d++;
                if (c&8) d++;
                if (c&16) d++;
                if (c&32) d++;
                if (c&64) d++;
                if (c&128) d++;
                if (d&1)
                   znptable16[c]=0;
                else
                   znptable16[c]=P_FLAG;
                if (!c) znptable16[c]|=Z_FLAG;
                if (c&0x8000) znptable16[c]|=N_FLAG;
      }
}

static uint8_t readmemblx86(uint32_t addr)
{
        if (addr<0xE0000) return x86ram[addr];
//        if (addr<0xC0000) return x86ram[addr-0x40000];
        if (addr>0xF0000) return x86rom[addr&0x3FFF];
        return 0xFF;
}

static uint16_t readmemwlx86(uint32_t seg, uint32_t addr)
{
        uint32_t addr2=seg+addr;
        if (addr2<0xE0000) return *(uint16_t *)(&x86ram[addr2]);
//        if (addr2<0xC0000) return *(uint16_t *)(&x86ram[addr2-0x40000]);
        if (addr2>0xF0000) return *(uint16_t *)(&x86rom[addr2&0x3FFF]);
        return 0xFFFF;
}

static uint16_t getword()
{
        pc+=2;
        return readmemwl(cs,(pc-2));
}

static void x86dumpregs()
{
        FILE *f;
        int c;
        f=fopen("x86ram.dmp","wb");
        fwrite(x86ram,896*1024,1,f);
                for (c=0xE0000;c<0x100000;c+=0x4000) fwrite(x86rom,16*1024,1,f);
        fclose(f);
        printf("AX=%04X BX=%04X CX=%04X DX=%04X DI=%04X SI=%04X BP=%04X SP=%04X\n",AX,BX,CX,DX,DI,SI,BP,SP);
        printf("PC=%04X CS=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",pc,CS,DS,ES,SS,flags);
        printf("%04X:%04X %08X %08X %08X\n",oldcs,oldpc,old8,old82,old83);
        printf("%i %04X %04X\n",x86ins,pc,x86pc);
/*        if (is386)
           printf("In %s mode\n",(msw&1)?((eflags&VM_FLAG)?"V86":"protected"):"real");
        else
           printf("In %s mode\n",(msw&1)?"protected":"real");
        printf("CS : base=%06X limit=%04X access=%02X\n",cs,_cs.limit,_cs.access);
        printf("DS : base=%06X limit=%04X access=%02X\n",ds,_ds.limit,_ds.access);
        printf("ES : base=%06X limit=%04X access=%02X\n",es,_es.limit,_es.access);
        if (is386)
        {
                printf("FS : base=%06X limit=%04X access=%02X\n",fs,_fs.limit,_fs.access);
                printf("GS : base=%06X limit=%04X access=%02X\n",gs,_gs.limit,_gs.access);
        }
        printf("SS : base=%06X limit=%04X access=%02X\n",ss,_ss.limit,_ss.access);
        printf("GDT : base=%06X limit=%04X\n",gdt.base,gdt.limit);
        printf("LDT : base=%06X limit=%04X\n",ldt.base,ldt.limit);
        printf("IDT : base=%06X limit=%04X\n",idt.base,idt.limit);
        printf("TR  : base=%06X limit=%04X\n", tr.base, tr.limit);
        if (is386)
        {
                printf("386 in %s mode   stack in %s mode\n",(use32)?"32-bit":"16-bit",(stack32)?"32-bit":"16-bit");
                printf("CR0=%08X CR2=%08X CR3=%08X\n",cr0,cr2,cr3);
        }*/
}

void x86_reset()
{
//        return;
        pc=0;
        loadcs(0xFFFF);
        flags=2;
        makemod1table();
}

void x86_init()
{
        FILE *f;
        char fn[512];
        if (!x86ram) x86ram=malloc(0x100000);
        if (!x86rom) x86rom=malloc(0x4000);
        x86makeznptable();
        memset(x86ram,0,0x100000);
        append_filename(fn,exedir,"roms/tube/BIOS.ROM",511);
        f=fopen(fn,"rb");
        fread(x86rom,0x4000,1,f);
        fclose(f);
}

void x86_close()
{
        if (x86rom) free(x86rom);
        if (x86ram) free(x86ram);
}

static void setznp8(uint8_t val)
{
        flags&=~0xC4;
        flags|=znptable8[val];
}

#define setznp168 setznp16
static void setznp16(uint16_t val)
{
        flags&=~0xC4;
//        flags|=((val&0x8000)?N_FLAG:((!val)?Z_FLAG:0));
//        flags|=(((znptable8[val&0xFF]&P_FLAG)==(znptable8[val>>8]&P_FLAG))?P_FLAG:0);
        flags|=znptable16[val];
}

/*void setznp168(uint16_t val)
{
        flags&=~0xC4;
        flags|=(znptable16[val]&0xC0)|(znptable8[val&0xFF]&4);
}*/

static void setadd8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd8nc(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b;
        flags&=~0x8D4;
        flags|=znptable8[c&0xFF];
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadc8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a+(uint16_t)b+tempc;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if (!((a^b)&0x80)&&((a^c)&0x80)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setadd16nc(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b;
        flags&=~0x8D4;
        flags|=znptable16[c&0xFFFF];
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}
static void x86setadc16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a+(uint32_t)b+tempc;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if (!((a^b)&0x8000)&&((a^c)&0x8000)) flags|=V_FLAG;
        if (((a&0xF)+(b&0xF))&0x10)      flags|=A_FLAG;
}

static void setsub8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(uint16_t)b;
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub8nc(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(uint16_t)b;
        flags&=~0x8D4;
        flags|=znptable8[c&0xFF];
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsbc8(uint8_t a, uint8_t b)
{
        uint16_t c=(uint16_t)a-(((uint16_t)b)+tempc);
        flags&=~0x8D5;
        flags|=znptable8[c&0xFF];
        if (c&0x100) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x80) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(uint32_t)b;
        flags&=~0x8D5;
        flags|=znptable16[c&0xFFFF];
        if (c&0x10000) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
//        if (x86output) printf("%04X %04X %i\n",a^b,a^c,flags&V_FLAG);
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void setsub16nc(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(uint32_t)b;
        flags&=~0x8D4;
        flags|=(znptable16[c&0xFFFF]&~4);
        flags|=(znptable8[c&0xFF]&4);
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}
static void x86setsbc16(uint16_t a, uint16_t b)
{
        uint32_t c=(uint32_t)a-(((uint32_t)b)+tempc);
        flags&=~0x8D5;
        flags|=(znptable16[c&0xFFFF]&~4);
        flags|=(znptable8[c&0xFF]&4);
        if (c&0x10000) flags|=C_FLAG;
        if ((a^b)&(a^c)&0x8000) flags|=V_FLAG;
        if (((a&0xF)-(b&0xF))&0x10)      flags|=A_FLAG;
}

static uint32_t x86sa,x86ss,x86src;
static uint32_t x86da,x86ds,x86dst;
static uint16_t x86ena;
static uint16_t x86imask=0;

static uint8_t inb(uint16_t port)
{
        if ((port&~0xF)==0x80) return tube_parasite_read(port>>1);
        return 0xFF;
        printf("Bad IN port %04X %04X:%04X\n",port,cs>>4,pc);
        x86dumpregs();
        exit(-1);
}

static void outb(uint16_t port, uint8_t val)
{
//        port&=0xFF;
//        printf("OUT %04X %02X %04X:%04X\n",port,val,cs>>4,pc);
        switch (port)
        {
                case 0xFF28: x86imask=val; return;
                case 0xFFC0: x86sa=(x86sa&0xFF00)|val;    x86src=x86sa+((x86ss&0xF)<<16); return;
                case 0xFFC1: x86sa=(x86sa&0xFF)|(val<<8); x86src=x86sa+((x86ss&0xF)<<16); return;//printf("SRC now %05X %04X:%04X\n",x86src,CS,pc); return;
                case 0xFFC2: x86ss=(x86ss&0xFF00)|val;    x86src=x86sa+((x86ss&0xF)<<16); return;
                case 0xFFC3: x86ss=(x86ss&0xFF)|(val<<8); x86src=x86sa+((x86ss&0xF)<<16); return;//printf("SRC now %05X %04X:%04X\n",x86src,CS,pc); return;
                case 0xFFC4: x86da=(x86da&0xFF00)|val;    x86dst=x86da+((x86ds&0xF)<<16); return;
                case 0xFFC5: x86da=(x86da&0xFF)|(val<<8); x86dst=x86da+((x86ds&0xF)<<16); return;//printf("DST now %05X %04X:%04X\n",x86dst,CS,pc); return;
                case 0xFFC6: x86ds=(x86ds&0xFF00)|val;    x86dst=x86da+((x86ds&0xF)<<16); return;
                case 0xFFC7: x86ds=(x86ds&0xFF)|(val<<8); x86dst=x86da+((x86ds&0xF)<<16); return;//printf("DST now %05X %04X:%04X\n",x86dst,CS,pc); return;
                case 0xFFCA: x86ena=(x86ena&0xFF00)|val; return;
                case 0xFFCB: x86ena=(x86ena&0xFF)|(val<<8); return;
        }
        if ((port&~0xF)==0x80)
        {
//                if (port!=0x8E || val!=0) printf("Tube write %02X %02X\n",port,val);
                tube_parasite_write(port>>1,val);
                return;
        }
}

static void x86_dma()
{
        if (!(x86ena&2)) return;
//        printf("Src %05X %04X:%04X  Dst %05X %04X:%04X\n",x86src,x86ss,x86sa, x86dst,x86ds,x86da);
        if (x86src<0x100)
        {
/*                if (x86dst==256*1024)
                {
                        x86dumpregs();
                        exit(-1);
                }*/
                writemembl(x86dst,inb(x86src));
                x86dst++;
        }
        else
        {
                outb(x86dst,readmembl(x86src));
                x86src++;
        }
}

static int firstrepcycle=1;
static void rep(int fv)
{
        uint8_t temp;
        int c=CX;
        uint8_t temp2;
        uint16_t tempw,tempw2;
        uint16_t ipc=oldpc;//pc-1;
        int changeds = 0;
        uint32_t oldds = 0;
        startrep:
        temp=readmembl(cs+pc); pc++;
//        if (firstrepcycle && temp==0xA5) printf("REP MOVSW %06X:%04X %06X:%04X\n",ds,SI,es,DI);
//        if (x86output) printf("REP %02X %04X\n",temp,ipc);
        switch (temp)
        {
                case 0x08:
                pc=ipc+1;
                cycles-=2;
                break;
                case 0x26: /*ES:*/
                oldds=ds;
                ds=es;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x2E: /*CS:*/
                oldds=ds;
                ds=cs;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x36: /*SS:*/
                oldds=ds;
                ds=ss;
                changeds=1;
                cycles-=2;
                goto startrep;
                break;
                case 0x6E: /*REP OUTSB*/
                if (c>0)
                {
                        temp2=readmembl(ds+SI);
                        outb(DX,temp2);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        c--;
                        cycles-=5;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xA4: /*REP MOVSB*/
                if (c>0)
                {
                        temp2=readmembl(ds+SI);
                        writemembl(es+DI,temp2);
//                        if (x86output) printf("Moved %02X from %04X:%04X to %04X:%04X\n",temp2,ds>>4,SI,es>>4,DI);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        c--;
                        cycles-=8;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
//                }
                break;
                case 0xA5: /*REP MOVSW*/
                if (c>0)
                {
                        tempw=readmemwl(ds,SI);
                        writememwl(es,DI,tempw);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        c--;
                        cycles-=8;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
//                }
                break;
                case 0xA6: /*REP CMPSB*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        temp=readmembl(ds+SI);
                        temp2=readmembl(es+DI);
//                        printf("CMPSB %c %c %i %05X %05X %04X:%04X\n",temp,temp2,c,ds+SI,es+DI,cs>>4,pc);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        c--;
                        cycles-=22;
                        setsub8(temp,temp2);
                }
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0))) { pc=ipc; firstrepcycle=0; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xA7: /*REP CMPSW*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        tempw=readmemwl(ds,SI);
                        tempw2=readmemwl(es,DI);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        c--;
                        cycles-=22;
                        setsub16(tempw,tempw2);
                }
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0))) { pc=ipc; firstrepcycle=0; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xAA: /*REP STOSB*/
                if (c>0)
                {
                        writemembl(es+DI,AL);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        c--;
                        cycles-=9;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xAB: /*REP STOSW*/
                if (c>0)
                {
                        writememwl(es,DI,AX);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        c--;
                        cycles-=9;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
//                printf("REP STOSW %04X:%04X %04X:%04X %04X %04X\n",CS,pc,ES,DI,AX,CX); }
                break;
                case 0xAC: /*REP LODSB*/
                if (c>0)
                {
                        temp2=readmembl(ds+SI);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        c--;
                        cycles-=4;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xAD: /*REP LODSW*/
                if (c>0)
                {
                        tempw2=readmemwl(ds,SI);
                        if (flags&D_FLAG) SI-=2;
                        else              SI+=2;
                        c--;
                        cycles-=4;
                }
                if (c>0) { firstrepcycle=0; pc=ipc; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                case 0xAE: /*REP SCASB*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        temp2=readmembl(es+DI);
//                        if (x86output) printf("SCASB %02X %c %02X %05X  ",temp2,temp2,AL,es+DI);
                        setsub8(AL,temp2);
//                        if (x86output && flags&Z_FLAG) printf("Match %02X %02X\n",AL,temp2);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        c--;
                        cycles-=15;
                }
//if (x86output)                printf("%i %i %i %i\n",c,(c>0),(fv==((flags&Z_FLAG)?1:0)),((c>0) && (fv==((flags&Z_FLAG)?1:0))));
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))  { pc=ipc; firstrepcycle=0; if (ssegs) ssegs++; }
                else firstrepcycle=1;
//                cycles-=120;
                break;
                case 0xAF: /*REP SCASW*/
                if (fv) flags|=Z_FLAG;
                else    flags&=~Z_FLAG;
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))
                {
                        tempw=readmemwl(es,DI);
                        setsub16(AX,tempw);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        c--;
                        cycles-=15;
                }
                if ((c>0) && (fv==((flags&Z_FLAG)?1:0)))  { pc=ipc; firstrepcycle=0; if (ssegs) ssegs++; }
                else firstrepcycle=1;
                break;
                default:
                        pc=ipc;
                        cycles-=20;
//                printf("Bad REP %02X\n",temp);
//                x86dumpregs();
//                exit(-1);
        }
        CX=c;
        if (changeds) ds=oldds;
}

static int inhlt=0;
static uint16_t lastpc,lastcs;
static int skipnextprint=0;
//#if 0
void x86_exec()
{
        uint8_t temp = 0, temp2;
        uint16_t addr, tempw, tempw2, tempw3, tempw4;
        signed char offset;
        int tempws;
        uint32_t templ;
        int c,cycdiff;
        int tempi;
//        tubecycles+=(cycs<<2);
//        printf("X86exec %i %i\n",tubecycles,cycs);
        while (tubecycles>0)
        {
                cycdiff=tubecycles;
//                old83=old82;
//                old82=old8;
//                old8=pc+(CS<<16);
                oldcs=CS;
                oldpc=pc;
                opcodestart:
                opcode=readmembl(cs+pc);
                tempc=flags&C_FLAG;
#if 0
                if (x86output && /*cs<0xF0000 && */!ssegs)//opcode!=0x26 && opcode!=0x36 && opcode!=0x2E && opcode!=0x3E)
                {
                        if ((opcode!=0xF2 && opcode!=0xF3) || firstrepcycle)
                        {
                                if (!skipnextprint) printf("%04X:%04X : %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %02X %04X\n",cs,pc,AX,BX,CX,DX,CS,DS,ES,SS,DI,SI,BP,SP,opcode,flags,rmdat);
                                skipnextprint=0;
//                                ins++;
/*                                if (ins==50000)
                                {
                                        x86dumpregs();
                                        exit(-1);
                                }*/
/*                                if (ins==500000)
                                {
                                        x86dumpregs();
                                        exit(-1);
                                }*/
                        }
                }
#endif
                pc++;
                inhlt=0;
//                if (ins==500000) { x86dumpregs(); exit(0); }*/
                switch (opcode)
                {
                        case 0x00: /*ADD 8,reg*/
                        fetchea();
                        temp=geteab();
                        setadd8(temp,getr8(reg));
                        temp+=getr8(reg);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x01: /*ADD 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setadd16(tempw,regs[reg].w);
                        tempw+=regs[reg].w;
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x02: /*ADD reg,8*/
                        fetchea();
                        temp=geteab();
                        setadd8(getr8(reg),temp);
                        setr8(reg,getr8(reg)+temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x03: /*ADD reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setadd16(regs[reg].w,tempw);
                        regs[reg].w+=tempw;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x04: /*ADD AL,#8*/
                        temp=readmembl(cs+pc); pc++;
                        setadd8(AL,temp);
                        AL+=temp;
                        tubecycles-=4;
                        break;
                        case 0x05: /*ADD AX,#16*/
                        tempw=getword();
                        setadd16(AX,tempw);
                        AX+=tempw;
                        tubecycles-=4;
                        break;

                        case 0x06: /*PUSH ES*/
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),ES);
                        SP-=2;
                        tubecycles-=9;
                        break;
                        case 0x07: /*POP ES*/
                        if (ssegs) ss=oldss;
                        tempw=readmemwl(ss,SP);
                        loadseg(tempw,&_es);
                        SP+=2;
                        tubecycles-=8;
                        break;

                        case 0x08: /*OR 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp|=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x09: /*OR 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw|=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x0A: /*OR reg,8*/
                        fetchea();
                        temp=geteab();
                        temp|=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(reg,temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x0B: /*OR reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw|=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        regs[reg].w=tempw;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x0C: /*OR AL,#8*/
                        AL|=readmembl(cs+pc); pc++;
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;
                        case 0x0D: /*OR AX,#16*/
                        AX|=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;

                        case 0x0E: /*PUSH CS*/
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),CS);
                        SP-=2;
                        tubecycles-=9;
                        break;

                        case 0x0F:
                        temp=readmembl(cs+pc); pc++;
                        switch (temp)
                        {
                                case 0x84: /*JE*/
                                tempw=getword();
                                if (flags&Z_FLAG) pc+=tempw;
                                tubecycles-=4;
                                break;
                                case 0x85: /*JNE*/
                                tempw=getword();
                                if (!(flags&Z_FLAG)) pc+=tempw;
                                tubecycles-=4;
                                break;

                                case 0xFF: /*Invalid - Windows 3.1 syscall trap?*/
                                pc-=2;
                                        if (ssegs) ss=oldss;
                                        writememwl(ss,((SP-2)&0xFFFF),flags|0xF000);
                                        writememwl(ss,((SP-4)&0xFFFF),CS);
                                        writememwl(ss,((SP-6)&0xFFFF),pc);
                                        SP-=6;
                                        addr=6<<2;
//                                        flags&=~I_FLAG;
                                        pc=readmemwl(0,addr);
                                        loadcs(readmemwl(0,addr+2));
                                        /*if (!pc && !cs)
                                        {
                                                printf("Bad int %02X %04X:%04X\n",temp,oldcs,oldpc);
                                                x86dumpregs();
                                                exit(-1);
                                        }*/
                                tubecycles-=70;
                                break;

                                default:
                                break;
//                                printf("Bad 0F opcode %02X\n",temp);
//                                pc-=2;
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0x10: /*ADC 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(reg);
                        setadc8(temp,temp2);
                        temp+=temp2+tempc;
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x11: /*ADC 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=regs[reg].w;
                        x86setadc16(tempw,tempw2);
                        tempw+=tempw2+tempc;
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x12: /*ADC reg,8*/
                        fetchea();
                        temp=geteab();
                        setadc8(getr8(reg),temp);
                        setr8(reg,getr8(reg)+temp+tempc);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x13: /*ADC reg,16*/
                        fetchea();
                        tempw=geteaw();
                        x86setadc16(regs[reg].w,tempw);
                        regs[reg].w+=tempw+tempc;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x14: /*ADC AL,#8*/
                        tempw=readmembl(cs+pc); pc++;
                        setadc8(AL,tempw);
                        AL+=tempw+tempc;
                        tubecycles-=4;
                        break;
                        case 0x15: /*ADC AX,#16*/
                        tempw=getword();
                        x86setadc16(AX,tempw);
                        AX+=tempw+tempc;
                        tubecycles-=4;
                        break;

                        case 0x16: /*PUSH SS*/
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),SS);
                        SP-=2;
                        tubecycles-=9;
                        break;
                        case 0x17: /*POP SS*/
                        if (ssegs) ss=oldss;
                        tempw=readmemwl(ss,SP);
                        loadseg(tempw,&_ss);
                        SP+=2;
                        noint=1;
                        tubecycles-=8;
//                        x86output=1;
                        break;

                        case 0x18: /*SBB 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(reg);
                        setsbc8(temp,temp2);
                        temp-=(temp2+tempc);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x19: /*SBB 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=regs[reg].w;
                        x86setsbc16(tempw,tempw2);
                        tempw-=(tempw2+tempc);
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x1A: /*SBB reg,8*/
                        fetchea();
                        temp=geteab();
                        setsbc8(getr8(reg),temp);
                        setr8(reg,getr8(reg)-(temp+tempc));
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x1B: /*SBB reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=regs[reg].w;
                        x86setsbc16(tempw2,tempw);
                        tempw2-=(tempw+tempc);
                        regs[reg].w=tempw2;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x1C: /*SBB AL,#8*/
                        temp=readmembl(cs+pc); pc++;
                        setsbc8(AL,temp);
                        AL-=(temp+tempc);
                        tubecycles-=4;
                        break;
                        case 0x1D: /*SBB AX,#16*/
                        tempw=getword();
                        x86setsbc16(AX,tempw);
                        AX-=(tempw+tempc);
                        tubecycles-=4;
                        break;

                        case 0x1E: /*PUSH DS*/
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),DS);
                        SP-=2;
                        tubecycles-=9;
                        break;
                        case 0x1F: /*POP DS*/
                        if (ssegs) ss=oldss;
                        tempw=readmemwl(ss,SP);
                        loadseg(tempw,&_ds);
                        if (ssegs) oldds=ds;
                        SP+=2;
                        tubecycles-=8;
                        break;

                        case 0x20: /*AND 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp&=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x21: /*AND 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw&=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x22: /*AND reg,8*/
                        fetchea();
                        temp=geteab();
                        temp&=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(reg,temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x23: /*AND reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw&=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        regs[reg].w=tempw;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x24: /*AND AL,#8*/
                        AL&=readmembl(cs+pc); pc++;
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;
                        case 0x25: /*AND AX,#16*/
                        AX&=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;

                        case 0x26: /*ES:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=es;
                        ssegs=2;
                        tubecycles-=4;
                        goto opcodestart;
//                        break;

                        case 0x27: /*DAA*/
                        if ((flags&A_FLAG) || ((AL&0xF)>9))
                        {
                                tempi=((uint16_t)AL)+6;
                                AL+=6;
                                flags|=A_FLAG;
                                if (tempi&0x100) flags|=C_FLAG;
                        }
                        if ((flags&C_FLAG) || (AL>0x9F))
                        {
                                AL+=0x60;
                                flags|=C_FLAG;
                        }
                        setznp8(AL);
                        tubecycles-=4;
                        break;

                        case 0x28: /*SUB 8,reg*/
                        fetchea();
                        temp=geteab();
                        setsub8(temp,getr8(reg));
                        temp-=getr8(reg);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x29: /*SUB 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(tempw,regs[reg].w);
                        tempw-=regs[reg].w;
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x2A: /*SUB reg,8*/
                        fetchea();
                        temp=geteab();
                        setsub8(getr8(reg),temp);
                        setr8(reg,getr8(reg)-temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x2B: /*SUB reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(regs[reg].w,tempw);
                        regs[reg].w-=tempw;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x2C: /*SUB AL,#8*/
                        temp=readmembl(cs+pc); pc++;
                        setsub8(AL,temp);
                        AL-=temp;
                        tubecycles-=4;
                        break;
                        case 0x2D: /*SUB AX,#16*/
                        tempw=getword();
                        setsub16(AX,tempw);
                        AX-=tempw;
                        tubecycles-=4;
                        break;
                        case 0x2E: /*CS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=cs;
                        ssegs=2;
                        tubecycles-=4;
                        goto opcodestart;
                        case 0x2F: /*DAS*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                tempi=((uint16_t)AL)-6;
                                AL-=6;
                                flags|=A_FLAG;
                                if (tempi&0x100) flags|=C_FLAG;
                        }
                        if ((flags&C_FLAG)||(AL>0x9F))
                        {
                                AL-=0x60;
                                flags|=C_FLAG;
                        }
                        setznp8(AL);
                        tubecycles-=4;
                        break;
                        case 0x30: /*XOR 8,reg*/
                        fetchea();
                        temp=geteab();
                        temp^=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteab(temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x31: /*XOR 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw^=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        seteaw(tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x32: /*XOR reg,8*/
                        fetchea();
                        temp=geteab();
                        temp^=getr8(reg);
                        setznp8(temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        setr8(reg,temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x33: /*XOR reg,16*/
                        fetchea();
                        tempw=geteaw();
                        tempw^=regs[reg].w;
                        setznp16(tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        regs[reg].w=tempw;
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x34: /*XOR AL,#8*/
                        AL^=readmembl(cs+pc); pc++;
                        setznp8(AL);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;
                        case 0x35: /*XOR AX,#16*/
                        AX^=getword();
                        setznp16(AX);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;

                        case 0x36: /*SS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=ss;
                        ssegs=2;
                        tubecycles-=4;
                        goto opcodestart;
//                        break;

                        case 0x37: /*AAA*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                AL+=6;
                                AH++;
                                flags|=(A_FLAG|C_FLAG);
                        }
                        else
                           flags&=~(A_FLAG|C_FLAG);
                        AL&=0xF;
                        tubecycles-=8;
                        break;

                        case 0x38: /*CMP 8,reg*/
                        fetchea();
                        temp=geteab();
                        setsub8(temp,getr8(reg));
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x39: /*CMP 16,reg*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(tempw,regs[reg].w);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x3A: /*CMP reg,8*/
                        fetchea();
                        temp=geteab();
                        setsub8(getr8(reg),temp);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x3B: /*CMP reg,16*/
                        fetchea();
                        tempw=geteaw();
                        setsub16(regs[reg].w,tempw);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x3C: /*CMP AL,#8*/
                        temp=readmembl(cs+pc); pc++;
                        setsub8(AL,temp);
                        tubecycles-=4;
                        break;
                        case 0x3D: /*CMP AX,#16*/
                        tempw=getword();
                        setsub16(AX,tempw);
                        tubecycles-=4;
                        break;

                        case 0x3E: /*DS:*/
                        oldss=ss;
                        oldds=ds;
                        ds=ss=ds;
                        ssegs=2;
                        tubecycles-=4;
                        goto opcodestart;
//                        break;

                        case 0x3F: /*AAS*/
                        if ((flags&A_FLAG)||((AL&0xF)>9))
                        {
                                AL-=6;
                                AH--;
                                flags|=(A_FLAG|C_FLAG);
                        }
                        else
                           flags&=~(A_FLAG|C_FLAG);
                        AL&=0xF;
                        tubecycles-=7;
                        break;

                        case 0x40: case 0x41: case 0x42: case 0x43: /*INC r16*/
                        case 0x44: case 0x45: case 0x46: case 0x47:
                        setadd16nc(regs[opcode&7].w,1);
                        regs[opcode&7].w++;
                        tubecycles-=3;
                        break;
                        case 0x48: case 0x49: case 0x4A: case 0x4B: /*DEC r16*/
                        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
                        setsub16nc(regs[opcode&7].w,1);
                        regs[opcode&7].w--;
                        tubecycles-=3;
                        break;

                        case 0x50: case 0x51: case 0x52: case 0x53: /*PUSH r16*/
                        case 0x54: case 0x55: case 0x56: case 0x57:
                        if (ssegs) ss=oldss;
                                SP-=2;
                                writememwl(ss,SP,regs[opcode&7].w);
                        tubecycles-=10;
                        break;
                        case 0x58: case 0x59: case 0x5A: case 0x5B: /*POP r16*/
                        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
                        if (ssegs) ss=oldss;
                        SP+=2;
                        regs[opcode&7].w=readmemwl(ss,(SP-2)&0xFFFF);
                        tubecycles-=8;
                        break;

                        case 0x60: /*PUSHA*/
                        writememwl(ss,((SP-2)&0xFFFF),AX);
                        writememwl(ss,((SP-4)&0xFFFF),CX);
                        writememwl(ss,((SP-6)&0xFFFF),DX);
                        writememwl(ss,((SP-8)&0xFFFF),BX);
                        writememwl(ss,((SP-10)&0xFFFF),SP);
                        writememwl(ss,((SP-12)&0xFFFF),BP);
                        writememwl(ss,((SP-14)&0xFFFF),SI);
                        writememwl(ss,((SP-16)&0xFFFF),DI);
                        SP-=16;
                        tubecycles-=36;
                        break;
                        case 0x61: /*POPA*/
                        DI=readmemwl(ss,((SP)&0xFFFF));
                        SI=readmemwl(ss,((SP+2)&0xFFFF));
                        BP=readmemwl(ss,((SP+4)&0xFFFF));
                        BX=readmemwl(ss,((SP+8)&0xFFFF));
                        DX=readmemwl(ss,((SP+10)&0xFFFF));
                        CX=readmemwl(ss,((SP+12)&0xFFFF));
                        AX=readmemwl(ss,((SP+14)&0xFFFF));
                        SP+=16;
                        tubecycles-=51;
                        break;
//                        case 0x66: /*BIOS trap*/
//                        callbios();
//                        tubecycles-=16;
//                        break;
//#if 0
                        case 0x68: /*PUSH #w*/
                        tempw=getword();
                        writememwl(ss,((SP-2)&0xFFFF),tempw);
                        SP-=2;
                        tubecycles-=9;
                        break;
                        case 0x69: /*IMUL r16*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=getword();
                        templ=((int)(signed short)tempw)*((int)(signed short)tempw2);
//                        printf("%04X*%04X = %08X\n",tempw,tempw2,templ);
                        if ((templ>>16)!=0 && (templ>>16)!=0xFFFF) flags|=C_FLAG|V_FLAG;
                        else                                       flags&=~(C_FLAG|V_FLAG);
                        regs[reg].w=templ&0xFFFF;
//                        seteaw(templ&0xFFFF);
                        tubecycles-=((mod==3)?40:34);
                        break;
                        case 0x6A: /*PUSH #eb*/
                        tempw=readmembl(cs+pc); pc++;
                        if (tempw&0x80) tempw|=0xFF00;
                        writememwl(ss,((SP-2)&0xFFFF),tempw);
                        SP-=2;
                        tubecycles-=9;
                        break;
//                        #if 0
                        case 0x6B: /*IMUL r8*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=readmembl(cs+pc); pc++;
                        if (tempw2&0x80) tempw2|=0xFF00;
//                        printf("%04X * %04X = ",tempw,tempw2);
                        templ=((int)(signed short)tempw)*((int)(signed short)tempw2);
//                        printf("%08X\n",templ);
                        if ((templ>>16)!=0 && (templ>>16)!=0xFFFF) flags|=C_FLAG|V_FLAG;
                        else                                       flags&=~(C_FLAG|V_FLAG);
                        regs[reg].w=templ&0xFFFF;
//                        seteaw(templ&0xFFFF);
                        tubecycles-=((mod==3)?34:25);
                        break;
//#endif
                        case 0x6C: /*INSB*/
                        temp=inb(DX);
                        writemembl(es+DI,temp);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        tubecycles-=14;
                        break;
                        case 0x6E: /*OUTSB*/
                        temp=readmembl(ds+SI);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        outb(DX,temp);
                        tubecycles-=14;
                        break;
//                        #endif

                        case 0x70: /*JO*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&V_FLAG) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x71: /*JNO*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&V_FLAG)) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x72: /*JB*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&C_FLAG) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x73: /*JNB*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&C_FLAG)) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x74: /*JZ*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&Z_FLAG) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x75: /*JNZ*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&Z_FLAG)) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x76: /*JBE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&(C_FLAG|Z_FLAG)) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x77: /*JNBE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&(C_FLAG|Z_FLAG))) { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x78: /*JS*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&N_FLAG)  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x79: /*JNS*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&N_FLAG))  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7A: /*JP*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (flags&P_FLAG)  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7B: /*JNP*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!(flags&P_FLAG))  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7C: /*JL*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (temp!=temp2)  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7D: /*JNL*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (temp==temp2)  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7E: /*JLE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if ((flags&Z_FLAG) || (temp!=temp2))  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;
                        case 0x7F: /*JNLE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        temp=(flags&N_FLAG)?1:0;
                        temp2=(flags&V_FLAG)?1:0;
                        if (!((flags&Z_FLAG) || (temp!=temp2)))  { pc+=offset; tubecycles-=9; }
                        tubecycles-=4;
                        break;

                        case 0x80: case 0x82:
                        fetchea();
                        temp=geteab();
                        temp2=readmembl(cs+pc); pc++;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD b,#8*/
                                setadd8(temp,temp2);
                                seteab(temp+temp2);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x08: /*OR b,#8*/
                                temp|=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x10: /*ADC b,#8*/
//                                temp2+=(flags&C_FLAG);
                                setadc8(temp,temp2);
                                seteab(temp+temp2+tempc);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x18: /*SBB b,#8*/
//                                temp2+=(flags&C_FLAG);
                                setsbc8(temp,temp2);
                                seteab(temp-(temp2+tempc));
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x20: /*AND b,#8*/
                                temp&=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x28: /*SUB b,#8*/
                                setsub8(temp,temp2);
                                seteab(temp-temp2);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x30: /*XOR b,#8*/
                                temp^=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteab(temp);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x38: /*CMP b,#8*/
                                setsub8(temp,temp2);
                                tubecycles-=((mod==3)?4:10);
                                break;

                                default:
                                printf("Bad 80 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0x81:
                        fetchea();
                        tempw=geteaw();
                        tempw2=getword();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD w,#16*/
                                setadd16(tempw,tempw2);
                                tempw+=tempw2;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x08: /*OR w,#16*/
                                tempw|=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x10: /*ADC w,#16*/
//                                tempw2+=(flags&C_FLAG);
                                x86setadc16(tempw,tempw2);
                                tempw+=tempw2+tempc;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x20: /*AND w,#16*/
                                tempw&=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x18: /*SBB w,#16*/
//                                tempw2+=(flags&C_FLAG);
                                x86setsbc16(tempw,tempw2);
                                seteaw(tempw-(tempw2+tempc));
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x28: /*SUB w,#16*/
                                setsub16(tempw,tempw2);
                                tempw-=tempw2;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x30: /*XOR w,#16*/
                                tempw^=tempw2;
                                setznp16(tempw);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x38: /*CMP w,#16*/
                                setsub16(tempw,tempw2);
                                tubecycles-=((mod==3)?4:10);
                                break;

                                default:
                                printf("Bad 81 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0x83:
                        fetchea();
                        tempw=geteaw();
                        tempw2=readmembl(cs+pc); pc++;
                        if (tempw2&0x80) tempw2|=0xFF00;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ADD w,#8*/
                                setadd16(tempw,tempw2);
                                tempw+=tempw2;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x08: /*OR w,#8*/
                                tempw|=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x10: /*ADC w,#8*/
//                                tempw2+=(flags&C_FLAG);
                                x86setadc16(tempw,tempw2);
                                tempw+=tempw2+tempc;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x18: /*SBB w,#8*/
//                                tempw2+=(flags&C_FLAG);
                                x86setsbc16(tempw,tempw2);
                                tempw-=(tempw2+tempc);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x20: /*AND w,#8*/
                                tempw&=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                break;
                                case 0x28: /*SUB w,#8*/
                                setsub16(tempw,tempw2);
                                tempw-=tempw2;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                break;
                                case 0x30: /*XOR w,#8*/
                                tempw^=tempw2;
                                setznp16(tempw);
                                seteaw(tempw);
                                tubecycles-=((mod==3)?4:16);
                                flags&=~(C_FLAG|A_FLAG|V_FLAG);
                                break;
                                case 0x38: /*CMP w,#8*/
                                setsub16(tempw,tempw2);
                                tubecycles-=((mod==3)?4:10);
                                break;

                                default:
                                printf("Bad 83 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0x84: /*TEST b,reg*/
                        fetchea();
                        temp=geteab();
                        temp2=getr8(reg);
                        setznp8(temp&temp2);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x85: /*TEST w,reg*/
                        fetchea();
                        tempw=geteaw();
                        tempw2=regs[reg].w;
                        setznp16(tempw&tempw2);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=((mod==3)?3:10);
                        break;
                        case 0x86: /*XCHG b,reg*/
                        fetchea();
                        temp=geteab();
                        seteab(getr8(reg));
                        setr8(reg,temp);
                        tubecycles-=((mod==3)?4:17);
                        break;
                        case 0x87: /*XCHG w,reg*/
                        fetchea();
                        tempw=geteaw();
                        seteaw(regs[reg].w);
                        regs[reg].w=tempw;
                        tubecycles-=((mod==3)?4:17);
                        break;

                        case 0x88: /*MOV b,reg*/
                        fetchea();
                        seteab(getr8(reg));
                        tubecycles-=((mod==3)?2:9);
                        break;
                        case 0x89: /*MOV w,reg*/
                        fetchea();
                        seteaw(regs[reg].w);
                        tubecycles-=((mod==3)?2:9);
                        break;
                        case 0x8A: /*MOV reg,b*/
                        fetchea();
                        temp=geteab();
                        setr8(reg,temp);
                        tubecycles-=((mod==3)?2:12);
                        break;
                        case 0x8B: /*MOV reg,w*/
                        fetchea();
                        tempw=geteaw();
                        regs[reg].w=tempw;
                        tubecycles-=((mod==3)?2:12);
                        break;

                        case 0x8C: /*MOV w,sreg*/
                        fetchea();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ES*/
                                seteaw(ES);
                                break;
                                case 0x08: /*CS*/
                                seteaw(CS);
                                break;
                                case 0x18: /*DS*/
                                if (ssegs) ds=oldds;
                                seteaw(DS);
                                break;
                                case 0x10: /*SS*/
                                if (ssegs) ss=oldss;
                                seteaw(SS);
                                break;
                        }
                        tubecycles-=((mod==3)?2:11);
                        break;

                        case 0x8D: /*LEA*/
                        fetchea();
                        regs[reg].w=eaaddr;
                        tubecycles-=6;
                        break;

                        case 0x8E: /*MOV sreg,w*/
//                        if (x86output) printf("MOV %04X  ",pc);
                        fetchea();
//                        if (x86output) printf("%04X %02X\n",pc,rmdat);
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ES*/
                                tempw=geteaw();
                                loadseg(tempw,&_es);
                                break;
                                case 0x18: /*DS*/
                                tempw=geteaw();
                                loadseg(tempw,&_ds);
                                if (ssegs) oldds=ds;
                                break;
                                case 0x10: /*SS*/
                                tempw=geteaw();
                                loadseg(tempw,&_ss);
                                if (ssegs) oldss=ss;
                                skipnextprint=1;
                                noint=1;
//                                printf("LOAD SS %04X %04X\n",tempw,SS);
//                              printf("SS loaded with %04X %04X:%04X %04X %04X %04X\n",ss>>4,cs>>4,pc,CX,DX,es>>4);
                                break;
                        }
                        tubecycles-=((mod==3)?2:9);
                        break;

                        case 0x8F: /*POPW*/
                        fetchea();
                        if (ssegs) ss=oldss;
                        tempw=readmemwl(ss,SP);
                        SP+=2;
                        //if (x86output) printf("POPW - %04X\n",tempw);
                        seteaw(tempw);
                        tubecycles-=((mod==3)?10:20);
                        break;

                        case 0x90: /*NOP*/
                        tubecycles-=3;
                        break;

                        case 0x91: case 0x92: case 0x93: /*XCHG AX*/
                        case 0x94: case 0x95: case 0x96: case 0x97:
                        tempw=AX;
                        AX=regs[opcode&7].w;
                        regs[opcode&7].w=tempw;
                        tubecycles-=3;
                        break;

                        case 0x98: /*CBW*/
                        AH=(AL&0x80)?0xFF:0;
                        tubecycles-=2;
                        break;
                        case 0x99: /*CWD*/
                        DX=(AX&0x8000)?0xFFFF:0;
                        tubecycles-=4;
                        break;
                        case 0x9A: /*CALL FAR*/
                        tempw=getword();
                        tempw2=getword();
                        tempw3=CS;
                        tempw4=pc;
                        if (ssegs) ss=oldss;
                        pc=tempw;
                        loadcs(tempw2);
/*                        if ((msw&1) && !(_cs.access&4) && ((CS&3)<(tempw3&3)))
                        {
                                printf("Call to non-confirming inner segment!\n");
                                x86dumpregs();
                                exit(-1);
                        }*/
                        writememwl(ss,(SP-2)&0xFFFF,tempw3);
                        writememwl(ss,(SP-4)&0xFFFF,tempw4);
                        SP-=4;
                        tubecycles-=23;
                        break;
                        case 0x9B: /*WAIT*/
                        tubecycles-=4;
                        break;
                        case 0x9C: /*PUSHF*/
//                        printf("PUSHF %04X:%04X\n",CS,pc);
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),flags|0xF000);
                        SP-=2;
                        tubecycles-=9;
                        break;
                        case 0x9D: /*POPF*/
//                        printf("POPF %04X:%04X\n",CS,pc);
/*                        if (CS==0xFFFF)
                        {
                                x86dumpregs();
                                exit(-1);
                        }*/
                        if (ssegs) ss=oldss;
                        flags=readmemwl(ss,SP)&0xFFF;
                        SP+=2;
                        tubecycles-=8;
                        break;
                        case 0x9E: /*SAHF*/
                        flags=(flags&0xFF00)|AH;
                        tubecycles-=3;
                        break;
                        case 0x9F: /*LAHF*/
                        AH=flags&0xFF;
                        tubecycles-=2;
                        break;

                        case 0xA0: /*MOV AL,(w)*/
                        addr=getword();
                        AL=readmembl(ds+addr);
                        tubecycles-=8;
                        break;
                        case 0xA1: /*MOV AX,(w)*/
                        addr=getword();
//                        printf("Reading AX from %05X %04X:%04X\n",ds+addr,ds>>4,addr);
                        AX=readmemwl(ds,addr);
                        tubecycles-=8;
                        break;
                        case 0xA2: /*MOV (w),AL*/
                        addr=getword();
                        writemembl(ds+addr,AL);
                        tubecycles-=9;
                        break;
                        case 0xA3: /*MOV (w),AX*/
                        addr=getword();
//                        if (!addr) printf("Write !addr %04X:%04X\n",cs>>4,pc);
                        writememwl(ds,addr,AX);
                        tubecycles-=9;
                        break;

                        case 0xA4: /*MOVSB*/
                        temp=readmembl(ds+SI);
                        writemembl(es+DI,temp);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        tubecycles-=9;
                        break;
                        case 0xA5: /*MOVSW*/
                        tempw=readmemwl(ds,SI);
                        writememwl(es,DI,tempw);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        tubecycles-=9;
                        break;
                        case 0xA6: /*CMPSB*/
                        temp =readmembl(ds+SI);
                        temp2=readmembl(es+DI);
                        setsub8(temp,temp2);
                        if (flags&D_FLAG) { DI--; SI--; }
                        else              { DI++; SI++; }
                        tubecycles-=22;
                        break;
                        case 0xA7: /*CMPSW*/
                        tempw =readmemwl(ds,SI);
                        tempw2=readmemwl(es,DI);
                        setsub16(tempw,tempw2);
                        if (flags&D_FLAG) { DI-=2; SI-=2; }
                        else              { DI+=2; SI+=2; }
                        tubecycles-=22;
                        break;
                        case 0xA8: /*TEST AL,#8*/
                        temp=readmembl(cs+pc); pc++;
                        setznp8(AL&temp);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;
                        case 0xA9: /*TEST AX,#16*/
                        tempw=getword();
                        setznp16(AX&tempw);
                        flags&=~(C_FLAG|V_FLAG|A_FLAG);
                        tubecycles-=4;
                        break;
                        case 0xAA: /*STOSB*/
                        writemembl(es+DI,AL);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        tubecycles-=10;
                        break;
                        case 0xAB: /*STOSW*/
                        writememwl(es,DI,AX);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        tubecycles-=10;
                        break;
                        case 0xAC: /*LODSB*/
                        AL=readmembl(ds+SI);
//                        printf("LODSB %04X:%04X %02X %04X:%04X\n",cs>>4,pc,AL,ds>>4,SI);
                        if (flags&D_FLAG) SI--;
                        else              SI++;
                        tubecycles-=10;
                        break;
                        case 0xAD: /*LODSW*/
//                        if (times) printf("LODSW %04X:%04X\n",cs>>4,pc);
                        AX=readmemwl(ds,SI);
                        if (flags&D_FLAG) SI-=2;
                        else              SI+=2;
                        tubecycles-=10;
                        break;
                        case 0xAE: /*SCASB*/
                        temp=readmembl(es+DI);
                        setsub8(AL,temp);
                        if (flags&D_FLAG) DI--;
                        else              DI++;
                        tubecycles-=15;
                        break;
                        case 0xAF: /*SCASW*/
                        tempw=readmemwl(es,DI);
                        setsub16(AX,tempw);
                        if (flags&D_FLAG) DI-=2;
                        else              DI+=2;
                        tubecycles-=15;
                        break;

                        case 0xB0: /*MOV AL,#8*/
                        AL=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB1: /*MOV CL,#8*/
                        CL=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB2: /*MOV DL,#8*/
                        DL=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB3: /*MOV BL,#8*/
                        BL=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB4: /*MOV AH,#8*/
                        AH=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB5: /*MOV CH,#8*/
                        CH=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB6: /*MOV DH,#8*/
                        DH=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB7: /*MOV BH,#8*/
                        BH=readmembl(cs+pc),pc++;
                        tubecycles-=4;
                        break;
                        case 0xB8: case 0xB9: case 0xBA: case 0xBB: /*MOV reg,#16*/
                        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                        regs[opcode&7].w=getword();
                        tubecycles-=4;
                        break;

                        case 0xC0:
                        fetchea();
                        c=readmembl(cs+pc); pc++;
                        temp=geteab();
                        c&=31;
                        if (!c) break;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL b,CL*/
                                while (c>0)
                                {
                                        temp2=(temp&0x80)?1:0;
                                        temp=(temp<<1)|temp2;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
//                                setznp8(temp);
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x08: /*ROR b,CL*/
                                while (c>0)
                                {
                                        temp2=temp&1;
                                        temp>>=1;
                                        if (temp2) temp|=0x80;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x10: /*RCL b,CL*/
                                while (c>0)
                                {
                                        tempc=(flags&C_FLAG)?1:0;
                                        if (temp&0x80) flags|=C_FLAG;
                                        else           flags&=~C_FLAG;
                                        temp=(temp<<1)|tempc;
                                        c--;
                                        tubecycles--;
                                }
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x18: /*RCR b,CL*/
                                while (c>0)
                                {
                                        tempc=(flags&C_FLAG)?0x80:0;
                                        if (temp&1) flags|=C_FLAG;
                                        else        flags&=~C_FLAG;
                                        temp=(temp>>1)|tempc;
                                        c--;
                                        tubecycles--;
                                }
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x20: case 0x30: /*SHL b,CL*/
                                if ((temp<<(c-1))&0x80) flags|=C_FLAG;
                                else                    flags&=~C_FLAG;
                                temp<<=c;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR b,CL*/
                                if ((temp>>(c-1))&1) flags|=C_FLAG;
                                else                 flags&=~C_FLAG;
                                temp>>=c;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;
                                case 0x38: /*SAR b,CL*/
                                if ((temp>>(c-1))&1) flags|=C_FLAG;
                                else                 flags&=~C_FLAG;
                                while (c>0)
                                {
                                        temp>>=1;
                                        if (temp&0x40) temp|=0x80;
                                        c--;
                                        tubecycles--;
                                }
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                default:
                                printf("Bad C0 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xC1:
                        fetchea();
                        c=readmembl(cs+pc); pc++;
                        c&=31;
                        tempw=geteaw();
                        if (!c) break;
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL w,CL*/
                                while (c>0)
                                {
                                        temp=(tempw&0x8000)?1:0;
                                        tempw=(tempw<<1)|temp;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp) flags|=C_FLAG;
                                else      flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x08: /*ROR w,CL*/
                                while (c>0)
                                {
                                        tempw2=(tempw&1)?0x8000:0;
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        tubecycles--;
                                }
                                if (tempw2) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
//                                setznp16(tempw);
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x10: /*RCL w,CL*/
                                while (c>0)
                                {
                                        tempc=(flags&C_FLAG)?1:0;
                                        if (tempw&0x8000) flags|=C_FLAG;
                                        else              flags&=~C_FLAG;
                                        tempw=(tempw<<1)|tempc;
                                        c--;
                                        tubecycles--;
                                }
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x18: /*RCR w,CL*/
                                while (c>0)
                                {
                                        tempc=(flags&C_FLAG)?0x8000:0;
                                        if (tempw&1) flags|=C_FLAG;
                                        else         flags&=~C_FLAG;
                                        tempw=(tempw>>1)|tempc;
                                        c--;
                                        tubecycles--;
                                }
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;

                                case 0x20: case 0x30: /*SHL w,CL*/
                                if ((tempw<<(c-1))&0x8000) flags|=C_FLAG;
                                else                       flags&=~C_FLAG;
                                tempw<<=c;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                case 0x28:            /*SHR w,CL*/
                                if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                else                  flags&=~C_FLAG;
                                tempw>>=c;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                case 0x38:            /*SAR w,CL*/
                                tempw2=tempw&0x8000;
                                if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                else                  flags&=~C_FLAG;
                                while (c>0)
                                {
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        tubecycles--;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                default:
                                printf("Bad C1 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xC2: /*RET*/
                        tempw=getword();
                        if (ssegs) ss=oldss;
                        pc=readmemwl(ss,SP);
//                        printf("RET to %04X\n",pc);
                        SP+=2+tempw;
                        tubecycles-=18;
                        break;
                        case 0xC3: /*RET*/
                        if (ssegs) ss=oldss;
                        pc=readmemwl(ss,SP);
//                        if (x86output) printf("RET to %04X %05X\n",pc,ss+SP);
                        SP+=2;
                        tubecycles-=16;
                        break;
                        case 0xC4: /*LES*/
                        fetchea();
                        regs[reg].w=readmemwl(easeg,eaaddr); //geteaw();
                        tempw=readmemwl(easeg,(eaaddr+2)&0xFFFF); //geteaw2();
                        loadseg(tempw,&_es);
                        tubecycles-=18;
                        break;
                        case 0xC5: /*LDS*/
                        fetchea();
                        regs[reg].w=readmemwl(easeg,eaaddr);
                        tempw=readmemwl(easeg,(eaaddr+2)&0xFFFF);
                        loadseg(tempw,&_ds);
                        if (ssegs) oldds=ds;
                        tubecycles-=18;
                        break;
                        case 0xC6: /*MOV b,#8*/
                        fetchea();
                        temp=readmembl(cs+pc); pc++;
                        seteab(temp);
                        tubecycles-=((mod==3)?4:13);
                        break;
                        case 0xC7: /*MOV w,#16*/
                        fetchea();
                        tempw=getword();
                        seteaw(tempw);
                        tubecycles-=((mod==3)?4:13);
                        break;
                        case 0xC8: /*ENTER*/
                        tempw3=getword();
                        tempi=readmembl(cs+pc); pc++;
                        writememwl(ss,((SP-2)&0xFFFF),BP); SP-=2;
                        tempw2=SP;
                        if (tempi>0)
                        {
                                while (--tempi)
                                {
                                        BP-=2;
                                        tempw=readmemwl(ss,BP);
                                        writememwl(ss,((SP-2)&0xFFFF),tempw); SP-=2;
                                        tubecycles-=16;
                                }
                                writememwl(ss,((SP-2)&0xFFFF),tempw2); SP-=2;
                        }
                        BP=tempw2;  SP-=tempw3;
                        tubecycles-=15;
                        break;
                        case 0xC9: /*LEAVE*/
                        SP=BP;
                        BP=readmemwl(ss,SP);
                        SP+=2;
                        tubecycles-=8;
                        break;
                        case 0xCA: /*RETF*/
                        tempw=getword();
                        if (ssegs) ss=oldss;
                        pc=readmemwl(ss,SP);
                        loadcs(readmemwl(ss,SP+2));
                        SP+=4;
                        SP+=tempw;
//                        cs=CS<<4;
                        tubecycles-=25;
                        break;
                        case 0xCB: /*RETF*/
                        if (ssegs) ss=oldss;
                        pc=readmemwl(ss,SP);
                        loadcs(readmemwl(ss,SP+2));
                        SP+=4;
//                        cs=CS<<4;
                        tubecycles-=22;
                        break;
                        case 0xCC: /*INT 3*/
                                if (ssegs) ss=oldss;
                                writememwl(ss,((SP-2)&0xFFFF),flags|0xF000);
                                writememwl(ss,((SP-4)&0xFFFF),CS);
                                writememwl(ss,((SP-6)&0xFFFF),pc);
                                SP-=6;
                                addr=3<<2;
                                flags&=~I_FLAG;
                                pc=readmemwl(0,addr);
                                loadcs(readmemwl(0,addr+2));
                        tubecycles-=45;
                        break;
                        case 0xCD: /*INT*/
                        lastpc=pc;
                        lastcs=CS;
                        temp=readmembl(cs+pc); pc++;
                        if (temp==0xE0 && CL==0x32) bem_debugf("XIOS call %02X %04X:%04X\n",readmembl(ds+DX),CS,pc);
/*                        if (temp==0x45)
                        {
                                printf("OSFILE %02X\n",AL);
                        }
                        if ((temp&~0xF)==0x40)
                        {
                                printf("BBC INT! %02X %02X\n",temp,AL);
                        }*/
//                        if (temp==0x10 && !AH) printf("Entering mode %02X\n",AL);
//                        if (temp==0x18 || temp==0x19) { printf("INT %02X\n",temp); x86output=1; }
//                        printf("INT %02X %04X %04X %04X %04X\n",temp,AX,BX,CX,DX);
/*                        if (temp==0x21) printf("INT 21 %04X %04X %04X %04X %04X:%04X %06X %06X\n",AX,BX,CX,DX,cs>>4,pc,ds,ds+DX);
                        if (temp==0x21 && AH==9)
                        {
                                addr=0;
                                while (ram[ds+DX+addr]!='$')
                                {
                                        printf("%c",ram[ds+DX+addr]);
                                        addr++;
                                }
                                printf("\n");
                                printf("Called from %04X\n",readmemwl(ss,SP));
                        }*/
//                        x86output=0;
//                        if (temp==0x13 && AH==3) printf("Write sector %04X:%04X %05X\n",es>>4,BX,es+BX);
/*                        if (temp==0x13 && (DL==0x80 || DL==0x81) && AH>0)
                        {
                                int13hdc();
                        }
                        else if (temp==0x13 && AH==2 && DL<2 && FASTDISC)
                        {
                                int13read();
                        }
                        else if (temp==0x13 && AH==3 && DL<2 && FASTDISC)
                        {
                                int13write();
                        }
                        else if (temp==0x13 && AH==4 && DL<2 && FASTDISC)
                        {
                                AH=0;
                                flags&=~C_FLAG;
                        }
                        else
                        {*/
                                        if (ssegs) ss=oldss;
                                        writememwl(ss,((SP-2)&0xFFFF),flags|0xF000);
                                        writememwl(ss,((SP-4)&0xFFFF),CS);
                                        writememwl(ss,((SP-6)&0xFFFF),pc);
                                        SP-=6;
                                        addr=temp<<2;
//                                        flags&=~I_FLAG;
                                        pc=readmemwl(0,addr);
                                        loadcs(readmemwl(0,addr+2));
/*                                        if (!pc && !cs)
                                        {
                                                printf("Bad int %02X %04X:%04X\n",temp,oldcs,oldpc);
                                                x86dumpregs();
                                                exit(-1);
                                        }*/
//                        }
                        tubecycles-=47;
                        break;
                        case 0xCF: /*IRET*/
//                        if (inint) printf("IRET %04X %04X:%04X\n",flags,cs>>4,pc,SP);
/*                        if (x86output)
                        {
                                x86dumpregs();
                                exit(-1);
                        }*/
//                        if (!inint) x86output=0;
                        if (ssegs) ss=oldss;
                                tempw=CS;
                                tempw2=pc;
//                                inint=0;
                                pc=readmemwl(ss,SP);
                                loadcs(readmemwl(ss,((SP+2)&0xFFFF)));
                                flags=readmemwl(ss,((SP+4)&0xFFFF))&0xFFF;
                                SP+=6;
                        tubecycles-=28;
//                        printf("%04X %04X\n",flags,SP);
                        break;
                        case 0xD0:
                        fetchea();
                        temp=geteab();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL b,1*/
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                temp<<=1;
                                if (flags&C_FLAG) temp|=1;
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x08: /*ROR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (flags&C_FLAG) temp|=0x80;
                                seteab(temp);
//                                setznp8(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x10: /*RCL b,1*/
                                temp2=flags&C_FLAG;
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                temp<<=1;
                                if (temp2) temp|=1;
                                seteab(temp);
//                                setznp8(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x18: /*RCR b,1*/
                                temp2=flags&C_FLAG;
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (temp2) temp|=0x80;
                                seteab(temp);
//                                setznp8(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x20: /*SHL b,1*/
                                if (temp&0x80) flags|=C_FLAG;
                                else           flags&=~C_FLAG;
                                if ((temp^(temp<<1))&0x80) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                temp<<=1;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                if (temp&0x80) flags|=V_FLAG;
                                else           flags&=~V_FLAG;
                                temp>>=1;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                break;
                                case 0x38: /*SAR b,1*/
                                if (temp&1) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                temp>>=1;
                                if (temp&0x40) temp|=0x80;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                flags&=~V_FLAG;
                                break;

                                default:
                                printf("Bad D0 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xD1:
                        fetchea();
                        tempw=geteaw();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL w,1*/
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                tempw<<=1;
                                if (flags&C_FLAG) tempw|=1;
                                seteaw(tempw);
//                                setznp16(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x08: /*ROR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (flags&C_FLAG) tempw|=0x8000;
                                seteaw(tempw);
//                                setznp16(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x10: /*RCL w,1*/
                                temp2=flags&C_FLAG;
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                tempw<<=1;
                                if (temp2) tempw|=1;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x18: /*RCR w,1*/
                                temp2=flags&C_FLAG;
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (temp2) tempw|=0x8000;
                                seteaw(tempw);
//                                setznp16(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tubecycles-=((mod==3)?2:15);
                                break;
                                case 0x20: /*SHL w,1*/
                                if (tempw&0x8000) flags|=C_FLAG;
                                else              flags&=~C_FLAG;
                                if ((tempw^(tempw<<1))&0x8000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tempw<<=1;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                if (tempw&0x8000) flags|=V_FLAG;
                                else              flags&=~V_FLAG;
                                tempw>>=1;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                break;

                                case 0x38: /*SAR w,1*/
                                if (tempw&1) flags|=C_FLAG;
                                else         flags&=~C_FLAG;
                                tempw>>=1;
                                if (tempw&0x4000) tempw|=0x8000;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=((mod==3)?2:15);
                                flags|=A_FLAG;
                                flags&=~V_FLAG;
                                break;

                                default:
                                printf("Bad D1 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xD2:
                        fetchea();
                        temp=geteab();
                        c=CL&31;
//                        tubecycles-=c;
                        if (!c) break;
//                        if (c>7) printf("Shiftb %i %02X\n",rmdat&0x38,c);
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL b,CL*/
                                while (c>0)
                                {
                                        temp2=(temp&0x80)?1:0;
                                        temp=(temp<<1)|temp2;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
//                                setznp8(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x08: /*ROR b,CL*/
                                while (c>0)
                                {
                                        temp2=temp&1;
                                        temp>>=1;
                                        if (temp2) temp|=0x80;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp2) flags|=C_FLAG;
                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x10: /*RCL b,CL*/
//                                printf("RCL %i %02X %02X\n",c,CL,temp);
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        temp2=temp&0x80;
                                        temp<<=1;
                                        if (temp2) flags|=C_FLAG;
                                        else       flags&=~C_FLAG;
                                        if (templ) temp|=1;
                                        c--;
                                        tubecycles--;
                                }
//                                printf("Now %02X\n",temp);
                                seteab(temp);
                                if ((flags&C_FLAG)^(temp>>7)) flags|=V_FLAG;
                                else                          flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x18: /*RCR b,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        temp2=temp&1;
                                        temp>>=1;
                                        if (temp2) flags|=C_FLAG;
                                        else       flags&=~C_FLAG;
                                        if (templ) temp|=0x80;
                                        c--;
                                        tubecycles--;
                                }
//                                if (temp2) flags|=C_FLAG;
//                                else       flags&=~C_FLAG;
                                seteab(temp);
                                if ((temp^(temp>>1))&0x40) flags|=V_FLAG;
                                else                       flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x20: case 0x30: /*SHL b,CL*/
                                if ((temp<<(c-1))&0x80) flags|=C_FLAG;
                                else                    flags&=~C_FLAG;
                                temp<<=c;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;
                                case 0x28: /*SHR b,CL*/
                                if ((temp>>(c-1))&1) flags|=C_FLAG;
                                else                 flags&=~C_FLAG;
                                temp>>=c;
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;
                                case 0x38: /*SAR b,CL*/
                                if ((temp>>(c-1))&1) flags|=C_FLAG;
                                else                 flags&=~C_FLAG;
                                while (c>0)
                                {
                                        temp>>=1;
                                        if (temp&0x40) temp|=0x80;
                                        c--;
                                        tubecycles--;
                                }
                                seteab(temp);
                                setznp8(temp);
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                default:
                                printf("Bad D2 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xD3:
                        fetchea();
                        tempw=geteaw();
                        c=CL&31;
//                      tubecycles-=c;
                        if (!c) break;
//                        if (c>15) printf("Shiftw %i %02X\n",rmdat&0x38,c);
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*ROL w,CL*/
                                while (c>0)
                                {
                                        temp=(tempw&0x8000)?1:0;
                                        tempw=(tempw<<1)|temp;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp) flags|=C_FLAG;
                                else      flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x08: /*ROR w,CL*/
                                while (c>0)
                                {
                                        tempw2=(tempw&1)?0x8000:0;
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        tubecycles--;
                                }
                                if (tempw2) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x10: /*RCL w,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        if (tempw&0x8000) flags|=C_FLAG;
                                        else              flags&=~C_FLAG;
                                        tempw=(tempw<<1)|templ;
                                        c--;
                                        tubecycles--;
                                }
                                if (temp) flags|=C_FLAG;
                                else      flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((flags&C_FLAG)^(tempw>>15)) flags|=V_FLAG;
                                else                            flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;
                                case 0x18: /*RCR w,CL*/
                                while (c>0)
                                {
                                        templ=flags&C_FLAG;
                                        tempw2=(templ&1)?0x8000:0;
                                        if (tempw&1) flags|=C_FLAG;
                                        else         flags&=~C_FLAG;
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        tubecycles--;
                                }
                                if (tempw2) flags|=C_FLAG;
                                else        flags&=~C_FLAG;
                                seteaw(tempw);
                                if ((tempw^(tempw>>1))&0x4000) flags|=V_FLAG;
                                else                           flags&=~V_FLAG;
                                tubecycles-=((mod==3)?5:17);
                                break;

                                case 0x20: case 0x30: /*SHL w,CL*/
                                if (c>16)
                                {
                                        tempw=0;
                                        flags&=~C_FLAG;
                                }
                                else
                                {
                                        if ((tempw<<(c-1))&0x8000) flags|=C_FLAG;
                                        else                       flags&=~C_FLAG;
                                        tempw<<=c;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                case 0x28:            /*SHR w,CL*/
                                if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                else                  flags&=~C_FLAG;
                                tempw>>=c;
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=c;
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                case 0x38:            /*SAR w,CL*/
                                tempw2=tempw&0x8000;
                                if ((tempw>>(c-1))&1) flags|=C_FLAG;
                                else                  flags&=~C_FLAG;
                                while (c>0)
                                {
                                        tempw=(tempw>>1)|tempw2;
                                        c--;
                                        tubecycles--;
                                }
                                seteaw(tempw);
                                setznp16(tempw);
                                tubecycles-=((mod==3)?5:17);
                                flags|=A_FLAG;
                                break;

                                default:
                                printf("Bad D3 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xD4: /*AAM*/
                        tempws=readmembl(cs+pc); pc++;
                        AH=AL/tempws;
                        AL%=tempws;
                        setznp168(AX);
                        tubecycles-=19;
                        break;
                        case 0xD5: /*AAD*/
                        tempws=readmembl(cs+pc); pc++;
                        AL=(AH*tempws)+AL;
                        AH=0;
                        setznp168(AX);
                        tubecycles-=15;
                        break;
                        case 0xD7: /*XLAT*/
                        addr=BX+AL;
                        AL=readmembl(ds+addr);
                        tubecycles-=11;
                        break;
                        case 0xD9: case 0xDA: case 0xDB: case 0xDD: /*ESCAPE*/
                        case 0xDC: case 0xDE: case 0xDF: case 0xD8:
                                fetchea();
                                geteab();
                        tubecycles-=6;
                        break;

                        case 0xE0: /*LOOPNE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        CX--;
                        if (CX && !(flags&Z_FLAG)) { pc+=offset; tubecycles-=11; }
                        tubecycles-=5;
                        break;
                        case 0xE1: /*LOOPE*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        CX--;
                        if (CX && (flags&Z_FLAG)) { pc+=offset; tubecycles-=11; }
                        tubecycles-=5;
                        break;
                        case 0xE2: /*LOOP*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        CX--;
                        if (CX) { pc+=offset; tubecycles-=10; }
                        tubecycles-=5;
                        break;
                        case 0xE3: /*JCXZ*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        if (!CX) { pc+=offset; tubecycles-=11; }
                        tubecycles-=5;
                        break;

                        case 0xE4: /*IN AL*/
                        temp=readmembl(cs+pc); pc++;
                        AL=inb(temp);
                        tubecycles-=10;
                        break;
                        case 0xE5: /*IN AX*/
                        temp=readmembl(cs+pc); pc++;
                        AL=inb(temp);
                        AH=inb(temp+1);
                        tubecycles-=10;
                        break;
                        case 0xE6: /*OUT AL*/
                        temp=readmembl(cs+pc); pc++;
                        outb(temp,AL);
                        tubecycles-=9;
                        break;
                        case 0xE7: /*OUT AX*/
                        temp=readmembl(cs+pc); pc++;
                        outb(temp,AL);
                        outb(temp+1,AH);
                        tubecycles-=9;
                        break;

                        case 0xE8: /*CALL rel 16*/
                        tempw=getword();
                        if (ssegs) ss=oldss;
                        writememwl(ss,((SP-2)&0xFFFF),pc);
                        SP-=2;
                        pc+=tempw;
                        tubecycles-=14;
                        break;
                        case 0xE9: /*JMP rel 16*/
//                        printf("PC was %04X\n",pc);
                        pc+=getword();
//                        printf("PC now %04X\n",pc);
                        tubecycles-=13;
                        break;
                        case 0xEA: /*JMP far*/
                        addr=getword();
                        tempw=getword();
                        pc=addr;
                        loadcs(tempw);
//                        cs=loadcs(CS);
//                        cs=CS<<4;
                        tubecycles-=13;
                        break;
                        case 0xEB: /*JMP rel*/
                        offset=(signed char)readmembl(cs+pc); pc++;
                        pc+=offset;
                        tubecycles-=13;
                        break;
                        case 0xEC: /*IN AL,DX*/
                        AL=inb(DX);
                        tubecycles-=8;
                        break;
                        case 0xED: /*IN AX,DX*/
                        AL=inb(DX);
                        AH=inb(DX+1);
                        tubecycles-=8;
                        break;
                        case 0xEE: /*OUT DX,AL*/
                        outb(DX,AL);
                        tubecycles-=7;
                        break;
                        case 0xEF: /*OUT DX,AX*/
                        outb(DX,AL);
                        outb(DX+1,AH);
                        tubecycles-=7;
                        break;

                        case 0xF0: /*LOCK*/
                        tubecycles-=4;
                        break;

                        case 0xF2: /*REPNE*/
                        rep(0);
                        break;
                        case 0xF3: /*REPE*/
                        rep(1);
                        break;

                        case 0xF4: /*HLT*/
//                        printf("IN HLT!!!! %04X:%04X %08X %08X %08X\n",oldcs,oldpc,old8,old82,old83);
//                        x86dumpregs();
//                        exit(-1);
                        inhlt=1;
                        pc--;
                        tubecycles-=2;
                        break;
                        case 0xF5: /*CMC*/
                        flags^=C_FLAG;
                        tubecycles-=2;
                        break;

                        case 0xF6:
                        fetchea();
                        temp=geteab();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*TEST b,#8*/
                                temp2=readmembl(cs+pc); pc++;
                                temp&=temp2;
                                setznp8(temp);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                tubecycles-=((mod==3)?4:10);
                                break;
                                case 0x10: /*NOT b*/
                                temp=~temp;
                                seteab(temp);
                                tubecycles-=((mod==3)?3:13);
                                break;
                                case 0x18: /*NEG b*/
                                setsub8(0,temp);
                                temp=0-temp;
                                seteab(temp);
                                tubecycles-=((mod==3)?3:13);
                                break;
                                case 0x20: /*MUL AL,b*/
                                setznp8(AL);
                                AX=AL*temp;
                                if (AX) flags&=~Z_FLAG;
                                else    flags|=Z_FLAG;
                                if (AH) flags|=(C_FLAG|V_FLAG);
                                else    flags&=~(C_FLAG|V_FLAG);
                                tubecycles-=26;
                                break;
                                case 0x28: /*IMUL AL,b*/
                                setznp8(AL);
                                tempws=(int)((signed char)AL)*(int)((signed char)temp);
                                AX=tempws&0xFFFF;
                                if (AX) flags&=~Z_FLAG;
                                else    flags|=Z_FLAG;
                                if (AH) flags|=(C_FLAG|V_FLAG);
                                else    flags&=~(C_FLAG|V_FLAG);
                                tubecycles-=25;
                                break;
                                case 0x30: /*DIV AL,b*/
                                tempw=AX;
                                if (temp)
                                {
                                        tempw2=tempw%temp;
/*                                        if (!tempw)
                                        {
                                                writememwl((ss+SP)-2,flags|0xF000);
                                                writememwl((ss+SP)-4,cs>>4);
                                                writememwl((ss+SP)-6,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0);
                                                cs=readmemwl(2)<<4;
                                                printf("Div by zero %04X:%04X\n",cs>>4,pc);
//                                                x86dumpregs();
//                                                exit(-1);
                                        }
                                        else
                                        {*/
                                                AH=tempw2;
                                                tempw/=temp;
                                                AL=tempw&0xFF;
//                                        }
                                }
                                else
                                {
                                        printf("DIVb BY 0 %04X:%04X\n",cs>>4,pc);
                                                writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                                                writememwl(ss,(SP-4)&0xFFFF,CS);
                                                writememwl(ss,(SP-6)&0xFFFF,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0,0);
                                                loadcs(readmemwl(0,2));
//                                                cs=loadcs(CS);
//                                                cs=CS<<4;
//                                        printf("Div by zero %04X:%04X %02X %02X\n",cs>>4,pc,0xf6,0x30);
//                                        x86dumpregs();
//                                        exit(-1);
                                }
                                tubecycles-=29;
                                break;
                                case 0x38: /*IDIV AL,b*/
                                tempws=(int)AX;
                                if (temp)
                                {
                                        tempw2=tempws%(int)((signed char)temp);
/*                                        if (!tempw)
                                        {
                                                writememwl((ss+SP)-2,flags|0xF000);
                                                writememwl((ss+SP)-4,cs>>4);
                                                writememwl((ss+SP)-6,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0);
                                                cs=readmemwl(2)<<4;
                                                printf("Div by zero %04X:%04X\n",cs>>4,pc);
                                        }
                                        else
                                        {*/
                                                AH=tempw2&0xFF;
                                                tempws/=(int)((signed char)temp);
                                                AL=tempws&0xFF;
//                                        }
                                }
                                else
                                {
                                        printf("IDIVb BY 0 %04X:%04X\n",cs>>4,pc);
                                                writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                                                writememwl(ss,(SP-4)&0xFFFF,CS);
                                                writememwl(ss,(SP-6)&0xFFFF,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0,0);
                                                loadcs(readmemwl(0,2));
//                                                cs=loadcs(CS);
//                                                cs=CS<<4;
//                                        printf("Div by zero %04X:%04X %02X %02X\n",cs>>4,pc,0xf6,0x38);
                                }
                                tubecycles-=44;
                                break;

                                default:
                                printf("Bad F6 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xF7:
                        fetchea();
                        tempw=geteaw();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*TEST w*/
                                tempw2=getword();
                                setznp16(tempw&tempw2);
                                flags&=~(C_FLAG|V_FLAG|A_FLAG);
                                tubecycles-=((mod==3)?4:10);
                                break;
                                case 0x10: /*NOT w*/
                                seteaw(~tempw);
                                tubecycles-=((mod==3)?3:13);
                                break;
                                case 0x18: /*NEG w*/
                                setsub16(0,tempw);
                                tempw=0-tempw;
                                seteaw(tempw);
                                tubecycles-=((mod==3)?3:13);
                                break;
                                case 0x20: /*MUL AX,w*/
                                setznp16(AX);
                                templ=AX*tempw;
                                AX=templ&0xFFFF;
                                DX=templ>>16;
                                if (AX|DX) flags&=~Z_FLAG;
                                else       flags|=Z_FLAG;
                                if (DX)    flags|=(C_FLAG|V_FLAG);
                                else       flags&=~(C_FLAG|V_FLAG);
                                tubecycles-=35;
                                break;
                                case 0x28: /*IMUL AX,w*/
                                setznp16(AX);
//                                printf("IMUL %i %i ",(int)((signed short)AX),(int)((signed short)tempw));
                                tempws=(int)((signed short)AX)*(int)((signed short)tempw);
                                if ((tempws>>15) && ((tempws>>15)!=-1)) flags|=(C_FLAG|V_FLAG);
                                else                                    flags&=~(C_FLAG|V_FLAG);
//                                printf("%i ",tempws);
                                AX=tempws&0xFFFF;
                                tempws=(uint16_t)(tempws>>16);
                                DX=tempws&0xFFFF;
//                                printf("%04X %04X\n",AX,DX);
//                                x86dumpregs();
//                                exit(-1);
                                if (AX|DX) flags&=~Z_FLAG;
                                else       flags|=Z_FLAG;
                                tubecycles-=34;
                                break;
                                case 0x30: /*DIV AX,w*/
                                templ=(DX<<16)|AX;
//                                printf("DIV %08X/%04X\n",templ,tempw);
                                if (tempw)
                                {
                                        tempw2=templ%tempw;
                                        DX=tempw2;
                                        templ/=tempw;
                                        AX=templ&0xFFFF;
                                }
                                else
                                {
//                                        printf("DIVw BY 0 %04X:%04X\n",cs>>4,pc);
//                                        x86dumpregs();
//                                        exit(-1);
//                                        printf("%04X:%04X\n",cs>>4,pc);
                                                writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                                                writememwl(ss,(SP-4)&0xFFFF,CS);
                                                writememwl(ss,(SP-6)&0xFFFF,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0,0);
                                                loadcs(readmemwl(0,2));
//                                                cs=loadcs(CS);
//                                                cs=CS<<4;
//                                        printf("Div by zero %04X:%04X %02X %02X 1\n",cs>>4,pc,0xf7,0x30);
                                }
                                tubecycles-=38;
                                break;
                                case 0x38: /*IDIV AX,w*/
                                tempws=(int)((DX<<16)|AX);
//                                printf("IDIV %i %i ",tempws,tempw);
                                if (tempw)
                                {
                                        tempw2=tempws%(int)((signed short)tempw);
//                                        printf("%04X ",tempw2);
                                                DX=tempw2;
                                                tempws/=(int)((signed short)tempw);
                                                AX=tempws&0xFFFF;
                                }
                                else
                                {
//                                        printf("IDIVw BY 0 %04X:%04X\n",cs>>4,pc);
//                                        printf("%04X:%04X\n",cs>>4,pc);
                                                writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                                                writememwl(ss,(SP-4)&0xFFFF,CS);
                                                writememwl(ss,(SP-6)&0xFFFF,pc);
                                                SP-=6;
                                                flags&=~I_FLAG;
                                                pc=readmemwl(0,0);
                                                loadcs(readmemwl(0,2));
//                                                cs=loadcs(CS);
//                                                cs=CS<<4;
//                                        printf("Div by zero %04X:%04X %02X %02X 1\n",cs>>4,pc,0xf7,0x38);
                                }
                                tubecycles-=53;
                                break;

                                default:
                                printf("Bad F7 opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xF8: /*CLC*/
                        flags&=~C_FLAG;
                        tubecycles-=2;
                        break;
                        case 0xF9: /*STC*/
//                        printf("STC %04X\n",pc);
                        flags|=C_FLAG;
                        tubecycles-=2;
                        break;
                        case 0xFA: /*CLI*/
                        flags&=~I_FLAG;
//                        printf("CLI at %04X:%04X\n",cs>>4,pc);
                        tubecycles-=3;
                        break;
                        case 0xFB: /*STI*/
                        flags|=I_FLAG;
//                        printf("STI at %04X:%04X\n",cs>>4,pc);
                        tubecycles-=2;
                        break;
                        case 0xFC: /*CLD*/
                        flags&=~D_FLAG;
                        tubecycles-=2;
                        break;
                        case 0xFD: /*STD*/
                        flags|=D_FLAG;
                        tubecycles-=2;
                        break;

                        case 0xFE: /*INC/DEC b*/
                        fetchea();
                        temp=geteab();
                        flags&=~V_FLAG;
                        if (rmdat&0x38)
                        {
                                setsub8nc(temp,1);
                                temp2=temp-1;
                                if ((temp&0x80) && !(temp2&0x80)) flags|=V_FLAG;
                        }
                        else
                        {
                                setadd8nc(temp,1);
                                temp2=temp+1;
                                if ((temp2&0x80) && !(temp&0x80)) flags|=V_FLAG;
                        }
                        seteab(temp2);
                        tubecycles-=((mod==3)?3:15);
                        break;

                        case 0xFF:
                        fetchea();
                        switch (rmdat&0x38)
                        {
                                case 0x00: /*INC w*/
                                tempw=geteaw();
                                setadd16nc(tempw,1);
//                                setznp16(tempw+1);
                                seteaw(tempw+1);
                                tubecycles-=((mod==3)?3:15);
                                break;
                                case 0x08: /*DEC w*/
                                tempw=geteaw();
                                setsub16nc(tempw,1);
                                seteaw(tempw-1);
                                tubecycles-=((mod==3)?3:15);
                                break;
                                case 0x10: /*CALL*/
                                tempw=geteaw();
                                if (ssegs) ss=oldss;
                                writememwl(ss,(SP-2)&0xFFFF,pc);
                                SP-=2;
                                pc=tempw;
                                tubecycles-=((mod==3)?13:19);
                                break;
                                case 0x18: /*CALL far*/
/*                                if (CS==0x6012 && pc==0x15EE)
                                {
                                        bem_debug("Mouse trap!\n");
                                        getmousepos(&AX,&CX,&DX);
                                }*/
                                tempw=readmemwl(easeg,eaaddr);
                                tempw2=readmemwl(easeg,(eaaddr+2)&0xFFFF); //geteaw2();
//                                printf("Call FAR %04X:%04X %04X:%04X\n",CS,pc,tempw2,tempw);
                                tempw3=CS;
                                tempw4=pc;
                                if (ssegs) ss=oldss;
                                pc=tempw;
                                loadcs(tempw2);
                                writememwl(ss,(SP-2)&0xFFFF,tempw3);
                                writememwl(ss,((SP-4)&0xFFFF),tempw4);
                                SP-=4;
                                tubecycles-=38;
                                break;
                                case 0x20: /*JMP*/
                                pc=geteaw();
                                tubecycles-=((mod==3)?17:11);
                                break;
                                case 0x28: /*JMP far*/
                                pc=readmemwl(easeg,eaaddr); //geteaw();
                                loadcs(readmemwl(easeg,(eaaddr+2)&0xFFFF)); //geteaw2();
//                                cs=loadcs(CS);
//                                cs=CS<<4;
                                tubecycles-=26;
                                break;
                                case 0x30: /*PUSH w*/
                                tempw=geteaw();
//                                if (x86output) printf("PUSH %04X %i %02X %04X %04X %02X %02X\n",tempw,rm,rmdat,easeg,eaaddr,ram[0x22340+0x5638],ram[0x22340+0x5639]);
                                if (ssegs) ss=oldss;
                                writememwl(ss,((SP-2)&0xFFFF),tempw);
                                SP-=2;
                                tubecycles-=((mod==3)?10:16);
                                break;

                                default:
                                break;
//                                printf("Bad FF opcode %02X\n",rmdat&0x38);
//                                x86dumpregs();
//                                exit(-1);
                        }
                        break;

                        default:
                                pc++;
                                tubecycles-=8;
/*                        if (!AT)
                        {
                                pc++;
                                tubecycles-=8;
                        }
                        else
                        {*/
//                                printf("Bad opcode %02X at %04X:%04X from %04X:%04X %08X\n",opcode,cs>>4,pc,old8>>16,old8&0xFFFF,old82);
//                                x86dumpregs();
//                                exit(-1);
//                        }
                        break;

                        pc--;
                        tubecycles-=8;
                        break;
                        printf("Bad opcode %02X at %04X:%04X from %04X:%04X %08X\n",opcode,cs>>4,pc,old8>>16,old8&0xFFFF,old82);
                        x86dumpregs();
                        exit(-1);
                }
                pc&=0xFFFF;
//                if (CS==0x1490 && pc==0x3BBA) bem_debugf("Here from %04X:%04X %08X %02X\n",oldcs,oldpc,old8,opcode);

//                if (CS==0x6012 && pc==3) bem_debugf("XIOS direct call %02X %04X %04X %04X %04X\n",AL,CX,DX,BX,SI);

/*                if (!CS && !pc)
                {
                        printf("At zero!\n");
                        x86dumpregs();
                        exit(-1);
                }*/

                if (tube_irq&2)
                {
                        //tube_irq&=~2;
//                        printf("Let's do DMA! %i\n",tube_irq);
                        x86_dma();
                }
/*                if (tube_irq&2 && !x86oldnmi && ram[4])
                {
                        if (inhlt) pc++;
                        if (AT) writememwl(ss,(SP-2)&0xFFFF,flags&~0xF000);
                        else    writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                        writememwl(ss,(SP-4)&0xFFFF,CS);
                        writememwl(ss,(SP-6)&0xFFFF,pc);
                        SP-=6;
                        temp=2;
                        addr=temp<<2;
                        flags&=~I_FLAG;
                        pc=readmemwl(0,addr);
                        loadcs(readmemwl(0,addr+2));
                }
                x86oldnmi=tube_irq&2;*/

                if (ssegs)
                {
                        ds=oldds;
                        ss=oldss;
                        ssegs=0;
                }
                cycdiff-=tubecycles;

x86ins++;
//if (x86ins==65300000) x86output=1;
                if ((flags&I_FLAG) && !ssegs && (tube_irq&1))
                {
                        if (inhlt) pc++;
                        writememwl(ss,(SP-2)&0xFFFF,flags|0xF000);
                        writememwl(ss,(SP-4)&0xFFFF,CS);
                        writememwl(ss,(SP-6)&0xFFFF,pc);
                        SP-=6;
                        temp=12;
                        addr=temp<<2;
                        flags&=~I_FLAG;
                        pc=readmemwl(0,addr);
                        loadcs(readmemwl(0,addr+2));
//                        inint=1;
                }

/*                if (pc==0xCC32 && es>0x180000)
                {
                        pc=0xCBEB;
//                        x86output=1;
//                        timetolive=500000;
                }*/

//                if (noint) noint=0;
//                ins++;
        }
}
//#endif
