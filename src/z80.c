/*B-em v2.2 by Tom Walker
  Z80 emulation
  I can't remember what emulator I originally wrote this for... probably ZX82
  I think a few bugs left*/

#include <allegro.h>
#include <stdio.h>

#include "b-em.h"
#include "tube.h"
#include "z80.h"
#include "daa.h"

#define pc z80pc
#define ins z80ins
#define output z80output
#define cyc z80cyc
#define cycles z80cycles

/*CPU*/
typedef union
{
        uint16_t w;
        struct
        {
                uint8_t l,h;
        } b;
} z80reg;

static z80reg af,bc,de,hl,ix,iy,ir,saf,sbc,sde,shl;
static uint16_t pc,sp;
static int iff1,iff2;
static int z80int;
static int im;
static uint8_t z80ram[0x10000];
static uint8_t z80rom[4096];

static int z80_oldnmi;

#define N_FLAG 0x80
#define Z_FLAG 0x40
#define H_FLAG 0x10
#define P_FLAG 0x04
#define V_FLAG 0x04
#define S_FLAG 0x02
#define C_FLAG 0x01

static int cycles;
static uint16_t opc,oopc;
static int tempc;
static int output=0;
static int ins=0;
static uint8_t znptable[256],znptablenv[256],znptable16[65536];
static uint8_t intreg;

static int tuberomin;

static inline uint8_t z80_readmem(uint16_t a)
{
//        printf("Read Z80 %04X %i\n",a,tuberomin);
        if (a>=0x8000) tuberomin=0;
        if (tuberomin && a<0x1000) return z80rom[a&0xFFF];
        return z80ram[a];
}

uint8_t tube_z80_readmem(uint32_t addr) {
    return z80_readmem(addr & 0xffff);
}

static inline void z80_writemem(uint16_t a, uint8_t v)
{
//        printf("Write Z80 %04X %02X %04X\n",a,v,pc);
        z80ram[a]=v;
}

void tube_z80_writemem(uint32_t addr, uint8_t byte) {
    z80_writemem(addr & 0xffff, byte);
}

int endtimeslice;
static void z80out(uint16_t a, uint8_t v)
{
        if ((a&0xFF)<8)
        {
//                printf("Z80 out %04X %02X\n",a,v);
                tube_parasite_write(a,v);
                endtimeslice=1;
        }
}

static uint8_t z80in(uint16_t a)
{
        if ((a&0xFF)<8)
        {
//                printf("Z80 read tube %04X\n",a);
                if ((a&0xFF)==2) tuberomin=1;
                if ((a&0xFF)==6) tuberomin=0;
                return tube_parasite_read(a);
        }
        return 0;
}

static inline void setzn(uint8_t v)
{
        af.b.l=znptable[v];
/*        af.b.l&=~(N_FLAG|Z_FLAG|V_FLAG|0x28|C_FLAG);
        af.b.l|=znptable[v];*/
}

static inline void setand(uint8_t v)
{
        af.b.l=znptable[v]|H_FLAG;
/*        af.b.l&=~(N_FLAG|Z_FLAG|V_FLAG|0x28|C_FLAG);
        af.b.l|=znptable[v];*/
}

static inline void setbit(uint8_t v)
{
        af.b.l=((znptable[v]|H_FLAG)&0xFE)|(af.b.l&1);
}

static inline void setbit2(uint8_t v, uint8_t v2)
{
        af.b.l=((znptable[v]|H_FLAG)&0xD6)|(af.b.l&1)|(v2&0x28);
}

static inline void setznc(uint8_t v)
{
        af.b.l&=~(N_FLAG|Z_FLAG|V_FLAG|0x28);
        af.b.l|=znptable[v];
}

static inline void z80_setadd(uint8_t a, uint8_t b)
{
       uint8_t r=a+b;
                                af.b.l = (r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG;
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) < (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r < a ) af.b.l |= C_FLAG;
                                if( (b^a^0x80) & (b^r) & 0x80 ) af.b.l |= V_FLAG;
}

static inline void setinc(uint8_t v)
{
        af.b.l&=~(N_FLAG|Z_FLAG|V_FLAG|0x28|H_FLAG);
        af.b.l|=znptable[(v+1)&0xFF];
        if (v==0x7F)          af.b.l|=V_FLAG;
        else                  af.b.l&=~V_FLAG;
        if (((v&0xF)+1)&0x10) af.b.l|=H_FLAG;
}

static inline void setdec(uint8_t v)
{
        af.b.l&=~(N_FLAG|Z_FLAG|V_FLAG|0x28|H_FLAG);
        af.b.l|=znptable[(v-1)&0xFF]|S_FLAG;
        if (v==0x80)             af.b.l|=V_FLAG;
        else                     af.b.l&=~V_FLAG;
        if (!(v&8) && ((v-1)&8)) af.b.l|=H_FLAG;
}

static inline void setadc(uint8_t a, uint8_t b)
{
       uint8_t r=a+b+(af.b.l&C_FLAG);
       if (af.b.l&C_FLAG)
       {
                                af.b.l = (r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG;
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) <= (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r <= a ) af.b.l |= C_FLAG;
                                if( (b^a^0x80) & (b^r) & 0x80 ) af.b.l |= V_FLAG;
       }
       else
       {
                                af.b.l = (r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG;
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) < (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r < a ) af.b.l |= C_FLAG;
                                if( (b^a^0x80) & (b^r) & 0x80 ) af.b.l |= V_FLAG;
       }
}

static inline void setadc16(uint16_t a, uint16_t b)
{
        uint32_t r=a+b+(af.b.l&1);
        af.b.l = (((a ^ r ^ b) >> 8) & H_FLAG) |
                ((r >> 16) & C_FLAG) |
                ((r >> 8) & (N_FLAG | 0x28)) |
                ((r & 0xffff) ? 0 : Z_FLAG) |
                (((b ^ a ^ 0x8000) & (b ^ r) & 0x8000) >> 13);
}

static inline void z80_setadd16(uint16_t a, uint16_t b)
{
        uint32_t r=a+b;
        af.b.l = (af.b.l & (N_FLAG | Z_FLAG | V_FLAG)) |
                (((a ^ r ^ b) >> 8) & H_FLAG) |
                ((r >> 16) & C_FLAG) | ((r >> 8) & 0x28);
}

static inline void setsbc(uint8_t a, uint8_t b)
{
       uint8_t r=a-(b+(af.b.l&C_FLAG));
       if (af.b.l&C_FLAG)
       {
                                af.b.l = S_FLAG | ((r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG);
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) >= (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r >= a ) af.b.l |= C_FLAG;
                                if( (b^a) & (a^r) & 0x80 ) af.b.l |= V_FLAG;
       }
       else
       {
                                af.b.l = S_FLAG | ((r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG);
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) > (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r > a ) af.b.l |= C_FLAG;
                                if( (b^a) & (a^r) & 0x80 ) af.b.l |= V_FLAG;
       }
}

static inline void setsbc16(uint16_t a, uint16_t b)
{
        uint32_t r = a - b - (af.b.l & C_FLAG);
        af.b.l = (((a ^ r ^ b) >> 8) & H_FLAG) | S_FLAG |
                ((r >> 16) & C_FLAG) |
                ((r >> 8) & (N_FLAG | 0x28)) |
                ((r & 0xffff) ? 0 : Z_FLAG) |
                (((b ^ a) & (a ^ r) &0x8000) >> 13);
}

