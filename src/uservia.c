/*B-em 0.7 by Tom Walker*/
/*User VIA emulation*/

#include <stdio.h>
#include "b-em.h"

unsigned short pc;
int t1back;
int bbcmode;
int output2,output;

#define TIMER1INT 0x40
#define TIMER2INT 0x20
#define PORTBINT  0x18
#define PORTAINT  0x03

#define         ORB     0x00
#define         ORA     0x01
#define         DDRB    0x02
#define         DDRA    0x03
#define         T1CL    0x04
#define         T1CH    0x05
#define         T1LL    0x06
#define         T1LH    0x07
#define         T2CL    0x08
#define         T2CH    0x09
#define         SR      0x0a
#define         ACR     0x0b
#define         PCR     0x0c
#define         IFR     0x0d
#define         IER     0x0e
#define         ORAnh   0x0f

void updateuserIFR()
{
        if ((uservia.ifr&0x7F)&(uservia.ier&0x7F))
        {
                uservia.ifr|=0x80;
                interrupt|=2;
        }
        else
        {
                uservia.ifr&=~0x80;
                interrupt&=~2;
        }
}

int lns;
void updateusertimers()
{
        if (uservia.t1c<-4)
        {
                uservia.t1c+=uservia.t1l+4;
//                printf("Timer 1 reset line %i %04X\n",lns,uservia.t1c);
                t1back=uservia.t1c;
                if (!uservia.t1hit)
                {
                       uservia.ifr|=TIMER1INT;
                       updateuserIFR();
                }
                if (!(uservia.acr&0x40))
                   uservia.t1hit=1;
                if (uservia.acr&0x80)
                {
                        uservia.orb^=0x80;
                        uservia.irb^=0x80;
                        uservia.portb^=0x80;
                }
        }
        if (!(uservia.acr&0x20)/* && !uservia.t2hit*/)
        {
                if (uservia.t2c<-4)
                {
                        uservia.t2c+=uservia.t2l+4;
                        if (!uservia.t2hit)
                        {
                                uservia.ifr|=TIMER2INT;
                                updateuserIFR();
                        }
                        uservia.t2hit=1;
                }
        }
}

void writeuservia(unsigned short addr, unsigned char val, int line)
{
//        printf("User VIA write %04X %02X\n",addr,val);
        switch (addr&0xF)
        {
                case ORA:
                uservia.ifr&=0xfc;//~PORTAINT;
                updateuserIFR();
                case ORAnh:
                uservia.ora=val;
                uservia.porta=(uservia.porta & ~uservia.ddra)|(uservia.ora & uservia.ddra);
                break;

                case ORB:
                uservia.orb=val;
                uservia.portb=(uservia.portb & ~uservia.ddrb)|(uservia.orb & uservia.ddrb);
                uservia.ifr&=0xfe;//~PORTBINT;
                updateuserIFR();
                break;

                case DDRA:
                uservia.ddra=val;
                break;
                case DDRB:
                uservia.ddrb=val;
                break;
                case ACR:
                uservia.acr=val;
                break;
                case PCR:
                uservia.pcr=val;
                break;
                case T1LL:
                case T1CL:
//                printf("T1L write %02X at %04X %i\n",val,pc,lns);
                uservia.t1l&=0x1FE00;
                uservia.t1l|=(val<<1);
                break;
                case T1LH:
//                printf("T1LH write %02X at %04X %i\n",val,pc,lns);
                uservia.t1l&=0x1FE;
                uservia.t1l|=(val<<9);
                if (uservia.acr&0x40)
                {
                        uservia.ifr&=~TIMER1INT;
                        updateuserIFR();
                }
                break;
                case T1CH:
//                printf("T1CH write %02X at %04X %i\n",val,pc,lns);
                uservia.t1l&=0x1FE;
                uservia.t1l|=(val<<9);
//                printf("T1 l now %05X\n",uservia.t1l);
                uservia.t1c=uservia.t1l;
                uservia.ifr&=~TIMER1INT;
                updateuserIFR();
                uservia.t1hit=0;
                break;
                case T2CL:
                uservia.t2l&=0x1FE00;
                uservia.t2l|=(val<<1);
//                printf("T2CL=%02X at line %i\n",val,line);
                break;
                case T2CH:
                uservia.t2l&=0x1FE;
                uservia.t2l|=(val<<9);
                uservia.t2c=uservia.t2l;
                uservia.ifr&=~TIMER2INT;
                updateuserIFR();
                uservia.t2hit=0;
//                printf("T2CH=%02X at line %i\n",val,line);
                break;
                case IER:
/*                if (val==0x40)
                {
                        printf("Here\n");
//                        output=1;
                }*/
                if (val&0x80)
                   uservia.ier|=(val&0x7F);
                else
                   uservia.ier&=~(val&0x7F);
                updateuserIFR();
//                if (uservia.ier&0x40) printf("0x40 enabled at %04X\n",pc);
//                uservia.ifr&=~uservia.ier;
                break;
                case IFR:
                uservia.ifr&=~(val&0x7F);
                updateuserIFR();
                break;
        }
}

unsigned char readuservia(unsigned short addr)
{
        unsigned char temp;
        switch (addr&0xF)
        {
                case ORA:
                uservia.ifr&=~PORTAINT;
                updateuserIFR();
                case ORAnh:
                temp=uservia.ora & uservia.ddra;
                temp|=(uservia.porta & ~uservia.ddra);
                temp&=0x7F;
                return temp;

                case ORB:
//                uservia.ifr&=~PORTBINT;
                updateuserIFR();
                temp=uservia.orb & uservia.ddrb;
                if (uservia.acr&2)
                   temp|=(uservia.irb & ~uservia.ddrb);
                else
                   temp|=(uservia.portb & ~uservia.ddrb);
//                temp|=0xF0;
                return temp;

                case DDRA:
                return uservia.ddra;
                case DDRB:
                return uservia.ddrb;
                case T1LL:
                return (uservia.t1l&0x1FE)>>1;
                case T1LH:
                return uservia.t1l>>9;
                case T1CL:
                uservia.ifr&=~TIMER1INT;
                updateuserIFR();
                if (uservia.t1c<0) return 0xFF;
                return (uservia.t1c>>1)&0xFF;
                case T1CH:
                if (uservia.t1c<0) return 0xFF;
                return (uservia.t1c>>1)>>8;
                case T2CL:
                uservia.ifr&=~TIMER2INT;
                updateuserIFR();
                if (uservia.t2c<0) return 0xFF;
                return (uservia.t2c>>1)&0xFF;
                case T2CH:
                if (uservia.t2c<0) return 0xFF;
                return (uservia.t2c>>1)>>8;
                case ACR:
                return uservia.acr;
                case PCR:
                return uservia.pcr;
                case IER:
                return uservia.ier|0x80;
                case IFR:
                return uservia.ifr;
        }
}

void resetuservia()
{
        uservia.ora=0x80;
        uservia.ifr=uservia.ier=0;
        uservia.t1c=uservia.t1l=0x1FFFE;
        uservia.t2c=uservia.t2l=0x1FFFE;
        uservia.t1hit=uservia.t2hit=0;
}

void dumpuservia()
{
        printf("T1 = %04X %04X T2 = %04X %04X\n",uservia.t1c,uservia.t1l,uservia.t2c,uservia.t2l);
        printf("%02X %02X\n",uservia.ifr,uservia.ier);
}
