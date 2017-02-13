/*B-em v2.2 by Tom Walker
  32016 parasite processor emulation (not working yet)*/
#include <stdio.h>
#include <allegro.h>
#include "tube.h"
#include "32016.h"
#include "b-em.h"

static int nsoutput=0;
#define r ns_r
#define pc ns_pc
#define sp ns_sp
#define fp ns_fp
#define sb ns_sb
#define intbase ns_intbase
#define psr ns_psr
#define mod ns_mod
#define cycles ns_cycles

#define readmemb ns_readmemb
#define writememb ns_writememb
#define readmemw ns_readmemw
#define writememw ns_writememw

static uint16_t readmemw(uint32_t addr);
static void writememw(uint32_t addr, uint16_t val);

static uint32_t r[8];
static uint32_t pc,sp[2],fp,sb,intbase;
static uint16_t psr,mod;
static uint32_t startpc;
static int nscfg;

#define C_FLAG 0x01
#define T_FLAG 0x02
#define L_FLAG 0x04
#define F_FLAG 0x20
#define V_FLAG 0x20
#define Z_FLAG 0x40
#define N_FLAG 0x80

#define U_FLAG 0x100
#define S_FLAG 0x200
#define P_FLAG 0x400
#define I_FLAG 0x800

#define SP ((psr&S_FLAG)>>9)

static void pushw(uint16_t val)
{
        sp[SP]-=2;
        writememw(sp[SP],val);
}
static void pushd(uint32_t val)
{
        sp[SP]-=4;
  //      if (nsoutput) printf("Push %08X to %08X\n",val,sp[SP]);
        writememw(sp[SP],val);
        writememw(sp[SP]+2,val>>16);
}
static uint16_t popw()
{
        uint16_t temp=readmemw(sp[SP]);
        sp[SP]+=2;
        return temp;
}
static uint32_t popd()
{
        uint32_t temp=readmemw(sp[SP])|(readmemw(sp[SP]+2)<<16);
        sp[SP]+=4;
        return temp;
}

static uint8_t *ns32016rom,*ns32016ram;

void n32016_reset()
{
        pc=0;
        psr=0;
        memcpy(ns32016ram,ns32016rom,16);
}

void n32016_init()
{
        FILE *f;
        char fn[512];
        if (!ns32016rom) ns32016rom=malloc(0x8000);
        if (!ns32016ram) ns32016ram=malloc(0x100000);
        append_filename(fn,exedir,"roms/tube/Pandora.rom",511);
        f=x_fopen(fn,"rb");
        fread(ns32016rom,0x8000,1,f);
        fclose(f);
        memset(ns32016ram,0,0x100000);
}

void n32016_close()
{
        if (ns32016rom) free(ns32016rom);
        if (ns32016ram) free(ns32016ram);
}

static void n32016_dumpregs()
{
        FILE *f=x_fopen("32016.dmp","wb");
        fwrite(ns32016ram,1024*1024,1,f);
        fclose(f);
        printf("R0=%08X R1=%08X R2=%08X R3=%08X\n",r[0],r[1],r[2],r[3]);
        printf("R4=%08X R5=%08X R6=%08X R7=%08X\n",r[4],r[5],r[6],r[7]);
        printf("PC=%08X SB=%08X SP0=%08X SP1=%08X\n",pc,sb,sp[0],sp[1]);
        printf("FP=%08X INTBASE=%08X PSR=%04X MOD=%04X\n",fp,intbase,psr,mod);
}

static uint8_t readmemb(uint32_t addr)
{
        uint8_t temp;
        addr&=0xFFFFFF;
        if (addr<0x100000) return ns32016ram[addr];
        if ((addr&~0x7FFF)==0xF00000) return ns32016rom[addr&0x7FFF];
        if (addr==0xF90000) return 0; /*What's here?*/
        if (addr>=0xFFFFF0)
        {
                temp=tube_parasite_read(addr>>1);
//                if (addr&2) printf("Read TUBE %08X %02X\n",addr,temp);
                return temp;
        }
        printf("Bad readmemb %08X\n",addr);
        n32016_dumpregs();
        exit(-1);
}

static uint16_t readmemw(uint32_t addr)
{
        addr&=0xFFFFFF;
        if (addr<0x100000)
        {
//                printf("Read %08X %04X\n",addr,ns32016ram[addr&0xFFFFF]|(ns32016ram[(addr+1)&0xFFFFF]<<8));
                return ns32016ram[addr&0xFFFFF]|(ns32016ram[(addr+1)&0xFFFFF]<<8);
        }
        if (addr<0x400000) return 0;
        if ((addr&~0x7FFF)==0xF00000)
        {
                return ns32016rom[addr&0x7FFF]|(ns32016rom[(addr+1)&0x7FFF]<<8);
        }
        printf("Bad readmemw %08X\n",addr);
        n32016_dumpregs();
        exit(-1);
}

static void writememb(uint32_t addr, uint8_t val)
{
        addr&=0xFFFFFF;
//        if (addr==0xFFDC8) printf("Writeb %08X %02X %08X\n",addr,val,pc);
        if (addr<0x100000) { ns32016ram[addr]=val; return; }
        if (addr==0xF90000) return;
        if (addr>=0xFFFFF0) { tube_parasite_write(addr>>1,val); /*printf("Write tube %08X %02X %c\n",addr,val,(val<33)?'.':val);*/ /*if (nsoutput) exit(-1);*/ /*if (val=='K') { n32016_dumpregs(); exit(-1); } */return; }
        printf("Bad writememb %08X %02X\n",addr,val);
        n32016_dumpregs();
        exit(-1);
}

