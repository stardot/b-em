/*B-em 0.8
  Tube emulation
  Emulates 4mhz 65C02, 4mhz Z80 and 4mhz ARM tubes for now*/
/*Doesn't actually work - so most of it has been commented out*/
#include <stdio.h>
/*#include "m6502.h"
#include "mz80.h"*/
#include "arm.h"

#define TUBE6502 1
#define TUBEZ80  2
#define TUBEARM  3

void exectube2();
int tubecycs=0,tubelinecycs;
int tubeirq=0;
int tubetype=TUBE6502;
int romin=1;
int tube=0;
int interrupt;
int slicereads=0;
unsigned short pc;
/*struct m6502context m6502;
struct mz80context mz80;*/

struct
{
        unsigned char ph1[24],ph2,ph3[2],ph4;
        unsigned char hp1,hp2,hp3[2],hp4;
        unsigned char hstat[4],pstat[4],r1stat;
        int ph1pos,ph3pos,hp3pos;
} tubeula;

unsigned char tuberam[65536],spareram[4096];

void updatetubeints()
{
        int oldtubeirq=tubeirq;
        tubeirq=0;
        interrupt&=~8;
        if ((tubeula.r1stat&1) && (tubeula.hstat[3]&128)) interrupt|=8;
        if ((tubeula.r1stat&2) && (tubeula.pstat[0]&128)) tubeirq|=1;
        if ((tubeula.r1stat&4) && (tubeula.pstat[3]&128)) tubeirq|=1;
        if ((tubeula.r1stat&8) && (tubeula.pstat[2]&128)) tubeirq|=2;
        if (tubeirq && !oldtubeirq) printf("TUBE IRQ %i\n",tubeirq);
        if (!tubeirq && oldtubeirq) printf("TUBE IRQ CLEARED\n");
//        printf("%i %i\n",tubeirq,interrupt);
//        if ((tubeula.r1stat&8) && (tubeula.pstat[3]&128)) tubeirq|=1;
}
unsigned char readtubehost(unsigned short addr)
{
        unsigned char temp=0;
        int c;
        if (!tube) return 0xFE;
        exectube2();
        if (pc!=0x6A1) printf("Tube host read %04X %04X  ",addr,pc);
        switch (addr&7)
        {
                case 0: /*Reg 1 Stat*/
                temp=tubeula.hstat[0]|tubeula.r1stat;
                break;
                case 1: /*Register 1*/
                temp=tubeula.ph1[0];
                for (c=0;c<23;c++) tubeula.ph1[c]=tubeula.ph1[c+1];
                tubeula.ph1pos--;
                tubeula.pstat[0]|=0x40;
                if (!tubeula.ph1pos) tubeula.hstat[0]&=~0x80;
                printf("Read pos %i\n",tubeula.ph1pos);
                break;
                case 2: /*Register 2 Stat*/
                temp=tubeula.hstat[1];
                break;
                case 3: /*Register 2*/
                temp=tubeula.ph2;
                if (tubeula.hstat[1]&0x80)
                {
                        tubeula.hstat[1]&=~0x80;
                        tubeula.pstat[1]|=0x40;
                }
                break;
                case 4: /*Register 3 Stat*/
                temp=tubeula.hstat[2];
                break;
                case 5: /*Register 3*/
                temp=tubeula.ph3[0];
                tubeula.ph3[0]=tubeula.ph3[1];
                tubeula.ph3pos--;
                tubeula.pstat[2]|=0x40;
                if (!tubeula.ph3pos) tubeula.hstat[2]&=~0x80;
                break;
                case 6: /*Register 4 Stat*/
                temp=tubeula.hstat[3];
                break;
                case 7: /*Register 4*/
                temp=tubeula.ph4;
                if (tubeula.hstat[3]&0x80)
                {
                        tubeula.hstat[3]&=~0x80;
                        tubeula.pstat[3]|=0x40;
                }
                break;
                default:
                printf("Tube host read %04X %04X\n",addr,pc);
        }
        updatetubeints();
        if (pc!=0x6A1) printf("%02X\n",temp);
//        exectubeshort();
        return temp;
}

