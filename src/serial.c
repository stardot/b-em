/*B-em 0.7 by Tom Walker*/
/*Serial ULA emulation*/

#include "serial.h"
#include "acia.h"

int output;
unsigned char serialreg;
unsigned char transmitrate,reciverate;

void initserial()
{
        /*Dunno what happens to this on reset*/
        serialreg=transmitrate=reciverate=0;
        motor=0;
}

unsigned char readserial(unsigned short addr)
{
        /*Reading from this has the same effect as writing &FE*/
        motor=0;
        return 0;
}

void writeserial(unsigned short addr, unsigned char val)
{
        serialreg=val;
        transmitrate=val&0x7;
        reciverate=(val>>3)&0x7;
        motor=val&0x80;
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
