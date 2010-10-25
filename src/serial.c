/*B-em v2.1 by Tom Walker
  Serial ULA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "serial.h"
#include "acia.h"

uint8_t serialreg;
uint8_t transmitrate,reciverate;

void initserial()
{
        /*Dunno what happens to this on reset*/
        serialreg=transmitrate=reciverate=0;
        motor=0;
}

uint8_t readserial(uint16_t addr)
{
        /*Reading from this has the same effect as writing &FE*/
        if (motor) tapemotorchange(0);
        motor=0;
        return 0;
}

void writeserial(uint16_t addr, uint8_t val)
{
        serialreg=val;
        transmitrate=val&0x7;
        reciverate=(val>>3)&0x7;
        if (motor != (val&0x80)) tapemotorchange(val>>7);
        motor=(val&0x80) && tapeloaded;
//        printf("Write serial %02X %04X\n",val,pc);
//        if (motor) output=1;
        if (val&0x40)
        {
                /*RS423*/
                aciasr&=~4; /*Clear acia DCD*/
        }
        else
        {
                /*Tape*/
                aciasr&=~8; /*Clear acia CTS*/
        }
}

void updateserialreg()
{
        transmitrate=serialreg&0x7;
        reciverate=(serialreg>>3)&0x7;
        motor=serialreg&0x80;
        if (serialreg&0x40)
        {
                /*RS423*/
                aciasr&=~4; /*Clear acia DCD*/
        }
        else
        {
                /*Tape*/
                aciasr&=~8; /*Clear acia CTS*/
        }
}

void saveserialulastate(FILE *f)
{
        putc(serialreg,f);
}

void loadserialulastate(FILE *f)
{
        writeserial(0,getc(f));
}