static void writememw(uint32_t addr, uint16_t val)
{
        addr&=0xFFFFFF;
//        if ((addr&~1)==0xFFDC8) printf("Writew %08X %04X %08X\n",addr,val,pc);
        if (addr<0x100000)
        {
//                printf("Write %08X %04X  ",addr,val);
                ns32016ram[addr]=val;
                ns32016ram[addr+1]=val>>8;
//                printf("%02X %02X\n",ns32016ram[addr],ns32016ram[addr+1]);
                return;
        }
        if (addr<0x400000) return;
        printf("Bad writememw %08X %04X\n",addr,val);
        n32016_dumpregs();
        exit(-1);
}

static uint32_t genaddr[2];
static int gentype[2];

static uint32_t getdisp()
{
        uint32_t addr=readmemb(pc); pc++;
        if (!(addr&0x80))
        {
//                printf("8 bit addr %08X\n",addr);
                return addr|((addr&0x40)?0xFFFFFF80:0);
        }
        else if (!(addr&0x40))
        {
                addr&=0x3F;
                addr=(addr<<8)|readmemb(pc); pc++;
//                printf("16 bit addr %08X\n",addr);
                return addr|((addr&0x2000)?0xFFFFC000:0);
        }
        else
        {
                addr&=0x3F;
                addr=(addr<<24)|(readmemb(pc)<<16); pc++;
                addr=addr|(readmemb(pc)<<8); pc++;
                addr=addr|readmemb(pc); pc++;
//                printf("32 bit addr %08X\n",addr);
                return addr|((addr&0x20000000)?0xC0000000:0);
        }
}

static int isize=0;
static int ilook[4]={1,2,0,4};

static int genindex[2];
static void getgen1(int gen, int c)
{
        if ((gen&0x1C)==0x1C)
        {
                genindex[c]=readmemb(pc);
                pc++;
        }
}

static int sdiff[2]={0,0};
static uint32_t nsimm[2];

static void getgen(int gen, int c)
{
        uint32_t temp,temp2;
//        if (nsoutput&2) printf("Gen %02X %i\n",gen&0x1F,c);
        switch (gen&0x1F)
        {
                case 0: case 1: case 2: case 3:
                case 4: case 5: case 6: case 7:
                gentype[c]=1;
                genaddr[c]=(uint32_t)&r[gen&7];
                break;
                case 8: case 9: case 0xA: case 0xB:
                case 0xC: case 0xD: case 0xE: case 0xF:
                gentype[c]=0;
                genaddr[c]=r[gen&7]+getdisp();
//                printf("RDisp %08X\n",genaddr[c]);
                break;
                case 0x10: /*Frame memory relative*/
                temp=getdisp();
                temp2=getdisp();
//                if (nsoutput) printf("First addr %08X (%08X+%08X\n",fp+temp,fp,temp);
                genaddr[c]=readmemw(fp+temp)|(readmemw(fp+temp+2)<<16);
//                if (nsoutput) printf("Second addr %08X\n",genaddr[c]);
                genaddr[c]+=temp2;
//                if (nsoutput) printf("Final addr %08X %08X\n",genaddr[c],temp2);
                gentype[c]=0;
                break;
                case 0x11: /*Stack memory relative*/
                temp=getdisp();
                temp2=getdisp();
//                if (nsoutput) printf("First addr %08X (%08X+%08X\n",sp[SP]+temp,sp[SP],temp);
                genaddr[c]=readmemw(sp[SP]+temp)|(readmemw(sp[SP]+temp+2)<<16);
//                if (nsoutput) printf("Second addr %08X\n",genaddr[c]);
                genaddr[c]+=temp2;
//                if (nsoutput) printf("Final addr %08X %08X\n",genaddr[c],temp2);
                gentype[c]=0;
                break;
                case 0x12: /*Static memory relative*/
                temp=getdisp();
                temp2=getdisp();
//                if (nsoutput) printf("First addr %08X (%08X+%08X\n",sb+temp,sb,temp);
                genaddr[c]=readmemw(sb+temp)|(readmemw(sb+temp+2)<<16);
//                if (nsoutput) printf("Second addr %08X\n",genaddr[c]);
                genaddr[c]+=temp2;
//                if (nsoutput) printf("Final addr %08X %08X\n",genaddr[c],temp2);
                gentype[c]=0;
                break;
                case 0x14: /*Immediate*/
/*                genaddr[c]=pc;
                gentype[c]=0;*/
                gentype[c]=1;
                genaddr[c]=(uint32_t)&nsimm[c];
                /*Why can't they just decided on an endian and then stick to it?*/
                if (isize==1) nsimm[c]=readmemb(pc);
                else if (isize==2) nsimm[c]=(readmemb(pc)<<8)|readmemb(pc+1);
                else               nsimm[c]=(readmemb(pc)<<24)|(readmemb(pc+1)<<16)|(readmemb(pc+2)<<8)|readmemb(pc+3);
                pc+=isize;
//                printf("PC %08X %i\n",pc,isize);
                break;
                
                case 0x15: /*Absolute*/
                gentype[c]=0;
                genaddr[c]=getdisp();
//                printf("Disp %08X\n",genaddr[c]);
                break;
                case 0x16: /*External*/
                gentype[c]=0;
                temp=readmemw(mod+4)+(readmemw(mod+6)<<16);
                temp+=getdisp();
                temp2=readmemw(temp)+(readmemw(temp+2)<<16);
                genaddr[c]=temp2+getdisp();
                break;
                case 0x17: /*Stack*/
//                nsoutput=1;
                gentype[c]=0;
                sdiff[c]=isize;
                genaddr[c]=sp[SP];
//                printf("TOS %i %i\n",sdiff,isize);
/*                if (c)
                {
                        sp[SP]-=isize;
                        genaddr[c]=sp[SP];
                }
                else
                {
                        genaddr[c]=sp[SP];
                        sp[SP]+=isize;
                }*/
                break;
                
                case 0x18: /*FP relative*/
                gentype[c]=0;
                genaddr[c]=getdisp()+fp;
//                printf("FPAddr %08X %08X\n",genaddr[c],fp);
                break;
                case 0x19: /*SP relative*/
                gentype[c]=0;
                genaddr[c]=getdisp()+sp[SP];
//                printf("SPAddr %08X %08X\n",genaddr[c],sp[SP]);
                break;
                case 0x1A: /*SB relative*/
                gentype[c]=0;
                genaddr[c]=getdisp()+sb;
//                if (nsoutput) printf("SBAddr %08X %08X\n",genaddr[c],sb);
                break;
                case 0x1B: /*PC relative*/
                gentype[c]=0;
                genaddr[c]=getdisp()+startpc;
//                printf("Addr %08X %08X %08X\n",genaddr[c],pc,startpc);
                break;
                
                case 0x1C: /*EA + Rn*/
                getgen(genindex[c]>>3,c);
                if (!gentype[c]) genaddr[c]+=r[genindex[c]&7];
                else             genaddr[c]=*(uint32_t *)genaddr[c]+r[genindex[c]&7];
//                if (nsoutput&2) printf("EA + R%i addr %08X %02X\n",genindex[c]&7,genaddr[c],genindex[c]);
                gentype[c]=0;
                break;
                case 0x1D: /*EA + Rn*2*/
                getgen(genindex[c]>>3,c);
                if (!gentype[c]) genaddr[c]+=(r[genindex[c]&7]*2);
                else             genaddr[c]=*(uint32_t *)genaddr[c]+(r[genindex[c]&7]*2);
//                printf("EA + Rn*2 addr %08X\n",genaddr[c]);
                gentype[c]=0;
                break;
                

                default:
                printf("Bad NS32016 gen mode %02X\n",gen&0x1F);
                n32016_dumpregs();
                exit(-1);
        }
}

