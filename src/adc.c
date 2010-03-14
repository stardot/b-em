/*B-em v2.0 by Tom Walker
  ADC emulation*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"

int joy1x,joy1y,joy2x,joy2y;

uint8_t adcstatus,adchigh,adclow,adclatch;
int adcconvert;

uint8_t readadc(uint16_t addr)
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

void writeadc(uint16_t addr, uint8_t val)
{
        if (!(addr&3))
        {
                adclatch=val;
                adcconvert=60;
                adcstatus=(val&0xF)|0x80; /*Busy, converting*/
//                printf("ADC conversion - %02X\n",val);
        }
}

void polladc()
{
        uint32_t val;
        joy1x=joy1y=0;
//        printf("%i\n",joy[0].stick[0].axis[0].pos);
        switch (adcstatus&3)
        {
                case 0: val=(128-joy[0].stick[0].axis[0].pos)*256; break;
                case 1: val=(128-joy[0].stick[0].axis[1].pos)*256; break;
                case 2: val=(128-joy[1].stick[0].axis[0].pos)*256; break;
                case 3: val=(128-joy[1].stick[0].axis[1].pos)*256; break;
        }
        if (val>0xFFFF) val=0xFFFF;
        if (val<0)      val=0;
//        val^=0xFFFF;
//        val++;
//        if (val==0x10000) val=0xFFFF;
        adcstatus=(adcstatus&0xF)|0x40; /*Not busy, conversion complete*/
        adcstatus|=(val&0xC000)>>10;
        adchigh=val>>8;
        adclow=val&0xFF;
        syscb1();
}

void initadc()
{
        adcstatus=0x40;            /*Not busy, conversion complete*/
        adchigh=adclow=adclatch=0;
        adcconvert=0;
        install_joystick(JOY_TYPE_AUTODETECT);
}