void writetubehost(unsigned short addr, unsigned char val)
{
        if (!tube) return;
        exectube2();
        printf("Tube host write %04X %04X %02X\n",addr,pc,val);
        switch (addr&7)
        {
                case 0: /*Register 1 stat*/
                if (val&0x80) tubeula.r1stat|=(val&0x3F);
                else          tubeula.r1stat&=~(val&0x3F);
                printf("R1stat %02X %02X\n",tubeula.r1stat,val);
                tubeula.hstat[0]=(tubeula.hstat[0]&0xC0)|(val&0x3F);
                break;
                case 1: /*Register 1*/
                tubeula.hp1=val;
                tubeula.pstat[0]|=0x80;
                tubeula.hstat[0]&=~0x40;
                break;
                case 3: /*Register 2*/
                tubeula.hp2=val;
                tubeula.pstat[1]|=0x80;
                tubeula.hstat[1]&=~0x40;
                break;
                case 5: /*Register 3*/
                if (tubeula.r1stat&16)
                {
                        if (tubeula.hp3pos<2)
                           tubeula.hp3[tubeula.hp3pos++]=val;
                        if (tubeula.hp3pos==2)
                        {
                                tubeula.pstat[2]|=0x80;
                                tubeula.hstat[2]&=~0x40;
                        }
                }
                else
                {
                        tubeula.hp3[0]=val;
                        tubeula.hp3pos=1;
                        tubeula.pstat[2]|=0x80;
                        tubeula.hstat[2]&=~0x40;
                }
                break;
                case 7: /*Register 4*/
                tubeula.hp4=val;
                tubeula.pstat[3]|=0x80;
                tubeula.hstat[3]&=~0x40;
                updatetubeints();
//                tubeirq|=1;
                break;
                default:
                printf("Tube host write %04X %02X %04X\n",addr,val,pc);
        }
        updatetubeints();
//        exectubeshort();
}

unsigned short get6502pc()
{
/*        m6502zpGetContext(&m6502);
        return m6502.m6502pc;*/
}

unsigned char readtube(unsigned long addr, char *p)
{
        unsigned char temp=0;
        /*if (PC!=0x3000D64) */if (addr&1) printf("Tube Parasite read %04X %04X  ",addr,get6502pc());
//        dumptuberegs();
        switch (addr&7)
        {
                case 0: /*Register 1 stat*/
                if (romin)
                {
                        printf("ROM mapped out\n");
                        memcpy(tuberam+0xF000,spareram,0x1000);
                        romin=0;
                }
//                if (tubeula.hbytes[0]!=24) return tubeula.pstat[0]|0x40;
                temp=tubeula.pstat[0]|tubeula.r1stat;
                break;
                case 1: /*Register 1*/
                temp=tubeula.hp1;
                if (tubeula.pstat[0]&0x80)
                {
                        tubeula.pstat[0]&=~0x80;
                        tubeula.hstat[0]|=0x40;
                }
                break;
                case 2: /*Register 2 stat*/
                temp=tubeula.pstat[1];
                printf("FEFA read %02X\n",temp);
                break;
                case 3: /*Register 2*/
                temp=tubeula.hp2;
                if (tubeula.pstat[1]&0x80)
                {
                        tubeula.pstat[1]&=~0x80;
                        tubeula.hstat[1]|=0x40;
                }
                break;
                case 4: /*Register 3 stat*/
                temp=tubeula.pstat[2];
                break;
                case 5: /*Register 3*/
                temp=tubeula.hp3[0];
                tubeula.hp3[0]=tubeula.hp3[1];
                tubeula.hp3pos--;
                tubeula.hstat[2]|=0x40;
                if (!tubeula.hp3pos) tubeula.pstat[2]&=~0x80;
                break;
                case 6: /*Register 4 stat*/
                temp=tubeula.pstat[3];
                break;
                case 7: /*Register 4*/
                temp=tubeula.hp4;
                if (tubeula.pstat[3]&0x80)
                {
                        tubeula.pstat[3]&=~0x80;
                        tubeula.hstat[3]|=0x40;
                }
//                updatetubeints();
//                tubeirq&=~1;
                break;
                default:
                printf("Tube parasite read %04X\n",addr);
        }
        updatetubeints();
        slicereads++;
//        if (tube==TUBE6502 && slicereads==2) m6502zpReleaseTimeslice();
        if (addr&1) printf("%02X\n",temp);
        return temp;
}