#define readgenb(c,temp)        if (gentype[c]) temp=*(uint8_t *)genaddr[c]; \
                                else \
                                { \
                                        temp=readmemb(genaddr[c]); \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]+sdiff[c]; \
                                }

#define readgenw(c,temp)        if (gentype[c]) temp=*(uint16_t *)genaddr[c]; \
                                else \
                                { \
                                        temp=readmemw(genaddr[c]); \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]+sdiff[c]; \
                                }

#define readgenl(c,temp)        if (gentype[c]) temp=*(uint32_t *)genaddr[c]; \
                                else \
                                { \
                                        temp=readmemw(genaddr[c])|(readmemw(genaddr[c]+2)<<16); \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]+sdiff[c]; \
                                }

#define readgenq(c,temp)        if (gentype[c]) temp=*(uint64_t *)genaddr[c]; \
                                else \
                                { \
                                        temp=readmemw(genaddr[c])|(readmemw(genaddr[c]+2)<<16); \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]+sdiff[c]; \
                                        temp|=((readmemw(genaddr[c])|(readmemw(genaddr[c]+2)<<16))<<16); \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]+sdiff[c]; \
                                }

#define writegenb(c,temp)       if (gentype[c]) *(uint8_t *)genaddr[c]=temp; \
                                else \
                                { \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]-sdiff[c]; \
                                        writememb(genaddr[c],temp); \
                                }

#define writegenw(c,temp)       if (gentype[c]) *(uint16_t *)genaddr[c]=temp; \
                                else \
                                { \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]-sdiff[c]; \
                                        writememw(genaddr[c],temp); \
                                }

#define writegenl(c,temp)       if (gentype[c]) *(uint32_t *)genaddr[c]=temp; \
                                else \
                                { \
                                        if (sdiff[c]) genaddr[c]=sp[SP]=sp[SP]-sdiff[c]; \
                                        writememw(genaddr[c],temp); \
                                        writememw(genaddr[c]+2,temp>>16); \
                                }


static uint16_t oldpsr;