static inline void setcpED(uint8_t a, uint8_t b)
{
       uint8_t r=a-b;
       af.b.l&=C_FLAG;
                                af.b.l |= S_FLAG | ((r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG);
                                af.b.l |= (b & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) > (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( (b^a) & (a^r) & 0x80 ) af.b.l |= V_FLAG;
}

static inline void setcp(uint8_t a, uint8_t b)
{
       uint8_t r=a-b;
                                af.b.l = S_FLAG | ((r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG);
                                af.b.l |= (b & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) > (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r > a ) af.b.l |= C_FLAG;
                                if( (b^a) & (a^r) & 0x80 ) af.b.l |= V_FLAG;
}

static inline void z80_setsub(uint8_t a, uint8_t b)
{
       uint8_t r=a-b;
                                af.b.l = S_FLAG | ((r) ? ((r & 0x80) ? N_FLAG : 0) : Z_FLAG);
                                af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
                                if( (r & 0x0f) > (a & 0x0f) ) af.b.l |= H_FLAG;
                                if( r > a ) af.b.l |= C_FLAG;
                                if( (b^a) & (a^r) & 0x80 ) af.b.l |= V_FLAG;
}

static void makeznptable()
{
        int c,d,e,f,g;
        for (c=0;c<256;c++)
        {
//                d|=(c&0xA8);
                e=c;
                f=0;
                for (g=0;g<8;g++)
                {
                        if (e&1) f++;
                        e>>=1;
                }
                d=c ? (c&N_FLAG) : Z_FLAG;
                d|=(c&0x28);
                d|=(f&1)?0:V_FLAG;
/*                if (!(f&1))
                   d|=4;*/
                znptable[c]=d;
                znptablenv[c]=d&~V_FLAG;
        }
//        znptable[0]|=0x40;
        for (c=0;c<65536;c++)
        {
                d=0;
                if (c&0x8000) d|=0x80;
                e=c;
                f=0;
                for (g=0;g<16;g++)
                {
                        if (e&1) f++;
                        e>>=1;
                }
                if (!(f&1))
                   d|=4;
                znptable16[c]=d;
        }
        znptable16[0]|=0x40;
}

void z80_init()
{
        FILE *f;
        char fn[512];
        append_filename(fn,exedir,"roms/tube/Z80_120.rom",511);
        f=fopen(fn,"rb");
        fread(z80rom,0x1000,1,f);
        fclose(f);
        makeznptable();
}

void z80_close()
{
}

void z80_dumpregs()
{
        bem_debugf("AF =%04X BC =%04X DE =%04X HL =%04X IX=%04X IY=%04X\n",af.w,bc.w,de.w,hl.w,ix.w,iy.w);
        bem_debugf("AF'=%04X BC'=%04X DE'=%04X HL'=%04X IR=%04X\n",saf.w,sbc.w,sde.w,shl.w,ir.w);
        bem_debugf("%c%c%c%c%c%c   PC =%04X SP =%04X\n",(af.b.l&N_FLAG)?'N':' ',(af.b.l&Z_FLAG)?'Z':' ',(af.b.l&H_FLAG)?'H':' ',(af.b.l&V_FLAG)?'V':' ',(af.b.l&S_FLAG)?'S':' ',(af.b.l&C_FLAG)?'C':' ',pc,sp);
        bem_debugf("%i ins  IFF1=%i IFF2=%i  %04X %04X\n",ins,iff1,iff2,opc,oopc);
//        error(s);
}

void z80_mem_dump()
{
        FILE *f=fopen("z80ram.dmp","wb");
        fwrite(z80ram,0x10000,1,f);
        fclose(f);
//        atexit(z80_dumpregs);
}

void z80_reset()
{
        pc=0;
//        atexit(z80_mem_dump);
        tuberomin=1;
}

static uint16_t oopc,opc;

void z80_exec()
{
        uint8_t opcode,temp;
        uint16_t addr;
        int enterint=0;
//        tubecycles+=(cy<<1);
        while (tubecycles>0)
        {
                oopc=opc;
                opc=pc;
                if ((tube_irq&1) && iff1) enterint=1;
                cycles=0;
                tempc=af.b.l&C_FLAG;
                opcode=z80_readmem(pc++);
                ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                switch (opcode)
                {
                        case 0x00: /*NOP*/
                        cycles+=4;
//                        printf("NOP!\n");
//                        z80_dumpregs();
//                        exit(-1);
                        break;
                        case 0x01: /*LD BC,nn*/
                        cycles+=4; bc.b.l=z80_readmem(pc++);
                        cycles+=3; bc.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x02: /*LD (BC),A*/
                        cycles+=4; z80_writemem(bc.w,af.b.h);
                        cycles+=3;
                        break;
                        case 0x03: /*INC BC*/
                        bc.w++;
                        cycles+=6;
                        break;
                        case 0x04: /*INC B*/
                        setinc(bc.b.h);
                        bc.b.h++;
                        cycles+=4;
                        break;
                        case 0x05: /*DEC B*/
                        setdec(bc.b.h);
                        bc.b.h--;
                        cycles+=4;
                        break;
                        case 0x06: /*LD B,nn*/
                        cycles+=4; bc.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x07: /*RLCA*/
                        temp=af.b.h&0x80;
                        af.b.h<<=1;
                        if (temp) af.b.h|=1;
//                        setzn(af.b.h);
                        if (temp) af.b.l|=C_FLAG;
                        else      af.b.l&=~C_FLAG;
                        cycles+=4;
                        break;
                        case 0x08: /*EX AF,AF'*/
                        addr=af.w; af.w=saf.w; saf.w=addr;
                        cycles+=4;
                        break;
                        case 0x09: /*ADD HL,BC*/
                        intreg=hl.b.h;
                        z80_setadd16(hl.w,bc.w);
                        hl.w+=bc.w;
                        cycles+=11;
                        break;
                        case 0x0A: /*LD A,(BC)*/
                        cycles+=4; af.b.h=z80_readmem(bc.w);
                        cycles+=3;
                        break;
                        case 0x0B: /*DEC BC*/
                        bc.w--;
                        cycles+=6;
                        break;
                        case 0x0C: /*INC C*/
                        setinc(bc.b.l);
                        bc.b.l++;
                        cycles+=4;
                        break;
                        case 0x0D: /*DEC C*/
                        setdec(bc.b.l);
                        bc.b.l--;
                        cycles+=4;
                        break;
                        case 0x0E: /*LD C,nn*/
                        cycles+=4; bc.b.l=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x0F: /*RRCA*/
                        temp=af.b.h&1;
                        af.b.h>>=1;
                        if (temp) af.b.h|=0x80;
//                        setzn(af.b.h);
                        if (temp) af.b.l|=C_FLAG;
                        else      af.b.l&=~C_FLAG;
                        cycles+=4;
                        break;

                        case 0x10: /*DJNZ*/
                        cycles+=5; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        if (--bc.b.h)
                        {
                                pc+=addr;
                                cycles+=8;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0x11: /*LD DE,nn*/
                        cycles+=4; de.b.l=z80_readmem(pc++);
                        cycles+=3; de.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x12: /*LD (DE),A*/
                        cycles+=4; z80_writemem(de.w,af.b.h);
                        cycles+=3;
                        break;
                        case 0x13: /*INC DE*/
                        de.w++;
                        cycles+=6;
                        break;
                        case 0x14: /*INC D*/
                        setinc(de.b.h);
                        de.b.h++;
                        cycles+=4;
                        break;
                        case 0x15: /*DEC D*/
                        setdec(de.b.h);
                        de.b.h--;
                        cycles+=4;
                        break;
                        case 0x16: /*LD D,nn*/
                        cycles+=4; de.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x17: /*RLA*/
                        temp=af.b.h&0x80;
                        af.b.h<<=1;
                        if (tempc) af.b.h|=1;
//                        setzn(af.b.h);
                        if (temp) af.b.l|=C_FLAG;
                        else      af.b.l&=~C_FLAG;
                        cycles+=4;
                        break;
                        case 0x18: /*JR*/
                        cycles+=4; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        pc+=addr;
                        intreg=pc>>8;
                        cycles+=8;
                        break;
                        case 0x19: /*ADD HL,DE*/
                        intreg=hl.b.h;
                        z80_setadd16(hl.w,de.w);
                        hl.w+=de.w;
                        cycles+=11;
                        break;
                        case 0x1A: /*LD A,(DE)*/
                        cycles+=4; af.b.h=z80_readmem(de.w);
                        cycles+=3;
                        break;
                        case 0x1B: /*DEC DE*/
                        de.w--;
                        cycles+=6;
                        break;
                        case 0x1C: /*INC E*/
                        setinc(de.b.l);
                        de.b.l++;
                        cycles+=4;
                        break;
                        case 0x1D: /*DEC E*/
                        setdec(de.b.l);
                        de.b.l--;
                        cycles+=4;
                        break;
                        case 0x1E: /*LD E,nn*/
                        cycles+=4; de.b.l=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x1F: /*RRA*/
                        temp=af.b.h&1;
                        af.b.h>>=1;
                        if (tempc) af.b.h|=0x80;
//                        setzn(af.b.h);
                        if (temp) af.b.l|=C_FLAG;
                        else      af.b.l&=~C_FLAG;
                        cycles+=4;
                        break;

                        case 0x20: /*JR NZ*/
                        cycles+=4; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        if (!(af.b.l&Z_FLAG))
                        {
                                pc+=addr;
                                cycles+=8;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0x21: /*LD HL,nn*/
                        cycles+=4; hl.b.l=z80_readmem(pc++);
                        cycles+=3; hl.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x22: /*LD (nn),HL*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        cycles+=3; z80_writemem(addr,hl.b.l);
                        cycles+=3; z80_writemem(addr+1,hl.b.h);
                        cycles+=3;
                        break;
                        case 0x23: /*INC HL*/
                        hl.w++;
                        cycles+=6;
                        break;
                        case 0x24: /*INC H*/
                        setinc(hl.b.h);
                        hl.b.h++;
                        cycles+=4;
                        break;
                        case 0x25: /*DEC H*/
                        setdec(hl.b.h);
                        hl.b.h--;
                        cycles+=4;
                        break;
                        case 0x26: /*LD H,nn*/
                        cycles+=4; hl.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x27: /*DAA*/
                        addr=af.b.h;
                        if (af.b.l&C_FLAG) addr|=256;
                        if (af.b.l&H_FLAG) addr|=512;
                        if (af.b.l&S_FLAG) addr|=1024;
                        af.w=DAATable[addr];
                        cycles+=4;
                        break;
                        case 0x28: /*JR Z*/
                        cycles+=4; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        if (af.b.l&Z_FLAG)
                        {
                                pc+=addr;
                                cycles+=8;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0x29: /*ADD HL,HL*/
                        intreg=hl.b.h;
                        z80_setadd16(hl.w,hl.w);
                        hl.w+=hl.w;
                        cycles+=11;
                        break;
                        case 0x2A: /*LD HL,(nn)*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        cycles+=3; hl.b.l=z80_readmem(addr);
                        cycles+=3; hl.b.h=z80_readmem(addr+1);
                        cycles+=3;
                        break;
                        case 0x2B: /*DEC HL*/
                        hl.w--;
                        cycles+=6;
                        break;
                        case 0x2C: /*INC L*/
                        setinc(hl.b.l);
                        hl.b.l++;
                        cycles+=4;
                        break;
                        case 0x2D: /*DEC L*/
                        setdec(hl.b.l);
                        hl.b.l--;
                        cycles+=4;
                        break;
                        case 0x2E: /*LD L,nn*/
                        cycles+=4; hl.b.l=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x2F: /*CPL*/
                        af.b.h^=0xFF;
                        af.b.l|=(H_FLAG|S_FLAG);
                        cycles+=4;
                        break;
                        case 0x30: /*JR NC*/
                        cycles+=4; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        if (!(af.b.l&C_FLAG))
                        {
                                pc+=addr;
                                cycles+=8;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0x31: /*LD SP,nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        cycles+=3; sp=(z80_readmem(pc++)<<8)|temp;
                        cycles+=3;
                        break;
                        case 0x32: /*LD (nn),A*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        cycles+=3; z80_writemem(addr,af.b.h);
                        cycles+=3;
                        break;
                        case 0x33: /*INC SP*/
                        sp++;
                        cycles+=6;
                        break;
                        case 0x34: /*INC (HL)*/
                        cycles+=4; temp=z80_readmem(hl.w);
                        setinc(temp);
                        cycles+=3; z80_writemem(hl.w,temp+1);
                        cycles+=3;
                        break;
                        case 0x35: /*DEC (HL)*/
                        cycles+=4; temp=z80_readmem(hl.w);
                        setdec(temp);
                        cycles+=3; z80_writemem(hl.w,temp-1);
                        cycles+=3;
                        break;
                        case 0x36: /*LD (HL),nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        cycles+=3; z80_writemem(hl.w,temp);
                        cycles+=3;
                        break;
                        case 0x37: /*SCF*/
                        af.b.l|=C_FLAG;
                        cycles+=4;
                        break;
                        case 0x38: /*JR C*/
                        cycles+=4; addr=z80_readmem(pc++);
                        if (addr&0x80) addr|=0xFF00;
                        if (af.b.l&C_FLAG)
                        {
                                pc+=addr;
                                cycles+=8;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0x39: /*ADD HL,SP*/
                        intreg=hl.b.h;
                        z80_setadd16(hl.w,sp);
                        hl.w+=sp;
                        cycles+=11;
                        break;
                        case 0x3A: /*LD A,(nn)*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        cycles+=3; af.b.h=z80_readmem(addr);
                        cycles+=3;
                        break;
                        case 0x3B: /*DEC SP*/
                        sp--;
                        cycles+=6;
                        break;
                        case 0x3C: /*INC A*/
                        setinc(af.b.h);
                        af.b.h++;
                        cycles+=4;
                        break;
                        case 0x3D: /*DEC A*/
                        setdec(af.b.h);
                        af.b.h--;
                        cycles+=4;
                        break;
                        case 0x3E: /*LD A,nn*/
                        cycles+=4; af.b.h=z80_readmem(pc++);
                        cycles+=3;
                        break;
                        case 0x3F: /*CCF*/
                        af.b.l^=C_FLAG;
                        cycles+=4;
                        break;

                        case 0x40: bc.b.h=bc.b.h;        cycles+=4; break; /*LD B,B*/
                        case 0x41: bc.b.h=bc.b.l;        cycles+=4; break; /*LD B,C*/
                        case 0x42: bc.b.h=de.b.h;        cycles+=4; break; /*LD B,D*/
                        case 0x43: bc.b.h=de.b.l;        cycles+=4; break; /*LD B,E*/
                        case 0x44: bc.b.h=hl.b.h;        cycles+=4; break; /*LD B,H*/
                        case 0x45: bc.b.h=hl.b.l;        cycles+=4; break; /*LD B,L*/
                        case 0x46: cycles+=4; bc.b.h=z80_readmem(hl.w); cycles+=3; break; /*LD B,(HL)*/
                        case 0x47: bc.b.h=af.b.h;        cycles+=4; break; /*LD B,A*/
                        case 0x48: bc.b.l=bc.b.h;        cycles+=4; break; /*LD C,B*/
                        case 0x49: bc.b.l=bc.b.l;        cycles+=4; break; /*LD C,C*/
                        case 0x4A: bc.b.l=de.b.h;        cycles+=4; break; /*LD C,D*/
                        case 0x4B: bc.b.l=de.b.l;        cycles+=4; break; /*LD C,E*/
                        case 0x4C: bc.b.l=hl.b.h;        cycles+=4; break; /*LD C,H*/
                        case 0x4D: bc.b.l=hl.b.l;        cycles+=4; break; /*LD C,L*/
                        case 0x4E: cycles+=4; bc.b.l=z80_readmem(hl.w); cycles+=3; break; /*LD C,(HL)*/
                        case 0x4F: bc.b.l=af.b.h;        cycles+=4; break; /*LD C,A*/
                        case 0x50: de.b.h=bc.b.h;        cycles+=4; break; /*LD D,B*/
                        case 0x51: de.b.h=bc.b.l;        cycles+=4; break; /*LD D,C*/
                        case 0x52: de.b.h=de.b.h;        cycles+=4; break; /*LD D,D*/
                        case 0x53: de.b.h=de.b.l;        cycles+=4; break; /*LD D,E*/
                        case 0x54: de.b.h=hl.b.h;        cycles+=4; break; /*LD D,H*/
                        case 0x55: de.b.h=hl.b.l;        cycles+=4; break; /*LD D,L*/
                        case 0x56: cycles+=4; de.b.h=z80_readmem(hl.w); cycles+=3; break; /*LD D,(HL)*/
                        case 0x57: de.b.h=af.b.h;        cycles+=4; break; /*LD D,A*/
                        case 0x58: de.b.l=bc.b.h;        cycles+=4; break; /*LD E,B*/
                        case 0x59: de.b.l=bc.b.l;        cycles+=4; break; /*LD E,C*/
                        case 0x5A: de.b.l=de.b.h;        cycles+=4; break; /*LD E,D*/
                        case 0x5B: de.b.l=de.b.l;        cycles+=4; break; /*LD E,E*/
                        case 0x5C: de.b.l=hl.b.h;        cycles+=4; break; /*LD E,H*/
                        case 0x5D: de.b.l=hl.b.l;        cycles+=4; break; /*LD E,L*/
                        case 0x5E: cycles+=4; de.b.l=z80_readmem(hl.w); cycles+=3; break; /*LD E,(HL)*/
                        case 0x5F: de.b.l=af.b.h;        cycles+=4; break; /*LD E,A*/
                        case 0x60: hl.b.h=bc.b.h;        cycles+=4; break; /*LD H,B*/
                        case 0x61: hl.b.h=bc.b.l;        cycles+=4; break; /*LD H,C*/
                        case 0x62: hl.b.h=de.b.h;        cycles+=4; break; /*LD H,D*/
                        case 0x63: hl.b.h=de.b.l;        cycles+=4; break; /*LD H,E*/
                        case 0x64: hl.b.h=hl.b.h;        cycles+=4; break; /*LD H,H*/
                        case 0x65: hl.b.h=hl.b.l;        cycles+=4; break; /*LD H,L*/
                        case 0x66: cycles+=4; hl.b.h=z80_readmem(hl.w); cycles+=3; break; /*LD H,(HL)*/
                        case 0x67: hl.b.h=af.b.h;        cycles+=4; break; /*LD H,A*/
                        case 0x68: hl.b.l=bc.b.h;        cycles+=4; break; /*LD L,B*/
                        case 0x69: hl.b.l=bc.b.l;        cycles+=4; break; /*LD L,C*/
                        case 0x6A: hl.b.l=de.b.h;        cycles+=4; break; /*LD L,D*/
                        case 0x6B: hl.b.l=de.b.l;        cycles+=4; break; /*LD L,E*/
                        case 0x6C: hl.b.l=hl.b.h;        cycles+=4; break; /*LD L,H*/
                        case 0x6D: hl.b.l=hl.b.l;        cycles+=4; break; /*LD L,L*/
                        case 0x6E: cycles+=4; hl.b.l=z80_readmem(hl.w); cycles+=3; break; /*LD L,(HL)*/
                        case 0x6F: hl.b.l=af.b.h;        cycles+=4; break; /*LD L,A*/
                        case 0x70: cycles+=4; z80_writemem(hl.w,bc.b.h); cycles+=3; break; /*LD (HL),B*/
                        case 0x71: cycles+=4; z80_writemem(hl.w,bc.b.l); cycles+=3; break; /*LD (HL),C*/
                        case 0x72: cycles+=4; z80_writemem(hl.w,de.b.h); cycles+=3; break; /*LD (HL),D*/
                        case 0x73: cycles+=4; z80_writemem(hl.w,de.b.l); cycles+=3; break; /*LD (HL),E*/
                        case 0x74: cycles+=4; z80_writemem(hl.w,hl.b.h); cycles+=3; break; /*LD (HL),H*/
                        case 0x75: cycles+=4; z80_writemem(hl.w,hl.b.l); cycles+=3; break; /*LD (HL),L*/
                        case 0x77: cycles+=4; z80_writemem(hl.w,af.b.h); cycles+=3; break; /*LD (HL),A*/
                        case 0x78: af.b.h=bc.b.h;        cycles+=4; break; /*LD A,B*/
                        case 0x79: af.b.h=bc.b.l;        cycles+=4; break; /*LD A,C*/
                        case 0x7A: af.b.h=de.b.h;        cycles+=4; break; /*LD A,D*/
                        case 0x7B: af.b.h=de.b.l;        cycles+=4; break; /*LD A,E*/
                        case 0x7C: af.b.h=hl.b.h;        cycles+=4; break; /*LD A,H*/
                        case 0x7D: af.b.h=hl.b.l;        cycles+=4; break; /*LD A,L*/
                        case 0x7E: cycles+=4; af.b.h=z80_readmem(hl.w); cycles+=3; break; /*LD A,(HL)*/
                        case 0x7F: af.b.h=af.b.h;        cycles+=4; break; /*LD A,A*/

                        case 0x76: /*HALT*/
                        if (!enterint) pc--;
//                        else printf("HALT %02X\n",bc.b.h);
                        cycles+=4;
                        break;

                        case 0x80: z80_setadd(af.b.h,bc.b.h); af.b.h+=bc.b.h; cycles+=4; break; /*ADD B*/
                        case 0x81: z80_setadd(af.b.h,bc.b.l); af.b.h+=bc.b.l; cycles+=4; break; /*ADD C*/
                        case 0x82: z80_setadd(af.b.h,de.b.h); af.b.h+=de.b.h; cycles+=4; break; /*ADD D*/
                        case 0x83: z80_setadd(af.b.h,de.b.l); af.b.h+=de.b.l; cycles+=4; break; /*ADD E*/
                        case 0x84: z80_setadd(af.b.h,hl.b.h); af.b.h+=hl.b.h; cycles+=4; break; /*ADD H*/
                        case 0x85: z80_setadd(af.b.h,hl.b.l); af.b.h+=hl.b.l; cycles+=4; break; /*ADD L*/
                        case 0x86: cycles+=4; temp=z80_readmem(hl.w); z80_setadd(af.b.h,temp); af.b.h+=temp; cycles+=3; break; /*ADD (HL)*/
                        case 0x87: z80_setadd(af.b.h,af.b.h); af.b.h+=af.b.h; cycles+=4; break; /*ADD A*/
                        case 0x88: setadc(af.b.h,bc.b.h); af.b.h+=bc.b.h+tempc; cycles+=4; break; /*ADC B*/
                        case 0x89: setadc(af.b.h,bc.b.l); af.b.h+=bc.b.l+tempc; cycles+=4; break; /*ADC C*/
                        case 0x8A: setadc(af.b.h,de.b.h); af.b.h+=de.b.h+tempc; cycles+=4; break; /*ADC D*/
                        case 0x8B: setadc(af.b.h,de.b.l); af.b.h+=de.b.l+tempc; cycles+=4; break; /*ADC E*/
                        case 0x8C: setadc(af.b.h,hl.b.h); af.b.h+=hl.b.h+tempc; cycles+=4; break; /*ADC H*/
                        case 0x8D: setadc(af.b.h,hl.b.l); af.b.h+=hl.b.l+tempc; cycles+=4; break; /*ADC L*/
                        case 0x8E: cycles+=4; temp=z80_readmem(hl.w); setadc(af.b.h,temp); af.b.h+=temp+tempc; cycles+=3; break; /*ADC (HL)*/
                        case 0x8F: setadc(af.b.h,af.b.h); af.b.h+=af.b.h+tempc; cycles+=4; break; /*ADC A*/

                        case 0x90: z80_setsub(af.b.h,bc.b.h); af.b.h-=bc.b.h; cycles+=4; break; /*SUB B*/
                        case 0x91: z80_setsub(af.b.h,bc.b.l); af.b.h-=bc.b.l; cycles+=4; break; /*SUB C*/
                        case 0x92: z80_setsub(af.b.h,de.b.h); af.b.h-=de.b.h; cycles+=4; break; /*SUB D*/
                        case 0x93: z80_setsub(af.b.h,de.b.l); af.b.h-=de.b.l; cycles+=4; break; /*SUB E*/
                        case 0x94: z80_setsub(af.b.h,hl.b.h); af.b.h-=hl.b.h; cycles+=4; break; /*SUB H*/
                        case 0x95: z80_setsub(af.b.h,hl.b.l); af.b.h-=hl.b.l; cycles+=4; break; /*SUB L*/
                        case 0x96: cycles+=4; temp=z80_readmem(hl.w); z80_setsub(af.b.h,temp); af.b.h-=temp; cycles+=3; break; /*SUB (HL)*/
                        case 0x97: z80_setsub(af.b.h,af.b.h); af.b.h-=af.b.h; cycles+=4; break; /*SUB A*/
                        case 0x98: setsbc(af.b.h,bc.b.h); af.b.h-=(bc.b.h+tempc); cycles+=4; break; /*SBC B*/
                        case 0x99: setsbc(af.b.h,bc.b.l); af.b.h-=(bc.b.l+tempc); cycles+=4; break; /*SBC C*/
                        case 0x9A: setsbc(af.b.h,de.b.h); af.b.h-=(de.b.h+tempc); cycles+=4; break; /*SBC D*/
                        case 0x9B: setsbc(af.b.h,de.b.l); af.b.h-=(de.b.l+tempc); cycles+=4; break; /*SBC E*/
                        case 0x9C: setsbc(af.b.h,hl.b.h); af.b.h-=(hl.b.h+tempc); cycles+=4; break; /*SBC H*/
                        case 0x9D: setsbc(af.b.h,hl.b.l); af.b.h-=(hl.b.l+tempc); cycles+=4; break; /*SBC L*/
                        case 0x9E: cycles+=4; temp=z80_readmem(hl.w); setsbc(af.b.h,temp); af.b.h-=(temp+tempc); cycles+=3; break; /*SBC (HL)*/
                        case 0x9F: setsbc(af.b.h,af.b.h); af.b.h-=(af.b.h+tempc); cycles+=4; break; /*SBC A*/

                        case 0xA0: af.b.h&=bc.b.h;        setand(af.b.h); cycles+=4; break; /*AND B*/
                        case 0xA1: af.b.h&=bc.b.l;        setand(af.b.h); cycles+=4; break; /*AND C*/
                        case 0xA2: af.b.h&=de.b.h;        setand(af.b.h); cycles+=4; break; /*AND D*/
                        case 0xA3: af.b.h&=de.b.l;        setand(af.b.h); cycles+=4; break; /*AND E*/
                        case 0xA4: af.b.h&=hl.b.h;        setand(af.b.h); cycles+=4; break; /*AND H*/
                        case 0xA5: af.b.h&=hl.b.l;        setand(af.b.h); cycles+=4; break; /*AND L*/
                        case 0xA6: cycles+=4; af.b.h&=z80_readmem(hl.w); setand(af.b.h); cycles+=3; break; /*AND (HL)*/
                        case 0xA7: af.b.h&=af.b.h;        setand(af.b.h); cycles+=4; break; /*AND A*/
                        case 0xA8: af.b.h^=bc.b.h;        setzn(af.b.h); cycles+=4; break; /*XOR B*/
                        case 0xA9: af.b.h^=bc.b.l;        setzn(af.b.h); cycles+=4; break; /*XOR C*/
                        case 0xAA: af.b.h^=de.b.h;        setzn(af.b.h); cycles+=4; break; /*XOR D*/
                        case 0xAB: af.b.h^=de.b.l;        setzn(af.b.h); cycles+=4; break; /*XOR E*/
                        case 0xAC: af.b.h^=hl.b.h;        setzn(af.b.h); cycles+=4; break; /*XOR H*/
                        case 0xAD: af.b.h^=hl.b.l;        setzn(af.b.h); cycles+=4; break; /*XOR L*/
                        case 0xAE: cycles+=4; af.b.h^=z80_readmem(hl.w); setzn(af.b.h); cycles+=3; break; /*XOR (HL)*/
                        case 0xAF: af.b.h^=af.b.h;        setzn(af.b.h); cycles+=4; break; /*XOR A*/
                        case 0xB0: af.b.h|=bc.b.h;        setzn(af.b.h); cycles+=4; break; /*OR B*/
                        case 0xB1: af.b.h|=bc.b.l;        setzn(af.b.h); cycles+=4; break; /*OR C*/
                        case 0xB2: af.b.h|=de.b.h;        setzn(af.b.h); cycles+=4; break; /*OR D*/
                        case 0xB3: af.b.h|=de.b.l;        setzn(af.b.h); cycles+=4; break; /*OR E*/
                        case 0xB4: af.b.h|=hl.b.h;        setzn(af.b.h); cycles+=4; break; /*OR H*/
                        case 0xB5: af.b.h|=hl.b.l;        setzn(af.b.h); cycles+=4; break; /*OR L*/
                        case 0xB6: cycles+=4; af.b.h|=z80_readmem(hl.w); setzn(af.b.h); cycles+=3; break; /*OR (HL)*/
                        case 0xB7: af.b.h|=af.b.h;        setzn(af.b.h); cycles+=4; break; /*OR A*/
                        case 0xB8: setcp(af.b.h,bc.b.h); cycles+=4; break; /*CP B*/
                        case 0xB9: setcp(af.b.h,bc.b.l); cycles+=4; break; /*CP C*/
                        case 0xBA: setcp(af.b.h,de.b.h); cycles+=4; break; /*CP D*/
                        case 0xBB: setcp(af.b.h,de.b.l); cycles+=4; break; /*CP E*/
                        case 0xBC: setcp(af.b.h,hl.b.h); cycles+=4; break; /*CP H*/
                        case 0xBD: setcp(af.b.h,hl.b.l); cycles+=4; break; /*CP L*/
                        case 0xBE: cycles+=4; temp=z80_readmem(hl.w); setcp(af.b.h,temp); cycles+=3; break; /*CP (HL)*/
                        case 0xBF: setcp(af.b.h,af.b.h); cycles+=4; break; /*CP A*/

                        case 0xC0: /*RET NZ*/
                        cycles+=5;
                        if (!(af.b.l&Z_FLAG))
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xC1: /*POP BC*/
                        cycles+=4; bc.b.l=z80_readmem(sp); sp++;
                        cycles+=3; bc.b.h=z80_readmem(sp); sp++;
                        cycles+=3;
                        break;
                        case 0xC2: /*JP NZ*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&Z_FLAG))
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xC3: /*JP xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8);
                        pc=addr;
                        cycles+=3;
                        break;
                        case 0xC4: /*CALL NZ,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&Z_FLAG))
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0xC5: /*PUSH BC*/
                        cycles+=5; sp--; z80_writemem(sp,bc.b.h);
                        cycles+=3; sp--; z80_writemem(sp,bc.b.l);
                        cycles+=3;
                        break;
                        case 0xC6: /*ADD A,nn*/
                        cycles+=4; temp=z80_readmem(pc++);
//                        printf("%04X : ADD %02X %02X - ",pc-1,af.b.h,temp);
                        z80_setadd(af.b.h,temp);
                        af.b.h+=temp;
                        cycles+=3;
//                        printf("%04X\n",af.w);
                        break;
                        case 0xC7: /*RST 0*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x00;
                        cycles+=3;
                        break;
                        case 0xC8: /*RET Z*/
                        cycles+=5;
                        if (af.b.l&Z_FLAG)
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xC9: /*RET*/
                        cycles+=4; pc=z80_readmem(sp); sp++;
                        cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                        cycles+=3;
                        break;
                        case 0xCA: /*JP Z*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&Z_FLAG)
                           pc=addr;
                        cycles+=3;
                        break;

                        case 0xCB: /*More opcodes*/
                        ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                        cycles+=4;
                        opcode=z80_readmem(pc++);
                        switch (opcode)
                        {
                                case 0x00: temp=bc.b.h&0x80; bc.b.h<<=1; if (temp) bc.b.h|=1; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC B*/
                                case 0x01: temp=bc.b.l&0x80; bc.b.l<<=1; if (temp) bc.b.l|=1; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC C*/
                                case 0x02: temp=de.b.h&0x80; de.b.h<<=1; if (temp) de.b.h|=1; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC D*/
                                case 0x03: temp=de.b.l&0x80; de.b.l<<=1; if (temp) de.b.l|=1; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC E*/
                                case 0x04: temp=hl.b.h&0x80; hl.b.h<<=1; if (temp) hl.b.h|=1; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC H*/
                                case 0x05: temp=hl.b.l&0x80; hl.b.l<<=1; if (temp) hl.b.l|=1; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC L*/
                                case 0x07: temp=af.b.h&0x80; af.b.h<<=1; if (temp) af.b.h|=1; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RLC A*/
                                case 0x06: /*RLC (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&0x80;
                                temp<<=1;
                                if (tempc) temp|=1;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x08: temp=bc.b.h&1; bc.b.h>>=1; if (temp) bc.b.h|=0x80; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC B*/
                                case 0x09: temp=bc.b.l&1; bc.b.l>>=1; if (temp) bc.b.l|=0x80; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC C*/
                                case 0x0A: temp=de.b.h&1; de.b.h>>=1; if (temp) de.b.h|=0x80; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC D*/
                                case 0x0B: temp=de.b.l&1; de.b.l>>=1; if (temp) de.b.l|=0x80; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC E*/
                                case 0x0C: temp=hl.b.h&1; hl.b.h>>=1; if (temp) hl.b.h|=0x80; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC H*/
                                case 0x0D: temp=hl.b.l&1; hl.b.l>>=1; if (temp) hl.b.l|=0x80; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC L*/
                                case 0x0F: temp=af.b.h&1; af.b.h>>=1; if (temp) af.b.h|=0x80; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RRC A*/
                                case 0x0E: /*RRC (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&1;
                                temp>>=1;
                                if (tempc) temp|=0x80;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x10: temp=bc.b.h&0x80; bc.b.h<<=1; if (tempc) bc.b.h|=1; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL B*/
                                case 0x11: temp=bc.b.l&0x80; bc.b.l<<=1; if (tempc) bc.b.l|=1; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL C*/
                                case 0x12: temp=de.b.h&0x80; de.b.h<<=1; if (tempc) de.b.h|=1; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL D*/
                                case 0x13: temp=de.b.l&0x80; de.b.l<<=1; if (tempc) de.b.l|=1; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL E*/
                                case 0x14: temp=hl.b.h&0x80; hl.b.h<<=1; if (tempc) hl.b.h|=1; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL H*/
                                case 0x15: temp=hl.b.l&0x80; hl.b.l<<=1; if (tempc) hl.b.l|=1; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL L*/
                                case 0x17: temp=af.b.h&0x80; af.b.h<<=1; if (tempc) af.b.h|=1; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RL A*/
                                case 0x16:  /*RL (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                addr=temp&0x80;
                                temp<<=1;
                                if (tempc) temp|=1;
                                setzn(temp);
                                if (addr) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x18: temp=bc.b.h&1; bc.b.h>>=1; if (tempc) bc.b.h|=0x80; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR B*/
                                case 0x19: temp=bc.b.l&1; bc.b.l>>=1; if (tempc) bc.b.l|=0x80; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR C*/
                                case 0x1A: temp=de.b.h&1; de.b.h>>=1; if (tempc) de.b.h|=0x80; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR D*/
                                case 0x1B: temp=de.b.l&1; de.b.l>>=1; if (tempc) de.b.l|=0x80; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR E*/
                                case 0x1C: temp=hl.b.h&1; hl.b.h>>=1; if (tempc) hl.b.h|=0x80; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR H*/
                                case 0x1D: temp=hl.b.l&1; hl.b.l>>=1; if (tempc) hl.b.l|=0x80; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR L*/
                                case 0x1F: temp=af.b.h&1; af.b.h>>=1; if (tempc) af.b.h|=0x80; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*RR A*/
                                case 0x1E:  /*RR (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                addr=temp&1;
                                temp>>=1;
                                if (tempc) temp|=0x80;
                                setzn(temp);
                                if (addr) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x20: temp=bc.b.h&0x80; bc.b.h<<=1; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA B*/
                                case 0x21: temp=bc.b.l&0x80; bc.b.l<<=1; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA C*/
                                case 0x22: temp=de.b.h&0x80; de.b.h<<=1; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA D*/
                                case 0x23: temp=de.b.l&0x80; de.b.l<<=1; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA E*/
                                case 0x24: temp=hl.b.h&0x80; hl.b.h<<=1; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA H*/
                                case 0x25: temp=hl.b.l&0x80; hl.b.l<<=1; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA L*/
                                case 0x27: temp=af.b.h&0x80; af.b.h<<=1; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLA H*/
                                case 0x26:  /*SLA (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x28: temp=bc.b.h&1; bc.b.h>>=1; if (bc.b.h&0x40) bc.b.h|=0x80; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA B*/
                                case 0x29: temp=bc.b.l&1; bc.b.l>>=1; if (bc.b.l&0x40) bc.b.l|=0x80; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA C*/
                                case 0x2A: temp=de.b.h&1; de.b.h>>=1; if (de.b.h&0x40) de.b.h|=0x80; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA D*/
                                case 0x2B: temp=de.b.l&1; de.b.l>>=1; if (de.b.l&0x40) de.b.l|=0x80; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA E*/
                                case 0x2C: temp=hl.b.h&1; hl.b.h>>=1; if (hl.b.h&0x40) hl.b.h|=0x80; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA H*/
                                case 0x2D: temp=hl.b.l&1; hl.b.l>>=1; if (hl.b.l&0x40) hl.b.l|=0x80; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA L*/
                                case 0x2F: temp=af.b.h&1; af.b.h>>=1; if (af.b.h&0x40) af.b.h|=0x80; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRA A*/
                                case 0x2E:  /*SRA (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&1;
                                temp>>=1;
                                if (temp&0x40) temp|=0x80;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x30: temp=bc.b.h&0x80; bc.b.h<<=1; bc.b.h|=1; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL B*/
                                case 0x31: temp=bc.b.l&0x80; bc.b.l<<=1; bc.b.l|=1; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL C*/
                                case 0x32: temp=de.b.h&0x80; de.b.h<<=1; de.b.h|=1; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL D*/
                                case 0x33: temp=de.b.l&0x80; de.b.l<<=1; de.b.l|=1; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL E*/
                                case 0x34: temp=hl.b.h&0x80; hl.b.h<<=1; hl.b.h|=1; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL H*/
                                case 0x35: temp=hl.b.l&0x80; hl.b.l<<=1; hl.b.l|=1; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL L*/
                                case 0x37: temp=af.b.h&0x80; af.b.h<<=1; af.b.h|=1; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SLL H*/
                                case 0x36:  /*SLL (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&0x80;
                                temp<<=1;
                                temp|=1;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x38: temp=bc.b.h&1; bc.b.h>>=1; setzn(bc.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL B*/
                                case 0x39: temp=bc.b.l&1; bc.b.l>>=1; setzn(bc.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL C*/
                                case 0x3A: temp=de.b.h&1; de.b.h>>=1; setzn(de.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL D*/
                                case 0x3B: temp=de.b.l&1; de.b.l>>=1; setzn(de.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL E*/
                                case 0x3C: temp=hl.b.h&1; hl.b.h>>=1; setzn(hl.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL H*/
                                case 0x3D: temp=hl.b.l&1; hl.b.l>>=1; setzn(hl.b.l); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL L*/
                                case 0x3F: temp=af.b.h&1; af.b.h>>=1; setzn(af.b.h); if (temp) af.b.l|=C_FLAG; cycles+=4; break; /*SRL H*/
                                case 0x3E:  /*SRL (HL)*/
                                cycles+=4; temp=z80_readmem(hl.w);
                                tempc=temp&1;
                                temp>>=1;
                                setzn(temp);
                                if (tempc) af.b.l|=C_FLAG;
                                cycles+=4; z80_writemem(hl.w,temp);
                                cycles+=3;
                                break;

                                case 0x40: setbit(bc.b.h&0x01); cycles+=4; break; /*BIT 0,B*/
                                case 0x41: setbit(bc.b.l&0x01); cycles+=4; break; /*BIT 0,C*/
                                case 0x42: setbit(de.b.h&0x01); cycles+=4; break; /*BIT 0,D*/
                                case 0x43: setbit(de.b.l&0x01); cycles+=4; break; /*BIT 0,E*/
                                case 0x44: setbit(hl.b.h&0x01); cycles+=4; break; /*BIT 0,H*/
                                case 0x45: setbit(hl.b.l&0x01); cycles+=4; break; /*BIT 0,L*/
                                case 0x47: setbit(af.b.h&0x01); cycles+=4; break; /*BIT 0,A*/
                                case 0x48: setbit(bc.b.h&0x02); cycles+=4; break; /*BIT 1,B*/
                                case 0x49: setbit(bc.b.l&0x02); cycles+=4; break; /*BIT 1,C*/
                                case 0x4A: setbit(de.b.h&0x02); cycles+=4; break; /*BIT 1,D*/
                                case 0x4B: setbit(de.b.l&0x02); cycles+=4; break; /*BIT 1,E*/
                                case 0x4C: setbit(hl.b.h&0x02); cycles+=4; break; /*BIT 1,H*/
                                case 0x4D: setbit(hl.b.l&0x02); cycles+=4; break; /*BIT 1,L*/
                                case 0x4F: setbit(af.b.h&0x02); cycles+=4; break; /*BIT 1,A*/
                                case 0x50: setbit(bc.b.h&0x04); cycles+=4; break; /*BIT 2,B*/
                                case 0x51: setbit(bc.b.l&0x04); cycles+=4; break; /*BIT 2,C*/
                                case 0x52: setbit(de.b.h&0x04); cycles+=4; break; /*BIT 2,D*/
                                case 0x53: setbit(de.b.l&0x04); cycles+=4; break; /*BIT 2,E*/
                                case 0x54: setbit(hl.b.h&0x04); cycles+=4; break; /*BIT 2,H*/
                                case 0x55: setbit(hl.b.l&0x04); cycles+=4; break; /*BIT 2,L*/
                                case 0x57: setbit(af.b.h&0x04); cycles+=4; break; /*BIT 2,A*/
                                case 0x58: setbit(bc.b.h&0x08); cycles+=4; break; /*BIT 3,B*/
                                case 0x59: setbit(bc.b.l&0x08); cycles+=4; break; /*BIT 3,C*/
                                case 0x5A: setbit(de.b.h&0x08); cycles+=4; break; /*BIT 3,D*/
                                case 0x5B: setbit(de.b.l&0x08); cycles+=4; break; /*BIT 3,E*/
                                case 0x5C: setbit(hl.b.h&0x08); cycles+=4; break; /*BIT 3,H*/
                                case 0x5D: setbit(hl.b.l&0x08); cycles+=4; break; /*BIT 3,L*/
                                case 0x5F: setbit(af.b.h&0x08); cycles+=4; break; /*BIT 3,A*/
                                case 0x60: setbit(bc.b.h&0x10); cycles+=4; break; /*BIT 4,B*/
                                case 0x61: setbit(bc.b.l&0x10); cycles+=4; break; /*BIT 4,C*/
                                case 0x62: setbit(de.b.h&0x10); cycles+=4; break; /*BIT 4,D*/
                                case 0x63: setbit(de.b.l&0x10); cycles+=4; break; /*BIT 4,E*/
                                case 0x64: setbit(hl.b.h&0x10); cycles+=4; break; /*BIT 4,H*/
                                case 0x65: setbit(hl.b.l&0x10); cycles+=4; break; /*BIT 4,L*/
                                case 0x67: setbit(af.b.h&0x10); cycles+=4; break; /*BIT 4,A*/
                                case 0x68: setbit(bc.b.h&0x20); cycles+=4; break; /*BIT 5,B*/
                                case 0x69: setbit(bc.b.l&0x20); cycles+=4; break; /*BIT 5,C*/
                                case 0x6A: setbit(de.b.h&0x20); cycles+=4; break; /*BIT 5,D*/
                                case 0x6B: setbit(de.b.l&0x20); cycles+=4; break; /*BIT 5,E*/
                                case 0x6C: setbit(hl.b.h&0x20); cycles+=4; break; /*BIT 5,H*/
                                case 0x6D: setbit(hl.b.l&0x20); cycles+=4; break; /*BIT 5,L*/
                                case 0x6F: setbit(af.b.h&0x20); cycles+=4; break; /*BIT 5,A*/
                                case 0x70: setbit(bc.b.h&0x40); cycles+=4; break; /*BIT 6,B*/
                                case 0x71: setbit(bc.b.l&0x40); cycles+=4; break; /*BIT 6,C*/
                                case 0x72: setbit(de.b.h&0x40); cycles+=4; break; /*BIT 6,D*/
                                case 0x73: setbit(de.b.l&0x40); cycles+=4; break; /*BIT 6,E*/
                                case 0x74: setbit(hl.b.h&0x40); cycles+=4; break; /*BIT 6,H*/
                                case 0x75: setbit(hl.b.l&0x40); cycles+=4; break; /*BIT 6,L*/
                                case 0x77: setbit(af.b.h&0x40); cycles+=4; break; /*BIT 6,A*/
                                case 0x78: setbit(bc.b.h&0x80); cycles+=4; break; /*BIT 7,B*/
                                case 0x79: setbit(bc.b.l&0x80); cycles+=4; break; /*BIT 7,C*/
                                case 0x7A: setbit(de.b.h&0x80); cycles+=4; break; /*BIT 7,D*/
                                case 0x7B: setbit(de.b.l&0x80); cycles+=4; break; /*BIT 7,E*/
                                case 0x7C: setbit(hl.b.h&0x80); cycles+=4; break; /*BIT 7,H*/
                                case 0x7D: setbit(hl.b.l&0x80); cycles+=4; break; /*BIT 7,L*/
                                case 0x7F: setbit(af.b.h&0x80); cycles+=4; break; /*BIT 7,A*/

                                case 0x46: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x01,intreg); cycles+=4; break; /*BIT 0,(HL)*/
                                case 0x4E: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x02,intreg); cycles+=4; break; /*BIT 1,(HL)*/
                                case 0x56: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x04,intreg); cycles+=4; break; /*BIT 2,(HL)*/
                                case 0x5E: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x08,intreg); cycles+=4; break; /*BIT 3,(HL)*/
                                case 0x66: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x10,intreg); cycles+=4; break; /*BIT 4,(HL)*/
                                case 0x6E: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x20,intreg); cycles+=4; break; /*BIT 5,(HL)*/
                                case 0x76: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x40,intreg); cycles+=4; break; /*BIT 6,(HL)*/
                                case 0x7E: cycles+=4; temp=z80_readmem(hl.w); setbit2(temp&0x80,intreg); cycles+=4; break; /*BIT 7,(HL)*/

                                case 0x80: bc.b.h&=~0x01; cycles+=4; break; /*RES 0,B*/
                                case 0x81: bc.b.l&=~0x01; cycles+=4; break; /*RES 0,C*/
                                case 0x82: de.b.h&=~0x01; cycles+=4; break; /*RES 0,D*/
                                case 0x83: de.b.l&=~0x01; cycles+=4; break; /*RES 0,E*/
                                case 0x84: hl.b.h&=~0x01; cycles+=4; break; /*RES 0,H*/
                                case 0x85: hl.b.l&=~0x01; cycles+=4; break; /*RES 0,L*/
                                case 0x87: af.b.h&=~0x01; cycles+=4; break; /*RES 0,A*/
                                case 0x88: bc.b.h&=~0x02; cycles+=4; break; /*RES 1,B*/
                                case 0x89: bc.b.l&=~0x02; cycles+=4; break; /*RES 1,C*/
                                case 0x8A: de.b.h&=~0x02; cycles+=4; break; /*RES 1,D*/
                                case 0x8B: de.b.l&=~0x02; cycles+=4; break; /*RES 1,E*/
                                case 0x8C: hl.b.h&=~0x02; cycles+=4; break; /*RES 1,H*/
                                case 0x8D: hl.b.l&=~0x02; cycles+=4; break; /*RES 1,L*/
                                case 0x8F: af.b.h&=~0x02; cycles+=4; break; /*RES 1,A*/
                                case 0x90: bc.b.h&=~0x04; cycles+=4; break; /*RES 2,B*/
                                case 0x91: bc.b.l&=~0x04; cycles+=4; break; /*RES 2,C*/
                                case 0x92: de.b.h&=~0x04; cycles+=4; break; /*RES 2,D*/
                                case 0x93: de.b.l&=~0x04; cycles+=4; break; /*RES 2,E*/
                                case 0x94: hl.b.h&=~0x04; cycles+=4; break; /*RES 2,H*/
                                case 0x95: hl.b.l&=~0x04; cycles+=4; break; /*RES 2,L*/
                                case 0x97: af.b.h&=~0x04; cycles+=4; break; /*RES 2,A*/
                                case 0x98: bc.b.h&=~0x08; cycles+=4; break; /*RES 3,B*/
                                case 0x99: bc.b.l&=~0x08; cycles+=4; break; /*RES 3,C*/
                                case 0x9A: de.b.h&=~0x08; cycles+=4; break; /*RES 3,D*/
                                case 0x9B: de.b.l&=~0x08; cycles+=4; break; /*RES 3,E*/
                                case 0x9C: hl.b.h&=~0x08; cycles+=4; break; /*RES 3,H*/
                                case 0x9D: hl.b.l&=~0x08; cycles+=4; break; /*RES 3,L*/
                                case 0x9F: af.b.h&=~0x08; cycles+=4; break; /*RES 3,A*/
                                case 0xA0: bc.b.h&=~0x10; cycles+=4; break; /*RES 4,B*/
                                case 0xA1: bc.b.l&=~0x10; cycles+=4; break; /*RES 4,C*/
                                case 0xA2: de.b.h&=~0x10; cycles+=4; break; /*RES 4,D*/
                                case 0xA3: de.b.l&=~0x10; cycles+=4; break; /*RES 4,E*/
                                case 0xA4: hl.b.h&=~0x10; cycles+=4; break; /*RES 4,H*/
                                case 0xA5: hl.b.l&=~0x10; cycles+=4; break; /*RES 4,L*/
                                case 0xA7: af.b.h&=~0x10; cycles+=4; break; /*RES 4,A*/
                                case 0xA8: bc.b.h&=~0x20; cycles+=4; break; /*RES 5,B*/
                                case 0xA9: bc.b.l&=~0x20; cycles+=4; break; /*RES 5,C*/
                                case 0xAA: de.b.h&=~0x20; cycles+=4; break; /*RES 5,D*/
                                case 0xAB: de.b.l&=~0x20; cycles+=4; break; /*RES 5,E*/
                                case 0xAC: hl.b.h&=~0x20; cycles+=4; break; /*RES 5,H*/
                                case 0xAD: hl.b.l&=~0x20; cycles+=4; break; /*RES 5,L*/
                                case 0xAF: af.b.h&=~0x20; cycles+=4; break; /*RES 5,A*/
                                case 0xB0: bc.b.h&=~0x40; cycles+=4; break; /*RES 6,B*/
                                case 0xB1: bc.b.l&=~0x40; cycles+=4; break; /*RES 6,C*/
                                case 0xB2: de.b.h&=~0x40; cycles+=4; break; /*RES 6,D*/
                                case 0xB3: de.b.l&=~0x40; cycles+=4; break; /*RES 6,E*/
                                case 0xB4: hl.b.h&=~0x40; cycles+=4; break; /*RES 6,H*/
                                case 0xB5: hl.b.l&=~0x40; cycles+=4; break; /*RES 6,L*/
                                case 0xB7: af.b.h&=~0x40; cycles+=4; break; /*RES 6,A*/
                                case 0xB8: bc.b.h&=~0x80; cycles+=4; break; /*RES 7,B*/
                                case 0xB9: bc.b.l&=~0x80; cycles+=4; break; /*RES 7,C*/
                                case 0xBA: de.b.h&=~0x80; cycles+=4; break; /*RES 7,D*/
                                case 0xBB: de.b.l&=~0x80; cycles+=4; break; /*RES 7,E*/
                                case 0xBC: hl.b.h&=~0x80; cycles+=4; break; /*RES 7,H*/
                                case 0xBD: hl.b.l&=~0x80; cycles+=4; break; /*RES 7,L*/
                                case 0xBF: af.b.h&=~0x80; cycles+=4; break; /*RES 7,A*/

                                case 0x86: cycles+=4; temp=z80_readmem(hl.w)&~0x01; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 0,(HL)*/
                                case 0x8E: cycles+=4; temp=z80_readmem(hl.w)&~0x02; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 1,(HL)*/
                                case 0x96: cycles+=4; temp=z80_readmem(hl.w)&~0x04; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 2,(HL)*/
                                case 0x9E: cycles+=4; temp=z80_readmem(hl.w)&~0x08; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 3,(HL)*/
                                case 0xA6: cycles+=4; temp=z80_readmem(hl.w)&~0x10; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 4,(HL)*/
                                case 0xAE: cycles+=4; temp=z80_readmem(hl.w)&~0x20; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 5,(HL)*/
                                case 0xB6: cycles+=4; temp=z80_readmem(hl.w)&~0x40; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 6,(HL)*/
                                case 0xBE: cycles+=4; temp=z80_readmem(hl.w)&~0x80; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*RES 7,(HL)*/

                                case 0xC0: bc.b.h|=0x01; cycles+=4; break; /*SET 0,B*/
                                case 0xC1: bc.b.l|=0x01; cycles+=4; break; /*SET 0,C*/
                                case 0xC2: de.b.h|=0x01; cycles+=4; break; /*SET 0,D*/
                                case 0xC3: de.b.l|=0x01; cycles+=4; break; /*SET 0,E*/
                                case 0xC4: hl.b.h|=0x01; cycles+=4; break; /*SET 0,H*/
                                case 0xC5: hl.b.l|=0x01; cycles+=4; break; /*SET 0,L*/
                                case 0xC7: af.b.h|=0x01; cycles+=4; break; /*SET 0,A*/
                                case 0xC8: bc.b.h|=0x02; cycles+=4; break; /*SET 1,B*/
                                case 0xC9: bc.b.l|=0x02; cycles+=4; break; /*SET 1,C*/
                                case 0xCA: de.b.h|=0x02; cycles+=4; break; /*SET 1,D*/
                                case 0xCB: de.b.l|=0x02; cycles+=4; break; /*SET 1,E*/
                                case 0xCC: hl.b.h|=0x02; cycles+=4; break; /*SET 1,H*/
                                case 0xCD: hl.b.l|=0x02; cycles+=4; break; /*SET 1,L*/
                                case 0xCF: af.b.h|=0x02; cycles+=4; break; /*SET 1,A*/
                                case 0xD0: bc.b.h|=0x04; cycles+=4; break; /*SET 2,B*/
                                case 0xD1: bc.b.l|=0x04; cycles+=4; break; /*SET 2,C*/
                                case 0xD2: de.b.h|=0x04; cycles+=4; break; /*SET 2,D*/
                                case 0xD3: de.b.l|=0x04; cycles+=4; break; /*SET 2,E*/
                                case 0xD4: hl.b.h|=0x04; cycles+=4; break; /*SET 2,H*/
                                case 0xD5: hl.b.l|=0x04; cycles+=4; break; /*SET 2,L*/
                                case 0xD7: af.b.h|=0x04; cycles+=4; break; /*SET 2,A*/
                                case 0xD8: bc.b.h|=0x08; cycles+=4; break; /*SET 3,B*/
                                case 0xD9: bc.b.l|=0x08; cycles+=4; break; /*SET 3,C*/
                                case 0xDA: de.b.h|=0x08; cycles+=4; break; /*SET 3,D*/
                                case 0xDB: de.b.l|=0x08; cycles+=4; break; /*SET 3,E*/
                                case 0xDC: hl.b.h|=0x08; cycles+=4; break; /*SET 3,H*/
                                case 0xDD: hl.b.l|=0x08; cycles+=4; break; /*SET 3,L*/
                                case 0xDF: af.b.h|=0x08; cycles+=4; break; /*SET 3,A*/
                                case 0xE0: bc.b.h|=0x10; cycles+=4; break; /*SET 4,B*/
                                case 0xE1: bc.b.l|=0x10; cycles+=4; break; /*SET 4,C*/
                                case 0xE2: de.b.h|=0x10; cycles+=4; break; /*SET 4,D*/
                                case 0xE3: de.b.l|=0x10; cycles+=4; break; /*SET 4,E*/
                                case 0xE4: hl.b.h|=0x10; cycles+=4; break; /*SET 4,H*/
                                case 0xE5: hl.b.l|=0x10; cycles+=4; break; /*SET 4,L*/
                                case 0xE7: af.b.h|=0x10; cycles+=4; break; /*SET 4,A*/
                                case 0xE8: bc.b.h|=0x20; cycles+=4; break; /*SET 5,B*/
                                case 0xE9: bc.b.l|=0x20; cycles+=4; break; /*SET 5,C*/
                                case 0xEA: de.b.h|=0x20; cycles+=4; break; /*SET 5,D*/
                                case 0xEB: de.b.l|=0x20; cycles+=4; break; /*SET 5,E*/
                                case 0xEC: hl.b.h|=0x20; cycles+=4; break; /*SET 5,H*/
                                case 0xED: hl.b.l|=0x20; cycles+=4; break; /*SET 5,L*/
                                case 0xEF: af.b.h|=0x20; cycles+=4; break; /*SET 5,A*/
                                case 0xF0: bc.b.h|=0x40; cycles+=4; break; /*SET 6,B*/
                                case 0xF1: bc.b.l|=0x40; cycles+=4; break; /*SET 6,C*/
                                case 0xF2: de.b.h|=0x40; cycles+=4; break; /*SET 6,D*/
                                case 0xF3: de.b.l|=0x40; cycles+=4; break; /*SET 6,E*/
                                case 0xF4: hl.b.h|=0x40; cycles+=4; break; /*SET 6,H*/
                                case 0xF5: hl.b.l|=0x40; cycles+=4; break; /*SET 6,L*/
                                case 0xF7: af.b.h|=0x40; cycles+=4; break; /*SET 6,A*/
                                case 0xF8: bc.b.h|=0x80; cycles+=4; break; /*SET 7,B*/
                                case 0xF9: bc.b.l|=0x80; cycles+=4; break; /*SET 7,C*/
                                case 0xFA: de.b.h|=0x80; cycles+=4; break; /*SET 7,D*/
                                case 0xFB: de.b.l|=0x80; cycles+=4; break; /*SET 7,E*/
                                case 0xFC: hl.b.h|=0x80; cycles+=4; break; /*SET 7,H*/
                                case 0xFD: hl.b.l|=0x80; cycles+=4; break; /*SET 7,L*/
                                case 0xFF: af.b.h|=0x80; cycles+=4; break; /*SET 7,A*/

                                case 0xC6: cycles+=4; temp=z80_readmem(hl.w)|0x01; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 0,(HL)*/
                                case 0xCE: cycles+=4; temp=z80_readmem(hl.w)|0x02; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 1,(HL)*/
                                case 0xD6: cycles+=4; temp=z80_readmem(hl.w)|0x04; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 2,(HL)*/
                                case 0xDE: cycles+=4; temp=z80_readmem(hl.w)|0x08; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 3,(HL)*/
                                case 0xE6: cycles+=4; temp=z80_readmem(hl.w)|0x10; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 4,(HL)*/
                                case 0xEE: cycles+=4; temp=z80_readmem(hl.w)|0x20; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 5,(HL)*/
                                case 0xF6: cycles+=4; temp=z80_readmem(hl.w)|0x40; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 6,(HL)*/
                                case 0xFE: cycles+=4; temp=z80_readmem(hl.w)|0x80; cycles+=4; z80_writemem(hl.w,temp); cycles+=3; break; /*SET 7,(HL)*/

                                default:
                                        break;
//                                printf("Bad CB opcode %02X at %04X\n",opcode,pc);
//                                z80_dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xCC: /*CALL Z,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&Z_FLAG)
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0xCD: /*CALL xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        cycles+=4; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=addr;
                        cycles+=3;
                        break;
                        case 0xCE: /*ADC A,nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        setadc(af.b.h,temp);
                        af.b.h+=temp+tempc;
                        cycles+=3;
                        break;
                        case 0xCF: /*RST 8*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x08;
                        cycles+=3;
                        break;

                        case 0xD0: /*RET NC*/
                        cycles+=5;
                        if (!(af.b.l&C_FLAG))
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xD1: /*POP DE*/
                        cycles+=4; de.b.l=z80_readmem(sp); sp++;
                        cycles+=3; de.b.h=z80_readmem(sp); sp++;
                        cycles+=3;
                        break;
                        case 0xD2: /*JP NC*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&C_FLAG))
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xD3: /*OUT (nn),A*/
                        cycles+=4; addr=z80_readmem(pc++);
                        cycles+=3; z80out(addr,af.b.h);
                        cycles+=4;
                        break;
                        case 0xD4: /*CALL NC,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&C_FLAG))
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0xD5: /*PUSH DE*/
                        cycles+=5; sp--; z80_writemem(sp,de.b.h);
                        cycles+=3; sp--; z80_writemem(sp,de.b.l);
                        cycles+=3;
                        break;
                        case 0xD6: /*SUB A,nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        z80_setsub(af.b.h,temp);
                        af.b.h-=temp;
                        cycles+=3;
                        break;
                        case 0xD7: /*RST 10*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x10;
                        cycles+=3;
                        break;
                        case 0xD8: /*RET C*/
                        cycles+=5;
                        if (af.b.l&C_FLAG)
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xD9: /*EXX*/
                        addr=bc.w; bc.w=sbc.w; sbc.w=addr;
                        addr=de.w; de.w=sde.w; sde.w=addr;
                        addr=hl.w; hl.w=shl.w; shl.w=addr;
                        cycles+=4;
                        break;
                        case 0xDA: /*JP C*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&C_FLAG)
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xDB: /*IN A,(n)*/
                        cycles+=4; temp=z80_readmem(pc++);
                        cycles+=3; af.b.h=z80in((af.b.h<<8)|temp);
                        cycles+=4;
                        break;
                        case 0xDC: /*CALL C,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&C_FLAG)
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;

                        case 0xDD: /*More opcodes*/
                        ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                        cycles+=4;
                        opcode=z80_readmem(pc++);
                        switch (opcode)
                        {
                                case 0xCD:
                                pc--;
//                                cycles+=4;
                                break;
                                case 0x09: /*ADD IX,BC*/
                                z80_setadd16(ix.w,bc.w);
                                ix.w+=bc.w;
                                cycles+=11;
                                break;
                                case 0x19: /*ADD IX,DE*/
                                z80_setadd16(ix.w,de.w);
                                ix.w+=de.w;
                                cycles+=11;
                                break;
                                case 0x21: /*LD IX,nn*/
                                cycles+=4; ix.b.l=z80_readmem(pc++);
                                cycles+=3; ix.b.h=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x22: /*LD (nn),IX*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; z80_writemem(addr,ix.b.l);
                                cycles+=3; z80_writemem(addr+1,ix.b.h);
                                cycles+=3;
                                break;
                                case 0x23: /*INC IX*/
                                ix.w++;
                                cycles+=6;
                                break;
                                case 0x24: /*INC IXh*/
                                setinc(ix.b.h);
                                ix.b.h++;
                                cycles+=4;
                                break;
                                case 0x25: /*DEC IXh*/
                                setdec(ix.b.h);
                                ix.b.h--;
                                cycles+=4;
                                break;
                                case 0x26: /*LD IXh,nn*/
                                cycles+=4; ix.b.h=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x29: /*ADD IX,IX*/
                                z80_setadd16(ix.w,ix.w);
                                ix.w+=ix.w;
                                cycles+=11;
                                break;
                                case 0x2A: /*LD IX,(nn)*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; ix.b.l=z80_readmem(addr);
                                cycles+=3; ix.b.h=z80_readmem(addr+1);
                                cycles+=3;
                                break;
                                case 0x2B: /*DEC IX*/
                                ix.w--;
                                cycles+=6;
                                break;
                                case 0x2C: /*INC IXl*/
                                setinc(ix.b.l);
                                ix.b.l++;
                                cycles+=4;
                                break;
                                case 0x2D: /*DEC IXl*/
                                setdec(ix.b.l);
                                ix.b.l--;
                                cycles+=4;
                                break;
                                case 0x2E: /*LD IXl,nn*/
                                cycles+=4; ix.b.l=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x34: /*INC (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                addr+=ix.w;
                                cycles+=3; temp=z80_readmem(addr);
                                setinc(temp);
                                cycles+=5; z80_writemem(addr,temp+1);
                                cycles+=7;
                                break;
                                case 0x35: /*DEC (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                addr+=ix.w;
                                cycles+=3; temp=z80_readmem(addr);
                                setdec(temp);
                                cycles+=5; z80_writemem(addr,temp-1);
                                cycles+=7;
                                break;
                                case 0x36: /*LD (IX+nn),nn*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(pc++);
                                cycles+=5; z80_writemem(ix.w+addr,temp);
                                cycles+=3;
                                break;
                                case 0x39: /*ADD IX,SP*/
                                z80_setadd16(ix.w,sp);
                                ix.w+=sp;
                                cycles+=11;
                                break;

                                case 0x46: /*LD B,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; bc.b.h=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x4E: /*LD C,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; bc.b.l=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x56: /*LD D,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; de.b.h=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x44: bc.b.h=ix.b.h; cycles+=3; break; /*LD B,IXh*/
                                case 0x45: bc.b.h=ix.b.l; cycles+=3; break; /*LD B,IXl*/
                                case 0x54: de.b.h=ix.b.h; cycles+=3; break; /*LD D,IXh*/
                                case 0x55: de.b.h=ix.b.l; cycles+=3; break; /*LD D,IXl*/
                                case 0x5C: de.b.l=ix.b.h; cycles+=3; break; /*LD E,IXh*/
                                case 0x5D: de.b.l=ix.b.l; cycles+=3; break; /*LD E,IXl*/
                                case 0x5E: /*LD E,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; de.b.l=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x66: /*LD H,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; hl.b.h=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x6E: /*LD L,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(ix.w+addr)>>8;
                                cycles+=7; hl.b.l=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x60: ix.b.h=bc.b.h; cycles+=3; break;  /*LD IXh,B*/
                                case 0x61: ix.b.h=bc.b.l; cycles+=3; break;  /*LD IXh,C*/
                                case 0x62: ix.b.h=de.b.h; cycles+=3; break;  /*LD IXh,D*/
                                case 0x63: ix.b.h=de.b.l; cycles+=3; break;  /*LD IXh,E*/
                                case 0x64: ix.b.h=hl.b.h; cycles+=3; break;  /*LD IXh,H*/
                                case 0x65: ix.b.h=hl.b.l; cycles+=3; break;  /*LD IXh,L*/
                                case 0x67: ix.b.h=af.b.h; cycles+=3; break;  /*LD IXh,A*/
                                case 0x68: ix.b.l=bc.b.h; cycles+=3; break;  /*LD IXl,B*/
                                case 0x69: ix.b.l=bc.b.l; cycles+=3; break;  /*LD IXl,C*/
                                case 0x6A: ix.b.l=de.b.h; cycles+=3; break;  /*LD IXl,D*/
                                case 0x6B: ix.b.l=de.b.l; cycles+=3; break;  /*LD IXl,E*/
                                case 0x6C: ix.b.l=hl.b.h; cycles+=3; break;  /*LD IXl,H*/
                                case 0x6D: ix.b.l=hl.b.l; cycles+=3; break;  /*LD IXl,L*/
                                case 0x6F: ix.b.l=af.b.h; cycles+=3; break;  /*LD IXl,A*/
                                case 0x70: /*LD (IX+nn),B*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,bc.b.h);
                                cycles+=8;
                                break;
                                case 0x71: /*LD (IX+nn),C*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,bc.b.l);
                                cycles+=8;
                                break;
                                case 0x72: /*LD (IX+nn),D*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,de.b.h);
                                cycles+=8;
                                break;
                                case 0x73: /*LD (IX+nn),E*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,de.b.l);
                                cycles+=8;
                                break;
                                case 0x74: /*LD (IX+nn),H*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,hl.b.h);
                                cycles+=8;
                                break;
                                case 0x75: /*LD (IX+nn),L*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,hl.b.l);
                                cycles+=8;
                                break;
                                case 0x77: /*LD (IX+nn),A*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(ix.w+addr,af.b.h);
                                cycles+=8;
                                break;
                                case 0x7E: /*LD A,(IX+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; af.b.h=z80_readmem(ix.w+addr);
                                cycles+=8;
                                break;
                                case 0x7C: af.b.h=ix.b.h; cycles+=3; break; /*LD A,IXh*/
                                case 0x7D: af.b.h=ix.b.l; cycles+=3; break; /*LD A,IXl*/

                                case 0x84: z80_setadd(af.b.h,ix.b.h); af.b.h+=ix.b.h; cycles+=3; break;         /*ADD IXh*/
                                case 0x85: z80_setadd(af.b.h,ix.b.l); af.b.h+=ix.b.l; cycles+=3; break;         /*ADD IXl*/
                                case 0x8C: setadc(af.b.h,ix.b.h); af.b.h+=ix.b.h+tempc; cycles+=3; break;   /*ADC IXh*/
                                case 0x8D: setadc(af.b.h,ix.b.l); af.b.h+=ix.b.l+tempc; cycles+=3; break;   /*ADC IXl*/
                                case 0x94: z80_setsub(af.b.h,ix.b.h); af.b.h-=ix.b.h; cycles+=3; break;         /*SUB IXh*/
                                case 0x95: z80_setsub(af.b.h,ix.b.l); af.b.h-=ix.b.l; cycles+=3; break;         /*SUB IXl*/
                                case 0x9C: setsbc(af.b.h,ix.b.h); af.b.h-=(ix.b.h+tempc); cycles+=3; break; /*SBC IXh*/
                                case 0x9D: setsbc(af.b.h,ix.b.l); af.b.h-=(ix.b.l+tempc); cycles+=3; break; /*SBC IXl*/
                                case 0xA4: setand(af.b.h&ix.b.h); af.b.h&=ix.b.h; cycles+=3; break;         /*AND IXh*/
                                case 0xA5: setand(af.b.h&ix.b.l); af.b.h&=ix.b.l; cycles+=3; break;         /*AND IXl*/
                                case 0xAC: setzn(af.b.h^ix.b.h);  af.b.h^=ix.b.h; cycles+=3; break;         /*XOR IXh*/
                                case 0xAD: setzn(af.b.h^ix.b.l);  af.b.h^=ix.b.l; cycles+=3; break;         /*XOR IXl*/
                                case 0xB4: setzn(af.b.h|ix.b.h);  af.b.h|=ix.b.h; cycles+=3; break;         /*OR  IXh*/
                                case 0xB5: setzn(af.b.h|ix.b.l);  af.b.h|=ix.b.l; cycles+=3; break;         /*OR  IXl*/
                                case 0xBC: setcp(af.b.h,ix.b.h); cycles+=3; break;                          /*CP  IXh*/
                                case 0xBD: setcp(af.b.h,ix.b.l); cycles+=3; break;                          /*CP  IXl*/

                                case 0x86: /*ADD (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                z80_setadd(af.b.h,temp);
                                af.b.h+=temp;
                                cycles+=8;
                                break;
                                case 0x8E: /*ADC (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setadc(af.b.h,temp);
                                af.b.h+=(temp+tempc);
                                cycles+=8;
                                break;
                                case 0x96: /*SUB (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                z80_setsub(af.b.h,temp);
                                af.b.h-=temp;
                                cycles+=8;
                                break;
                                case 0x9E: /*SBC (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setsbc(af.b.h,temp);
                                af.b.h-=(temp+tempc);
                                cycles+=8;
                                break;
                                case 0xA6: /*AND (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setand(af.b.h);
                                cycles+=8;
                                break;
                                case 0xAE: /*XOR (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setzn(af.b.h);
                                cycles+=8;
                                break;
                                case 0xB6: /*OR (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setzn(af.b.h);
                                cycles+=8;
                                break;
                                case 0xBE: /*CP (IX+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(ix.w+addr);
                                setcp(af.b.h,temp);
                                cycles+=8;
                                break;

                                case 0xCB: /*More opcodes*/
                                ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                                cycles+=4; addr=z80_readmem(pc++);
                                if (addr&0x80) addr|=0xFF00;
                                cycles+=3; opcode=z80_readmem(pc++);
                                switch (opcode)
                                {
                                        case 0x06: /*RLC (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        tempc=temp&0x80;
                                        temp<<=1;
                                        if (tempc) temp|=1;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x0E: /*RRC (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        tempc=temp&1;
                                        temp>>=1;
                                        if (tempc) temp|=0x80;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x16:  /*RL (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        addr=temp&0x80;
                                        temp<<=1;
                                        if (tempc) temp|=1;
                                        setzn(temp);
                                        if (addr) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x1E:  /*RR (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        addr=temp&1;
                                        temp>>=1;
                                        if (tempc) temp|=0x80;
                                        setzn(temp);
                                        if (addr) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x26:  /*SLA (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        tempc=temp&0x80;
                                        temp<<=1;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x2E:  /*SRA (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        tempc=temp&1;
                                        temp>>=1;
                                        if (temp&0x40) temp|=0x80;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x3E:  /*SRL (IX+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+ix.w);
                                        tempc=temp&1;
                                        temp>>=1;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+ix.w,temp);
                                        cycles+=3;
                                        break;


                                        case 0x46: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&1,ix.w+addr); cycles+=4; break; /*BIT 0,(IX+nn)*/
                                        case 0x4E: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&2,ix.w+addr); cycles+=4; break; /*BIT 1,(IX+nn)*/
                                        case 0x56: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&4,ix.w+addr); cycles+=4; break; /*BIT 2,(IX+nn)*/
                                        case 0x5E: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&8,ix.w+addr); cycles+=4; break; /*BIT 3,(IX+nn)*/
                                        case 0x66: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&0x10,ix.w+addr); cycles+=4; break; /*BIT 4,(IX+nn)*/
                                        case 0x6E: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&0x20,ix.w+addr); cycles+=4; break; /*BIT 5,(IX+nn)*/
                                        case 0x76: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&0x40,ix.w+addr); cycles+=4; break; /*BIT 6,(IX+nn)*/
                                        case 0x7E: cycles+=5; temp=z80_readmem(ix.w+addr); setbit2(temp&0x80,ix.w+addr); cycles+=4; break; /*BIT 7,(IX+nn)*/

                                        case 0x86: cycles+=5; temp=z80_readmem(ix.w+addr)&~1; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;    /*RES 0,(IX+nn)*/
                                        case 0x8E: cycles+=5; temp=z80_readmem(ix.w+addr)&~2; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;    /*RES 1,(IX+nn)*/
                                        case 0x96: cycles+=5; temp=z80_readmem(ix.w+addr)&~4; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;    /*RES 2,(IX+nn)*/
                                        case 0x9E: cycles+=5; temp=z80_readmem(ix.w+addr)&~8; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;    /*RES 3,(IX+nn)*/
                                        case 0xA6: cycles+=5; temp=z80_readmem(ix.w+addr)&~0x10; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break; /*RES 4,(IX+nn)*/
                                        case 0xAE: cycles+=5; temp=z80_readmem(ix.w+addr)&~0x20; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break; /*RES 5,(IX+nn)*/
                                        case 0xB6: cycles+=5; temp=z80_readmem(ix.w+addr)&~0x40; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break; /*RES 6,(IX+nn)*/
                                        case 0xBE: cycles+=5; temp=z80_readmem(ix.w+addr)&~0x80; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break; /*RES 7,(IX+nn)*/
                                        case 0xC6: cycles+=5; temp=z80_readmem(ix.w+addr)|1; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;     /*SET 0,(IX+nn)*/
                                        case 0xCE: cycles+=5; temp=z80_readmem(ix.w+addr)|2; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;     /*SET 1,(IX+nn)*/
                                        case 0xD6: cycles+=5; temp=z80_readmem(ix.w+addr)|4; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;     /*SET 2,(IX+nn)*/
                                        case 0xDE: cycles+=5; temp=z80_readmem(ix.w+addr)|8; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;     /*SET 3,(IX+nn)*/
                                        case 0xE6: cycles+=5; temp=z80_readmem(ix.w+addr)|0x10; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;  /*SET 4,(IX+nn)*/
                                        case 0xEE: cycles+=5; temp=z80_readmem(ix.w+addr)|0x20; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;  /*SET 5,(IX+nn)*/
                                        case 0xF6: cycles+=5; temp=z80_readmem(ix.w+addr)|0x40; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;  /*SET 6,(IX+nn)*/
                                        case 0xFE: cycles+=5; temp=z80_readmem(ix.w+addr)|0x80; cycles+=4; z80_writemem(ix.w+addr,temp); cycles+=3; break;  /*SET 7,(IX+nn)*/

                                        default:
                                        break;
//                                        printf("Bad DD CB opcode %02X at %04X\n",opcode,pc);
//                                        z80_dumpregs();
//                                        exit(-1);
                                }
                                break;

                                case 0xE1: /*POP IX*/
                                cycles+=4; ix.b.l=z80_readmem(sp); sp++;
                                cycles+=3; ix.b.h=z80_readmem(sp); sp++;
                                cycles+=3;
                                break;
                                case 0xE3: /*EX (SP),IX*/
                                cycles+=4; addr=z80_readmem(sp);
                                cycles+=3; addr|=(z80_readmem(sp+1)<<8);
                                cycles+=4; z80_writemem(sp,ix.b.l);
                                cycles+=3; z80_writemem(sp+1,ix.b.h);
                                ix.w=addr;
                                cycles+=5;
                                break;
                                case 0xE5: /*PUSH IX*/
                                cycles+=5; sp--; z80_writemem(sp,ix.b.h);
                                cycles+=3; sp--; z80_writemem(sp,ix.b.l);
                                cycles+=3;
                                break;
                                case 0xE9: /*JP (IX)*/
                                pc=ix.w;
                                cycles+=4;
                                break;

                                case 0xF9: /*LD SP,IX*/
                                sp=ix.w;
                                cycles+=6;
                                break;

                                default:
                                break;
//                                printf("Bad DD opcode %02X at %04X\n",opcode,pc);
//                                z80_dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xDE: /*SBC A,nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        setsbc(af.b.h,temp);
                        af.b.h-=(temp+tempc);
                        cycles+=3;
                        break;
                        case 0xDF: /*RST 18*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x18;
                        cycles+=3;
                        break;

                        case 0xE0: /*RET PO*/
                        cycles+=5;
                        if (!(af.b.l&V_FLAG))
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xE1: /*POP HL*/
                        cycles+=4; hl.b.l=z80_readmem(sp); sp++;
                        cycles+=3; hl.b.h=z80_readmem(sp); sp++;
                        cycles+=3;
                        break;
                        case 0xE2: /*JP PO*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&V_FLAG))
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xE3: /*EX (SP),HL*/
                        cycles+=4; addr=z80_readmem(sp);
                        cycles+=3; addr|=(z80_readmem(sp+1)<<8);
                        cycles+=4; z80_writemem(sp,hl.b.l);
                        cycles+=3; z80_writemem(sp+1,hl.b.h);
                        hl.w=addr;
                        cycles+=5;
                        break;
                        case 0xE4: /*CALL PO,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&V_FLAG))
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0xE5: /*PUSH HL*/
                        cycles+=5; sp--; z80_writemem(sp,hl.b.h);
                        cycles+=3; sp--; z80_writemem(sp,hl.b.l);
                        cycles+=3;
                        break;
                        case 0xE6: /*AND nn*/
                        cycles+=4; af.b.h&=z80_readmem(pc++);
                        setand(af.b.h);
                        cycles+=3;
                        break;
                        case 0xE7: /*RST 20*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x20;
                        cycles+=3;
                        break;
                        case 0xE8: /*RET PE*/
                        cycles+=5;
                        if (af.b.l&V_FLAG)
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xE9: /*JP (HL)*/
                        pc=hl.w;
                        cycles+=4;
                        break;
                        case 0xEA: /*JP PE*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&V_FLAG)
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xEB: /*EX DE,HL*/
                        addr=de.w; de.w=hl.w; hl.w=addr;
                        cycles+=4;
                        break;
                        case 0xEC: /*CALL PE,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&V_FLAG)
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;

                        case 0xED: /*More opcodes*/
                        ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                        cycles+=4;
                        opcode=z80_readmem(pc++);
                        switch (opcode)
                        {
                                case 0x40: /*IN B,(C)*/
                                cycles+=4; bc.b.h=z80in(bc.w);
                                setzn(bc.b.h);
                                cycles+=4;
                                break;
                                case 0x42: /*SBC HL,BC*/
                                setsbc16(hl.w,bc.w);
                                hl.w-=(bc.w+tempc);
                                cycles+=11;
                                break;
                                case 0x43: /*LD (nn),BC*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; z80_writemem(addr,bc.b.l);
                                cycles+=3; z80_writemem(addr+1,bc.b.h);
                                cycles+=3;
                                break;
                                case 0x44: /*NEG*/
                                z80_setsub(0,af.b.h);
                                af.b.h=0-af.b.h;
                                cycles+=4;
                                break;
                                case 0x45: /*RETN*/
                                output=0;
                                iff1=iff2;
                                cycles+=4; pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                                break;
                                case 0x47: /*LD I,A*/
                                ir.b.h=af.b.h;
                                bem_debugf("I now %02X %04X\n",ir.b.h,ir.w);
                                cycles+=5;
                                break;
                                case 0x4A: /*ADC HL,BC*/
                                setadc16(hl.w,bc.w);
                                hl.w+=(bc.w+tempc);
                                cycles+=11;
                                break;
                                case 0x4B: /*LD BC,(nn)*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; bc.b.l=z80_readmem(addr);
                                cycles+=3; bc.b.h=z80_readmem(addr+1);
                                cycles+=3;
                                break;
                                case 0x4D: /*RETI*/
                                cycles+=4; pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                                break;
                                case 0x4F: /*LD R,A*/
                                ir.b.l=af.b.h;
                                cycles+=5;
                                break;
                                case 0x50: /*IN D,(C)*/
                                cycles+=4; de.b.h=z80in(bc.w);
                                setzn(de.b.h);
                                cycles+=4;
                                break;
                                case 0x51: /*OUT (C),D*/
                                cycles+=4; z80out(bc.w,de.b.h);
                                cycles+=4;
                                break;
                                case 0x52: /*SBC HL,DE*/
                                setsbc16(hl.w,de.w);
                                hl.w-=(de.w+tempc);
                                cycles+=11;
                                break;
                                case 0x53: /*LD (nn),DE*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; z80_writemem(addr,de.b.l);
                                cycles+=3; z80_writemem(addr+1,de.b.h);
                                cycles+=3;
                                break;
                                case 0x56: /*IM 1*/
                                im=1;
                                cycles+=4;
                                break;
                                case 0x57: /*LD A,I*/
                                af.b.h=ir.b.h;
                                cycles+=5;
                                break;
                                case 0x58: /*IN E,(C)*/
                                cycles+=4; de.b.l=z80in(bc.w);
                                setzn(de.b.l);
                                cycles+=4;
                                break;
                                case 0x59: /*OUT (C),E*/
                                cycles+=4; z80out(bc.w,de.b.l);
                                cycles+=4;
                                break;
                                case 0x5A: /*ADC HL,DE*/
                                setadc16(hl.w,de.w);
                                hl.w+=(de.w+tempc);
                                cycles+=11;
                                break;
                                case 0x5B: /*LD DE,(nn)*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; de.b.l=z80_readmem(addr);
                                cycles+=3; de.b.h=z80_readmem(addr+1);
                                cycles+=3;
                                break;
                                case 0x5E: /*IM 2*/
//                                printf("IM 2\n");
                                im=2;
                                cycles+=4;
                                break;
                                case 0x5F: /*LD A,R*/
                                af.b.h=ir.b.l;
                                af.b.l&=C_FLAG;
                                af.b.l|=(af.b.h&0xA8);
                                if (!af.b.h) af.b.l|=Z_FLAG;
                                if (iff2 && !enterint) af.b.l|=V_FLAG;
                                cycles+=5;
                                break;
                                case 0x60: /*IN H,(C)*/
                                cycles+=4; hl.b.h=z80in(bc.w);
                                setzn(hl.b.h);
                                cycles+=4;
                                break;
                                case 0x61: /*OUT (C),H*/
                                cycles+=4; z80out(bc.w,hl.b.h);
                                cycles+=4;
                                break;
                                case 0x62: /*SBC HL,HL*/
                                setsbc16(hl.w,hl.w);
                                hl.w-=(hl.w+tempc);
                                cycles+=11;
                                break;
                                case 0x67: /*RRD*/
                                cycles+=4; addr=z80_readmem(hl.w)|((af.b.h&0xF)<<8);
                                addr=(addr>>4)|((addr<<8)&0xF00);
                                cycles+=3; z80_writemem(hl.w,addr&0xFF);
                                af.b.h=(af.b.h&0xF0)|(addr>>8);
                                setznc(af.b.h);
                                cycles+=7;
                                break;
                                case 0x69: /*OUT (C),L*/
                                cycles+=4; z80out(bc.w,hl.b.l);
                                cycles+=4;
                                break;
                                case 0x6A: /*ADC HL,HL*/
                                setadc16(hl.w,hl.w);
                                hl.w+=(hl.w+tempc);
                                cycles+=11;
                                break;
                                case 0x6F: /*RLD*/
                                cycles+=4; addr=z80_readmem(hl.w)|((af.b.h&0xF)<<8);
                                addr=((addr<<4)&0xFF0)|(addr>>8);
                                cycles+=3; z80_writemem(hl.w,addr&0xFF);
                                af.b.h=(af.b.h&0xF0)|(addr>>8);
                                setznc(af.b.h);
                                cycles+=7;
                                break;
                                case 0x72: /*SBC HL,SP*/
                                setsbc16(hl.w,sp);
                                hl.w-=(sp+tempc);
                                cycles+=11;
                                break;
                                case 0x73: /*LD (nn),SP*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; z80_writemem(addr,sp);
                                cycles+=3; z80_writemem(addr+1,sp>>8);
                                cycles+=3;
                                break;
                                case 0x78: /*IN A,(C)*/
                                cycles+=4; af.b.h=z80in(bc.w);
                                setzn(af.b.h);
                                cycles+=4;
                                break;
                                case 0x79: /*OUT (C),A*/
                                cycles+=4; z80out(bc.w,af.b.h);
                                cycles+=4;
                                break;
                                case 0x7A: /*ADC HL,SP*/
                                setadc16(hl.w,sp);
                                hl.w+=(sp+tempc);
                                cycles+=11;
                                break;
                                case 0x7B: /*LD SP,(nn)*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; sp=z80_readmem(addr);
                                cycles+=3; sp|=(z80_readmem(addr+1)<<8);
                                cycles+=3;
                                break;
                                case 0xA0: /*LDI*/
                                cycles+=4; temp=z80_readmem(hl.w++);
                                cycles+=3; z80_writemem(de.w,temp); de.w++;
                                bc.w--;
                                af.b.l&=~(H_FLAG|S_FLAG|V_FLAG);
                                if (bc.w) af.b.l|=V_FLAG;
                                cycles+=5;
                                break;
                                case 0xA1: /*CPI*/
                                cycles+=4; temp=z80_readmem(hl.w++);
                                setcpED(af.b.h,temp);
                                bc.w--;
                                if (bc.w) af.b.l|=V_FLAG;
                                else      af.b.l&=~V_FLAG;
                                cycles+=8;
                                break;
                                case 0xA2: /*INI*/
                                cycles+=5; temp=z80in(bc.w); //z80_readmem(hl.w++);
                                cycles+=3; z80_writemem(hl.w++,temp);
                                af.b.l|=N_FLAG;
                                bc.b.h--;
                                if (!bc.b.h) af.b.l|=Z_FLAG;
                                else         af.b.l&=~Z_FLAG;
                                af.b.l|=S_FLAG;
                                cycles+=4;
                                break;
                                case 0xA3: /*OUTI*/
                                cycles+=5; temp=z80_readmem(hl.w++);
                                cycles+=3; z80out(bc.w,temp);
                                bc.b.h--;
                                if (!bc.b.h) af.b.l|=Z_FLAG;
                                else         af.b.l&=~Z_FLAG;
                                af.b.l|=S_FLAG;
                                cycles+=4;
                                break;
                                case 0xA8: /*LDD*/
                                cycles+=4; temp=z80_readmem(hl.w--);
                                cycles+=3; z80_writemem(de.w,temp); de.w--;
                                bc.w--;
                                af.b.l&=~(H_FLAG|S_FLAG|V_FLAG);
                                if (bc.w) af.b.l|=V_FLAG;
                                cycles+=5;
                                break;
                                case 0xAB: /*OUTD*/
                                cycles+=5; temp=z80_readmem(hl.w--);
                                cycles+=3; z80out(bc.w,temp);
                                bc.b.h--;
                                if (!bc.b.h) af.b.l|=Z_FLAG;
                                else         af.b.l&=~Z_FLAG;
                                af.b.l|=S_FLAG;
                                cycles+=4;
                                break;
                                case 0xB0: /*LDIR*/
                                cycles+=4; temp=z80_readmem(hl.w++);
                                cycles+=3; z80_writemem(de.w,temp); de.w++;
                                bc.w--;
                                if (bc.w) { pc-=2; cycles+=5; }
                                af.b.l&=~(H_FLAG|S_FLAG|V_FLAG);
                                cycles+=5;
                                break;
                                case 0xB1: /*CPIR*/
                                cycles+=4; temp=z80_readmem(hl.w++);
                                bc.w--;
                                setcpED(af.b.h,temp);
                                if (bc.w && (af.b.h!=temp))
                                {
                                        pc-=2;
                                        cycles+=13;
                                        af.b.l&=~V_FLAG;
                                }
                                else
                                {
                                        af.b.l|=V_FLAG;
                                        cycles+=8;
                                }
                                break;
                                case 0xB8: /*LDDR*/
                                cycles+=4; temp=z80_readmem(hl.w--);
                                cycles+=3; z80_writemem(de.w,temp); de.w--;
                                bc.w--;
                                if (bc.w) { pc-=2; cycles+=5; }
                                af.b.l&=~(H_FLAG|S_FLAG|V_FLAG);
                                cycles+=5;
                                break;
                                case 0xB9: /*CPDR*/
                                cycles+=4; temp=z80_readmem(hl.w--);
                                bc.w--;
                                setcpED(af.b.h,temp);
                                if (bc.w && (af.b.h!=temp))
                                {
                                        pc-=2;
                                        cycles+=13;
                                        af.b.l&=~V_FLAG;
                                }
                                else
                                {
                                        af.b.l|=V_FLAG;
                                        cycles+=8;
                                }
                                break;

                                default:
                                break;
//                                printf("Bad ED opcode %02X at %04X\n",opcode,pc);
//                                z80_dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xEE: /*XOR nn*/
                        cycles+=4; af.b.h^=z80_readmem(pc++);
                        af.b.l&=~3;
                        setzn(af.b.h);
                        cycles+=3;
                        break;
                        case 0xEF: /*RST 28*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x28;
                        cycles+=3;
                        break;

                        case 0xF0: /*RET P*/
                        cycles+=5;
                        if (!(af.b.l&N_FLAG))
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xF1: /*POP AF*/
                        cycles+=4; af.b.l=z80_readmem(sp); sp++;
                        cycles+=3; af.b.h=z80_readmem(sp); sp++;
                        cycles+=3;
                        break;
                        case 0xF2: /*JP P*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&N_FLAG))
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xF3: /*DI*/
                        iff1=iff2=0;
                        cycles+=4;
                        break;
                        case 0xF4: /*CALL P,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (!(af.b.l&N_FLAG))
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;
                        case 0xF5: /*PUSH AF*/
                        cycles+=5; sp--; z80_writemem(sp,af.b.h);
                        cycles+=3; sp--; z80_writemem(sp,af.b.l);
                        cycles+=3;
                        break;
                        case 0xF6: /*OR nn*/
                        cycles+=4; af.b.h|=z80_readmem(pc++);
                        af.b.l&=~3;
                        setzn(af.b.h);
                        cycles+=3;
                        break;
                        case 0xF7: /*RST 30*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x30;
                        cycles+=3;
                        break;
                        case 0xF8: /*RET M*/
                        cycles+=5;
                        if (af.b.l&N_FLAG)
                        {
                                pc=z80_readmem(sp); sp++;
                                cycles+=3; pc|=(z80_readmem(sp)<<8); sp++;
                                cycles+=3;
                        }
                        break;
                        case 0xF9: /*LD SP,HL*/
                        sp=hl.w;
                        cycles+=6;
                        break;
                        case 0xFA: /*JP M*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&N_FLAG)
                           pc=addr;
                        cycles+=3;
                        break;
                        case 0xFB: /*EI*/
                        iff1=iff2=1;
                        cycles+=4;
                        break;
                        case 0xFC: /*CALL M,xxxx*/
                        cycles+=4; addr=z80_readmem(pc);
                        cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                        if (af.b.l&N_FLAG)
                        {
                                cycles+=4; sp--; z80_writemem(sp,pc>>8);
                                cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                                pc=addr;
                                cycles+=3;
                        }
                        else
                           cycles+=3;
                        break;

                        case 0xFD: /*More opcodes*/
                        ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                        cycles+=4;
                        opcode=z80_readmem(pc++);
                        switch (opcode)
                        {
                                case 0x3A:
                                pc--;
                                break;

                                case 0x09: /*ADD IY,BC*/
                                z80_setadd16(iy.w,bc.w);
                                iy.w+=bc.w;
                                cycles+=11;
                                break;
                                case 0x19: /*ADD IY,DE*/
                                z80_setadd16(iy.w,de.w);
                                iy.w+=de.w;
                                cycles+=11;
                                break;
                                case 0x21: /*LD IY,nn*/
                                cycles+=4; iy.b.l=z80_readmem(pc++);
                                cycles+=3; iy.b.h=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x22: /*LD (nn),IY*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; z80_writemem(addr,iy.b.l);
                                cycles+=3; z80_writemem(addr+1,iy.b.h);
                                cycles+=3;
                                break;
                                case 0x23: /*INC IY*/
                                iy.w++;
                                cycles+=6;
                                break;
                                case 0x24: /*INC IYh*/
                                setinc(iy.b.h);
                                iy.b.h++;
                                cycles+=4;
                                break;
                                case 0x25: /*DEC IYh*/
                                setdec(iy.b.h);
                                iy.b.h--;
                                cycles+=4;
                                break;
                                case 0x26: /*LD IYh,nn*/
                                cycles+=4; iy.b.h=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x29: /*ADD IY,IY*/
                                z80_setadd16(iy.w,iy.w);
                                iy.w+=iy.w;
                                cycles+=11;
                                break;
                                case 0x2A: /*LD IY,(nn)*/
                                cycles+=4; addr=z80_readmem(pc);
                                cycles+=3; addr|=(z80_readmem(pc+1)<<8); pc+=2;
                                cycles+=3; iy.b.l=z80_readmem(addr);
                                cycles+=3; iy.b.h=z80_readmem(addr+1);
                                cycles+=3;
                                break;
                                case 0x2B: /*DEC IY*/
                                iy.w--;
                                cycles+=6;
                                break;
                                case 0x2C: /*INC IYl*/
                                setinc(iy.b.l);
                                iy.b.l++;
                                cycles+=4;
                                break;
                                case 0x2D: /*DEC IYl*/
                                setdec(iy.b.l);
                                iy.b.l--;
                                cycles+=4;
                                break;
                                case 0x2E: /*LD IYl,nn*/
                                cycles+=4; iy.b.l=z80_readmem(pc++);
                                cycles+=3;
                                break;
                                case 0x34: /*INC (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                addr+=iy.w;
                                cycles+=3; temp=z80_readmem(addr);
                                setinc(temp);
                                cycles+=5; z80_writemem(addr,temp+1);
                                cycles+=7;
                                break;
                                case 0x35: /*DEC (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                addr+=iy.w;
                                cycles+=3; temp=z80_readmem(addr);
                                setdec(temp);
                                cycles+=5; z80_writemem(addr,temp-1);
                                cycles+=7;
                                break;
                                case 0x36: /*LD (IY+nn),nn*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(pc++);
                                cycles+=5; z80_writemem(iy.w+addr,temp);
                                cycles+=3;
                                break;
                                case 0x39: /*ADD IY,SP*/
//                                output=1;
                                z80_setadd16(iy.w,sp);
                                iy.w+=sp;
                                cycles+=11;
                                break;

                                case 0x44: bc.b.h=iy.b.h; cycles+=3; break; /*LD B,IYh*/
                                case 0x45: bc.b.h=iy.b.l; cycles+=3; break; /*LD B,IYl*/
                                case 0x4C: bc.b.l=iy.b.h; cycles+=3; break; /*LD C,IYh*/
                                case 0x4D: bc.b.l=iy.b.l; cycles+=3; break; /*LD C,IYl*/
                                case 0x54: de.b.h=iy.b.h; cycles+=3; break; /*LD D,IYh*/
                                case 0x55: de.b.h=iy.b.l; cycles+=3; break; /*LD D,IYl*/
                                case 0x5C: de.b.l=iy.b.h; cycles+=3; break; /*LD E,IYh*/
                                case 0x5D: de.b.l=iy.b.l; cycles+=3; break; /*LD E,IYl*/

                                case 0x46: /*LD B,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; bc.b.h=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x4E: /*LD C,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; bc.b.l=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x56: /*LD D,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; de.b.h=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x5E: /*LD E,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; de.b.l=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x66: /*LD H,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; hl.b.h=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x6E: /*LD L,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                intreg=(iy.w+addr)>>8;
                                cycles+=7; hl.b.l=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;
                                case 0x60: iy.b.h=bc.b.h; cycles+=3; break;  /*LD IYh,B*/
                                case 0x61: iy.b.h=bc.b.l; cycles+=3; break;  /*LD IYh,C*/
                                case 0x62: iy.b.h=de.b.h; cycles+=3; break;  /*LD IYh,D*/
                                case 0x63: iy.b.h=de.b.l; cycles+=3; break;  /*LD IYh,E*/
                                case 0x64: iy.b.h=hl.b.h; cycles+=3; break;  /*LD IYh,H*/
                                case 0x65: iy.b.h=hl.b.l; cycles+=3; break;  /*LD IYh,L*/
                                case 0x67: iy.b.h=af.b.h; cycles+=3; break;  /*LD IYh,A*/
                                case 0x68: iy.b.l=bc.b.h; cycles+=3; break;  /*LD IYl,B*/
                                case 0x69: iy.b.l=bc.b.l; cycles+=3; break;  /*LD IYl,C*/
                                case 0x6A: iy.b.l=de.b.h; cycles+=3; break;  /*LD IYl,D*/
                                case 0x6B: iy.b.l=de.b.l; cycles+=3; break;  /*LD IYl,E*/
                                case 0x6C: iy.b.l=hl.b.h; cycles+=3; break;  /*LD IYl,H*/
                                case 0x6D: iy.b.l=hl.b.l; cycles+=3; break;  /*LD IYl,L*/
                                case 0x6F: iy.b.l=af.b.h; cycles+=3; break;  /*LD IYl,A*/

                                case 0x84: z80_setadd(af.b.h,iy.b.h); af.b.h+=iy.b.h; cycles+=3; break;         /*ADD IYh*/
                                case 0x85: z80_setadd(af.b.h,iy.b.l); af.b.h+=iy.b.l; cycles+=3; break;         /*ADD IYl*/
                                case 0x8C: setadc(af.b.h,iy.b.h); af.b.h+=iy.b.h+tempc; cycles+=3; break;   /*ADC IYh*/
                                case 0x8D: setadc(af.b.h,iy.b.l); af.b.h+=iy.b.l+tempc; cycles+=3; break;   /*ADC IYl*/
                                case 0x94: z80_setsub(af.b.h,iy.b.h); af.b.h-=iy.b.h; cycles+=3; break;         /*SUB IYh*/
                                case 0x95: z80_setsub(af.b.h,iy.b.l); af.b.h-=iy.b.l; cycles+=3; break;         /*SUB IYl*/
                                case 0x9C: setsbc(af.b.h,iy.b.h); af.b.h-=(iy.b.h+tempc); cycles+=3; break; /*SBC IYh*/
                                case 0x9D: setsbc(af.b.h,iy.b.l); af.b.h-=(iy.b.l+tempc); cycles+=3; break; /*SBC IYl*/
                                case 0xA4: setand(af.b.h&iy.b.h); af.b.h&=iy.b.h; cycles+=3; break;         /*AND IYh*/
                                case 0xA5: setand(af.b.h&iy.b.l); af.b.h&=iy.b.l; cycles+=3; break;         /*AND IYl*/
                                case 0xAC: setzn(af.b.h^iy.b.h);  af.b.h^=iy.b.h; cycles+=3; break;         /*XOR IYh*/
                                case 0xAD: setzn(af.b.h^iy.b.l);  af.b.h^=iy.b.l; cycles+=3; break;         /*XOR IYl*/
                                case 0xB4: setzn(af.b.h|iy.b.h);  af.b.h|=iy.b.h; cycles+=3; break;         /*OR  IYh*/
                                case 0xB5: setzn(af.b.h|iy.b.l);  af.b.h|=iy.b.l; cycles+=3; break;         /*OR  IYl*/
                                case 0xBC: setcp(af.b.h,iy.b.h); cycles+=3; break;                          /*CP  IYh*/
                                case 0xBD: setcp(af.b.h,iy.b.l); cycles+=3; break;                          /*CP  IYl*/

                                case 0x70: /*LD (IY+nn),B*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,bc.b.h);
                                cycles+=8;
                                break;
                                case 0x71: /*LD (IY+nn),C*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,bc.b.l);
                                cycles+=8;
                                break;
                                case 0x72: /*LD (IY+nn),D*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,de.b.h);
                                cycles+=8;
                                break;
                                case 0x73: /*LD (IY+nn),E*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,de.b.l);
                                cycles+=8;
                                break;
                                case 0x74: /*LD (IY+nn),H*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,hl.b.h);
                                cycles+=8;
                                break;
                                case 0x75: /*LD (IY+nn),L*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,hl.b.l);
                                cycles+=8;
                                break;
                                case 0x77: /*LD (IY+nn),A*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; z80_writemem(iy.w+addr,af.b.h);
                                cycles+=8;
                                break;
                                case 0x7E: /*LD A,(IY+nn)*/
                                addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=7; af.b.h=z80_readmem(iy.w+addr);
                                cycles+=8;
                                break;

                                case 0x7C: af.b.h=iy.b.h; cycles+=3; break; /*LD A,IYh*/
                                case 0x7D: af.b.h=iy.b.l; cycles+=3; break; /*LD A,IYl*/

                                case 0x86: /*ADD (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(iy.w+addr);
                                z80_setadd(af.b.h,temp);
                                af.b.h+=temp;
                                cycles+=8;
                                break;
                                case 0x8E: /*ADC (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(iy.w+addr);
                                setadc(af.b.h,temp);
                                af.b.h+=(temp+tempc);
                                cycles+=8;
                                break;
                                case 0x96: /*SUB (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(iy.w+addr);
                                z80_setsub(af.b.h,temp);
                                af.b.h-=temp;
                                cycles+=8;
                                break;
                                case 0x9E: /*SBC (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(iy.w+addr);
                                setsbc(af.b.h,temp);
                                af.b.h-=(temp+tempc);
                                cycles+=8;
                                break;
                                case 0xA6: /*AND (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; af.b.h&=z80_readmem(iy.w+addr);
                                setand(af.b.h);
                                cycles+=8;
                                break;
                                case 0xAE: /*XOR (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; af.b.h^=z80_readmem(iy.w+addr);
                                setzn(af.b.h);
                                cycles+=8;
                                break;
                                case 0xB6: /*OR (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; af.b.h|=z80_readmem(iy.w+addr);
                                setzn(af.b.h);
                                cycles+=8;
                                break;
                                case 0xBE: /*CP (IY+nn)*/
                                cycles+=4; addr=z80_readmem(pc++); if (addr&0x80) addr|=0xFF00;
                                cycles+=3; temp=z80_readmem(iy.w+addr);
                                setcp(af.b.h,temp);
                                cycles+=8;
                                break;

                                case 0xCB: /*More opcodes*/
                                ir.b.l=((ir.b.l+1)&0x7F)|(ir.b.l&0x80);
                                cycles+=4; addr=z80_readmem(pc++);
                                if (addr&0x80) addr|=0xFF00;
                                cycles+=3; opcode=z80_readmem(pc++);
                                switch (opcode)
                                {
                                        case 0x06: /*RLC (IY+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+iy.w);
                                        tempc=temp&0x80;
                                        temp<<=1;
                                        if (tempc) temp|=1;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+iy.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x0E: /*RRC (IY+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+iy.w);
                                        tempc=temp&1;
                                        temp>>=1;
                                        if (tempc) temp|=0x80;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+iy.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x16:  /*RL (IY+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+iy.w);
                                        addr=temp&0x80;
                                        temp<<=1;
                                        if (tempc) temp|=1;
                                        setzn(temp);
                                        if (addr) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+iy.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x1E:  /*RR (IY+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+iy.w);
                                        addr=temp&1;
                                        temp>>=1;
                                        if (tempc) temp|=0x80;
                                        setzn(temp);
                                        if (addr) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+iy.w,temp);
                                        cycles+=3;
                                        break;
                                        case 0x2E:  /*SRA (IY+nn)*/
                                        cycles+=5; temp=z80_readmem(addr+iy.w);
                                        tempc=temp&1;
                                        temp>>=1;
                                        if (temp&0x40) temp|=0x80;
                                        setzn(temp);
                                        if (tempc) af.b.l|=C_FLAG;
                                        cycles+=4; z80_writemem(addr+iy.w,temp);
                                        cycles+=3;
                                        break;

                                        case 0x46: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&1,iy.w+addr); cycles+=4; break; /*BIT 0,(iy+nn)*/
                                        case 0x4E: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&2,iy.w+addr); cycles+=4; break; /*BIT 1,(iy+nn)*/
                                        case 0x56: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&4,iy.w+addr); cycles+=4; break; /*BIT 2,(iy+nn)*/
                                        case 0x5E: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&8,iy.w+addr); cycles+=4; break; /*BIT 3,(iy+nn)*/
                                        case 0x66: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&0x10,iy.w+addr); cycles+=4; break; /*BIT 4,(iy+nn)*/
                                        case 0x6E: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&0x20,iy.w+addr); cycles+=4; break; /*BIT 5,(iy+nn)*/
                                        case 0x76: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&0x40,iy.w+addr); cycles+=4; break; /*BIT 6,(iy+nn)*/
                                        case 0x7E: cycles+=5; temp=z80_readmem(iy.w+addr); setbit2(temp&0x80,iy.w+addr); cycles+=4; break; /*BIT 7,(iy+nn)*/

                                        case 0x86: cycles+=5; temp=z80_readmem(iy.w+addr)&~1; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;    /*RES 0,(iy+nn)*/
                                        case 0x8E: cycles+=5; temp=z80_readmem(iy.w+addr)&~2; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;    /*RES 1,(iy+nn)*/
                                        case 0x96: cycles+=5; temp=z80_readmem(iy.w+addr)&~4; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;    /*RES 2,(iy+nn)*/
                                        case 0x9E: cycles+=5; temp=z80_readmem(iy.w+addr)&~8; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;    /*RES 3,(iy+nn)*/
                                        case 0xA6: cycles+=5; temp=z80_readmem(iy.w+addr)&~0x10; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break; /*RES 4,(iy+nn)*/
                                        case 0xAE: cycles+=5; temp=z80_readmem(iy.w+addr)&~0x20; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break; /*RES 5,(iy+nn)*/
                                        case 0xB6: cycles+=5; temp=z80_readmem(iy.w+addr)&~0x40; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break; /*RES 6,(iy+nn)*/
                                        case 0xBE: cycles+=5; temp=z80_readmem(iy.w+addr)&~0x80; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break; /*RES 7,(iy+nn)*/
                                        case 0xC6: cycles+=5; temp=z80_readmem(iy.w+addr)|1; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;     /*SET 0,(iy+nn)*/
                                        case 0xCE: cycles+=5; temp=z80_readmem(iy.w+addr)|2; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;     /*SET 1,(iy+nn)*/
                                        case 0xD6: cycles+=5; temp=z80_readmem(iy.w+addr)|4; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;     /*SET 2,(iy+nn)*/
                                        case 0xDE: cycles+=5; temp=z80_readmem(iy.w+addr)|8; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;     /*SET 3,(iy+nn)*/
                                        case 0xE6: cycles+=5; temp=z80_readmem(iy.w+addr)|0x10; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;  /*SET 4,(iy+nn)*/
                                        case 0xEE: cycles+=5; temp=z80_readmem(iy.w+addr)|0x20; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;  /*SET 5,(iy+nn)*/
                                        case 0xF6: cycles+=5; temp=z80_readmem(iy.w+addr)|0x40; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;  /*SET 6,(iy+nn)*/
                                        case 0xFE: cycles+=5; temp=z80_readmem(iy.w+addr)|0x80; cycles+=4; z80_writemem(iy.w+addr,temp); cycles+=3; break;  /*SET 7,(iy+nn)*/

                                        default:
                                        break;
