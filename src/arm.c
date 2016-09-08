/*B-em v2.2 by Tom Walker
  ARM1 parasite CPU emulation
  Originally from Arculator*/

#include <stdio.h>
#include <stdint.h>
#include <allegro.h>
#include "b-em.h"
#include "arm.h"
#include "tube.h"


uint32_t *usrregs[16],userregs[16],superregs[16],fiqregs[16],irqregs[16];
uint32_t armregs[16];

#define PC ((armregs[15])&0x3FFFFFC)

static uint32_t opcode2,opcode3;

static int armirq=0;
static int databort;
static uint32_t *armrom,*armram;
static uint8_t *armromb,*armramb;
#define USER       0
#define FIQ        1
#define IRQ        2
#define SUPERVISOR 3

#define NFSET ((armregs[15]&0x80000000)?1:0)
#define ZFSET ((armregs[15]&0x40000000)?1:0)
#define CFSET ((armregs[15]&0x20000000)?1:0)
#define VFSET ((armregs[15]&0x10000000)?1:0)

#define NFLAG 0x80000000
#define ZFLAG 0x40000000
#define CFLAG 0x20000000
#define VFLAG 0x10000000
#define IFLAG 0x08000000

#define RD ((opcode>>12)&0xF)
#define RN ((opcode>>16)&0xF)
#define RM (opcode&0xF)

#define MULRD ((opcode>>16)&0xF)
#define MULRN ((opcode>>12)&0xF)
#define MULRS ((opcode>>8)&0xF)
#define MULRM (opcode&0xF)

#define GETADDR(r) ((r==15)?(armregs[15]&0x3FFFFFC):armregs[r])
#define LOADREG(r,v) if (r==15) { armregs[15]=(armregs[15]&0xFC000003)|((v+4)&0x3FFFFFC); refillpipeline(); } else armregs[r]=v;
#define GETREG(r) ((r==15) ? armregs[15]+4 : armregs[r])
#define LDRRESULT(a,v) ((a&3)?(v>>((a&3)<<3))|(v<<(((a&3)^3)<<3)):v)

#define undefined()\
                                        templ=armregs[15]-4; \
                                        armregs[15]|=3;\
                                        updatemode(SUPERVISOR);\
                                        armregs[14]=templ;\
                                        armregs[15]&=0xFC000003;\
                                        armregs[15]|=0x08000008;\
                                        refillpipeline();\
                                        tubecycles--

static int mode;
static int osmode=0,memmode;

static void updatemode(int m)
{
        int c;
        usrregs[15]=&armregs[15];
        switch (mode) /*Store back registers*/
        {
                case USER:
                for (c=8;c<15;c++) userregs[c]=armregs[c];
                break;
                case IRQ:
                for (c=8;c<13;c++) userregs[c]=armregs[c];
                irqregs[0]=armregs[13];
                irqregs[1]=armregs[14];
                break;
                case FIQ:
                for (c=8;c<15;c++) fiqregs[c]=armregs[c];
                break;
                case SUPERVISOR:
                for (c=8;c<13;c++) userregs[c]=armregs[c];
                superregs[0]=armregs[13];
                superregs[1]=armregs[14];
                break;
        }
        mode=m;
        switch (m)
        {
                case USER:
                for (c=8;c<15;c++) armregs[c]=userregs[c];
                memmode=osmode;
                for (c=0;c<15;c++) usrregs[c]=&armregs[c];
                break;
                case IRQ:
                for (c=8;c<13;c++) armregs[c]=userregs[c];
                armregs[13]=irqregs[0];
                armregs[14]=irqregs[1];
                for (c=0;c<13;c++) usrregs[c]=&armregs[c];
                for (c=13;c<15;c++) usrregs[c]=&userregs[c];
                memmode=2;
                break;
                case FIQ:
                for (c=8;c<15;c++) armregs[c]=fiqregs[c];
                for (c=0;c<8;c++)  usrregs[c]=&armregs[c];
                for (c=8;c<15;c++) usrregs[c]=&userregs[c];
                memmode=2;
                break;
                case SUPERVISOR:
                for (c=8;c<13;c++) armregs[c]=userregs[c];
                armregs[13]=superregs[0];
                armregs[14]=superregs[1];
                for (c=0;c<13;c++) usrregs[c]=&armregs[c];
                for (c=13;c<15;c++) usrregs[c]=&userregs[c];
                memmode=2;
                break;
        }
}