void n32016_exec()
{
        uint32_t opcode;
        uint32_t temp = 0,temp2,temp3,temp4;
        uint64_t temp64;
        int c;
        while (tubecycles>0)
        {
                sdiff[0]=sdiff[1]=0;
//                if (pc==0xF00A3C) nsoutput=1;
//                if (pc==0xF00A73) nsoutput=0;
                startpc=pc;
                opcode=readmemb(pc);
//                if (nsoutput && (pc<0xF000A0 || pc>0xF000B3)) printf("%08X %08X %08X %08X %08X %08X %04X : %02X %02X %02X %02X\n",pc,r[0],r[1],r[2],r[3],sp[SP],psr,opcode,readmemb(pc+1),readmemb(pc+2),readmemb(pc+3));
                pc++;
                isize=ilook[opcode&3];
                switch (opcode)
                {
                        case 0x0E: /*String instruction*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        opcode|=(readmemb(pc)<<16); pc++;
                        temp2=(opcode>>15)&0xF;
                        switch ((opcode>>8)&0x3F)
                        {
                                case 0x00: /*MOVS byte*/
                                if (temp2&3)
                                {
                                        printf("Bad NS32016 MOVS %02X %04X %01X\n",(opcode>>15)&0xF,opcode,(opcode>>15)&0xF);
                                        n32016_dumpregs();
                                        exit(-1);
                                }
//                                printf("MOVSB %08X %08X %08X  %08X\n",r[1],r[2],r[0],pc);
                                while (r[0])
                                {
                                        temp=readmemb(r[1]);  r[1]++;
                                        if ((temp2&0xC)==0xC && temp==r[4]) { /*printf("Break EQ\n");*/ break; }
                                        if ((temp2&0xC)==0x4 && temp!=r[4]) { /*printf("Break NE\n");*/ break; }
                                        writememb(r[2],temp); r[2]++;
                                        r[0]--;
//                                        printf("MOVS %02X %08X %08X %08X %01X\n",temp,r[1],r[2],r[4],temp);
                                }
                                break;
                                case 0x03: /*MOVS dword*/
//                                printf("MOVSD %08X %08X %08X  %08X\n",r[1],r[2],r[0],pc);
                                if (temp2)
                                {
                                        printf("Bad NS32016 MOVS %02X %04X %01X\n",(opcode>>15)&0xF,opcode,(opcode>>15)&0xF);
                                        n32016_dumpregs();
                                        exit(-1);
                                }
                                while (r[0])
                                {
                                        temp=readmemw(r[1]); temp|=(readmemw(r[1]+2)<<16); r[1]+=4;
                                        writememw(r[2],temp); writememw(r[2]+2,temp>>16); r[2]+=4;
                                        r[0]--;
                                }
                                break;
                                
                                case 0x0B: /*SETCFG*/
                                nscfg=temp;
                                break;
                                
                                default:
                                printf("Bad NS32016 0E opcode %02X %04X %01X\n",(opcode>>8)&0x3F,opcode,(opcode>>15)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        
                        case 0x1C: case 0x9C: /*CMPQ byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenb(0,temp);
//                        if (!temp) nsoutput=1;
                        psr&=~(Z_FLAG|N_FLAG|L_FLAG);
                        if (temp==temp2) psr|=Z_FLAG;
                        if (temp2>temp)  psr|=L_FLAG;
                        if (((signed char)temp2)>((signed char)temp)) psr|=N_FLAG;
                        break;
                        case 0x1F: case 0x9F: /*CMPQ dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenl(0,temp);
//                        if (!temp) nsoutput=1;
                        psr&=~(Z_FLAG|N_FLAG|L_FLAG);
                        if (temp==temp2) psr|=Z_FLAG;
                        if (temp2>temp)  psr|=L_FLAG;
                        if (((signed long)temp2)>((signed long)temp)) psr|=N_FLAG;
//                        printf("CMPQ %08X %08X %i %i\n",temp,temp2,temp>temp2,((signed long)temp)>((signed long)temp2));
                        break;
                        case 0x5C: case 0xDC: /*MOVQ byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp=(opcode>>7)&0xF; if (temp&8) temp|=0xFFFFFFF0;
                        writegenb(0,temp);
                        break;
                        case 0x5D: case 0xDD: /*MOVQ word*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp=(opcode>>7)&0xF; if (temp&8) temp|=0xFFFFFFF0;
                        writegenw(0,temp);
                        break;
                        case 0x5F: case 0xDF: /*MOVQ dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp=(opcode>>7)&0xF; if (temp&8) temp|=0xFFFFFFF0;
                        writegenl(0,temp);
                        break;
                        case 0x0C: case 0x8C: /*ADDQ byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenb(0,temp);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)&0x100)                            psr|=C_FLAG;
                        if ((temp^(temp+temp2))&(temp2^(temp+temp2))&0x80) psr|=V_FLAG;
                        temp+=temp2;
                        writegenb(0,temp);
                        break;
                        case 0x0D: case 0x8D: /*ADDQ word*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenw(0,temp);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)&0x10000)                            psr|=C_FLAG;
                        if ((temp^(temp+temp2))&(temp2^(temp+temp2))&0x8000) psr|=V_FLAG;
                        temp+=temp2;
                        writegenw(0,temp);
                        break;
                        case 0x0F: case 0x8F: /*ADDQ dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenl(0,temp);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)<temp)                                   psr|=C_FLAG;
                        if ((temp^(temp+temp2))&(temp2^(temp+temp2))&0x80000000) psr|=V_FLAG;
                        temp+=temp2;
                        writegenl(0,temp);
                        break;
                        case 0x3C: case 0xBC: /*ScondB*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
//                        readgenb(0,temp);
temp=0;
                        switch ((opcode>>7)&0xF)
                        {
                                case 0x0: if   (psr&Z_FLAG)  temp=1; break;
                                case 0x1: if (!(psr&Z_FLAG)) temp=1; break;
                                case 0x2: if   (psr&C_FLAG)  temp=1; break;
                                case 0x3: if (!(psr&C_FLAG)) temp=1; break;
                                case 0x4: if   (psr&L_FLAG)  temp=1; break;
                                case 0x5: if (!(psr&L_FLAG)) temp=1; break;
                                case 0x6: if   (psr&N_FLAG)  temp=1; break;
                                case 0x7: if (!(psr&N_FLAG)) temp=1; break;
                                case 0x8: if (!(psr&(L_FLAG|Z_FLAG))) temp=1; break;
                                case 0x9: if (psr&(L_FLAG|Z_FLAG)) temp=1; break;
                                case 0xA: if (!(psr&(N_FLAG|Z_FLAG)))  temp=1; break;
                                case 0xB: if (psr&(N_FLAG|Z_FLAG)) temp=1; break;
                                case 0xC: if   (psr&Z_FLAG)  temp=1; break;
                                case 0xD: if (!(psr&Z_FLAG)) temp=1; break;
                                case 0xE: temp=1; break;
                                case 0xF: break;
                        }
                        writegenb(0,temp);
                        break;
                        case 0x4C: case 0xCC: /*ACBB*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenb(0,temp);
                        temp+=temp2;
                        writegenb(0,temp);
                        temp2=getdisp();
                        if (temp&0xFF) pc=startpc+temp2;
                        break;
                        case 0x4F: case 0xCF: /*ACBD*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        temp2=(opcode>>7)&0xF; if (temp2&8) temp2|=0xFFFFFFF0;
                        readgenl(0,temp);
                        temp+=temp2;
                        writegenl(0,temp);
                        temp2=getdisp();
                        if (temp) pc=startpc+temp2;
                        break;
                        
                        case 0x00: case 0x40: case 0x80: case 0xC0: /*ADD byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)&0x100)                            psr|=C_FLAG;
                        if ((temp^(temp+temp2))&(temp2^(temp+temp2))&0x80) psr|=V_FLAG;
                        temp2+=temp;
                        writegenb(1,temp2);
                        break;
                        case 0x03: case 0x43: case 0x83: case 0xC3: /*ADD dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenl(0,temp);
                        readgenl(1,temp2);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)<temp)                                   psr|=C_FLAG;
                        if ((temp^(temp+temp2))&(temp2^(temp+temp2))&0x80000000) psr|=V_FLAG;
                        temp2+=temp;
                        writegenl(1,temp2);
                        break;
                        case 0x04: case 0x44: case 0x84: case 0xC4: /*CMP byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        psr&=~(Z_FLAG|N_FLAG|L_FLAG);
                        if (temp==temp2) psr|=Z_FLAG;
                        if (temp>temp2)  psr|=L_FLAG;
                        if (((signed char)temp)>((signed char)temp2)) psr|=N_FLAG;
                        break;
                        case 0x07: case 0x47: case 0x87: case 0xC7: /*CMP dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        nsoutput|=2;
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        nsoutput&=~2;
                        readgenl(0,temp);
                        readgenl(1,temp2);
                        
//                        printf("CMP %08X %08X %i %i\n",temp,temp2,temp>temp2,(((signed long)temp)>((signed long)temp2)));
                        psr&=~(Z_FLAG|N_FLAG|L_FLAG);
                        if (temp==temp2) psr|=Z_FLAG;
                        if (temp>temp2)  psr|=L_FLAG;
                        if (((signed long)temp)>((signed long)temp2)) psr|=N_FLAG;
                        break;
                        case 0x08: case 0x48: case 0x88: case 0xC8: /*BIC byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        temp2&=~temp;
                        writegenb(1,temp2);
                        break;
                        case 0x09: case 0x49: case 0x89: case 0xC9: /*BIC word*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenw(0,temp);
                        readgenw(1,temp2);
                        temp2&=~temp;
                        writegenw(1,temp2);
                        break;

                        case 0x14: case 0x54: case 0x94: case 0xD4: /*MOV byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        writegenb(1,temp);
                        break;
                        case 0x15: case 0x55: case 0x95: case 0xD5: /*MOV word*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenw(0,temp);
                        writegenw(1,temp);
                        break;
                        case 0x17: case 0x57: case 0x97: case 0xD7: /*MOV dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
  //                      printf("Read from %08X write to %08X\n",genaddr[0],genaddr[1]);
                        readgenl(0,temp);
//                        printf("Dat %08X\n",temp);
                        writegenl(1,temp);
                        break;
                        case 0x18: case 0x58: case 0x98: case 0xB8: /*OR byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        temp2|=temp;
                        writegenb(1,temp2);
                        break;

                        case 0x23: case 0x63: case 0xA3: case 0xE3: /*SUB dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenl(0,temp);
                        readgenl(1,temp2);
                        psr&=~(C_FLAG|V_FLAG);
                        if ((temp+temp2)>temp)                           psr|=C_FLAG;
                        if ((temp^temp2)&(temp^(temp+temp2))&0x80000000) psr|=V_FLAG;
                        temp2-=temp;
                        writegenl(1,temp2);
                        break;
                        case 0x27: case 0x67: case 0xA7: case 0xE7: /*ADDR dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
//                        printf("Writegenl %08X ",sp[SP]);
                        writegenl(1,genaddr[0]);
//                        printf("%08X\n",sp[SP]);
                        break;
                        
                        case 0x28: case 0x68: case 0xA8: case 0xE8: /*AND byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        temp2&=temp;
                        writegenb(1,temp2);
                        break;
                        case 0x2B: case 0x6B: case 0xAB: case 0xEB: /*AND dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenl(0,temp);
                        readgenl(1,temp2);
                        temp2&=temp;
                        writegenl(1,temp2);
                        break;
                        case 0x34: case 0x74: case 0xB4: case 0xF4: /*TBITB*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenb(0,temp);
                        readgenb(1,temp2);
                        psr&=~F_FLAG;
                        temp&=7;
                        if (temp2&(1<<temp)) psr|=F_FLAG;
                        break;
                        case 0x37: case 0x77: case 0xB7: case 0xF7: /*TBITD*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        readgenl(0,temp);
                        readgenl(1,temp2);
                        psr&=~F_FLAG;
                        temp&=31;
                        if (temp2&(1<<temp)) psr|=F_FLAG;
                        break;
                        case 0x4E: /*Type 6*/
                        opcode=readmemb(pc); pc++;
                        opcode|=(readmemb(pc)<<8); pc++;
                        isize=ilook[opcode&3];
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        if ((opcode&0x3F)==0x17) isize=1;
                        getgen(opcode>>11,0);
                        isize=ilook[opcode&3];
                        getgen(opcode>>6,1);
                        switch (opcode&0x3F)
                        {
                                case 8: /*CBITB*/
                                readgenb(0,temp); temp&=31;
                                if (gentype[1]) { readgenl(1,temp2); }
                                else            { readgenb(1,temp2); }
                                if (temp2&(1<<temp)) psr|=F_FLAG;
                                else                 psr&=~F_FLAG;
                                temp2&=~(1<<temp);
                                if (gentype[1]) { writegenl(1,temp2); }
                                else            { writegenb(1,temp2); }
                                break;
                                case 0x17: /*LSHD*/
                                readgenb(0,temp); if (temp&0xE0) temp|=0xE0;
                                readgenl(1,temp2);
                                if (temp&0xE0) temp2>>=((temp^0xFF)+1);
                                else           temp2<<=temp;
                                writegenl(1,temp2);
                                break;
                                case 0x30: /*ABSB*/
                                readgenb(0,temp);
                                if (temp&0x80) temp=(temp^0xFF)+1;
                                writegenb(1,temp);
                                break;
                                case 0x34: /*COMB*/
                                readgenb(0,temp);
                                writegenb(1,~temp);
                                break;

                                default:
                                printf("Bad NS32016 4E opcode %04X %01X\n",opcode,opcode&0x3F);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;

                        case 0x7C: /*Type 3 byte*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        switch ((opcode>>7)&0xF)
                        {
                                case 2: /*BICPSR*/
                                readgenb(0,temp);
                                psr&=~temp;
                                break;
                                case 6: /*BISPSR*/
                                readgenb(0,temp);
                                psr|=temp;
//                                nsoutput=1;
                                break;
                                case 0xA: /*ADJSP*/
                                readgenb(0,temp2);
                                if (temp2&0x80) temp2|=0xFFFFFF00;
                                sp[SP]-=temp2;
                                break;
                                case 0xE: /*CASE*/
                                readgenb(0,temp);
                                if (temp&0x80) temp|=0xFFFFFF00;
                                pc=startpc+temp;
                                break;
                                
                                default:
                                printf("Bad NS32016 7C opcode %04X %01X\n",opcode,(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;

                        case 0x7D: /*Type 3 word*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        switch ((opcode>>7)&0xF)
                        {
                                case 2: /*BICPSR*/
                                readgenw(0,temp);
                                psr&=~temp;
                                break;
                                case 6: /*BISPSR*/
                                readgenw(0,temp);
                                psr|=temp;
                                break;
                                case 0xA: /*ADJSP*/
                                readgenw(0,temp2);
                                if (temp&0x8000) temp|=0xFFFF0000;
                                sp[SP]-=temp2;
                                break;
                                case 0xE: /*CASE*/
                                readgenw(0,temp);
                                if (temp&0x8000) temp|=0xFFFF0000;
                                pc=startpc+temp;
                                break;

                                default:
                                printf("Bad NS32016 7D opcode %04X %01X\n",opcode,(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        case 0x7F: /*Type 3 dword*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        switch ((opcode>>7)&0xF)
                        {
                                case 0: /*CXPD*/
                                readgenl(0,temp);
//                                printf("CXPD %08X\n",temp);
                                pushw(0);
                                pushw(mod);
                                pushd(pc);
                                mod=temp&0xFFFF;
                                temp3=temp>>16;
//                                printf("MOD %04X OFFSET %04X\n",mod,temp3);
                                sb=readmemw(mod)|(readmemw(mod+2)<<16);
//                                printf("SB = %08X\n",sb);
                                temp2=readmemw(mod+8)|(readmemw(mod+10)<<16);
//                                printf("PC temp2 = %08X\n",temp2);
                                pc=temp2+temp3;
//                                printf("PC = %08X\n",pc);
                                break;
                                case 4: /*JUMP*/
                                if (gentype[0]) pc=*(uint32_t *)genaddr[0];
                                else            pc=genaddr[0];
                                break;
                                case 0xA: /*ADJSP*/
                                readgenl(0,temp2);
                                sp[SP]-=temp2;
                                break;

                                default:
                                printf("Bad NS32016 7F opcode %04X %01X\n",opcode,(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;

                        case 0x2F: case 0xAF: /*SPR*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        switch ((opcode>>7)&0xF)
                        {
                                case 0x8: writegenl(0,fp); break;
                                case 0x9: writegenl(0,sp[SP]); break;
                                case 0xA: writegenl(0,sb); break;
                                case 0xF: writegenl(0,mod); /*nsoutput=1; */break;
                                
                                default:
                                printf("Bad SPR reg %01X\n",(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        case 0x6C: case 0xEC: /*LPRB*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        readgenb(0,temp);
                        switch ((opcode>>7)&0xF)
                        {
                                case 0: psr=(psr&0xFF00)|(temp&0xFF); break;
                                case 9: sp[SP]=temp; break;

                                default:
                                printf("Bad LPRB reg %01X\n",(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        case 0x6D: case 0xED: /*LPRW*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        readgenw(0,temp);
                        switch ((opcode>>7)&0xF)
                        {
                                case 15: mod=temp; break;
                                
                                default:
                                printf("Bad LPRW reg %01X\n",(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        case 0x6F: case 0xEF: /*LPRD*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        getgen1(opcode>>11,0);
                        getgen(opcode>>11,0);
                        readgenl(0,temp);
                        switch ((opcode>>7)&0xF)
                        {
                                case 9: sp[SP]=temp; break;
                                case 0xA: sb=temp; break;
                                case 0xE: intbase=temp; /*printf("INTBASE %08X %08X\n",temp,pc); */break;

                                default:
                                printf("Bad LPRD reg %01X\n",(opcode>>7)&0xF);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;

                        case 0xCE: /*Format 7*/
                        opcode=readmemb(pc); pc++;
                        opcode|=(readmemb(pc)<<8); pc++;
                        isize=ilook[opcode&3];
                        getgen1(opcode>>11,0);
                        getgen1(opcode>>6,1);
                        getgen(opcode>>11,0);
                        getgen(opcode>>6,1);
                        switch (opcode&0x3F)
                        {
                                case 0x00: /*MOVMB*/
                                temp=getdisp();
                                while (temp)
                                {
                                        temp2=readmemb(genaddr[0]); genaddr[0]++;
                                        writememb(genaddr[1],temp2); genaddr[1]++;
                                        temp--;
                                }
                                break;
                                case 0x08: /*INSSB*/
                                temp3=readmemb(pc); pc++;
                                readgenb(0,temp);
                                readgenb(1,temp2);
                                for (c=0;c<=(temp3&31);c++)
                                {
                                        temp2&=~(1<<((c+(temp3>>3))&7));
                                        if (temp&(1<<c)) temp2|=(1<<((c+(temp3>>3))&7));
                                }
                                writegenb(1,temp2);
                                break;
                                case 0x18: /*MOVZBD*/
//                                printf("Read MOVZ from %08X\n",genaddr[0]);
                                readgenb(0,temp);
                                if (sdiff[1]) sdiff[1]=4;
                                writegenl(1,temp)
                                break;
                                case 0x19: /*MOVZWD*/
                                readgenw(0,temp);
                                if (sdiff[1]) sdiff[1]=4;
                                writegenl(1,temp)
                                break;
                                case 0x2F: /*DEID*/
                                readgenl(0,temp);
                                readgenq(1,temp64);
                                if (!temp)
                                {
                                        printf("Divide by zero - DEID CE\n");
                                        n32016_dumpregs();
                                        exit(-1);
                                }
                                temp3=temp64%temp;
                                writegenl(1,temp3);
                                temp3=temp64/temp;
                                if (gentype[1]) *(uint32_t *)(genaddr[1]+4)=temp3;
                                else            { writememw(genaddr[1]+4,temp3); writememw(genaddr[1]+4+2,temp3>>16); }
                                break;
                                case 0x33: /*QUOD*/
                                readgenl(0,temp);
                                readgenl(1,temp2);
                                if (!temp)
                                {
                                        printf("Divide by zero - QUOD CE\n");
                                        n32016_dumpregs();
                                        exit(-1);
                                }
                                temp2/=temp;
                                writegenl(1,temp2);
                                break;
                                case 0x37: /*REMD*/
                                readgenl(0,temp);
                                readgenl(1,temp2);
                                if (!temp)
                                {
                                        printf("Divide by zero - QUOD CE\n");
                                        n32016_dumpregs();
                                        exit(-1);
                                }
                                temp2%=temp;
                                writegenl(1,temp2);
                                break;

                                default:
                                printf("Bad NS32016 CE opcode %04X %01X\n",opcode,opcode&0x3F);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;
                        
                        case 0x2E: case 0x6E: case 0xAE: case 0xEE: /*Type 8*/
                        opcode|=(readmemb(pc)<<8); pc++;
                        opcode|=(readmemb(pc)<<16); pc++;
                        getgen1(opcode>>19,0);
                        getgen1(opcode>>14,1);
                        getgen(opcode>>19,0);
                        getgen(opcode>>14,1);
                        temp=((opcode>>6)&3)|((opcode&0x400)>>8);
                        temp=(temp<<2)|((opcode>>8)&3);
                        switch (temp)
                        {
                                case 0: /*EXT*/
                                temp=r[(opcode>>11)&7]&31;
                                temp2=getdisp();
                                readgenl(0,temp3);
                                temp4=0;
                                for (c=0;c<temp2;c++)
                                {
                                        if (temp3&(1<<((c+temp)&31))) temp4|=(1<<c);
                                }
                                writegenl(1,temp4);
                                break;
                                case 0xC: /*CHECKB*/
                                readgenb(1,temp3);
                                temp=readmemb(genaddr[0]);
                                temp2=readmemb(genaddr[0]+1);
                                if (temp>=temp3 && temp3>=temp2)
                                {
                                        r[(opcode>>11)&7]=temp3-temp2;
                                        psr&=~F_FLAG;
                                }
                                else
                                   psr|=F_FLAG;
                                break;

//                                printf("EXT - R%i %08X R%i %08X R%i %08X %08X\n",temp,genaddr[0],((int)genaddr[0]-(int)&r[0])/4,genaddr[1],((int)genaddr[1]-(int)&r[0])/4,temp2,pc);
//                                exit(-1);

                                default:
                                printf("Bad NS32016 Type 8 opcode %04X %01X %i\n",opcode,temp,(opcode>>11)&7);
                                n32016_dumpregs();
                                exit(-1);
                        }
                        break;

                        case 0x02: /*BSR*/
                        temp=getdisp();
                        pushd(pc);
                        pc=startpc+temp;
                        break;
                        case 0x12: /*RET*/
                        temp=getdisp();
                        pc=popd();
                        sp[SP]+=temp;
                        break;
                        case 0x22: /*CXP*/
                        temp=getdisp();
                        pushw(0);
                        pushw(mod);
                        pushd(pc);
//                        printf("CXP %08X\n",temp);
                        temp2=readmemw(mod+4)+(readmemw(mod+6)<<16)+(4*temp);
//                        printf("%08X\n",temp2);
                        temp=readmemw(temp2)+(readmemw(temp2+2)<<16);
//                        printf("%08X\n",temp);
                        mod=temp&0xFFFF;
//                        printf("MOD=%04X\n",mod);
                        sb=readmemw(mod)+(readmemw(mod+2)<<16);
//                        printf("SB=%08X\n",sb);
                        pc=readmemw(mod+8)+(readmemw(mod+10)<<16)+(temp>>16);
//                        printf("PC=%08X\n",pc);
                        nsoutput=1;
                        break;
                        case 0x32: /*RXP*/
//                        nsoutput=1;
                        temp=getdisp();
                        pc=popd();
                        temp2=popd();
                        mod=temp2&0xFFFF;
                        sp[SP]+=temp;
                        sb=readmemw(mod)|(readmemw(mod+2)<<16);
                        break;
                        case 0x42: /*RETT*/
                        temp=getdisp();
                        pc=popd();
                        mod=popw();
                        psr=popw();
                        sp[SP]+=temp;
                        sb=readmemw(mod)|(readmemw(mod+2)<<16);
                        break;
                        case 0x62: /*SAVE*/
                        temp=readmemb(pc); pc++;
                        for (c=0;c<8;c++)
                        {
                                if (temp&(1<<c))
                                {
                                        pushd(r[c]);
//                                        printf("SAVE R%i\n",c);
                                }
                        }
                        break;
                        case 0x72: /*RESTORE*/
                        temp=readmemb(pc); pc++;
                        for (c=0;c<8;c++)
                        {
                                if (temp&(1<<c))
                                {
                                        r[c^7]=popd(r[c]);
//                                        printf("RESTORE R%i\n",c^7);
                                }
                        }
                        break;
                        case 0x82: /*ENTER*/
                        temp=readmemb(pc); pc++;
                        temp2=getdisp();
                        pushd(fp);
                        fp=sp[SP];
                        sp[SP]-=temp2;
//                        printf("ENTER - Rlist %02X Disp %08X\n",temp,temp2);
                        for (c=0;c<8;c++)
                        {
                                if (temp&(1<<c))
                                {
                                        pushd(r[c]);
//                                        printf("ENTER R%i\n",c);
                                }
                        }
                        break;
                        case 0x92: /*EXIT*/
                        temp=readmemb(pc); pc++;
                        for (c=0;c<8;c++)
                        {
                                if (temp&(1<<c))
                                {
                                        r[c^7]=popd(r[c]);
//                                        printf("EXIT R%i\n",c^7);
                                }
                        }
                        sp[SP]=fp;
                        fp=popd();
                        break;
                        case 0xE2: /*SVC*/
//                        if (startpc<0x8000) nsoutput=1;
//if (startpc==0xF016D9) nsoutput=1;
                        temp=psr;
                        psr&=~0x700;
//                        printf("Push %04X\n",temp); pushw(temp);
//                        printf("Push %04X\n",mod); pushw(mod);
//                        printf("Push %08X\n",startpc); pushd(startpc);
//                        printf("SVC!\n");
//                        n32016_dumpregs();
//                        exit(-1);
                        temp=readmemw(intbase+(5*4))|(readmemw(intbase+(5*4)+2)<<16);
                        mod=temp&0xFFFF;
                        temp3=temp>>16;
//                        printf("MOD %04X OFFSET %04X\n",mod,temp3);
                        sb=readmemw(mod)|(readmemw(mod+2)<<16);
//                        printf("SB = %08X\n",sb);
                        temp2=readmemw(mod+8)|(readmemw(mod+10)<<16);
//                        printf("PC temp2 = %08X\n",temp2);
                        pc=temp2+temp3;
                        //printf("PC = %08X\n",pc);
                        break;

                        case 0x0A: /*BEQ*/
                        temp=getdisp();
                        if (psr&Z_FLAG) pc=startpc+temp;
                        break;
                        case 0x1A: /*BNE*/
                        temp=getdisp();
                        if (!(psr&Z_FLAG)) pc=startpc+temp;
                        break;
                        case 0x4A: /*BH*/
                        temp=getdisp();
                        if (psr&L_FLAG) pc=startpc+temp;
                        break;
                        case 0x5A: /*BLS*/
                        temp=getdisp();
                        if (!(psr&L_FLAG)) pc=startpc+temp;
                        break;
                        case 0x6A: /*BGT*/
                        temp=getdisp();
                        if (psr&N_FLAG) pc=startpc+temp;
                        break;
                        case 0x7A: /*BLE*/
                        temp=getdisp();
                        if (!(psr&N_FLAG)) pc=startpc+temp;
                        break;
                        case 0x8A: /*BFS*/
                        temp=getdisp();
                        if (psr&F_FLAG) pc=startpc+temp;
                        break;
                        case 0x9A: /*BFC*/
                        temp=getdisp();
                        if (!(psr&F_FLAG)) pc=startpc+temp;
                        break;
                        case 0xAA: /*BLO*/
                        temp=getdisp();
                        if (!(psr&(L_FLAG|Z_FLAG))) pc=startpc+temp;
                        break;
                        case 0xBA: /*BHS*/
                        temp=getdisp();
                        if (psr&(L_FLAG|Z_FLAG)) pc=startpc+temp;
                        break;
                        case 0xCA: /*BLT*/
                        temp=getdisp();
                        if (!(psr&(N_FLAG|Z_FLAG))) pc=startpc+temp;
                        break;
                        case 0xDA: /*BGE*/
                        temp=getdisp();
                        if (psr&(N_FLAG|Z_FLAG)) pc=startpc+temp;
                        break;
                        case 0xEA: /*BR*/
                        pc=startpc+getdisp();
                        break;
                        
                        default:
                        printf("Bad NS32016 opcode %02X\n",opcode);
                        n32016_dumpregs();
                        exit(-1);
                }
                tubecycles-=8;
                if (tube_irq&2)
                {
                        temp=psr;
                        psr&=~0xF00;
                        pushw(temp);
                        pushw(mod);
                        pushd(pc);
//                        printf("NMI!\n");
                        temp=readmemw(intbase+(1*4))|(readmemw(intbase+(1*4)+2)<<16);
                        mod=temp&0xFFFF;
                        temp3=temp>>16;
//                        printf("MOD %04X OFFSET %04X\n",mod,temp3);
                        sb=readmemw(mod)|(readmemw(mod+2)<<16);
//                        printf("SB = %08X\n",sb);
                        temp2=readmemw(mod+8)|(readmemw(mod+10)<<16);
//                        printf("PC temp2 = %08X\n",temp2);
                        pc=temp2+temp3;
//                        printf("PC = %08X\n",pc);
                }
                if ((tube_irq&1) && (psr&0x800))
                {
                        temp=psr;
                        psr&=~0xF00;
                        pushw(temp);
                        pushw(mod);
                        pushd(pc);
//                        printf("Interrupt!\n");
                        temp=readmemw(intbase)|(readmemw(intbase+2)<<16);
                        mod=temp&0xFFFF;
                        temp3=temp>>16;
//                        printf("MOD %04X OFFSET %04X\n",mod,temp3);
                        sb=readmemw(mod)|(readmemw(mod+2)<<16);
//                        printf("SB = %08X\n",sb);
                        temp2=readmemw(mod+8)|(readmemw(mod+10)<<16);
//                        printf("PC temp2 = %08X\n",temp2);
                        pc=temp2+temp3;
//                        printf("PC = %08X\n",pc);
//                        nsoutput=1;
                }
/*                if ((oldpsr^psr)&0x800)
                {
                        if (psr&0x800) printf("INT enabled at %08X\n",startpc);
                        else           printf("INT disabled at %08X\n",startpc);
                }*/
                oldpsr=psr;
        }
}