//                                        printf("Bad FD CB opcode %02X at %04X\n",opcode,pc);
//                                        z80_dumpregs();
//                                        exit(-1);
                                }
                                break;

                                case 0xE1: /*POP IY*/
                                cycles+=4; iy.b.l=z80_readmem(sp); sp++;
                                cycles+=3; iy.b.h=z80_readmem(sp); sp++;
                                cycles+=3;
                                break;
                                case 0xE3: /*EX (SP),IY*/
                                cycles+=4; addr=z80_readmem(sp);
                                cycles+=3; addr|=(z80_readmem(sp+1)<<8);
                                cycles+=4; z80_writemem(sp,iy.b.l);
                                cycles+=3; z80_writemem(sp+1,iy.b.h);
                                iy.w=addr;
                                cycles+=5;
                                break;
                                case 0xE5: /*PUSH IY*/
                                cycles+=5; sp--; z80_writemem(sp,iy.b.h);
                                cycles+=3; sp--; z80_writemem(sp,iy.b.l);
                                cycles+=3;
                                break;
                                case 0xE9: /*JP (IY)*/
                                pc=iy.w;
                                cycles+=4;
                                break;

                                case 0xF9: /*LD SP,IY*/
                                sp=iy.w;
                                cycles+=6;
                                break;

