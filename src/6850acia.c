/*B-em v2.0 by Tom Walker
  6850 ACIA emulation*/

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
uint8_t serialreg;
FILE *tape;
FILE *tapelog;
int tapepos;
uint8_t aciasr=8;
uint8_t aciadrs;
uint8_t aciadrw;

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

uint8_t readacia(uint16_t addr)
{
        uint8_t temp;
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

void writeacia(uint16_t addr, uint8_t val)
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

uint16_t newdat;

void receive(uint8_t val) /*Called when the acia recives some data*/
{
        aciadr=val;
        aciasr|=RECIEVE|0x80;
//        printf("recieve interrupt\n");
        updateaciaint();
        newdat=val|0x100;
//        printf("Recieved %02X %04X   ",val,pc);
//        sprintf(err2,"Recieved %02X\n",val);
//        fputs(err2,cswlog);
}

int cswena;
/*Every 128 clocks, ie 15.625khz*/
/*Div by 13 gives roughly 1200hz*/

extern ueftoneon,cswtoneon;
void pollacia()
{
//        int c;
//        printf("Poll tape %i %i\n",motor,cswena);
        if (motor)
        {
                startblit();
                if (cswena) pollcsw();
                else        polltape();
                endblit();
//                for (c=0;c<2;c++) pollcsw();
                if (newdat&0x100)
                {
                        newdat&=0xFF;
                        adddatnoise(newdat);
                }
                else if (cswtoneon || ueftoneon) addhighnoise();
        }
//           polltape();
}
