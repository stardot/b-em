/*B-em 1.1 by Tom Walker
  ARM emulation (for ARM tube)*/

#include <stdio.h>
#include "b-em.h"
#include "arm.h"

unsigned long opcode2,opcode3;
int armirq=0;
unsigned long *armrom,*armram;
unsigned char *armromb,*armramb;
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
                                        cycles--

/*0=i/o, 1=all, 2=r/o, 3=os r/o, 4=super only, 5=read mem, write io*/
/*0=user, 1=os, 2=super*/
int modepritabler[3][6]=
{
        {0,1,1,0,0,1},
        {0,1,1,1,0,1},
        {0,1,1,1,1,1}
};
int modepritablew[3][6]=
{
        {0,1,0,0,0,0},
        {0,1,1,0,0,0},
        {0,1,1,1,1,0}
};

int mode;
int osmode=0,memmode;

void updatemode(int m)
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

unsigned char flaglookup[16][16];
unsigned long rotatelookup[4096];

void refillpipeline2();
void resetarm()
{
        int c,d,exec,data;
        unsigned long rotval,rotamount;
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

void dumparmregs()
{
        int c;
        FILE *f=fopen("armram.dmp","wb");
        fwrite(armram,0x10000,1,f);
        fclose(f);
        printf("R 0=%08X R 4=%08X R 8=%08X R12=%08X\n",armregs[0],armregs[4],armregs[8],armregs[12]);
        printf("R 1=%08X R 5=%08X R 9=%08X R13=%08X\n",armregs[1],armregs[5],armregs[9],armregs[13]);
        printf("R 2=%08X R 6=%08X R10=%08X R14=%08X\n",armregs[2],armregs[6],armregs[10],armregs[14]);
        printf("R 3=%08X R 7=%08X R11=%08X R15=%08X\n",armregs[3],armregs[7],armregs[11],armregs[15]);
        printf("f12=%08X  ",fiqregs[12]);
        printf("PC =%07X\n",PC);
}

unsigned long *armread[64];
void loadarmrom()
{
        FILE *f;
        int c;
        armrom=(unsigned long *)malloc(0x4000);
        armram=(unsigned long *)malloc(0x400000);
        armromb=(unsigned char *)armrom;
        armramb=(unsigned char *)armram;
        f=fopen("roms/tube/armeval_100.rom","rb");
        fread(armromb,0x4000,1,f);
        fclose(f);
        memcpy(armramb,armromb,0x4000);
        for (c=0;c<64;c++) armread[c]=0;
        for (c=0;c<4;c++) armread[c]=&armram[c*0x40000];
        armread[48]=armrom;
}

int endtimeslice=0;
#define readarml(a) ((armread[(a>>20)&63])?armread[(a>>20)&63][(a&0xFFFFF)>>2]:readarmfl(a))

unsigned char readarmb(unsigned long addr)
{
        if (addr<0x400000) return armramb[addr];
        if ((addr&~0x1F)==0x1000000)
        {
                //printf("Read %08X\n",addr);
                return readtube((addr&0x1C)>>2,NULL);
        }
        if ((addr>=0x3000000) && (addr<0x3004000)) return armromb[addr&0x3FFF];
        printf("Bad ARM read byte %08X\n",addr);
        dumparmregs();
        exit(-1);
}
unsigned long readarmfl(unsigned long addr)
{
        if (addr<0x400000) return armram[addr>>2];
        if (addr<0x400010) return 0xFFFFFFFF;
        if ((addr>=0x3000000) && (addr<0x3004000)) return armrom[(addr&0x3FFC)>>2];
        printf("Bad ARM read long %08X\n",addr);
        dumparmregs();
        exit(-1);
}

void writearmb(unsigned long addr, unsigned char val)
{
        if (addr<0x400000)
        {
                armramb[addr]=val;
                return;
        }
        if ((addr&~0x1F)==0x1000000)
        {
//                printf("Write %08X %02X\n",addr,val);
                writetube((addr&0x1C)>>2,val,NULL);
                endtimeslice=1;
                return;
        }
        printf("Bad ARM write byte %08X %02X\n",addr,val);
        dumparmregs();
        exit(-1);
}
void writearml(unsigned long addr, unsigned long val)
{
        if (addr<0x400000)
        {
                armram[addr>>2]=val;
                return;
        }
        if (addr<0x400010) return;
        printf("Bad ARM write long %08X %08X\n",addr,val);
        dumparmregs();
        exit(-1);
}

#define checkneg(v) (v&0x80000000)
#define checkpos(v) !(v&0x80000000)

inline void setadd(unsigned long op1, unsigned long op2, unsigned long res)
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

inline void setsub(unsigned long op1, unsigned long op2, unsigned long res)
{
        char s[80];
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

inline void setarmzn(unsigned long op)
{
        armregs[15]&=0x3FFFFFFF;
        if (!op)               armregs[15]|=ZFLAG;
        else if (checkneg(op)) armregs[15]|=NFLAG;
}

inline unsigned long shift(unsigned long opcode)
{
        unsigned long shiftmode=(opcode>>5)&3;
        unsigned long shiftamount=(opcode>>7)&31;
        unsigned long temp;
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
                break;
                default:
                printf("Shift mode %i amount %i\n",shiftmode,shiftamount);
                dumpregs();
                exit(-1);
        }
}

inline unsigned shift2(unsigned opcode)
{
        unsigned shiftmode=(opcode>>5)&3;
        unsigned shiftamount=(opcode>>7)&31;
        unsigned long temp;
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
                break;

                default:
                printf("Shift2 mode %i amount %i\n",shiftmode,shiftamount);
                dumpregs();
                exit(-1);
        }
}