static uint8_t flaglookup[16][16];
static uint32_t rotatelookup[4096];

static void refillpipeline2();

#define countbits(c) countbitstable[c]
static int countbitstable[65536];
void arm_reset()
{
        int c,d,exec,data;
        uint32_t rotval,rotamount;
        for (c=0;c<65536;c++)
        {
                countbitstable[c]=0;
                for (d=0;d<16;d++)
                {
                        if (c&(1<<d)) countbitstable[c]+=4;
                }
        }
        for (c=0;c<16;c++)
        {
                for (d=0;d<16;d++)
                {
                        armregs[15]=d<<28;
                        switch (c)
                        {
                                case 0:  /*EQ*/ exec=ZFSET; break;
                                case 1:  /*NE*/ exec=!ZFSET; break;
                                case 2:  /*CS*/ exec=CFSET; break;
                                case 3:  /*CC*/ exec=!CFSET; break;
                                case 4:  /*MI*/ exec=NFSET; break;
                                case 5:  /*PL*/ exec=!NFSET; break;
                                case 6:  /*VS*/ exec=VFSET; break;
                                case 7:  /*VC*/ exec=!VFSET; break;
                                case 8:  /*HI*/ exec=(CFSET && !ZFSET); break;
                                case 9:  /*LS*/ exec=(!CFSET || ZFSET); break;
                                case 10: /*GE*/ exec=(NFSET == VFSET); break;
                                case 11: /*LT*/ exec=(NFSET != VFSET); break;
                                case 12: /*GT*/ exec=(!ZFSET && (NFSET==VFSET)); break;
                                case 13: /*LE*/ exec=(ZFSET || (NFSET!=VFSET)); break;
                                case 14: /*AL*/ exec=1; break;
                                case 15: /*NV*/ exec=0; break;
                        }
                        flaglookup[c][d]=exec;
                }
        }

        for (data=0;data<4096;data++)
        {
                rotval=data&0xFF;
                rotamount=((data>>8)&0xF)<<1;
                rotval=(rotval>>rotamount)|(rotval<<(32-rotamount));
                rotatelookup[data]=rotval;
        }
        updatemode(SUPERVISOR);
        armregs[15]=0x0C00000B;
        mode=3;
        memmode=2;
        memcpy(armramb,armromb,0x4000);
        refillpipeline2();
}

void arm_dumpregs()
{
        FILE *f=fopen("armram.dmp","wb");
        fwrite(armram,0x10000,1,f);
        fclose(f);
        bem_debugf("R 0=%08X R 4=%08X R 8=%08X R12=%08X\n",armregs[0],armregs[4],armregs[8],armregs[12]);
        bem_debugf("R 1=%08X R 5=%08X R 9=%08X R13=%08X\n",armregs[1],armregs[5],armregs[9],armregs[13]);
        bem_debugf("R 2=%08X R 6=%08X R10=%08X R14=%08X\n",armregs[2],armregs[6],armregs[10],armregs[14]);
        bem_debugf("R 3=%08X R 7=%08X R11=%08X R15=%08X\n",armregs[3],armregs[7],armregs[11],armregs[15]);
        bem_debugf("f12=%08X  ",fiqregs[12]);
        bem_debugf("PC =%07X\n",PC);
}

static uint32_t *armread[64];
static uint32_t armmask[64];
void arm_init()
{
        FILE *f;
        int c;
        char fn[256];
        if (!armrom) armrom=(uint32_t *)malloc(0x4000);
        if (!armram) armram=(uint32_t *)malloc(0x400000);
        armromb=(uint8_t *)armrom;
        armramb=(uint8_t *)armram;
        append_filename(fn,exedir,"roms/tube/ARMeval_100.rom",511);
        f=fopen(fn,"rb");
        fread(armromb,0x4000,1,f);
        fclose(f);
        memcpy(armramb,armromb,0x4000);
        for (c=0;c<64;c++) armread[c]=0;
        for (c=0;c<4;c++) armread[c]=&armram[c*0x40000];
        armread[48]=armrom;
        for (c=0;c<64;c++) armmask[c]=0xFFFFF;
        armmask[48]=0x3FFF;
}