void writetube(unsigned long addr, unsigned char val, char *p)
{
        printf("Tube parasite write %04X %04X %02X\n",addr,get6502pc(),val);
//        dumptuberegs();
        switch (addr&7)
        {
                case 1: /*Register 1*/
                if (tubeula.ph1pos<24)
                {
                        printf("Write pos %i\n",tubeula.ph1pos);
                        tubeula.ph1[tubeula.ph1pos++]=val;
                        tubeula.hstat[0]|=0x80;
                        if (tubeula.ph1pos==24) tubeula.pstat[0]&=~0x40;
                }
                break;
                case 3: /*Register 2*/
                tubeula.ph2=val;
                tubeula.hstat[1]|=0x80;
                tubeula.pstat[1]&=~0x40;
                break;
                case 5: /*Register 3*/
                if (tubeula.r1stat&16)
                {
                        if (tubeula.ph3pos<2)
                           tubeula.ph3[tubeula.ph3pos++]=val;
                        if (tubeula.ph3pos==2)
                        {
                                tubeula.hstat[2]|=0x80;
                                tubeula.pstat[2]&=~0x40;
                        }
                }
                else
                {
                        tubeula.ph3[0]=val;
                        tubeula.ph3pos=1;
                        tubeula.hstat[2]|=0x80;
                        tubeula.pstat[2]&=~0x40;
                }
                break;
                case 7: /*Register 4*/
                tubeula.ph4=val;
                tubeula.hstat[3]|=0x80;
                tubeula.pstat[3]&=~0x40;
                break;
                default:
                printf("Tube parasite write %04X %02X\n",addr,val);
        }
        updatetubeints();
//        if (tube==TUBE6502) m6502zpReleaseTimeslice();
}

unsigned short readtubep(unsigned short port, char *pPR)
{
        return readtube(port,NULL);
}
static void writetubep(unsigned short port, unsigned char val, char *pPW)
{
        writetube(port,val,NULL);
//        m6502zpReleaseTimeslice();
}

void write6502high(unsigned long addr, unsigned char val, char *p)
{
//        printf("%04X write %02X %i\n",addr,val,romin);
        if (romin) spareram[addr&0xFFF]=val;
        else       tuberam[addr]=val;
}

/*struct MemoryReadByte read6502tube[] =
{
        {0xFEF8,0xFEFF,readtube},
        {-1,-1,NULL}
};
struct MemoryWriteByte write6502tube[] =
{
        {0xFEF8,0xFEFF,writetube},
        {0xF000,0xFFFF,write6502high},
        {-1,-1,NULL}
};

struct MemoryReadByte readz80tube[] =
{
        {-1,-1,NULL}
};
struct MemoryWriteByte writez80tube[] =
{
        {-1,-1,NULL}
};
struct z80PortRead readpz80tube[] =
{
        {0,7,readtubep,NULL},
        {-1,-1,NULL}
};
struct z80PortWrite writepz80tube[] =
{
        {0,7,writetubep,NULL},
        {-1,-1,NULL}
};*/

void tubeinit6502()
{
/*        FILE *f=fopen("roms/tube/6502tube.rom","rb");
        fread(tuberam+0xF800,0x0800,1,f);
        fclose(f);
        m6502.m6502Base=tuberam;
        m6502.m6502MemoryRead=read6502tube;
        m6502.m6502MemoryWrite=write6502tube;
        m6502zpSetContext(&m6502);
        m6502zpreset();
        tubeula.ph1pos=tubeula.ph3pos=tubeula.hp3pos=0;
        tubeula.r1stat=0;
        tubeula.hstat[0]=tubeula.hstat[1]=tubeula.hstat[2]=tubeula.hstat[3]=0x40;
        tubeula.pstat[0]=tubeula.pstat[1]=tubeula.pstat[2]=tubeula.pstat[3]=0x40;*/
}

void tubeinitz80()
{
/*        FILE *f=fopen("roms/tube/z80_120.rom","rb");
        fread(tuberam,0x1000,1,f);
        fclose(f);
        mz80.z80Base=tuberam;
        mz80.z80MemRead=readz80tube;
        mz80.z80MemWrite=writez80tube;
        mz80.z80IoRead=readpz80tube;
        mz80.z80IoWrite=writepz80tube;
        mz80SetContext(&mz80);
        mz80reset();
        tubeula.ph1pos=tubeula.ph3pos=tubeula.hp3pos=0;
        tubeula.r1stat=0;
        tubeula.hstat[0]=tubeula.hstat[1]=tubeula.hstat[2]=tubeula.hstat[3]=0x40;
        tubeula.pstat[0]=tubeula.pstat[1]=tubeula.pstat[2]=tubeula.pstat[3]=0x40;*/
/*        tubeula.hpos[0]=tubeula.hpos[1]=tubeula.hpos2[0]=tubeula.hpos2[1]=0;
        tubeula.ppos=tubeula.ppos2=0;
        tubeula.hstat[0]=0;
        tubeula.hbytes[0]=tubeula.hbytes[1]=0;
        tubeula.hbytes[2]=tubeula.hbytes[3]=0;*/
}