inline unsigned rotate(unsigned data)
{
        unsigned long rotval;
        rotval=rotatelookup[data&4095];
        if (data&0x100000 && data&0xF00)
        {
                if (rotval&0x80000000) armregs[15]|=CFLAG;
                else                   armregs[15]&=~CFLAG;
        }
        return rotval;
}

#define rotate2(v) rotatelookup[v&4095]

int ldrlookup[4]={0,8,16,24};

#define ldrresult(v,a) ((v>>ldrlookup[addr&3])|(v<<(32-ldrlookup[addr&3])))

void refillpipeline()
{
        opcode2=readarml(PC-4);
        opcode3=readarml(PC);
}

void refillpipeline2()
{
        opcode2=readarml(PC-8);
        opcode3=readarml(PC-4);
}

int accc=0;
void execarm(int cycles)
{
        unsigned long opcode,templ,templ2,mask,addr,addr2;
        int exec,c;
        unsigned char temp;
        FILE *f;
        while (cycles>0)
        {
                opcode=opcode2;
                opcode2=opcode3;
                opcode3=readarml(PC);
                        if (flaglookup[opcode>>28][armregs[15]>>28])
                        {
                                switch ((opcode>>20)&0xFF)
                                {
                                        case 0x00: /*AND reg*/
                                        if (((opcode&0xE000090)==0x90)) /*MUL*/
                                        {
                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
                                                cycles-=17;
                                        }
                                        else
                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift(opcode);
                                                        armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                }
                                                cycles--;
                                        }
                                        break;
                                        case 0x01: /*ANDS reg*/
                                        if (((opcode&0xE000090)==0x90)) /*MULS*/
                                        {
                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
                                                setarmzn(armregs[MULRD]);
                                                cycles-=17;
                                        }
                                        else
                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift(opcode);
                                                        armregs[15]=(GETADDR(RN)&templ)+4;
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                        setarmzn(armregs[RD]);
                                                }
                                                cycles--;
                                        }
                                        break;

                                        case 0x02: /*EOR reg*/
                                        if (((opcode&0xE000090)==0x90)) /*MLA*/
                                        {
                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
                                                cycles-=17;
                                        }
                                        else
                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift(opcode);
                                                        armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)^templ;
                                                }
                                                cycles--;
                                        }
                                        break;
                                        case 0x03: /*EORS reg*/
                                        if (((opcode&0xE000090)==0x90)) /*MLA*/
                                        {
                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
                                                setarmzn(armregs[MULRD]);
                                                cycles-=17;
                                        }
                                        else
                                        {
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
                                                cycles--;
                                        }
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
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        cycles--;
                                        break;

                                        case 0x06: /*RSB reg*/
                                        if (RD==15)
                                        {
                                                templ=shift(opcode);
                                                armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        cycles--;
                                        break;

                                        case 0x08: /*ADD reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
        //                                        printf("R15=%08X+%08X+4=",GETADDR(RN),templ);
                                                armregs[15]=((GETADDR(RN)+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
        //                                        printf("%08X\n",armregs[15]);
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        cycles--;
                                        break;
                                        case 0x09: /*ADDS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
        //                                        printf("R15=%08X+%08X+4=",GETADDR(RN),templ);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
        //                                        printf("%08X\n",armregs[15]);
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
        //                                        printf("ADDS %08X+%08X = ",GETADDR(RN),templ);
                                                armregs[RD]=GETADDR(RN)+templ;
        //                                        printf("%08X\n",armregs[RD]);
        //                                        setarmzn(templ);
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        cycles--;
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
                                                templ=shift(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        cycles--;
                                        break;

                                        case 0x10: /*SWP word*/
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        cycles-=3;
                                        refillpipeline();
                                        break;

                                        case 0x11: /*TST reg*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                templ=armregs[15]&0x3FFFFFC;
                                                armregs[15]=((GETADDR(RN)&shift2(opcode))&0xFC000003)|templ;
//                                                refillpipeline();
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&shift(opcode));
                                        }
                                        cycles--;
                                        break;

                                        case 0x12: /*SWP byte*/
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        cycles-=3;
                                        refillpipeline();
                                        break;

                                        case 0x13: /*TEQ reg*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                templ=armregs[15]&0x3FFFFFC;
                                                armregs[15]=((GETADDR(RN)^shift(opcode))&0xFC000003)|templ;
//                                                refillpipeline();
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^shift(opcode));
                                        }
                                        cycles--;
                                        break;

                                        case 0x15: /*CMP reg*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                armregs[15]&=0x3FFFFFC;
                                                armregs[15]|=((GETADDR(RN)-shift(opcode))&0xFC000003);
//                                                refillpipeline();
                                        }
                                        else
                                           setsub(GETADDR(RN),shift(opcode),GETADDR(RN)-shift(opcode));
                                        cycles--;
                                        break;

                                        case 0x17: /*CMN reg*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                armregs[15]&=0x3FFFFFC;
                                                armregs[15]|=((GETADDR(RN)+shift2(opcode))&0xFC000003);
//                                                refillpipeline();
                                        }
                                        else
                                           setadd(GETADDR(RN),shift2(opcode),GETADDR(RN)+shift2(opcode));
                                        cycles--;
                                        break;

                                        case 0x18: /*ORR reg*/
                                        if (RD==15)
                                        {
                                                templ=shift(opcode);
                                                armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                        }
                                        cycles--;
                                        break;
                                        case 0x19: /*ORRS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift(opcode);
                                                armregs[15]=(GETADDR(RN)|templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
                                        break;

                                        case 0x1A: /*MOV reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|((shift(opcode)+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=shift(opcode);
                                        cycles--;
                                        break;
                                        case 0x1B: /*MOVS reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=shift(opcode)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
                                        break;

                                        case 0x1C: /*BIC reg*/
                                        if (RD==15)
                                        {
                                                templ=shift(opcode);
                                                armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                        }
                                        cycles--;
                                        break;
                                        case 0x1D: /*BICS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift(opcode);
                                                armregs[15]=(GETADDR(RN)&~templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
                                        break;

                                        case 0x1E: /*MVN reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~shift(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~shift(opcode);
                                        cycles--;
                                        break;
                                        case 0x1F: /*MVNS reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~shift(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x21: /*ANDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(GETADDR(RN)&templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x23: /*EORS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(GETADDR(RN)^templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)^templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x25: /*SUBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(GETADDR(RN)-templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x27: /*RSBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(templ-GETADDR(RN))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x29: /*ADDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x2B: /*ADCS imm*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=rotate(opcode);
                                                armregs[15]=GETADDR(RN)+templ+templ2+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=rotate(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x2D: /*SBCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        cycles--;
                                        break;
                                        case 0x2E: /*RSC imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(((templ-(GETADDR(RN)+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        cycles--;
                                        break;
                                        case 0x2F: /*RSCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(templ-(GETADDR(RN)+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                setsub(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        cycles--;
                                        break;

                                        case 0x31: /*TST imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                templ=armregs[15]&0x3FFFFFC;
                                                armregs[15]=((GETADDR(RN)&rotate2(opcode))&0xFC000003)|templ;
//                                                refillpipeline();
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&rotate(opcode));
                                        }
                                        cycles--;
                                        break;

                                        case 0x33: /*TEQ imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xFC000003)|templ;
//                                                        if (!olog) olog=fopen("armlog.txt","wt");
//                                                        sprintf(s,"TEQP %08X %i\n",armregs[15],getline());
//                                                        fputs(s,olog);
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xF0000000)|templ;
                                                }
//                                                refillpipeline();
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^rotate(opcode));
                                        }
                                        cycles--;
                                        break;

                                        case 0x35: /*CMP imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                armregs[15]&=0x3FFFFFC;
                                                armregs[15]|=((GETADDR(RN)-rotate2(opcode))&0xFC000003);
//                                                refillpipeline();
                                        }
                                        else
                                           setsub(GETADDR(RN),rotate(opcode),GETADDR(RN)-rotate(opcode));
                                        cycles--;
                                        break;

                                        case 0x37: /*CMN imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                armregs[15]&=0x3FFFFFC;
                                                armregs[15]|=((GETADDR(RN)+rotate2(opcode))&0xFC000003);
//                                                refillpipeline();
                                        }
                                        else
                                           setadd(GETADDR(RN),rotate(opcode),GETADDR(RN)+rotate(opcode));
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x39: /*ORRS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
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
                                        cycles--;
                                        break;

                                        case 0x3A: /*MOV imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(rotate2(opcode)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=rotate2(opcode);
                                        cycles--;
                                        break;
                                        case 0x3B: /*MOVS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=rotate(opcode)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
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
                                        cycles--;
                                        break;
                                        case 0x3D: /*BICS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate(opcode);
                                                armregs[15]=(GETADDR(RN)&~templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
                                        break;

                                        case 0x3E: /*MVN imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~rotate2(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~rotate2(opcode);
                                        cycles--;
                                        break;
                                        case 0x3F: /*MVNS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~rotate(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        cycles--;
                                        break;

                                        case 0x4A: /*STRT*/
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
                                        writearml(addr,armregs[RD]);
                                        memmode=templ;
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
                                        cycles-=3;
                                        break;

                                        case 0x4B: /*LDRT*/
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
                                        templ2=readarml(addr);
                                        memmode=templ;
                                        if (databort) break;
                                        templ2=ldrresult(templ2,addr);
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
                                        cycles-=4;
                                        break;

                                        case 0x47: /*LDRBT*/
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
                                        cycles-=4;
                                        break;

                                        case 0x60: case 0x68:
                                        case 0x70: case 0x72: case 0x78: case 0x7A:
                                        case 0x40: case 0x48: /*STR*/
                                        case 0x50: case 0x52: case 0x58: case 0x5A:
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
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
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
                                        cycles-=3;
                                        break;

                                        case 0x41: case 0x49: /*LDR*/
                                        case 0x51: case 0x53: case 0x59: case 0x5B:
                                        case 0x61: case 0x69:
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
                                        cycles-=4;
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
                                        cycles-=4;
                                        break;

                                        case 0x64: case 0x6C:
                                        case 0x74: case 0x76: case 0x7C: case 0x7E:
                                        if (opcode&0x10)
                                        {
                                                undefined();
                                                break;
                                        }
                                        case 0x44: case 0x4C: /*STRB*/
                                        case 0x54: case 0x56: case 0x5C: case 0x5E:
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        writearmb(addr,armregs[RD]);
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
                                        cycles-=3;
                                        break;

                                        case 0x80: /*STMDA*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        addr-=4;
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x81: /*LDMDA*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        addr-=4;
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x82: /*STMDA !*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        addr-=4;
                                                        armregs[RN]-=4;
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x83: /*LDMDA !*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        addr-=4;
                                                        armregs[RN]-=4;
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x85: /*LDMDA ^*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        if (opcode&0x8000)
                                        {
                                                for (c=15;c>-1;c--)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                if (c==15 && !(armregs[15]&3))
                                                                   armregs[15]=(readarml(addr)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                                else
                                                                   armregs[c]=readarml(addr);
                                                                addr-=4;
                                                                cycles--;
                                                        }
                                                        mask>>=1;
                                                }
                                                armregs[15]+=4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                for (c=15;c>-1;c--)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                *usrregs[c]=readarml(addr);
                                                                addr-=4;
                                                                cycles--;
                                                        }
                                                        mask>>=1;
                                                }
                                        }
                                        cycles-=3;
                                        break;

                                        case 0x88: /*STMIA*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        addr+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x89: /*LDMIA*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        addr+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x8A: /*STMIA !*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        addr+=4;
                                                        armregs[RN]+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x8B: /*LDMIA !*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        addr+=4;
                                                        armregs[RN]+=4;
                                                        cycles--;
                                                }
                                                        mask<<=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x8C: /*STMIA ^*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        if (c==15) { writearml(addr,*usrregs[c]+4); }
                                                        else       { writearml(addr,*usrregs[c]); }
                                                        addr+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x8D: /*LDMIA ^*/
                                        mask=1;
                                        addr=armregs[RN];
                                        if (opcode&0x8000)
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                if (c==15 && !(armregs[15]&3))
                                                                   armregs[15]=(readarml(addr)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                                else
                                                                   armregs[c]=readarml(addr);
                                                                addr+=4;
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                                armregs[15]+=4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                *usrregs[c]=readarml(addr);
                                                                addr+=4;
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                        }
                                        cycles-=3;
                                        break;

                                        case 0x8F: /*LDMIA !^*/
                                        mask=1;
                                        addr=armregs[RN];
                                        if (opcode&0x8000)
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                if (c==15 && !(armregs[15]&3))
                                                                   armregs[15]=(readarml(addr)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                                else
                                                                   armregs[c]=readarml(addr);
                                                                addr+=4;
                                                                armregs[RN]+=4;
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                                armregs[15]+=4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                *usrregs[c]=readarml(addr);
                                                                addr+=4;
                                                                armregs[RN]+=4;
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                        }
                                        cycles-=3;
                                        break;

                                        case 0x90: /*STMDB*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x91: /*LDMDB*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x92: /*STMDB !*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        templ=0;
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        armregs[RN]-=4;
                                                        if (c==15)                { writearml(addr,armregs[c]+4); }
                                                        else if (c==RN && !templ) { writearml(addr,armregs[c]+4); }
                                                        else                      { writearml(addr,armregs[c]); templ=1;}
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x93: /*LDMDB !*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        armregs[RN]-=4;
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x94: /*STMDB ^*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        if (c==15) { writearml(addr,*usrregs[c]+4); }
                                                        else       { writearml(addr,*usrregs[c]); }
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x95: /*LDMDB ^*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        if (opcode&0x8000)
                                        {
                                                for (c=15;c>-1;c--)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                addr-=4;
                                                                if (c==15 && !(armregs[15]&3))
                                                                   armregs[15]=(readarml(addr)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                                else
                                                                   armregs[c]=readarml(addr);
                                                                cycles--;
                                                        }
                                                        mask>>=1;
                                                }
                                                armregs[15]+=4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                for (c=15;c>-1;c--)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                addr-=4;
                                                                *usrregs[c]=readarml(addr);
                                                                cycles--;
                                                        }
                                                        mask>>=1;
                                                }
                                        }
                                        cycles-=3;
                                        break;

                                        case 0x96: /*STMDB !^*/
                                        mask=0x8000;
                                        addr=armregs[RN];
                                        templ=0;
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        armregs[RN]-=4;
                                                        if (c==15)                { writearml(addr,*usrregs[c]+4); }
                                                        else if (c==RN && !templ) { writearml(addr,*usrregs[c]+4); }
                                                        else                      { writearml(addr,*usrregs[c]); }
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        cycles-=2;
                                        break;
                                        case 0x98: /*STMIB*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        if (c==15) { writearml(addr,armregs[c]+4); }
                                                        else       { writearml(addr,armregs[c]); }
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x99: /*LDMIB*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        if (c==15) armregs[15]=(armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                        else       armregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        if (opcode&0x8000) refillpipeline();
                                        cycles-=3;
                                        break;

                                        case 0x9A: /*STMIB !*/
                                        mask=1;
                                        addr=armregs[RN];
                                        templ=0;
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        armregs[RN]+=4;
                                                        if (c==15)                { writearml(addr,armregs[c]+4); }
                                                        else if (c==RN && !templ) { writearml(addr,armregs[c]-4); }
                                                        else                      { writearml(addr,armregs[c]); }
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x9C: /*STMIB ^*/
                                        mask=1;
                                        addr=armregs[RN];
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        if (c==15) { writearml(addr,*usrregs[c]+4); }
                                                        else       { writearml(addr,*usrregs[c]); }
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        cycles-=2;
                                        break;

                                        case 0x9D: /*LDMIB ^*/
                                        mask=1;
                                        addr=armregs[RN];
                                        if (opcode&0x8000)
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                addr+=4;
                                                                if (c==15 && !(armregs[15]&3))
                                                                   armregs[15]=(readarml(addr)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                                else
                                                                   armregs[c]=readarml(addr);
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                                armregs[15]+=4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        if (opcode&mask)
                                                        {
                                                                addr+=4;
                                                                *usrregs[c]=readarml(addr);
                                                                cycles--;
                                                        }
                                                        mask<<=1;
                                                }
                                        }
                                        cycles-=3;
                                        break;

                                        case 0xB0: case 0xB1: case 0xB2: case 0xB3: /*BL*/
                                        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                                        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
                                        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[14]=armregs[15]-4;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        cycles-=4;
                                        break;

                                        case 0xA0: case 0xA1: case 0xA2: case 0xA3: /*B*/
                                        case 0xA4: case 0xA5: case 0xA6: case 0xA7:
                                        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                                        case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        cycles-=4;
                                        break;

                                        case 0xE0: case 0xE2: case 0xE4: case 0xE6: /*MCR*/
                                        case 0xE8: case 0xEA: case 0xEC: case 0xEE:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        cycles-=4;
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
                                        cycles-=4;
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
                                        cycles-=4;
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
                                        cycles-=4;
                                        refillpipeline();
                                        break;

                                        default:
                                        printf("Bad opcode %02X %08X at %07X\n",(opcode>>20)&0xFF,opcode,PC);
                                        dumpregs();
                                        exit(-1);
                                }
                        }
                        if (databort|armirq|tubeirq)
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
//                                        printf("FIQ\n");
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
//                                        printf("IRQ\n");
                                }
                        }
//                if (armregs[12]==0x1000000) printf("R12=1000000 %08X  %i\n",PC,armins);
                armirq=tubeirq;
                if ((armregs[15]&3)!=mode) updatemode(armregs[15]&3);
                armregs[15]+=4;
//                printf("%08X : %08X %08X %08X  %08X\n",PC,armregs[0],armregs[1],armregs[2],opcode);
/*                if (!PC)
                {
                        printf("Branch through zero\n");
                        dumpregs();
                        exit(-1);
                }*/
                if (endtimeslice)
                {
                        endtimeslice=0;
                        return;
                }
//                if (output && !(*armregs[15]&0x8000000) && PC<0x2000000) printf("%07X : %08X %08X %08X %08X %08X %08X %08X %08X\n%08i: %08X %08X %08X %08X %08X %08X %08X %08X\n",PC,*armregs[0],*armregs[1],*armregs[2],*armregs[3],*armregs[4],*armregs[5],*armregs[6],*armregs[7],inscount,*armregs[8],*armregs[9],*armregs[10],*armregs[11],*armregs[12],*armregs[13],*armregs[14],*armregs[15]);
        }
}
