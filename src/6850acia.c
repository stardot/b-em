/*B-em 0.7 by Tom Walker*/
/*6850 acia emulation*/
/*God this is old :)*/
#include <stdio.h>
#include "acia.h"
#include "serial.h"

#define DCD     4
#define RECIEVE 1

int lns;
int dreg=0;
int output;
int cleardcd=0;
int tapedelay=0;
unsigned char serialreg;
FILE *tape;
FILE *tapelog;
int tapepos;
int interrupt;
unsigned char aciasr=8;
unsigned char aciadrs;

void updateaciaint()
{
        if ((aciasr&0x80) && (aciacr&0x80))
        {
                interrupt|=4;
//                output=1;
        }
        else
           interrupt&=~4;
}

void resetacia()
{
        aciadrf=0;
        aciasr=(aciasr&8)|4;
        updateaciaint();
}

unsigned char readacia(unsigned short addr)
{
        char s[80];
        unsigned char temp;
        if (addr&1)
        {
//                printf("Reading ACIA data %02X %i %i\n",aciadr,lns,dreg);
                aciasr&=~0x81;
                updateaciaint();
                temp=aciadr;
                if (dreg==2) aciadr=aciadrs;
                if (dreg) dreg--;
                return temp;
        }
        else
        {
//                sprintf(s,"Reading 6850 status reg %02X\n",aciasr);
//                fputs(s,tapelog);
                if (aciasr&DCD && !cleardcd)
                   cleardcd=2;
                else if (aciasr&DCD)
                {
                        cleardcd--;
                        if (!cleardcd) aciasr&=~DCD;
                }
                return aciasr;
        }
}

void writeacia(unsigned short addr, unsigned char val)
{
        if (addr&1)
        {
//                sprintf(s,"Writing 6850 data reg %02X\n",val);
//                fputs(s,tapelog);
                aciadr=val;
                aciasr&=0xFD;
        }
        else
        {
//                sprintf(s,"Writing 6850 control reg %02X\n",val);
//                fputs(s,tapelog);
//                printf("Write CR %02X\n",val);
                aciacr=val;
                if (val==3)
                   resetacia();
        }
}

void dcd()
{
        aciasr|=DCD|0x80;
        updateaciaint();
//        printf("DCD high\n");
//        if (aciacr&0x80) interrupt|=4;
}

void dcdlow()
{
        aciasr|=0x80;
        aciasr&=~DCD;
        updateaciaint();
//        printf("DCD low\n");
//        if (aciacr&0x80) interrupt|=4;
}

void receive(unsigned char val) /*Called when the acia recives some data*/
{
        char s[80];
//        printf("Receiving %02X %i\n",val,lns);
//        printf("Receiving %02X %02X %02X\n",val,aciacr,aciasr);
//        sprintf(s,"Writing %02X to acia\n",val);
//        fputs(s,tapelog);
        if (!dreg)
        {
                aciadr=val;
                dreg=1;
        }
        else if (dreg==1)
        {
                aciadrs=val;
                dreg=2;
        }
        else
        {
//                printf("Missed a byte\n");
        }
        aciadrf=1;
        aciasr|=RECIEVE|0x80;
        updateaciaint();
//        output=1;
}

void pollacia()
{
//        printf("Motor status %i\n",motor);
        if (motor)
        {
                if (dreg)
                {
//                        printf("dreg\n");
                        aciasr|=RECIEVE|0x80;
                        updateaciaint();
                }
                else
                {
                        polltape();
//                        printf("Polling tape\n");
                }

        }
}