/*                                case 0x95:
                                ir.b.l--;
                                pc--;
                                cycles+=4;
                                break;*/

                                default:
                                break;
//                                printf("Bad FD opcode %02X at %04X\n",opcode,pc);
//                                z80_dumpregs();
//                                exit(-1);
                        }
                        break;

                        case 0xFE: /*CP nn*/
                        cycles+=4; temp=z80_readmem(pc++);
                        setcp(af.b.h,temp);
                        cycles+=3;
                        break;
                        case 0xFF: /*RST 38*/
                        cycles+=5; sp--; z80_writemem(sp,pc>>8);
                        cycles+=3; sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x38;
                        cycles+=3;
                        break;

                        default:
                        break;
//                        printf("Bad opcode %02X at %04X\n",opcode,pc);
//                        z80_dumpregs();
//                        z80_mem_dump();
//                        exit(-1);
                }
/*                if (pc==0)
                {
                        printf("PC = 0\n");
                        z80_dumpregs();
                        exit(-1);
                }*/
//                if (ins==200000) { z80_dumpregs(); exit(-1); }
//                if (pc==0xC3CF) output=1;
//                if (pc==0x90B7) { z80_dumpregs(); exit(-1); }
//                if (pc==0x8066) { z80_dumpregs(); exit(-1); }
//                if (output) ins++;
                ins++;
