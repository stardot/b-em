/*B-em 0.82 by Tom Walker
  ARM emulation (for ARM tube)*/

#include <stdio.h>
#include "arm.h"

int tubeirq;
int armirq=0;
unsigned long *armrom,*armram;
unsigned char *armromb,*armramb;
#define USER       0
#define FIQ        1
#define IRQ        2
#define SUPERVISOR 3

#define NFSET ((*armregs[15]&0x80000000)?1:0)
#define ZFSET ((*armregs[15]&0x40000000)?1:0)
#define CFSET ((*armregs[15]&0x20000000)?1:0)
#define VFSET ((*armregs[15]&0x10000000)?1:0)

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

#define GETADDR(r) ((r==15)?(*armregs[15]&0x3FFFFFC):*armregs[r])
#define LOADREG(r,v) if (r==15) *armregs[15]=(*armregs[15]&0xFC000003)|((v+4)&0x3FFFFFC); else *armregs[r]=v;
#define GETREG(r) ((r==15) ? *armregs[15]+4 : *armregs[r])
#define LDRRESULT(a,v) ((a&3)?(v>>((a&3)<<3))|(v<<(((a&3)^3)<<3)):v)

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
        mode=m;
        switch (m)
        {
                case USER:
                for (c=0;c<16;c++) armregs[c]=&userregs[c];
                memmode=osmode;
                break;
                case IRQ:
                for (c=0;c<16;c++) armregs[c]=&userregs[c];
                armregs[13]=&irqregs[13];
                armregs[14]=&irqregs[14];
                memmode=2;
                break;
                case FIQ:
                for (c=0;c<16;c++) armregs[c]=&userregs[c];
                for (c=8;c<15;c++) armregs[c]=&fiqregs[c];
                memmode=2;
                break;
                case SUPERVISOR:
                for (c=0;c<16;c++) armregs[c]=&userregs[c];
                armregs[13]=&superregs[13];
                armregs[14]=&superregs[14];
                memmode=2;
                break;
        }
}

void resetarm()
{
        updatemode(SUPERVISOR);
        *armregs[15]=0x0C00000B;
        mode=3;
        memmode=2;
        memcpy(armramb,armromb,0x4000);
}

void dumparmregs()
{
        int c;
        FILE *f=fopen("armram.dmp","wb");
        fwrite(armram,0x10000,1,f);
        fclose(f);
        printf("R 0=%08X R 4=%08X R 8=%08X R12=%08X\n",*armregs[0],*armregs[4],*armregs[8],*armregs[12]);
        printf("R 1=%08X R 5=%08X R 9=%08X R13=%08X\n",*armregs[1],*armregs[5],*armregs[9],*armregs[13]);
        printf("R 2=%08X R 6=%08X R10=%08X R14=%08X\n",*armregs[2],*armregs[6],*armregs[10],*armregs[14]);
        printf("R 3=%08X R 7=%08X R11=%08X R15=%08X\n",*armregs[3],*armregs[7],*armregs[11],*armregs[15]);
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
        *armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkneg(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkneg(op2) && checkpos(res)))  *armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkneg(op2) && checkpos(res)) ||
            (checkpos(op1) && checkpos(op2) && checkneg(res)))
            *armregs[15]|=VFLAG;
        if (!res)                          *armregs[15]|=ZFLAG;
        else if (checkneg(res))            *armregs[15]|=NFLAG;
}

inline void setsub(unsigned long op1, unsigned long op2, unsigned long res)
{
        char s[80];
        *armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkpos(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkpos(op2) && checkpos(res)))  *armregs[15]|=CFLAG;
        if (!res)               *armregs[15]|=ZFLAG;
        else if (checkneg(res)) *armregs[15]|=NFLAG;
        if ((checkneg(op1) && checkpos(op2) && checkpos(res)) ||
            (checkpos(op1) && checkneg(op2) && checkneg(res)))
            *armregs[15]|=VFLAG;
}