void arm_close()
{
        if (armrom) free(armrom);
        if (armram) free(armram);
}

int endtimeslice=0;
static uint32_t readarmfl(uint32_t addr);
static uint32_t readarml(uint32_t a)
{
        if (armread[(a>>20)&63])
                return armread[(a>>20)&63][(a&0xFFFFF)>>2];
        return readarmfl(a);
}
//#define readarml(a) ((armread[(a>>20)&63])?armread[(a>>20)&63][(a&0xFFFFF)>>2]:readarmfl(a))

uint8_t readarmb(uint32_t addr)
{
        if (addr<0x400000) return armramb[addr];
        if ((addr&~0x1F)==0x1000000)
        {
                //bem_debugf("Read %08X\n",addr);
                return tube_parasite_read((addr&0x1C)>>2);
        }
        if ((addr>=0x3000000) && (addr<0x3004000)) return armromb[addr&0x3FFF];
        return 0xFF;
/*        bem_debugf("Bad ARM read byte %08X\n",addr);
        dumparmregs();
        exit(-1);*/
}
static uint32_t readarmfl(uint32_t addr)
{
        if (addr<0x400000) return armram[addr>>2];
        if (addr<0x400010) return 0xFFFFFFFF;
        if ((addr>=0x3000000) && (addr<0x3004000)) return armrom[(addr&0x3FFC)>>2];
        return 0xFFFFFFFF;
/*        bem_debugf("Bad ARM read long %08X\n",addr);
        dumparmregs();
        exit(-1);*/
}

void writearmb(uint32_t addr, uint8_t val)
{
        if (addr<0x400000)
        {
                armramb[addr]=val;
                return;
        }
        if ((addr&~0x1F)==0x1000000)
        {
//                bem_debugf("Write %08X %02X\n",addr,val);
                tube_parasite_write((addr&0x1C)>>2,val);
                endtimeslice=1;
                return;
        }
/*        bem_debugf("Bad ARM write byte %08X %02X\n",addr,val);
        dumparmregs();
        exit(-1);*/
}
static void writearml(uint32_t addr, uint32_t val)
{
        if (addr<0x400000)
        {
                armram[addr>>2]=val;
                return;
        }
/*        if (addr<0x400010) return;
        bem_debugf("Bad ARM write long %08X %08X\n",addr,val);
        dumparmregs();
        exit(-1);*/
}

#define checkneg(v) (v&0x80000000)
#define checkpos(v) !(v&0x80000000)

