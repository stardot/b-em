/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.3


              All of this code is (C)opyright Tom Walker 1999
         You may use SMALL sections from this program (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*6850 ACAI emulation*/

#include <stdio.h>
#include "acai.h"
#include "6502.h"
#include "serial.h"

int tapedelay=0;
unsigned char serialreg;
FILE *tape;
FILE *tapelog;
int tapepos;

void resetacai()
{
        acaidrf=0;
        acaisr=0;
}

unsigned char readacai(unsigned short addr)
{
        char s[80];
        if (addr&1)
        {
                acaisr&=190; /*Clear RDRF, OVRN and PE*/
                intStatus&=~4;
//                sprintf(s,"Reading 6850 data reg %02X\n",acaidr);
//                fputs(s,tapelog);
                return acaidr;
        }
        else
        {
//                sprintf(s,"Reading 6850 status reg %02X\n",acaisr);
//                fputs(s,tapelog);
                return acaisr;
        }
}

void writeacai(unsigned short addr, unsigned char val)
{
        if (addr&1)
        {
//                sprintf(s,"Writing 6850 data reg %02X\n",val);
//                fputs(s,tapelog);
                acaidr=val;
                acaisr&=0xFD;
        }
        else
        {
//                sprintf(s,"Writing 6850 control reg %02X\n",val);
//                fputs(s,tapelog);
                acaicr=val;
                if (val==3)
                   resetacai();
        }
}

void dcd()
{
        acaisr=4;
        if (acaicr&0x80)
        {
                acaisr|=0x80;
                intStatus|=4;
        }
}

void writetoacai(unsigned char val) /*Called when the ACAI recives some data*/
{
        char s[80];
        sprintf(s,"Writing %02X to ACAI\n",val);
        fputs(s,tapelog);
        acaidr=val;
        acaidrf=1;
        acaisr=5;
        if (acaicr&0x80)
        {
                acaisr|=0x80; /*Should this be set before checking the CR?*/
                intStatus|=4;
        }
}

void pollacia()
{
//        int val;
/*        if (((serialreg&128)||motor)&&!tapedelay)
        {
//                dcd();
//                tapedelay=18;
                uefbyte();
        }
        else if (motor)
           tapedelay--;
        else
           tapedelay=0;*/
}
