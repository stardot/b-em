/*B-em v2.1 by Tom Walker
  Tube ULA emulation*/

#include <stdio.h>
#include "b-em.h"
/*#include "m6502.h"
#include "mz80.h"*/

#define TUBE6502  1
#define TUBEZ80   2
#define TUBEARM   3
#define TUBEX86   4
#define TUBE65816 5
#define TUBE32016 6

int tube6502speed=1;
int tubedat=0;

int tubetimetolive,tubeoutput;
int tubecycs,tubelinecycs;
int tube;
int tubeskipint;
int tubenmi,tubeoldnmi,tubenmilock;
int tubeirqx;

int tubecycs=0,tubelinecycs;
int tubeirq=0;
int tubetype=TUBEX86;
int romin=1;
int tube=1;
int interrupt;
int slicereads=0;
uint16_t pc;
/*struct m6502context m6502;
struct mz80context mz80;*/

struct
{
        uint8_t ph1[24],ph2,ph3[2],ph4;
        uint8_t hp1,hp2,hp3[2],hp4;
        uint8_t hstat[4],pstat[4],r1stat;
        int ph1pos,ph3pos,hp3pos;
} tubeula;

uint8_t *tuberam,spareram[4096];

void updatetubeints()
{
        int oldtubeirq=tubeirq;
        tubeirq=tubeirqx=0;
        interrupt&=~8;
        if ((tubeula.r1stat&1) && (tubeula.hstat[3]&128)) interrupt|=8;
        if ((tubeula.r1stat&2) && (tubeula.pstat[0]&128)) tubeirq|=1;
        if ((tubeula.r1stat&4) && (tubeula.pstat[3]&128)) tubeirq|=1;
        if ((tubeula.r1stat&2) && (tubeula.pstat[0]&128)) tubeirqx|=1;
        if ((tubeula.r1stat&4) && (tubeula.pstat[3]&128)) tubeirqx|=2;

//        if ((tubeula.r1stat&8) && ((tubeula.pstat[2]&128) || !tubeula.ph3pos)) tubeirq|=2;
        if ((tubeula.r1stat&8) && !(tubeula.r1stat&16) && ((tubeula.hp3pos>0) || (tubeula.ph3pos==0))) tubeirq|=2;
        if ((tubeula.r1stat&8) &&  (tubeula.r1stat&16) && ((tubeula.hp3pos>1) || (tubeula.ph3pos==0))) tubeirq|=2;
//        if (tubeirq&2) printf("NMI set - %i %i %i\n",tubeula.r1stat&16,tubeula.hp3pos,tubeula.ph3pos);
}

uint8_t readtubehost(uint16_t addr)
{
        uint8_t temp=0;
        int c;
        if (!tubeexec) return 0xFE;
//        rpclog("Read HOST  %04X ",addr);
//        exit(-1);
        switch (addr&7)
        {
                case 0: /*Reg 1 Stat*/
                temp=(tubeula.hstat[0]&0xC0)|tubeula.r1stat;
                break;
                case 1: /*Register 1*/
                temp=tubeula.ph1[0];
                for (c=0;c<23;c++) tubeula.ph1[c]=tubeula.ph1[c+1];
                tubeula.ph1pos--;
                tubeula.pstat[0]|=0x40;
                if (!tubeula.ph1pos) tubeula.hstat[0]&=~0x80;
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
                if (tubeula.ph3pos>0)
                {
                        tubeula.ph3[0]=tubeula.ph3[1];
                        tubeula.ph3pos--;
                        tubeula.pstat[2]|=0x40;
                        if (!tubeula.ph3pos) tubeula.hstat[2]&=~0x80;
                }
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
        }
//        rpclog("%02X\n",temp);
        updatetubeints();
        return temp;
}

void writetubehost(uint16_t addr, uint8_t val)
{
        if (!tubeexec) return;
//        rpclog("Write HOST %04X %02X\n",addr,val);
        switch (addr&7)
        {
                case 0: /*Register 1 stat*/
                if (val&0x80) tubeula.r1stat|=(val&0x3F);
                else          tubeula.r1stat&=~(val&0x3F);
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
                tubedat++;
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
                        updatetubeints();
                }
//                printf("Write R3 %i\n",tubeula.hp3pos);
                break;
                case 7: /*Register 4*/
                tubeula.hp4=val;
                tubeula.pstat[3]|=0x80;
                tubeula.hstat[3]&=~0x40;
                break;
        }
        updatetubeints();
}

uint16_t get6502pc()
{
}

uint8_t readtube(uint32_t addr)
{
        uint8_t temp=0;
        switch (addr&7)
        {
                case 0: /*Register 1 stat*/
                if (romin)
                {
                        if (tubetype==TUBE6502 || tubetype==TUBE65816)
                           tube6502mapoutrom();
                        romin=0;
                }
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
//                printf("Read tube R3 %i\n",tubeula.hp3pos);
                tubedat--;
                temp=tubeula.hp3[0];
                if (tubeula.hp3pos>0)
                {
                        tubeula.hp3[0]=tubeula.hp3[1];
                        tubeula.hp3pos--;
                        if (!tubeula.hp3pos)
                        {
                                tubeula.hstat[2]|=0x40;
                                tubeula.pstat[2]&=~0x80;
                        }
                }
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
                break;
        }
        updatetubeints();
        return temp;
}

void writetube(uint32_t addr, uint8_t val)
{
        switch (addr&7)
        {
                case 1: /*Register 1*/
                if (tubeula.ph1pos<24)
                {
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
        }
        updatetubeints();
}

void tubeinit6502()
{
        tubetype=TUBE6502;
        tubeinitmem();
        tubeloadrom();
        tubereset6502();
        tubeexec=tubeexec65c02;
        tubeshift=tube6502speed;//1;
}

void updatetubespeed()
{
        if (tubetype==TUBE6502) tubeshift=tube6502speed;
}

void tubeinitarm()
{
        tubetype=TUBEARM;
        loadarmrom();
        resetarm();
        tubeexec=execarm;
        tubeshift=1;
}

void tubeinitz80()
{
        tubetype=TUBEZ80;
        z80_loadrom();
        resetz80();
        tubeexec=execz80;
        tubeshift=2;
}

void tubeinitx86()
{
        tubetype=TUBEX86;
        initmemx86();
        resetx86();
        tubeexec=execx86;
        tubeshift=2;
}

void tubeinit65816()
{
        tubetype=TUBE65816;
        init65816();
        reset65816();
        tubeexec=exec65816;
        tubeshift=3;
}

void tubeinit32016()
{
        tubetype=TUBE32016;
        init32016();
        reset32016();
        tubeexec=exec32016;
        tubeshift=2;
}

void resettube()
{
        tubeula.ph1pos=tubeula.hp3pos=0;
        tubeula.ph3pos=1;
        tubeula.r1stat=0;
        tubeula.hstat[0]=tubeula.hstat[1]=tubeula.hstat[3]=0x40;
        tubeula.pstat[0]=tubeula.pstat[1]=tubeula.pstat[2]=tubeula.pstat[3]=0x40;
        tubeula.hstat[2]=0xC0;
        romin=1;
}

void dumptube()
{
        FILE *f=fopen("tuberam.dmp","wb");
        fwrite(tuberam,65536,1,f);
        fclose(f);
}

