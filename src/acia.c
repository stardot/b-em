/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "csw.h"
#include "uef.h"
#include "tapenoise.h"

#define DCD     4
#define RECIEVE 1

int acia_tapespeed=0;

uint8_t acia_sr;
static uint8_t acia_cr, acia_dr;

void acia_updateint()
{
        if (((acia_sr&0x80) && (acia_cr&0x80)) || (!(acia_sr & 0x02) && ((acia_cr & 0x60) == 0x20)))
           interrupt|=4;
        else
           interrupt&=~4;
}

void acia_reset()
{
        acia_sr = (acia_sr & 8) | 6;
        acia_updateint();
}

uint8_t acia_read(uint16_t addr)
{
        uint8_t temp;
        if (addr & 1)
        {
                temp = acia_dr;
                acia_sr &= ~0x81;
                acia_updateint();
                return temp;
        }
        else
        {
		temp = (acia_sr & 0x7F) | (acia_sr & acia_cr & 0x80);
                return temp;
        }
}

void acia_write(uint16_t addr, uint8_t val)
{
        if (addr & 1)
        {
		putchar(val);
		// acia_sr &= 0xFD;
                // acia_updateint();
        }
        else if (val != acia_cr)
	{
		if (!(val & 0x40)) // interupts disabled as serial TX buffer empties.
			fflush(stdout);
	        acia_cr = val;
	        if (val == 3)
              		acia_reset();
	        switch (val & 3)
                {
	                case 1: acia_tapespeed=0; break;
	                case 2: acia_tapespeed=1; break;
	        }
        }
}

void dcd()
{
        if (acia_sr & DCD) return;
        acia_sr |= DCD | 0x80;
        acia_updateint();
}

void dcdlow()
{
        acia_sr &= ~DCD;
        acia_updateint();
}

static uint16_t newdat;

void acia_receive(uint8_t val) /*Called when the acia recives some data*/
{
        acia_dr = val;
        acia_sr |= RECIEVE | 0x80;
        acia_updateint();
        
        newdat=val|0x100;
}

/*Every 128 clocks, ie 15.625khz*/
/*Div by 13 gives roughly 1200hz*/

extern int ueftoneon,cswtoneon;
void acia_poll()
{
//        int c;
//        printf("Poll tape %i %i\n",motor,cswena);
        if (motor)
       	{
       	        startblit();
               	if (csw_ena) csw_poll();
               	else         uef_poll();
               	endblit();

                if (newdat&0x100)
                {
                        newdat&=0xFF;
                        tapenoise_adddat(newdat);
                }
                else if (csw_toneon || uef_toneon) tapenoise_addhigh();
        }
	//           polltape();
}

void acia_savestate(FILE *f)
{
        putc(acia_cr,f);
        putc(acia_sr,f);
}

void acia_loadstate(FILE *f)
{
        acia_cr=getc(f);
        acia_sr=getc(f);
}
