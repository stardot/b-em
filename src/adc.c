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

/*ADC emulation*/

#include "gfx.h"
#include "adc.h"

int joy1x,joy1y,joy2x,joy2y;

unsigned char adcstatus,adchigh,adclow,adclatch;

unsigned char readadc(unsigned short addr)
{
        switch (addr&3)
        {
                case 0:
                return adcstatus;
                break;
                case 1:
                return adchigh;
                break;
                case 2:
                return adclow;
                break;
        }
        return 0x40;
}

void writeadc(unsigned short addr, unsigned char val)
{
        if (addr==0xFEC0)
        {
                adclatch=val;
                adcconvert=1;
                if (adclatch&8)
                   adctime=20000;
                else
                   adctime=8000;
                adcstatus=(val&0xF)|0x80; /*Busy, converting*/
        }
}

void adcpoll()
{
        int val;
        joy1x=joy1y=0;
        if (!(shifts&SHIFT_NUMLOCK))
        {
                if (keys[KEY_LEFT])
                   joy1x=-4095;
                else if (keys[KEY_RIGHT])
                   joy1x=4095;
                else
                   joy1x=0;
                if (keys[KEY_UP])
                   joy1y=-4095;
                else if (keys[KEY_DOWN])
                   joy1y=4095;
                else
                   joy1y=0;
        }
        adcstatus=(adcstatus&0xF)|0x40; /*Not busy, conversion complete*/
        switch (adcstatus&3)
        {
                case 0:
                val=joy1x;
                break;
                case 1:
                val=joy1y;
                break;
                case 2:
                val=joy2x;
                break;
                case 3:
                val=joy2y;
                break;
        }
        adcstatus|=(val&0xC000)>>10;
        adchigh=val>>8;
        adclow=val&0xFF;
        SysCB1(1);
}

void initadc()
{
        adcstatus=0x40;            /*Not busy, conversion complete*/
        adchigh=adclow=adclatch=0;
        adcconvert=0;
}
