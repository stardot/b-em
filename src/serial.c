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

/*Serial ULA emulation*/

#include "serial.h"
#include "acai.h"

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
        /*The byte read from the ULA is always zero, but on reading it the
          tape motor LED goes off*/
        motor=0;
        return 0;
}

void writeserial(unsigned short addr, unsigned char val)
{
        serialreg=val;
        transmitrate=val&0x7;
        reciverate=(val>>3)&0x7;
        motor=val&0x80;
        if (val&0x40)
        {
                /*RS423*/
                acaisr&=~4; /*Clear ACAI DCD*/
        }
        else
        {
                /*Tape*/
                acaisr&=~8; /*Clear ACAI CTS*/
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
                acaisr&=~4; /*Clear ACAI DCD*/
        }
        else
        {
                /*Tape*/
                acaisr&=~8; /*Clear ACAI CTS*/
        }
}