//                if (ins==107000) output=1;
//                if (ins==108000) output=0;
//                if (pc==0xC8D7) output=1;
//                if (pc==0xC8E7) output=0;
//                if (ins==11329) output=1;
                if (output) printf("%04X : %04X %04X %04X %04X %04X %04X %04X %04X %02X\n",pc,af.w&0xFF00,bc.w,de.w,hl.w,ix.w,iy.w,sp,ir.w,opcode);
//                if ((pc<0x37) && !(pc&7)) printf("RST %02X\n",pc);
//                if (pc==8) { z80_dumpregs(); exit(-1); }
//                if (pc==0xC3DC) output=1;
//                if (ins==500) { z80_dumpregs(); exit(-1); }
                if (enterint)
                {
//                        printf("Interrupt at %04X IM %i\n",pc,im);
                        iff2=iff1;
                        iff1=0;
                        sp--; z80_writemem(sp,pc>>8);
                        sp--; z80_writemem(sp,pc&0xFF);
                        switch (im)
                        {
                                case 0: case 1: pc=0x38; break;
                                case 2:
//                                printf("IM2 %04X %02X %02X %02X\n",ir.w,z80_readmem(ir.w|0xFF),z80_readmem(ir.w&~0xFF),z80_readmem((ir.w|0xFF)+1));
                                pc=z80_readmem(0xFFFE)|(z80_readmem(0xFFFF)<<8);
//                                pc=z80_readmem(ir.w|0xFF);
//                                pc|=(z80_readmem((ir.w|0xFF)+1)<<8);
//                                printf("PC now %04X\n",pc);
                                cycles+=8;
                                break;
                        }
//                        printf("PC now %04X\n",pc);
                        z80int=enterint=0;
                        cycles+=11;
                }
                if (tube_irq&2 && !z80_oldnmi)
                {
//                        printf("NMI at %04X IM %i\n",pc,im);
                        iff2=iff1;
                        iff1=0;
                        sp--; z80_writemem(sp,pc>>8);
                        sp--; z80_writemem(sp,pc&0xFF);
                        pc=0x66;
                        tuberomin=1;
//                        printf("PC now %04X\n",pc);
                        z80int=enterint=0;
                        cycles+=11;
//                        output=1;
                }
                z80_oldnmi=tube_irq&2;
//                pollula(cycles);
                tubecycles-=cycles;
        }
}
