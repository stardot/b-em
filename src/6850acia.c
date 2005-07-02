/*B-em 0.8 by Tom Walker*/
/*6850 acia emulation*/

#include <stdio.h>
#include "acia.h"
#include "serial.h"

#define DCD     4
#define RECIEVE 1

unsigned short pc;
int chunklen;
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
        if (addr&1)
        {
                aciasr&=~0x81;
                updateaciaint();
                temp=aciadr;
//                printf("Read data %02X\n",temp);
                return temp;
        }
        else
        {
//                printf("Read status %02X\n",aciasr);
                return aciasr;
        }
}

void writeacia(unsigned short addr, unsigned char val)
{
        if (addr&1)
        {
                aciadr=val;
                aciasr&=0xFD;
        }
        else
        {
                aciacr=val;
                if (val==3)
                   resetacia();
//                printf("Write CTRL %02X %04X\n",val,pc);
        }
}

void dcd()
{
        aciasr|=DCD|0x80;
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
        updateaciaint();
//        printf("Recieved %02X\n",val);
}

void pollacia()
{
        if (motor)
           polltape();
}
