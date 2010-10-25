/*B-em v2.1 by Tom Walker
  User VIA + Master 512 mouse emulation*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"

//#define printf rpclog
#undef printf
VIA uservia;

int mx=0,my=0;

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

int timerout=1;

int vc,sc;

void updateusertimers()
{
        if (uservia.t1c<-3)
        {
                while (uservia.t1c<-3)
                      uservia.t1c+=uservia.t1l+4;
                if (!uservia.t1hit)
                {
//                        rpclog("User T1 interrupt! %04X %i %i\n",uservia.t1l,vc,sc);
                        uservia.ifr|=TIMER1INT;
                        updateuserIFR();
                }
                if ((uservia.acr&0x80) && !uservia.t1hit)
                {
                        uservia.orb^=0x80;
                        uservia.irb^=0x80;
                        uservia.portb^=0x80;
                        timerout^=1;
                }
                if (!(uservia.acr&0x40))
                   uservia.t1hit=1;
        }
        if (!(uservia.acr&0x20))
        {
                if (uservia.t2c<-3 && !uservia.t2hit)
                {
                        if (!uservia.t2hit)
                        {
//                                rpclog("User T2 interrupt!\n");
                                uservia.ifr|=TIMER2INT;
                                updateuserIFR();
                        }
                        uservia.t2hit=1;
                }
        }
}

void writeuservia(uint16_t addr, uint8_t val)
{
//        rpclog("Write User VIA %04X %02X %04X\n",addr,val,pc);
        switch (addr&0xF)
        {
                case ORA:
                uservia.ifr&=0xfc;//~PORTAINT;
                updateuserIFR();
                case ORAnh:
//                        rpclog("Write User ORA %02X\n",val);
                uservia.ora=val;
                uservia.porta=(uservia.porta & ~uservia.ddra)|(uservia.ora & uservia.ddra);
                break;

                case ORB:
                uservia.orb=val;
                uservia.portb=(uservia.portb & ~uservia.ddrb)|(uservia.orb & uservia.ddrb);
                uservia.ifr&=0xee;//~PORTBINT;
                updateuserIFR();
                break;

                case DDRA:
//                rpclog("User DDRA = %02X\n",val);
                uservia.ddra=val;
                break;
                case DDRB:
                uservia.ddrb=val;
                break;
                case ACR:
                uservia.acr=val;
//                rpclog("SR ACR = %02X\n",val);
                break;
                case PCR:
                uservia.pcr=val;
                break;
                case SR:
                uservia.sr=val;
                break;
                case T1LL:
                case T1CL:
                uservia.t1l&=0x1FE00;
                uservia.t1l|=(val<<1);
//                rpclog("T1L now %05X\n",uservia.t1l);
                break;
                case T1LH:
                uservia.t1l&=0x1FE;
                uservia.t1l|=(val<<9);
                if (uservia.acr&0x40)
                {
                        uservia.ifr&=~TIMER1INT;
                        updateuserIFR();
                }
//                rpclog("T1L now %05X\n",uservia.t1l);
                break;
                case T1CH:
                if ((uservia.acr&0xC0)==0x80) timerout=0;
                uservia.t1l&=0x1FE;
                uservia.t1l|=(val<<9);
                uservia.t1c=uservia.t1l+1;
                uservia.ifr&=~TIMER1INT;
                updateuserIFR();
                uservia.t1hit=0;
//                rpclog("T1L now %05X\n",uservia.t1l);
                break;
                case T2CL:
                uservia.t2l&=0x1FE00;
                uservia.t2l|=(val<<1);
//                printf("T2L now %05X\n",uservia.t2l);
                break;
                case T2CH:
                if ((uservia.t2c==-3 && (uservia.ier&TIMER2INT)) ||
                    (uservia.ifr&uservia.ier&TIMER2INT))
                {
                        interrupt|=128;
                }
                uservia.t2l&=0x1FE;
                uservia.t2l|=(val<<9);
                uservia.t2c=uservia.t2l+1;
                uservia.ifr&=~TIMER2INT;
                updateuserIFR();
                uservia.t2hit=0;
//                printf("T2L now %05X\n",uservia.t2l);
                break;
                case IER:
                if (val&0x80)
                   uservia.ier|=(val&0x7F);
                else
                   uservia.ier&=~(val&0x7F);
                updateuserIFR();
//                printf("USER IER now %02X\n",sysvia.ier);
//                printf("User IER now %02X\n",val);
                break;
                case IFR:
                uservia.ifr&=~(val&0x7F);
                updateuserIFR();
                break;
        }
}

uint8_t readuservia(uint16_t addr)
{
        uint8_t temp;
//        if (addr==0xFE6A) rpclog("Read User VIA %04X %04X\n",addr,pc);
        addr&=0xF;
        switch (addr&0xF)
        {
                case ORA:
                uservia.ifr&=~PORTAINT;
                updateuserIFR();
                case ORAnh:
//                        rpclog("Read User ORA %02X %02X %02X\n",uservia.ora,uservia.porta,uservia.ddra);
                temp=uservia.ora & uservia.ddra;
                temp|=(uservia.porta & ~uservia.ddra);
                temp&=0x7F;
                return temp;

                case ORB:
                uservia.ifr&=~PORTBINT;
                updateuserIFR();
                temp=uservia.orb & uservia.ddrb;
//                rpclog("ORB %02X %02X %02X %i\n",temp,uservia.orb,uservia.ddrb,uservia.acr&2);
                if (uservia.acr&2)
                   temp|=(uservia.irb & ~uservia.ddrb);
                else
                   temp|=(uservia.portb & ~uservia.ddrb);
//                temp|=0xFF;
                if (timerout) temp|=0x80;
                else          temp&=~0x80;
//                rpclog("Read ORB %02X %04X\n",temp,pc);
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
                if (uservia.t1c<-1) return 0xFF;
                return ((uservia.t1c+1)>>1)&0xFF;
                case T1CH:
                if (uservia.t1c<-1) return 0xFF;
                return (uservia.t1c+1)>>9;
                case T2CL:
                uservia.ifr&=~TIMER2INT;
                updateuserIFR();
                return ((uservia.t2c+1)>>1)&0xFF;
                case T2CH:
                return (uservia.t2c+1)>>9;
                case SR:
                return uservia.sr;
                case ACR:
                return uservia.acr;
                case PCR:
                return uservia.pcr;
                case IER:
                return uservia.ier|0x80;
                case IFR:
//                        printf("Reading user IFR %02X\n",uservia.ifr);
                return uservia.ifr;
        }
        return 0xFE;
}

void resetuservia()
{
        uservia.ora=0x80; uservia.orb=0xFF;
        uservia.ddra=uservia.ddrb=0xFF;
        uservia.ifr=uservia.ier=0;
        uservia.t1c=uservia.t1l=0x1FFFE;
        uservia.t2c=uservia.t2l=0x1FFFE;
        uservia.t1hit=uservia.t2hit=1;
        timerout=1;
        uservia.acr=0;
}

void dumpuservia()
{
        rpclog("T1 = %04X %04X T2 = %04X %04X\n",uservia.t1c,uservia.t1l,uservia.t2c,uservia.t2l);
        rpclog("%02X %02X  %02X %02X\n",uservia.ifr,uservia.ier,uservia.pcr,uservia.acr);
}

int mxs=0,mys=0;
int mfirst=1;
int mon=0;
int beebmousex=0,beebmousey=0;
void domouse()
{
        int x,y;
//        if (key[KEY_HOME]) mon=1;
//        if (!mon) return;
//        x=mouse_x/2;
//        y=mouse_y/2;
//        if (x<0 || x>320 || y<0 && y>256) return;
        if (mouse_x>0 && mouse_x<640 && mouse_y>0 && mouse_y<512)
        {
                beebmousex=mouse_x;
                beebmousey=256-(mouse_y/2);
        }
        if (uservia.ifr&0x18) return;
//        #if 0
        if (mouse_x!=mx)
        {
                uservia.ifr|=0x10;
                if (mxs==((mouse_x>mx)?1:-1)) uservia.portb^=8;
//                printf("%i %i %i %i %i\n",mouse_x,mx,mxs,mxs==(mouse_x>mx)?1:-1,uservia.portb&8);
                mxs=(mouse_x>mx)?1:-1;
                mx+=mxs;
        }
        if (mouse_y!=my)
        {
                if (mfirst)
                {
                        mfirst=0;
                        uservia.portb|=0x10;
//                        uservia.portb&=~0x10;
                }
                uservia.ifr|=0x08;
                if (mys==((mouse_y>my)?1:-1)) uservia.portb^=0x10;
//                else printf("Changed!\n");
//                printf("%i %i %i %i %i\n",mouse_y,my,mys,mys==(mouse_y>my)?1:-1,uservia.portb&0x10);
                mys=(mouse_y>my)?1:-1;
                my+=mys;
        }
        updateuserIFR();
        if (mouse_b&1) uservia.portb&=~1;
        else           uservia.portb|=1;
        if (mouse_b&2) uservia.portb&=~4;
        else           uservia.portb|=4;
        uservia.portb|=2;
//        #endif
        ram[0x2821]=beebmousey>>8;
        ram[0x2822]=beebmousey&0xFF;
        ram[0x2823]=beebmousex>>8;
        ram[0x2824]=beebmousex&0xFF;
//        textprintf(screen,font,0,0,makecol(255,255,255),"%02X%02X %02X%02X  %i %i   ",ram[0x2821],ram[0x2822],ram[0x2823],ram[0x2824],mouse_x,mouse_y);
//        rpclog("%02X%02X %02X%02X\n",ram[0x2821],ram[0x2822],ram[0x2823],ram[0x2824]);
}

void getmousepos(uint16_t *AX, uint16_t *CX, uint16_t *DX)
{
        int c=mouse_b&1;
        if (mouse_b&2) c|=4;
        if (mouse_b&4) c|=2;
        *AX=c;
        *CX=beebmousex;
        *DX=beebmousey;
}

void saveuserviastate(FILE *f)
{
        putc(uservia.ora,f);
        putc(uservia.orb,f);
        putc(uservia.ira,f);
        putc(uservia.irb,f);
        putc(uservia.porta,f);
        putc(uservia.portb,f);
        putc(uservia.ddra,f);
        putc(uservia.ddrb,f);
        putc(uservia.sr,f);
        putc(uservia.acr,f);
        putc(uservia.pcr,f);
        putc(uservia.ifr,f);
        putc(uservia.ier,f);
        putc(uservia.t1l,f); putc(uservia.t1l>>8,f); putc(uservia.t1l>>16,f); putc(uservia.t1l>>24,f);
        putc(uservia.t2l,f); putc(uservia.t2l>>8,f); putc(uservia.t2l>>16,f); putc(uservia.t2l>>24,f);
        putc(uservia.t1c,f); putc(uservia.t1c>>8,f); putc(uservia.t1c>>16,f); putc(uservia.t1c>>24,f);
        putc(uservia.t2c,f); putc(uservia.t2c>>8,f); putc(uservia.t2c>>16,f); putc(uservia.t2c>>24,f);
        putc(uservia.t1hit,f);
        putc(uservia.t2hit,f);
        putc(uservia.ca1,f);
        putc(uservia.ca2,f);
}

void loaduserviastate(FILE *f)
{
        uservia.ora=getc(f);
        uservia.orb=getc(f);
        uservia.ira=getc(f);
        uservia.irb=getc(f);
        uservia.porta=getc(f);
        uservia.portb=getc(f);
        uservia.ddra=getc(f);
        uservia.ddrb=getc(f);
        uservia.sr=getc(f);
        uservia.acr=getc(f);
        uservia.pcr=getc(f);
        uservia.ifr=getc(f);
        uservia.ier=getc(f);
        uservia.t1l=getc(f); uservia.t1l|=getc(f)<<8; uservia.t1l|=getc(f)<<16; uservia.t1l|=getc(f)<<24;
        uservia.t2l=getc(f); uservia.t2l|=getc(f)<<8; uservia.t2l|=getc(f)<<16; uservia.t2l|=getc(f)<<24;
        uservia.t1c=getc(f); uservia.t1c|=getc(f)<<8; uservia.t1c|=getc(f)<<16; uservia.t1c|=getc(f)<<24;
        uservia.t2c=getc(f); uservia.t2c|=getc(f)<<8; uservia.t2c|=getc(f)<<16; uservia.t2c|=getc(f)<<24;
        uservia.t1hit=getc(f);
        uservia.t2hit=getc(f);
        uservia.ca1=getc(f);
        uservia.ca2=getc(f);
}