void resettube()
{
        tubeula.ph1pos=tubeula.ph3pos=tubeula.hp3pos=0;
        tubeula.r1stat=0;
        tubeula.hstat[0]=tubeula.hstat[1]=tubeula.hstat[2]=tubeula.hstat[3]=0x40;
        tubeula.pstat[0]=tubeula.pstat[1]=tubeula.pstat[2]=tubeula.pstat[3]=0x40;
}

void dumptuberegs()
{
/*        if (tubetype==TUBE6502)
        {
                m6502zpGetContext(&m6502);
                printf("Tube regs :\n");
                printf("A=%02X X=%02X Y=%02X PC=%04X\n",m6502.m6502af>>8,m6502.m6502x,m6502.m6502y,m6502.m6502pc);
        }
        else
        {
                mz80GetContext(&mz80);
                printf("Tube regs :\n");
                printf("AF=%04X BC=%04X DE=%04X HL=%04X PC=%04X\n",mz80.z80af.af,mz80.z80bc.bc,mz80.z80de.de,mz80.z80hl.hl,mz80.z80pc);
        }*/
}

void dumptube()
{
        FILE *f=fopen("tuberam.dmp","wb");
        fwrite(tuberam,65536,1,f);
        fclose(f);
}

void exectube()
{
        unsigned long res;
        slicereads=0;
        if (tubelinecycs<=0)
        {
                tubelinecycs=8;
//                printf("Error - tubelinecycs %i\n",tubelinecycs);
//                dumpregs();
//                exit(-1);
        }
//        printf("Tube execute\n");
        if (tubetype==TUBEZ80)
        {
/*                if (tubeirq&2)
                   mz80nmi();
                if (tubeirq&1)
                   mz80int(0);
                res=mz80exec(256);
                if (res!=0x80000000)
                {
                        printf("Tube error %08X\n",res);
                        dumpregs();
                        dumptuberegs();
                        dumptube();
                        exit(-1);
                }*/
        }
        else if (tubetype==TUBE6502)
        {
/*                if (tubeirq&2)
                   m6502zpnmi();
                if (tubeirq&1)
                   m6502zpint(0);
                res=m6502zpexec(tubelinecycs*2);
                if (res!=0x80000000)
                {
                        printf("Tube error %08X\n",res);
                        dumpregs();
                        dumptuberegs();
                        dumptube();
                        exit(-1);
                }*/
        }
        else
        {
                execarm(256);
        }
}

/*void exectubeshort()
{
        unsigned long res;
        slicereads=0;
        if (tubetype==TUBEZ80)
        {
                if (tubeirq&2)
                   mz80nmi();
                if (tubeirq&1)
                   mz80int(0);
//                mz80nmi();
                res=mz80exec(256);
                if (res!=0x80000000)
                {
                        printf("Tube error %08X\n",res);
                        dumpregs();
                        dumptuberegs();
                        dumptube();
                        exit(-1);
                }
        }
        else if (tubetype==TUBE6502)
        {
                if (tubeirq&2)
                   m6502zpnmi();
                if (tubeirq&1)
                   m6502zpint(0);
                res=m6502zpexec(128);
                if (res!=0x80000000)
                {
                        printf("Tube error %08X\n",res);
                        dumpregs();
                        dumptuberegs();
                        dumptube();
                        exit(-1);
                }
        }
        else
        {
                execarm(128);
        }
}*/

void exectube2()
{
        unsigned long res;
        if (tubecycs<=0)
        {
                return;
//                printf("Error - tubecycs %i\n",tubecycs);
//                dumpregs();
//                dumpram();
//                dumptuberegs();
//                dumptube();
//                exit(-1);
        }
//        printf("Execute for %i cycles\n",tubecycs);
        if (tubetype==TUBE6502)
        {
/*                if (tubeirq&2)
                   m6502zpnmi();
                if (tubeirq&1)
                   m6502zpint(0);
                res=m6502zpexec(tubecycs*2);
                if (res!=0x80000000)
                {
                        printf("Tube error %08X\n",res);
                        dumpregs();
                        dumptuberegs();
                        dumptube();
                        exit(-1);
                }*/
        }
        tubelinecycs-=tubecycs;
        tubecycs=0;
}