inline void setarmzn(unsigned long op)
{
        *armregs[15]&=0x3FFFFFFF;
        if (!op)               *armregs[15]|=ZFLAG;
        else if (checkneg(op)) *armregs[15]|=NFLAG;
}

inline unsigned long shift(unsigned long opcode)
{
        unsigned long shiftmode=(opcode>>5)&3;
        unsigned long shiftamount=(opcode>>7)&31;
        unsigned long temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return *armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=*armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
        }
        temp=*armregs[RM];
        if (RM==15)        temp+=4;
        if (opcode&0x100000 && shiftamount) *armregs[15]&=~CFLAG;
        switch (shiftmode)
        {
                case 0: /*LSL*/
                if (!shiftamount) return temp;
                if (shiftamount==32)
                {
                        if (temp&1 && opcode&0x100000) *armregs[15]|=CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp<<(shiftamount-1))&0x80000000) *armregs[15]|=CFLAG;
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
                        if (temp&0x80000000 && opcode&0x100000) *armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               *armregs[15]&=~CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp>>(shiftamount-1))&1) *armregs[15]|=CFLAG;
                }
                return temp>>shiftamount;

                case 2: /*ASR*/
                if (!shiftamount)
                {
                        if (opcode&0x10) return temp;
                }
                if (shiftamount>=32 || !shiftamount)
                {
                        if (temp&0x80000000 && opcode&0x100000) *armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               *armregs[15]&=~CFLAG;
                        if (temp&0x80000000) return 0xFFFFFFFF;
                        return 0;
                }
                if (opcode&0x100000)
                {
                        if ((temp>>(shiftamount-1))&1) *armregs[15]|=CFLAG;
                }
                return (int)temp>>shiftamount;

                case 3: /*ROR*/
                if (opcode&0x100000) *armregs[15]&=~CFLAG;
                if (!shiftamount && !(opcode&0x10))
                {
                        if (opcode&0x100000 && temp&1) *armregs[15]|=CFLAG;
                        return (((cflag)?1:0)<<31)|(temp>>1);
                }
                if (!shiftamount)
                {
                        if (opcode&0x100000) *armregs[15]|=cflag;
                        return temp;
                }
                if (!(shiftamount&0x1F))
                {
                        if (opcode&0x100000 && temp&0x80000000) *armregs[15]|=CFLAG;
                        return temp;
                }
                if (opcode&0x100000)
                {
                        if (((temp>>shiftamount)|(temp<<(32-shiftamount)))&0x80000000) *armregs[15]|=CFLAG;
                }
                return (temp>>shiftamount)|(temp<<(32-shiftamount));
                break;
                default:
                printf("Shift mode %i amount %i\n",shiftmode,shiftamount);
                dumparmregs();
                exit(-1);
        }
}

inline unsigned shift2(unsigned opcode)
{
        unsigned shiftmode=(opcode>>5)&3;
        unsigned shiftamount=(opcode>>7)&31;
        unsigned long temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return *armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=*armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
        }
        temp=*armregs[RM];
        if (RM==15) temp+=4;
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
                dumparmregs();
                exit(-1);
        }
}

inline unsigned rotate(unsigned data)
{
        unsigned rotval=data&0xFF;
        unsigned rotamount=((data>>8)&0xF)<<1;
        rotval=(rotval>>rotamount)|(rotval<<(32-rotamount));
        if (data&0x100000 && rotamount)
        {
                if (rotval&0x80000000) *armregs[15]|=CFLAG;
                else                   *armregs[15]&=~CFLAG;
        }
        return rotval;
}

int ldrlookup[4]={0,8,16,24};

#define ldrresult(v,a) ((v>>ldrlookup[addr&3])|(v<<(32-ldrlookup[addr&3])))

