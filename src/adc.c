/*B-em 1.0 by Tom Walker*/
/*ADC emulation*/

#include <stdio.h>
#include <allegro.h>

int joy1x,joy1y,joy2x,joy2y;

unsigned char adcstatus,adchigh,adclow,adclatch;
int adcconvert;

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
                adcstatus=(val&0xF)|0x80; /*Busy, converting*/
        }
}

void polladc()
{
        unsigned long val;
        if (adcconvert)
        {
        joy1x=joy1y=0;
        if (joy[0].stick[0].axis[0].d1)
           joy1x=0xFFFF;
        else if (joy[0].stick[0].axis[0].d2)
           joy1x=0;
        else
           joy1x=0x8000;
        if (joy[0].stick[0].axis[1].d1)
           joy1y=0xFFFF;
        else if (joy[0].stick[0].axis[1].d2)
           joy1y=0;
        else
           joy1y=0x8000;
        joy2x=joy2y=0;
        if (joy[1].stick[0].axis[0].d1)
           joy2x=0xFFFF;
        else if (joy[1].stick[0].axis[0].d2)
           joy2x=0x7FFF;
        else
           joy2x=0;
        if (joy[1].stick[0].axis[1].d1)
           joy2y=0xFFFF;
        else if (joy[1].stick[0].axis[1].d2)
           joy2y=0x7FFF;
        else
           joy2y=0;
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
        syscb1();
        }
        adcconvert=0;
}

void initadc()
{
        adcstatus=0x40;            /*Not busy, conversion complete*/
        adchigh=adclow=adclatch=0;
        adcconvert=0;
//        load_joystick_data("joystick.dat");
        install_joystick(JOY_TYPE_AUTODETECT);
}

void saveadcstate(FILE *f)
{
        putc(adcstatus,f);
        putc(adclow,f);
        putc(adchigh,f);
        putc(adclatch,f);
        putc(adcconvert,f);
}

void loadadcstate(FILE *f)
{
        adcstatus=getc(f);
        adclow=getc(f);
        adchigh=getc(f);
        adclatch=getc(f);
        adcconvert=getc(f);
}
