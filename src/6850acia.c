/*B-em 1.4 by Tom Walker*/
/*6850 acia emulation*/

#include <stdio.h>
#include "b-em.h"
#include "acia.h"
#include "serial.h"

#define DCD     4
#define RECIEVE 1

char err2[256];
FILE *cswlog;

int tapespeed=0;

int dreg=0;
int cleardcd=0;
int tapedelay=0;
unsigned char serialreg;
FILE *tape;
FILE *tapelog;
int tapepos;
unsigned char aciasr=8;
unsigned char aciadrs;
unsigned char aciadrw;

void updateaciaint()
{
        if ((aciasr&0x80) && (aciacr&0x80))
        {
                interrupt|=4;
//                output=1;
        }
        else
           interrupt&=~4;
//        printf("Update int %02X %02X %i\n",aciasr,aciacr,interrupt);
}

void resetacia()
{
        aciasr=(aciasr&8)|4;
        updateaciaint();
}

unsigned char readacia(unsigned short addr)
{
        char s[80];
        unsigned char temp;
//        if (!cswlog) cswlog=fopen("cswlog.txt","wt");
        if (addr&1)
        {
                temp=aciadr;
//                rpclog("Read data %02X %04X %02X %i\n",temp,pc,aciasr,interrupt);
                aciasr&=~0x81;
                updateaciaint();
//                sprintf(err2,"Read data %02X\n",temp);
//                fputs(err2,cswlog);
                return temp;
        }
        else
        {
//                rpclog("Read status %02X %02X %04X\n",aciasr,(aciasr&aciacr&0x80),pc);
//                output=1;
//                timetolive=3;
//                sprintf(err2,"Read status %02X\n",aciasr);
//                fputs(err2,cswlog);
                return (aciasr&0x7F)|(aciasr&aciacr&0x80);
        }
}

void writeacia(unsigned short addr, unsigned char val)
{
//        rpclog("Write ACIA %04X %02X %04X\n",addr,val,pc);
        if (addr&1)
        {
                aciadrw=val;
                aciasr&=0xFD;
                updateaciaint();
        }
        else
        {
                aciacr=val;
                if (val==3)
                   resetacia();
//                rpclog("Write CTRL %02X %04X\n",val,pc);
                switch (val&3)
                {
                        case 1: tapespeed=0; break;
                        case 2: tapespeed=1; break;
                }
        }
}

void dcd()
{
        if (aciasr&DCD) return;
        aciasr|=DCD|0x80;
//        printf("DCD interrupt\n");
        updateaciaint();
}

void dcdlow()
{
        aciasr&=~DCD;
        updateaciaint();
}

void receive(unsigned char val) /*Called when the acia recives some data*/
{
        aciadr=val;
        aciasr|=RECIEVE|0x80;
//        printf("recieve interrupt\n");
        updateaciaint();
//        printf("Recieved %02X %04X   ",val,pc);
//        sprintf(err2,"Recieved %02X\n",val);
//        fputs(err2,cswlog);
}

int cswena;
void pollacia()
{
        int c;
        if (motor)
        {
                if (cswena) pollcsw();
                else        polltape();
//                for (c=0;c<2;c++) pollcsw();
        }
//           polltape();
}