int accc=0;
void execarm(int cycles)
{
        unsigned long opcode,templ,templ2,mask,addr,addr2;
        int exec,c;
        unsigned char temp;
        FILE *f;
        while (cycles>0)
        {
                opcode=readarml(PC);
                switch (opcode>>28)
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
                if (exec && ((opcode&0xE000090)==0x90))
                {
                        switch ((opcode>>20)&0xFF)
                        {
                                case 0x00: /*MUL*/
                                *armregs[MULRD]=(*armregs[MULRM])*(*armregs[MULRS]);
                                cycles-=32;
                                break;
                                case 0x01: /*MULS*/
                                *armregs[MULRD]=(*armregs[MULRM])*(*armregs[MULRS]);
                                setarmzn(*armregs[MULRD]);
                                cycles-=32;
                                break;
                                case 0x02: /*MLA*/
                                *armregs[MULRD]=((*armregs[MULRM])*(*armregs[MULRS]))+*armregs[MULRN];
                                cycles-=32;
                                break;

                                default:
                                printf("Bad ext opcode %02X %08X at %07X\n",(opcode>>20)&0xFF,opcode,PC);
                                dumpregs();
                                exit(-1);
                        }
                }
                else if (exec)
                {
                        switch ((opcode>>20)&0xFF)
                        {
                                case 0x00: /*AND reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)&templ;
                                }
                                cycles--;
                                break;
                                case 0x01: /*ANDS reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(GETADDR(RN)&templ)+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)&templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x02: /*EOR reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)^templ;
                                }
                                cycles--;
                                break;
                                case 0x03: /*EORS reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=(GETADDR(RN)^templ)+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)^templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x04: /*SUB reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
//                                        printf("R15=%08X-%08X+4=",GETADDR(RN),templ);
                                        *armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
//                                        printf("%08X\n",*armregs[15]);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)-templ;
                                }
                                cycles--;
                                break;
                                case 0x05: /*SUBS reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=(GETADDR(RN)-templ)+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                        *armregs[RD]=GETADDR(RN)-templ;
                                }
                                cycles--;
                                break;

                                case 0x06: /*RSB reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=templ-GETADDR(RN);
                                }
                                cycles--;
                                break;
                                case 0x07: /*RSBS reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=(templ-GETADDR(RN))+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                        *armregs[RD]=templ-GETADDR(RN);
                                }
                                cycles--;
                                break;

                                case 0x08: /*ADD reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
//                                        printf("R15=%08X+%08X+4=",GETADDR(RN),templ);
                                        *armregs[15]=((GETADDR(RN)+templ+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
//                                        printf("%08X\n",*armregs[15]);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)+templ;
                                }
                                cycles--;
                                break;
                                case 0x09: /*ADDS reg*/
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=GETADDR(RN)+templ+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                        *armregs[RD]=GETADDR(RN)+templ;
                                }
                                cycles--;
                                break;

                                case 0x0A: /*ADC reg*/
                                if (RD==15)
                                {
                                        templ2=CFSET;
                                        templ=shift2(opcode);
                                        *armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ2=CFSET;
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)+templ+templ2;
                                }
                                cycles--;
                                break;
                                case 0x0B: /*ADCS reg*/
                                if (RD==15)
                                {
                                        templ2=CFSET;
                                        templ=shift2(opcode);
                                        *armregs[15]=GETADDR(RN)+templ+templ2+4;
                                }
                                else
                                {
                                        templ2=CFSET;
                                        templ=shift(opcode);
                                        setadd(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                        *armregs[RD]=GETADDR(RN)+templ+templ2;
                                }
                                cycles--;
                                break;

                                case 0x0C: /*SBC reg*/
                                templ2=CFSET;
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)-(templ+templ2);
                                }
                                cycles--;
                                break;
                                case 0x0D: /*SBCS reg*/
                                templ2=CFSET;
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        setsub(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                        *armregs[RD]=GETADDR(RN)-(templ+templ2);
                                }
                                cycles--;
                                break;

                                case 0x0E: /*RSC reg*/
                                templ2=CFSET;
                                if (RD==15)
                                {
                                        templ=shift2(opcode);
                                        *armregs[15]=((((templ+templ2)-GETADDR(RN))+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=(templ+templ2)-GETADDR(RN);
                                }
                                cycles--;
                                break;

                                case 0x11: /*TST reg*/
                                if (RD==15)
                                {
                                        opcode&=~0x100000;
                                        templ=*armregs[15]&0x3FFFFFC;
                                        *armregs[15]=((GETADDR(RN)&shift2(opcode))&0xFC000003)|templ;
                                }
                                else
                                {
                                        setarmzn(GETADDR(RN)&shift(opcode));
                                }
                                cycles--;
                                break;

                                case 0x13: /*TEQ reg*/
                                if (RD==15)
                                {
                                        opcode&=~0x100000;
                                        templ=*armregs[15]&0x3FFFFFC;
                                        *armregs[15]=((GETADDR(RN)^shift(opcode))&0xFC000003)|templ;
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
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
                                        *armregs[15]&=0x3FFFFFC;
                                        *armregs[15]|=((GETADDR(RN)-shift(opcode))&0xFC000003);
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
                                }
                                else
                                   setsub(GETADDR(RN),shift(opcode),GETADDR(RN)-shift(opcode));
                                cycles--;
                                break;

                                case 0x17: /*CMN reg*/
                                if (RD==15)
                                {
                                        opcode&=~0x100000;
                                        *armregs[15]&=0x3FFFFFC;
                                        *armregs[15]|=((GETADDR(RN)+shift2(opcode))&0xFC000003);
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
                                }
                                else
                                   setadd(GETADDR(RN),shift2(opcode),GETADDR(RN)+shift2(opcode));
                                cycles--;
                                break;

                                case 0x18: /*ORR reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)|templ;
                                }
                                cycles--;
                                break;
                                case 0x19: /*ORRS reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(GETADDR(RN)|templ)+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)|templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x1A: /*MOV reg*/
                                if (RD==15)
                                   *armregs[15]=(*armregs[15]&0xFC000003)|((shift(opcode)+4)&0x3FFFFFC);
                                else
                                   *armregs[RD]=shift(opcode);
                                cycles--;
                                break;
                                case 0x1B: /*MOVS reg*/
                                if (RD==15)
                                   *armregs[15]=shift(opcode)+4;
                                else
                                {
                                        *armregs[RD]=shift(opcode);
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x1C: /*BIC reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)&~templ;
                                }
                                cycles--;
                                break;
                                case 0x1D: /*BICS reg*/
                                if (RD==15)
                                {
                                        templ=shift(opcode);
                                        *armregs[15]=(GETADDR(RN)&~templ)+4;
                                }
                                else
                                {
                                        templ=shift(opcode);
                                        *armregs[RD]=GETADDR(RN)&~templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x1E: /*MVN reg*/
                                if (RD==15)
                                   *armregs[15]=(*armregs[15]&0xFC000003)|(((~shift(opcode))+4)&0x3FFFFFC);
                                else
                                   *armregs[RD]=~shift(opcode);
                                cycles--;
                                break;
                                case 0x1F: /*MVNS reg*/
                                if (RD==15)
                                   *armregs[15]=(~shift(opcode))+4;
                                else
                                {
                                        *armregs[RD]=~shift(opcode);
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x20: /*AND imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)&templ;
                                }
                                cycles--;
                                break;
                                case 0x21: /*ANDS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)&templ)+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)&templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x22: /*EOR imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)^templ;
                                }
                                cycles--;
                                break;
                                case 0x23: /*EORS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)^templ)+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)^templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x24: /*SUB imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)-templ;
                                }
                                cycles--;
                                break;
                                case 0x25: /*SUBS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)-templ)+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                        *armregs[RD]=GETADDR(RN)-templ;
                                }
                                cycles--;
                                break;

                                case 0x26: /*RSB imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=templ-GETADDR(RN);
                                }
                                cycles--;
                                break;
                                case 0x27: /*RSBS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(templ-GETADDR(RN))+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                        *armregs[RD]=templ-GETADDR(RN);
                                }
                                cycles--;
                                break;

                                case 0x28: /*ADD imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)+templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)+templ;
//                                        printf("RD=%08X\n",*armregs[RD]);
                                }
                                cycles--;
                                break;
                                case 0x29: /*ADDS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=GETADDR(RN)+templ+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                        *armregs[RD]=GETADDR(RN)+templ;
                                }
                                cycles--;
                                break;

                                case 0x2A: /*ADC imm*/
                                if (RD==15)
                                {
                                        templ2=CFSET;
                                        templ=rotate(opcode);
                                        *armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ2=CFSET;
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)+templ+templ2;
                                }
                                cycles--;
                                break;
                                case 0x2B: /*ADCS imm*/
                                if (RD==15)
                                {
                                        templ2=CFSET;
                                        templ=rotate(opcode);
                                        *armregs[15]=GETADDR(RN)+templ+templ2+4;
                                }
                                else
                                {
                                        templ2=CFSET;
                                        templ=rotate(opcode);
                                        setadd(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                        *armregs[RD]=GETADDR(RN)+templ+templ2;
                                }
                                cycles--;
                                break;

                                case 0x2C: /*SBC imm*/
                                templ2=CFSET;
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)-(templ+templ2);
                                }
                                cycles--;
                                break;
                                case 0x2D: /*SBCS imm*/
                                templ2=CFSET;
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        setsub(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                        *armregs[RD]=GETADDR(RN)-(templ+templ2);
                                }
                                cycles--;
                                break;

                                case 0x31: /*TST imm*/
                                if (RD==15)
                                {
                                        opcode&=~0x100000;
                                        templ=*armregs[15]&0x3FFFFFC;
                                        *armregs[15]=((GETADDR(RN)&rotate(opcode))&0xFC000003)|templ;
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
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
                                        templ=*armregs[15]&0x3FFFFFC;
                                        *armregs[15]=((GETADDR(RN)^rotate(opcode))&0xFC000003)|templ;
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
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
                                        *armregs[15]&=0x3FFFFFC;
                                        *armregs[15]|=((GETADDR(RN)-rotate(opcode))&0xFC000003);
                                }
                                else
                                   setsub(GETADDR(RN),rotate(opcode),GETADDR(RN)-rotate(opcode));
                                cycles--;
                                break;

                                case 0x37: /*CMN imm*/
                                if (RD==15)
                                {
                                        opcode&=~0x100000;
                                        *armregs[15]&=0x3FFFFFC;
                                        *armregs[15]|=((GETADDR(RN)+rotate(opcode))&0xFC000003);
//                                        printf("R15 now %08X %08X\n",*armregs[15],*armregs[15]&0xFC000003);
                                }
                                else
                                   setadd(GETADDR(RN),rotate(opcode),GETADDR(RN)+rotate(opcode));
                                cycles--;
                                break;

                                case 0x38: /*ORR imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)|templ;
                                }
                                cycles--;
                                break;
                                case 0x39: /*ORRS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)|templ)+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)|templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x3A: /*MOV imm*/
                                if (RD==15)
                                   *armregs[15]=(*armregs[15]&0xFC000003)|(rotate(opcode)&0x3FFFFFC);
                                else
                                   *armregs[RD]=rotate(opcode);
                                cycles--;
                                break;
                                case 0x3B: /*MOVS imm*/
                                if (RD==15)
                                   *armregs[15]=rotate(opcode)+4;
                                else
                                {
                                        *armregs[RD]=rotate(opcode);
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x3C: /*BIC imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)&~templ;
                                }
                                cycles--;
                                break;
                                case 0x3D: /*BICS imm*/
                                if (RD==15)
                                {
                                        templ=rotate(opcode);
                                        *armregs[15]=(GETADDR(RN)&~templ)+4;
                                }
                                else
                                {
                                        templ=rotate(opcode);
                                        *armregs[RD]=GETADDR(RN)&~templ;
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x3E: /*MVN imm*/
                                if (RD==15)
                                   *armregs[15]=(*armregs[15]&0xFC000003)|(((~rotate(opcode))+4)&0x3FFFFFC);
                                else
                                   *armregs[RD]=~rotate(opcode);
                                cycles--;
                                break;
                                case 0x3F: /*MVNS imm*/
                                if (RD==15)
                                   *armregs[15]=(~rotate(opcode))+4;
                                else
                                {
                                        *armregs[RD]=~rotate(opcode);
                                        setarmzn(*armregs[RD]);
                                }
                                cycles--;
                                break;

                                case 0x4A: /*STRT*/
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                templ=memmode;
                                memmode=0;
                                writearml(addr,*armregs[RD]);
                                memmode=templ;
                                if (databort) break;
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
                                cycles-=2;
                                break;

                                case 0x4B: /*LDRT*/
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                templ=memmode;
                                memmode=0;
                                templ2=readarml(addr);
                                memmode=templ;
                                if (databort) break;
                                LOADREG(RD,templ2);
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
                                cycles-=2;
                                break;

                                case 0x40: case 0x48: case 0x50: case 0x52: /*STR*/
                                case 0x58: case 0x5A: case 0x60: case 0x68:
                                case 0x70: case 0x72: case 0x78: case 0x7A:
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                writearml(addr,*armregs[RD]);
                                if (databort) break;
//                                printf("R%i to %07X = %08X\n",RD,addr,*armregs[RD]);
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
//                                printf("R%i = %08X\n",RN,*armregs[RN]);
                                cycles-=2;
                                break;

                                case 0x41: case 0x49: case 0x51: case 0x53: /*LDR*/
                                case 0x59: case 0x5B: case 0x69: case 0x71:
                                case 0x73: case 0x79: case 0x7B:
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                templ=readarml(addr);
                                templ=ldrresult(templ,addr);
                                if (databort) break;
//                                printf("R%i from %07X = %08X\n",RD,addr,*armregs[RD]);
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
                                LOADREG(RD,templ);
//                                printf("R%i = %08X\n",RN,*armregs[RN]);
                                cycles-=2;
                                break;

                                case 0x45: case 0x4D: case 0x55: case 0x57: /*LDRB*/
                                case 0x5D: case 0x5F: case 0x6D: case 0x75:
                                case 0x7D: case 0x7F:
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                templ=readarmb(addr);
                                if (databort) break;
//                                printf("R%i from %07X = %02X\n",RD,addr,*armregs[RD]);
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
                                *armregs[RD]=templ;
//                                printf("R%i = %08X\n",RN,*armregs[RN]);
                                cycles-=2;
                                break;

                                case 0x44: case 0x4C: case 0x54: case 0x56: /*STRB*/
                                case 0x5C: case 0x5E: case 0x6C: case 0x7C:
                                case 0x7E:
                                addr=GETADDR(RN);
                                if (opcode&0x2000000) addr2=shift2(opcode);
                                else                  addr2=opcode&0xFFF;
                                if (opcode&0x1000000)
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                }
                                writearmb(addr,*armregs[RD]);
                                if (databort) break;
//                                printf("R%i to %07X = %02X\n",RD,addr,*armregs[RD]);
                                if (!(opcode&0x1000000))
                                {
                                        if (opcode&0x800000) addr+=addr2;
                                        else                 addr-=addr2;
                                        *armregs[RN]=addr;
                                }
                                else
                                {
                                        if (opcode&0x200000) *armregs[RN]=addr;
                                }
//                                printf("R%i = %08X  PC %07X\n",RN,*armregs[RN],PC);
//                                exit(0);
                                cycles-=2;
                                break;

                                case 0x80: /*STMDA*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                addr-=4;
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x81: /*LDMDA*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                addr-=4;
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x82: /*STMDA !*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                addr-=4;
                                                *armregs[RN]-=4;
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x83: /*LDMDA !*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                addr-=4;
                                                *armregs[RN]-=4;
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x88: /*STMIA*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                addr+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x89: /*LDMIA*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                addr+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x8A: /*STMIA !*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                addr+=4;
                                                *armregs[RN]+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x8B: /*LDMIA !*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
//                                                if (output) printf("R%i = %08X from %07X\n",c,*armregs[c],addr);
                                                addr+=4;
                                                *armregs[RN]+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x8C: /*STMIA ^*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,userregs[c]+4); }
                                                else       { writearml(addr,userregs[c]); }
                                                addr+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x8D: /*LDMIA ^*/
                                mask=1;
                                addr=*armregs[RN];
                                if (opcode&0x8000)
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        *armregs[c]=readarml(addr);
                                                        addr+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        *armregs[15]+=4;
                                }
                                else
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        userregs[c]=readarml(addr);
                                                        addr+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                }
                                cycles--;
                                break;

                                case 0x8E: /*STMIA !^*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                if (c==15) { writearml(addr,userregs[c]+4); }
                                                else       { writearml(addr,userregs[c]); }
                                                addr+=4;
                                                *armregs[RN]+=4;
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x8F: /*LDMIA !^*/
                                mask=1;
                                addr=*armregs[RN];
//                                userregs[RN]=addr;
                                if (opcode&0x8000)
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        *armregs[c]=readarml(addr);
//                                                        printf("R%i=%08X from %07X\n",c,*armregs[c],addr);
                                                        addr+=4;
                                                        *armregs[RN]+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        *armregs[15]+=4;
                                }
                                else
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        userregs[c]=readarml(addr);
                                                        addr+=4;
                                                        *armregs[RN]+=4;
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                }
                                cycles--;
                                break;

                                case 0x90: /*STMDB*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                addr-=4;
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x91: /*LDMDB*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                addr-=4;
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x92: /*STMDB !*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                addr-=4;
                                                *armregs[RN]-=4;
//                                                if (addr==0x1C01BFC) printf("1C01BFC R%i\n",c);
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
//                                                if (output) printf("R%i = %08X  to  %07X\n",c,*armregs[c],addr);
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x93: /*LDMDB !*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                addr-=4;
                                                *armregs[RN]-=4;
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x94: /*STMDB ^*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                for (c=15;c>-1;c--)
                                {
                                        if (opcode&mask)
                                        {
                                                addr-=4;
                                                if (c==15) { writearml(addr,userregs[c]+4); }
                                                else       { writearml(addr,userregs[c]); }
                                                cycles--;
                                        }
                                        mask>>=1;
                                }
                                cycles--;
                                break;

                                case 0x95: /*LDMDB ^*/
                                mask=0x8000;
                                addr=*armregs[RN];
                                if (opcode&0x8000)
                                {
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        *armregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                        *armregs[15]+=4;
                                }
                                else
                                {
                                        for (c=15;c>-1;c--)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr-=4;
                                                        userregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask>>=1;
                                        }
                                }
                                cycles--;
                                break;

                                case 0x98: /*STMIB*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                addr+=4;
                                                if (c==15) { writearml(addr,*armregs[c]+4); }
                                                else       { writearml(addr,*armregs[c]); }
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x99: /*LDMIB*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                addr+=4;
                                                if (c==15) *armregs[15]=(*armregs[15]&0xFC000003)|((readarml(addr)+4)&0x3FFFFFC);
                                                else       *armregs[c]=readarml(addr);
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x9C: /*STMIB ^*/
                                mask=1;
                                addr=*armregs[RN];
                                for (c=0;c<16;c++)
                                {
                                        if (opcode&mask)
                                        {
                                                addr+=4;
                                                if (c==15) { writearml(addr,userregs[c]+4); }
                                                else       { writearml(addr,userregs[c]); }
                                                cycles--;
                                        }
                                        mask<<=1;
                                }
                                cycles--;
                                break;

                                case 0x9D: /*LDMIB ^*/
                                mask=1;
                                addr=*armregs[RN];
                                if (opcode&0x8000)
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        *armregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                        *armregs[15]+=4;
                                }
                                else
                                {
                                        for (c=0;c<16;c++)
                                        {
                                                if (opcode&mask)
                                                {
                                                        addr+=4;
                                                        userregs[c]=readarml(addr);
                                                        cycles--;
                                                }
                                                mask<<=1;
                                        }
                                }
                                cycles--;
                                break;

                                case 0xB0: case 0xB1: case 0xB2: case 0xB3: /*BL*/
                                case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                                case 0xB8: case 0xB9: case 0xBA: case 0xBB:
                                case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                                templ=(opcode&0xFFFFFF)<<2;
                                *armregs[14]=*armregs[15]-4;
                                *armregs[15]=((*armregs[15]+templ+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                cycles-=3;
                                break;

                                case 0xA0: case 0xA1: case 0xA2: case 0xA3: /*B*/
                                case 0xA4: case 0xA5: case 0xA6: case 0xA7:
                                case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                                case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                                templ=(opcode&0xFFFFFF)<<2;
                                *armregs[15]=((*armregs[15]+templ+4)&0x3FFFFFC)|(*armregs[15]&0xFC000003);
                                cycles-=3;
                                break;

                                case 0xCB: case 0xD1: case 0xD2: case 0xD5: /*Co-pro*/
                                case 0xD6: case 0xD8: case 0xD9: case 0xDC:
                                case 0xDD: case 0xE0: case 0xE1: case 0xE2:
                                case 0xE3: case 0xE4: case 0xE5: case 0xED:
//                                printf("Illegal Co-pro instruction ins %i\n",ins);
                                templ=*armregs[15]-4;
                                *armregs[15]|=3;
                                updatemode(SUPERVISOR);
                                *armregs[14]=templ;
                                *armregs[15]&=0xFC000003;
                                *armregs[15]|=0x08000008;
                                cycles--;
                                break;

                                case 0xF0: /*SWI*/
                                templ=*armregs[15]-4;
                                *armregs[15]|=3;
                                updatemode(SUPERVISOR);
                                *armregs[14]=templ;
                                *armregs[15]&=0xFC000003;
                                *armregs[15]|=0x0800000C;
                                cycles--;
                                break;

                                default:
                                printf("Bad opcode %02X %08X\n",(opcode>>20)&0xFF,opcode);
                                dumparmregs();
                                exit(-1);
                        }
                }
                if (databort)
                {
                        templ=*armregs[15];
                        *armregs[15]|=3;
                        updatemode(SUPERVISOR);
                        *armregs[14]=templ;
                        *armregs[15]&=0xFC000003;
                        *armregs[15]|=0x08000014;
                        databort=0;
//                        printf("Data abort ins %i\n",ins);
//                        output=1;
                }
                else if ((armirq&2) && !(*armregs[15]&0x4000000))
                {
                        templ=*armregs[15];
                        *armregs[15]|=3;
                        updatemode(FIQ);
                        *armregs[14]=templ;
                        *armregs[15]&=0xFC000001;
                        *armregs[15]|=0x0C000020;
//                        printf("Parasite FIQ %08X\n",PC+4);
                }
                else if (tubeirq && !(*armregs[15]&0x8000000))
                {
                        templ=*armregs[15];
                        *armregs[15]|=3;
                        updatemode(IRQ);
                        *armregs[14]=templ;
                        *armregs[15]&=0xFC000002;
                        *armregs[15]|=0x0800001C;
//                        printf("Parasite IRQ\n");
                }
                if ((*armregs[15]&3)!=mode) updatemode(*armregs[15]&3);
                *armregs[15]+=4;
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