static inline void setadd(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkneg(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkneg(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkneg(op2) && checkpos(res)) ||
            (checkpos(op1) && checkpos(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
        if (!res)                          armregs[15]|=ZFLAG;
        else if (checkneg(res))            armregs[15]|=NFLAG;
}

static inline void setsub(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkpos(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkpos(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if (!res)               armregs[15]|=ZFLAG;
        else if (checkneg(res)) armregs[15]|=NFLAG;
        if ((checkneg(op1) && checkpos(op2) && checkpos(res)) ||
            (checkpos(op1) && checkneg(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
}

static inline void setsbc(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if (!res)                           armregs[15]|=ZFLAG;
        else if (checkneg(res))             armregs[15]|=NFLAG;
        if ((checkneg(op1) && checkpos(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkpos(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkpos(op2) && checkpos(res)) ||
            (checkpos(op1) && checkneg(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
}

static inline void setadc(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkneg(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkneg(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkneg(op2) && checkpos(res)) ||
            (checkpos(op1) && checkpos(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
        if (!res)                          armregs[15]|=ZFLAG;
        else if (checkneg(res))            armregs[15]|=NFLAG;
}

static inline void setarmzn(uint32_t op)
{
        armregs[15]&=0x3FFFFFFF;
        if (!op)               armregs[15]|=ZFLAG;
        else if (checkneg(op)) armregs[15]|=NFLAG;
}

static inline uint32_t shift(uint32_t opcode)
{
        uint32_t shiftmode=(opcode>>5)&3;
        uint32_t shiftamount=(opcode>>7)&31;
        uint32_t temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
//                cycles--;
        }
        temp=armregs[RM];
//        if (RM==15)        temp+=4;
        if (opcode&0x100000 && shiftamount) armregs[15]&=~CFLAG;
        switch (shiftmode)
        {
                case 0: /*LSL*/
                if (!shiftamount) return temp;
                if (shiftamount==32)
                {
                        if (temp&1 && opcode&0x100000) armregs[15]|=CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp<<(shiftamount-1))&0x80000000) armregs[15]|=CFLAG;
                }
                return temp<<shiftamount;

                case 1: /*LSR*/
                if (!shiftamount && !(opcode&0x10))
                {
                        shiftamount=32;
                }
                if (!shiftamount) return temp;
                if (shiftamount==32)
                {
                        if (temp&0x80000000 && opcode&0x100000) armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               armregs[15]&=~CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp>>(shiftamount-1))&1) armregs[15]|=CFLAG;
                }
                return temp>>shiftamount;

                case 2: /*ASR*/
                if (!shiftamount)
                {
                        if (opcode&0x10) return temp;
                }
                if (shiftamount>=32 || !shiftamount)
                {
                        if (temp&0x80000000 && opcode&0x100000) armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               armregs[15]&=~CFLAG;
                        if (temp&0x80000000) return 0xFFFFFFFF;
                        return 0;
                }
                if (opcode&0x100000)
                {
                        if (((int)temp>>(shiftamount-1))&1) armregs[15]|=CFLAG;
                }
                return (int)temp>>shiftamount;

                case 3: /*ROR*/
                if (opcode&0x100000) armregs[15]&=~CFLAG;
                if (!shiftamount && !(opcode&0x10))
                {
                        if (opcode&0x100000 && temp&1) armregs[15]|=CFLAG;
                        return (((cflag)?1:0)<<31)|(temp>>1);
                }
                if (!shiftamount)
                {
                        if (opcode&0x100000) armregs[15]|=cflag;
                        return temp;
                }
                if (!(shiftamount&0x1F))
                {
                        if (opcode&0x100000 && temp&0x80000000) armregs[15]|=CFLAG;
                        return temp;
                }
                if (opcode&0x100000)
                {
                        if (((temp>>shiftamount)|(temp<<(32-shiftamount)))&0x80000000) armregs[15]|=CFLAG;
                }
                return (temp>>shiftamount)|(temp<<(32-shiftamount));
        }
        return 0; /*Should never reach here*/
}

static inline unsigned shift2(unsigned opcode)
{
        unsigned shiftmode=(opcode>>5)&3;
        unsigned shiftamount=(opcode>>7)&31;
        uint32_t temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
        }
        temp=armregs[RM];
//        if (RM==15) temp+=4;
        switch (shiftmode)
        {
                case 0: /*LSL*/
                if (!shiftamount)    return temp;
                if (shiftamount>=32) return 0;
                return temp<<shiftamount;

                case 1: /*LSR*/
                if (!shiftamount && !(opcode&0x10))    return 0;
                if (shiftamount>=32) return 0;
                return temp>>shiftamount;

                case 2: /*ASR*/
                if (!shiftamount && !(opcode&0x10)) shiftamount=32;
                if (shiftamount>=32)
                {
                        if (temp&0x80000000)
                           return 0xFFFFFFFF;
                        return 0;
                }
                return (int)temp>>shiftamount;

                case 3: /*ROR*/
                if (!shiftamount && !(opcode&0x10)) return (((cflag)?1:0)<<31)|(temp>>1);
                if (!shiftamount)                   return temp;
                return (temp>>shiftamount)|(temp<<(32-shiftamount));
        }
        return 0; /*Should never reach here*/
}

static inline unsigned rotate(unsigned data)
{
        uint32_t rotval;
        rotval=rotatelookup[data&4095];
        if (data&0x100000 && data&0xF00)
        {
                if (rotval&0x80000000) armregs[15]|=CFLAG;
                else                   armregs[15]&=~CFLAG;
        }
        return rotval;
}

#define rotate2(v) rotatelookup[v&4095]

static int ldrlookup[4]={0,8,16,24};

#define ldrresult(v,a) ((v>>ldrlookup[addr&3])|(v<<(32-ldrlookup[addr&3])))

static void refillpipeline()
{
        opcode2=readarml(PC-4);
        opcode3=readarml(PC);
}

static void refillpipeline2()
{
        opcode2=readarml(PC-8);
        opcode3=readarml(PC-4);
}

int accc=0;
void arm_exec()
{
        uint32_t opcode,templ,templ2,mask,addr,addr2;
        int c;
        while (tubecycles>0)
        {
                opcode=opcode2;
                opcode2=opcode3;
                opcode3=readarml(PC);
                        if (flaglookup[opcode>>28][armregs[15]>>28])
                        {
                                switch ((opcode>>20)&0xFF)
                                {
                                        case 0x00: /*AND reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MUL*/
//                                        {
//                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                cycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                }
                                                tubecycles--;
//                                        }
                                        break;
                                        case 0x01: /*ANDS reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MULS*/
//                                        {
//                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                setarmzn(armregs[MULRD]);
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(GETADDR(RN)&templ)+4;
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                        setarmzn(armregs[RD]);
                                                }
                                                tubecycles--;
//                                        }
                                        break;

                                        case 0x02: /*EOR reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MLA*/
///                                        {
//                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[RD]=GETADDR(RN)^templ;
                                                }
                                                tubecycles--;
//                                        }
                                        break;
                                        case 0x03: /*EORS reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MLAS*/
//                                        {
//                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                setarmzn(armregs[MULRD]);
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(GETADDR(RN)^templ)+4;
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)^templ;
                                                        setarmzn(armregs[RD]);
                                                }
                                                tubecycles--;
//                                        }
                                        break;

                                        case 0x04: /*SUB reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x05: /*SUBS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)-templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x06: /*RSB reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x07: /*RSBS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(templ-GETADDR(RN))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x08: /*ADD reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x09: /*ADDS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x0A: /*ADC reg*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0B: /*ADCS reg*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+templ2+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                setadc(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x0C: /*SBC reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0D: /*SBCS reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsbc(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0E: /*RSC reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((templ-(GETADDR(RN)+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0F: /*RSCS reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(templ-(GETADDR(RN)+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsbc(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x10: /*SWP word*/
                                        break;

                                        case 0x11: /*TST reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)&shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)&shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&shift(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x12:
                                        break;

                                        case 0x13: /*TEQ reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)^shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)^shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^shift(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x14: /*SWPB*/
                                        break;

                                        case 0x15: /*CMP reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)-shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)-shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setsub(GETADDR(RN),shift(opcode),GETADDR(RN)-shift2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x17: /*CMN reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)+shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)+shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setadd(GETADDR(RN),shift2(opcode),GETADDR(RN)+shift2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x18: /*ORR reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x19: /*ORRS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)|templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1A: /*MOV reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|((shift2(opcode)+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=shift2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x1B: /*MOVS reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3) armregs[15]=shift2(opcode)+4;
                                                else               armregs[15]=((shift2(opcode)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1C: /*BIC reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x1D: /*BICS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)&~templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1E: /*MVN reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~shift2(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~shift2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x1F: /*MVNS reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~shift2(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x20: /*AND imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)&templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x21: /*ANDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)&templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x22: /*EOR imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)^templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x23: /*EORS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)^templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)^templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x24: /*SUB imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x25: /*SUBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)-templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x26: /*RSB imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x27: /*RSBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(templ-GETADDR(RN))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x28: /*ADD imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)+templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x29: /*ADDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x2A: /*ADC imm*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2B: /*ADCS imm*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+templ2+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                setadc(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x2C: /*SBC imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2D: /*SBCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsbc(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2E: /*RSC imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((templ-(GETADDR(RN)+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2F: /*RSCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(templ-(GETADDR(RN)+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsbc(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
//                                                setsub(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x31: /*TST imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)&rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)&rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&rotate(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x33: /*TEQ imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^rotate(opcode));
//                                                bem_debugf("TEQ %08X %08X\n",GETADDR(RN),rotate(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x34:
                                        break;
                                        case 0x35: /*CMP imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)-rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)-rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setsub(GETADDR(RN),rotate2(opcode),GETADDR(RN)-rotate2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x37: /*CMN imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)+rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)+rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setadd(GETADDR(RN),rotate2(opcode),GETADDR(RN)+rotate2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x38: /*ORR imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x39: /*ORRS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                if (armregs[15]&3)
                                                   armregs[15]=(GETADDR(RN)|templ)+4;
                                                else
                                                   armregs[15]=(((GETADDR(RN)|templ)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3A: /*MOV imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|((rotate2(opcode)+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=rotate2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x3B: /*MOVS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=rotate2(opcode)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3C: /*BIC imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x3D: /*BICS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                if (armregs[15]&3) armregs[15]=(GETADDR(RN)&~templ)+4;
                                                else               armregs[15]=(((GETADDR(RN)&~templ)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3E: /*MVN imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~rotate2(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~rotate2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x3F: /*MVNS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~rotate2(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x47: case 0x4F: /*LDRBT*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=memmode;
                                        memmode=0;
                                        templ2=readarmb(addr);
                                        memmode=templ;
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        tubecycles-=4;
                                        break;

                                        case 0x41: case 0x49: case 0x61: case 0x69: /*LDR Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        templ2=ldrresult(readarml(addr),addr);
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=4;
                                        break;
                                        case 0x43: case 0x4B: case 0x63: case 0x6B: /*LDRT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        templ=memmode; memmode=0;
                                        templ2=ldrresult(readarml(addr),addr);
                                        memmode=templ;
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=4;
                                        break;

                                        case 0x40: case 0x48: case 0x60: case 0x68: /*STR Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        if (databort) break;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x42: case 0x4A: case 0x62: case 0x6A: /*STRT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        templ=memmode; memmode=0;
                                        if (databort) break;
                                        memmode=templ;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x50: case 0x58: case 0x70: case 0x78: /*STR Rd,[Rn,offset]*/
                                        case 0x52: case 0x5A: case 0x72: case 0x7A: /*STR Rd,[Rn,offset]!*/
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (opcode&0x800000) addr=GETADDR(RN)+addr2;
                                        else                 addr=GETADDR(RN)-addr2;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        if (databort) break;
                                        if (opcode&0x200000) armregs[RN]=addr;
                                        tubecycles-=3;
                                        break;

                                        case 0x44: case 0x4C: case 0x64: case 0x6C: /*STRB Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        writearmb(addr,armregs[RD]);
                                        if (databort) break;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x46: case 0x4E: case 0x66: case 0x6E: /*STRBT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        writearmb(addr,armregs[RD]);
                                        templ=memmode;
                                        memmode=0;
                                        if (databort) break;
                                        memmode=templ;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x54: case 0x5C: case 0x74: case 0x7C: /*STRB Rd,[Rn,offset]*/
                                        case 0x56: case 0x5E: case 0x76: case 0x7E: /*STRB Rd,[Rn,offset]!*/
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (opcode&0x800000) addr=GETADDR(RN)+addr2;
                                        else                 addr=GETADDR(RN)-addr2;
                                        writearmb(addr,armregs[RD]);
                                        if (databort) break;
                                        if (opcode&0x200000) armregs[RN]=addr;
                                        tubecycles-=3;
                                        break;


//                                        case 0x41: case 0x49: /*LDR*/
                                        case 0x51: case 0x53: case 0x59: case 0x5B:
//                                        case 0x61: case 0x69:
                                        case 0x71: case 0x73: case 0x79: case 0x7B:
                                        if ((opcode&0x2000010)==0x2000010)
                                        {
                                                undefined();
                                                break;
                                        }
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=readarml(addr);
                                        templ=ldrresult(templ,addr);
                                        if (databort) break;
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        LOADREG(RD,templ);
//                                        if (RD==15) refillpipeline();
                                        tubecycles-=4;
/*                                        if (RD==7)
                                        {
                                                if (!olog) olog=fopen("armlog.txt","wt");
                                                sbem_debugf(s,"LDR R7 %08X,%07X\n",armregs[7],PC);
                                                fputs(s,olog);
                                        }*/
                                        break;

                                        case 0x65: case 0x6D:
                                        case 0x75: case 0x77: case 0x7D: case 0x7F:
                                        if (opcode&0x10)
                                        {
                                                undefined();
                                                break;
                                        }
                                        case 0x45: case 0x4D: /*LDRB*/
                                        case 0x55: case 0x57: case 0x5D: case 0x5F:
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=readarmb(addr);
                                        if (databort) break;
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        armregs[RD]=templ;
                                        tubecycles-=4;
                                        break;

#define STMfirst()      mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        if (c==15) { writearml(addr,armregs[c]+4); } \
                                        else       { writearml(addr,armregs[c]); } \
                                        addr+=4; \
                                        tubecycles--; \
                                        break; \
                                } \
                                mask<<=1; \
                        } \
                        mask<<=1; c++;

#define STMall()        for (;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        writearml(addr,armregs[c]); \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                writearml(addr,armregs[15]+4); \
                                tubecycles--; \
                        }

#define STMfirstS()     mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        if (c==15) { writearml(addr,armregs[c]+4); } \
                                        else       { writearml(addr,*usrregs[c]); } \
                                        addr+=4; \
                                        tubecycles--; \
                                        break; \
                                } \
                                mask<<=1; \
                        } \
                        mask<<=1; c++;

#define STMallS()       for (;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        writearml(addr,*usrregs[c]); \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                writearml(addr,armregs[15]+4); \
                                tubecycles--; \
                        }

#define LDMall()        mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        templ=readarml(addr); if (!databort) armregs[c]=templ; \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                templ=readarml(addr); \
                                if (!databort) armregs[15]=(armregs[15]&0xFC000003)|((templ+4)&0x3FFFFFC); \
                                tubecycles--; \
                                refillpipeline(); \
                        }

#define LDMallS()       mask=1; \
                        if (opcode&0x8000) \
                        { \
                                for (c=0;c<15;c++) \
                                { \
                                        if (opcode&mask) \
                                        { \
                                                if (!(addr&0xC)) tubecycles--; \
                                                templ=readarml(addr); if (!databort) armregs[c]=templ; \
                                                addr+=4; \
                                                tubecycles--; \
                                        } \
                                        mask<<=1; \
                                } \
                                if (!(addr&0xC)) tubecycles--; \
                                templ=readarml(addr); \
                                if (!databort) \
                                { \
                                        if (armregs[15]&3) armregs[15]=(templ+4); \
                                        else               armregs[15]=(armregs[15]&0x0C000003)|((templ+4)&0xF3FFFFFC); \
                                } \
                                tubecycles--; \
                                refillpipeline(); \
                        } \
                        else \
                        { \
                                for (c=0;c<15;c++) \
                                { \
                                        if (opcode&mask) \
                                        { \
                                                if (!(addr&0xC)) tubecycles--; \
                                                templ=readarml(addr); if (!databort) *usrregs[c]=templ; \
                                                addr+=4; \
                                                tubecycles--; \
                                        } \
                                        mask<<=1; \
                                } \
                        }

                                        case 0x80: /*STMDA*/
                                        case 0x82: /*STMDA !*/
                                        case 0x90: /*STMDB*/
                                        case 0x92: /*STMDB !*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        STMfirst();
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        STMall()
                                        tubecycles--;
                                        break;
                                        case 0x88: /*STMIA*/
                                        case 0x8A: /*STMIA !*/
                                        case 0x98: /*STMIB*/
                                        case 0x9A: /*STMIB !*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        STMfirst();
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        STMall();
                                        tubecycles--;
                                        break;
                                        case 0x84: /*STMDA ^*/
                                        case 0x86: /*STMDA ^!*/
                                        case 0x94: /*STMDB ^*/
                                        case 0x96: /*STMDB ^!*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        STMfirstS();
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        STMallS()
                                        tubecycles--;
                                        break;
                                        case 0x8C: /*STMIA ^*/
                                        case 0x8E: /*STMIA ^!*/
                                        case 0x9C: /*STMIB ^*/
                                        case 0x9E: /*STMIB ^!*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        STMfirstS();
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        STMallS();
                                        tubecycles--;
                                        break;

                                        case 0x81: /*LDMDA*/
                                        case 0x83: /*LDMDA !*/
                                        case 0x91: /*LDMDB*/
                                        case 0x93: /*LDMDB !*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
//                                        bem_debugf("LDMDB %08X\n",addr);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        LDMall();
                                        tubecycles-=2;
                                        break;
                                        case 0x89: /*LDMIA*/
                                        case 0x8B: /*LDMIA !*/
                                        case 0x99: /*LDMIB*/
                                        case 0x9B: /*LDMIB !*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        LDMall();
                                        tubecycles-=2;
                                        break;
                                        case 0x85: /*LDMDA ^*/
                                        case 0x87: /*LDMDA ^!*/
                                        case 0x95: /*LDMDB ^*/
                                        case 0x97: /*LDMDB ^!*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        LDMallS();
                                        tubecycles-=2;
                                        break;
                                        case 0x8D: /*LDMIA ^*/
                                        case 0x8F: /*LDMIA ^!*/
                                        case 0x9D: /*LDMIB ^*/
                                        case 0x9F: /*LDMIB ^!*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        LDMallS();
                                        tubecycles-=2;
                                        break;

                                        case 0xB0: case 0xB1: case 0xB2: case 0xB3: /*BL*/
                                        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                                        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
                                        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[14]=armregs[15]-4;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        tubecycles-=4;
                                        break;

                                        case 0xA0: case 0xA1: case 0xA2: case 0xA3: /*B*/
                                        case 0xA4: case 0xA5: case 0xA6: case 0xA7:
                                        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                                        case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        tubecycles-=4;
                                        break;

                                        case 0xE0: case 0xE2: case 0xE4: case 0xE6: /*MCR*/
                                        case 0xE8: case 0xEA: case 0xEC: case 0xEE:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xE1: case 0xE3: case 0xE5: case 0xE7: /*MRC*/
                                        case 0xE9: case 0xEB: case 0xED: case 0xEF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xC0: case 0xC1: case 0xC2: case 0xC3: /*Co-pro*/
                                        case 0xC4: case 0xC5: case 0xC6: case 0xC7:
                                        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
                                        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                                        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
                                        case 0xD4: case 0xD5: case 0xD6: case 0xD7:
                                        case 0xD8: case 0xD9: case 0xDA: case 0xDB:
                                        case 0xDC: case 0xDD: case 0xDE: case 0xDF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xF0: case 0xF1: case 0xF2: case 0xF3: /*SWI*/
                                        case 0xF4: case 0xF5: case 0xF6: case 0xF7:
                                        case 0xF8: case 0xF9: case 0xFA: case 0xFB:
                                        case 0xFC: case 0xFD: case 0xFE: case 0xFF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x0800000C;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        default:
                                        break;
                                }
                        }
                        if (databort|armirq|tube_irq)
                        {
                                if (databort==1)     /*Data abort*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000014;
                                        refillpipeline();
                                        databort=0;
                                }
                                else if (databort==2) /*Address Exception*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000018;
                                        refillpipeline();
                                        databort=0;
                                }
                                else if ((armirq&2) && !(armregs[15]&0x4000000)) /*FIQ*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(FIQ);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000001;
                                        armregs[15]|=0x0C000020;
                                        refillpipeline();
//                                        bem_debug("FIQ\n");
                                }
                                else if ((armirq&1) && !(armregs[15]&0x8000000)) /*IRQ*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(IRQ);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000002;
                                        armregs[15]|=0x0800001C;
                                        refillpipeline();
//                                        bem_debug("IRQ\n");
                                }
                        }
//                if (armregs[12]==0x1000000) bem_debugf("R12=1000000 %08X  %i\n",PC,armins);
                armirq=tube_irq;
                if ((armregs[15]&3)!=mode) updatemode(armregs[15]&3);
                armregs[15]+=4;
//                bem_debugf("%08X : %08X %08X %08X  %08X\n",PC,armregs[0],armregs[1],armregs[2],opcode);
/*                if (!PC)
                {
                        bem_debug("Branch through zero\n");
                        dumpregs();
                        exit(-1);
                }*/
                if (endtimeslice)
                {
                        endtimeslice=0;
                        return;
                }
//                if (output && !(*armregs[15]&0x8000000) && PC<0x2000000) bem_debugf("%07X : %08X %08X %08X %08X %08X %08X %08X %08X\n%08i: %08X %08X %08X %08X %08X %08X %08X %08X\n",PC,*armregs[0],*armregs[1],*armregs[2],*armregs[3],*armregs[4],*armregs[5],*armregs[6],*armregs[7],inscount,*armregs[8],*armregs[9],*armregs[10],*armregs[11],*armregs[12],*armregs[13],*armregs[14],*armregs[15]);
        }
}
