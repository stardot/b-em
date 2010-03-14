/*B-em v2.0 by Tom Walker
  System VIA + keyboard emulation*/

#include <allegro.h>
#include <stdio.h>
#include "b-em.h"

VIA sysvia;
void updatekeyboard();

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

void updatesysIFR()
{
        if ((sysvia.ifr&0x7F)&(sysvia.ier&0x7F))
        {
                sysvia.ifr|=0x80;
                interrupt|=1;
//                printf("Interrupt %02X %04X\n",sysvia.ifr,pc);
        }
        else
        {
                sysvia.ifr&=~0x80;
                interrupt&=~1;
        }
}

int lns;
void updatesystimers()
{
        if (sysvia.t1c<-3)
        {
                while (sysvia.t1c<-3)
                      sysvia.t1c+=sysvia.t1l+4;
                if (!sysvia.t1hit)
                {
                       sysvia.ifr|=TIMER1INT;
                       updatesysIFR();
                }
                if (!(sysvia.acr&0x40))
                   sysvia.t1hit=1;
        }
        if (!(sysvia.acr&0x20))
        {
                if (sysvia.t2c<-3)
                {
                        if (!sysvia.t2hit)
                        {
                                sysvia.ifr|=TIMER2INT;
                                updatesysIFR();
                        }
                        sysvia.t2hit=1;
                }
        }
}

void vblankint()
{
        if (!sysvia.ca1 && (sysvia.pcr&1))
        {
                sysvia.ifr|=2;
                updatesysIFR();
        }
        sysvia.ca1=1;
}
void vblankintlow()
{
        if (sysvia.ca1 && !(sysvia.pcr&1))
        {
                sysvia.ifr|=2;
                updatesysIFR();
        }
        sysvia.ca1=0;
}

void sysca2high()
{
        if ((!sysvia.ca2 && (sysvia.pcr&4)) || OS01) /*OS 0.10 sets PCR to 0 and expects the keyboard to still work*/
        {
                sysvia.ifr|=1;
                updatesysIFR();
        }
        sysvia.ca2=1;
}
void sysca2low()
{
        if (sysvia.ca2 && !(sysvia.pcr&4))
        {
                sysvia.ifr|=1;
                updatesysIFR();
        }
        sysvia.ca2=0;
}

void syscb1()
{
        sysvia.ifr|=0x10;
        updatesysIFR();
}

uint8_t IC32=0;

int scrsize;
int keycol,keyrow;
int bbckey[16][16];
int keysdown=0;

uint8_t sdbval;

void writeIC32(uint8_t val)
{
        uint8_t oldIC32=IC32;
        int temp=0;
        if (val&8)
           IC32|=(1<<(val&7));
        else
           IC32&=~(1<<(val&7));
//        printf("IC32 now %02X\n",IC32);
        scrsize=((IC32&16)?2:0)|((IC32&32)?1:0);
        if (!(IC32&8)&&(oldIC32&8))
           updatekeyboard();
        if (!(IC32&1))
           writesound(sdbval);
        if ((IC32&192)!=(oldIC32&192))
        {
                if (!(IC32&64)) temp|=KB_CAPSLOCK_FLAG;
                if (!(IC32&128)) temp|=KB_SCROLOCK_FLAG;
        }
        if (MASTER) cmosupdate(IC32,sdbval);
}

void writedatabus(uint8_t val)
{
        sdbval=val;
        if (!(IC32&8))
        {
                keyrow=(val>>4)&7;
                keycol=val&0xF;
                updatekeyboard();
        }
        if (MASTER) cmosupdate(IC32,sdbval);
        if (!(IC32&1)) writesound(val);
}

void writesysvia(uint16_t addr, uint8_t val)
{
        switch (addr&0xF)
        {
                case ORA:
                sysvia.ifr&=0xfd;
                if (!(sysvia.pcr&4) || (sysvia.pcr&8)) sysvia.ifr&=~1;
                updatesysIFR();
                case ORAnh:
                sysvia.ora=val;
                sysvia.porta=(sysvia.porta & ~sysvia.ddra)|(sysvia.ora & sysvia.ddra);
                writedatabus(val);
                break;

                case ORB:
                sysvia.orb=val;
                sysvia.portb=(sysvia.portb & ~sysvia.ddrb)|(sysvia.orb & sysvia.ddrb);
                sysvia.ifr&=0xef;//~PORTBINT;
                if (!(sysvia.pcr&0x40) || (sysvia.pcr&0x80)) sysvia.ifr&=~8;
                writeIC32(val);
                updatesysIFR();
                if (MASTER) cmoswriteaddr(val);
                break;

                case DDRA:
                sysvia.ddra=val;
                break;
                case DDRB:
                sysvia.ddrb=val;
                break;
                case ACR:
                sysvia.acr=val;
                break;
                case PCR:
                sysvia.pcr=val;
//                printf("PCR write %02X %04X\n",val,pc);
                break;
                case T1LL:
                case T1CL:
                sysvia.t1l&=0x1FE00;
                sysvia.t1l|=(val<<1);
//                rpclog("ST1L now %04X\n",sysvia.t1l);
                break;
                case T1LH:
                sysvia.t1l&=0x1FE;
                sysvia.t1l|=(val<<9);
                if (sysvia.acr&0x40)
                {
                        sysvia.ifr&=~TIMER1INT;
                        updatesysIFR();
                }
//                rpclog("ST1L now %04X\n",sysvia.t1l);
                break;
                case T1CH:
                sysvia.t1l&=0x1FE;
                sysvia.t1l|=(val<<9);
                sysvia.t1c=sysvia.t1l+1;
                sysvia.ifr&=~TIMER1INT;
                updatesysIFR();
                sysvia.t1hit=0;
//                rpclog("ST1L now %04X\n",sysvia.t1l);
                break;
                case T2CL:
                sysvia.t2l&=0x1FE00;
                sysvia.t2l|=(val<<1);
//                rpclog("ST2L now %04X\n",sysvia.t2l);
                break;
                case T2CH:
                if (sysvia.t2c==-3 && (sysvia.ier&TIMER2INT) && !(sysvia.ifr&TIMER2INT))
                {
                        interrupt|=128;
                }
                sysvia.t2l&=0x1FE;
                sysvia.t2l|=(val<<9);
                sysvia.t2c=sysvia.t2l+1;
                sysvia.ifr&=~TIMER2INT;
                updatesysIFR();
                sysvia.t2hit=0;
//                rpclog("ST2L now %04X\n",sysvia.t2l);
                break;
                case IER:
                if (val&0x80)
                   sysvia.ier|=(val&0x7F);
                else
                   sysvia.ier&=~(val&0x7F);
                updatesysIFR();
//                printf("IER now %02X\n",sysvia.ier);
                break;
                case IFR:
//                        printf("Write IFR %02X %04X\n",val,pc);
                sysvia.ifr&=~(val&0x7F);
                updatesysIFR();
                break;
        }
}

uint8_t readsysvia(uint16_t addr)
{
        uint8_t temp;
        addr&=0xF;
        switch (addr&0xF)
        {
                case ORA:
                sysvia.ifr&=~PORTAINT;
                updatesysIFR();
                case ORAnh:
                if (MASTER && cmosenabled()) return cmosread();
                temp=sysvia.ora & sysvia.ddra;
                temp|=(sysvia.porta & ~sysvia.ddra);
                temp&=0x7F;
                if (bbckey[keycol][keyrow]) return temp|0x80;
                return temp;

                case ORB:
                sysvia.ifr&=0xEF;//~PORTBINT;
                updatesysIFR();
                temp=sysvia.orb & sysvia.ddrb;
                if (sysvia.acr&2)
                   temp|=(sysvia.irb & ~sysvia.ddrb);
                else
                   temp|=(sysvia.portb & ~sysvia.ddrb);
                temp|=0xF0;
                if (joybutton[0]) temp&=~0x10;
                if (joybutton[1]) temp&=~0x20;
                return temp;

                case DDRA:
                return sysvia.ddra;
                case DDRB:
                return sysvia.ddrb;
                case T1LL:
                return (sysvia.t1l&0x1FE)>>1;
                case T1LH:
                return sysvia.t1l>>9;
                case T1CL:
                sysvia.ifr&=~TIMER1INT;
                updatesysIFR();
                if (sysvia.t1c<-1) return 0xFF;
                return ((sysvia.t1c+1)>>1)&0xFF;
                case T1CH:
                if (sysvia.t1c<-1) return 0xFF;
                return ((sysvia.t1c+1)>>1)>>8;
                case T2CL:
                sysvia.ifr&=~TIMER2INT;
                updatesysIFR();
                if (sysvia.acr&0x20) return (sysvia.t2c>>1)&0xFF;
                return ((sysvia.t2c+1)>>1)&0xFF;
                case T2CH:
                if (sysvia.acr&0x20) return (sysvia.t2c>>1)>>8;
                return ((sysvia.t2c+1)>>1)>>8;
                case ACR:
                return sysvia.acr;
                case PCR:
                return sysvia.pcr;
                case IER:
                return sysvia.ier|0x80;
                case IFR:
//                        printf("Reading IFR %02X\n",sysvia.ifr);
                return sysvia.ifr;
        }
        return 0xFE;
}

void presskey(int row, int col);
int autoboot;
void resetsysvia()
{
        sysvia.ifr=sysvia.ier=0;
        sysvia.t1c=sysvia.t1l=0x1FFFE;
        sysvia.t2c=sysvia.t2l=0x1FFFE;
        sysvia.t1hit=sysvia.t2hit=0;
        if (autoboot)
           presskey(0,0);
}

/*Keyboard*/
#include "scan2bbc.h"

uint8_t codeconvert[128]=
{
        0,30,48,46,32,18,33,34,    //0
        35,23,36,37,38,50,49,24,   //8
        25,16,19,31,20,22,47,17,   //16
        45,21,44,11,2,3,4,5,       //24
        6,7,8,9,10,100,101,102,       //32
        103,104,105,106,107,108,109,59,   //40
        60,61,62,63,64,65,66,67,   //48
        68,87,88,1,41,12,13,14,    //56
        15,26,27,28,39,40,43,86,   //64
        51,52,53,57,0,83,116,79,     //72
        0,115,75,77,72,80,114,55,      //80
        74,78,83,111,84,0,115,125,   //88
        112,121,123,42,54,29,0,56, //96
        0,39,92,56,70,69,58,0,     //104
        0,0,58,0,0,0,0,0,          //112
        0,0,0,0,0,113,58,0,          //120
};

void presskey(int row, int col)
{
        bbckey[col][row]=1;
}

void releasekey(int row, int col)
{
        bbckey[col][row]=0;
}

static inline int TranslateKey(int index, int *row, int *col)
{
        unsigned int vkey=scan2bbc[index & 127];
        if (vkey==0xaa) return -1;
        *col=vkey&15;
        *row=(vkey>>4)&15;
        return *row;
}

int keys2[128];

void clearkeys()
{
        int c;
        int row,col;
        for (c=0;c<128;c++)
        {
                keys2[c]=0;
                if (TranslateKey(codeconvert[keylookup[c]],&row,&col)>0) releasekey(row,col);
        }
}

void checkkeys()
{
        int c;
        int row,col;
        int rc;
        memset(bbckey,0,sizeof(bbckey));
        for (c=0;c<128;c++)
        {
                rc=c;
                if (keyas && c==KEY_A) rc=KEY_CAPSLOCK;
//                if (keyas && c==KEY_S) rc=KEY_LCONTROL;
                if (key[c]/*!=keys2[c] && */ && rc!=KEY_F11)
                {
//                rpclog("%i %i\n",c,rc);
                        if (TranslateKey(codeconvert[keylookup[rc]],&row,&col)>0)
                        {
                                if (key[c])
                                   presskey(row,col);
//                                else
//                                   releasekey(row,col);
                        }
                }
        }
        if (key[keylookup[KEY_RSHIFT]]||key[keylookup[KEY_LSHIFT]]||autoboot)
           presskey(0,0);
//        else
//           releasekey(0,0);
        if (key[keylookup[KEY_LCONTROL]] || key[keylookup[KEY_RCONTROL]] || (keyas && key[KEY_S]))
           presskey(0,1);
//        else
//           releasekey(0,1);
        for (c=0;c<128;c++)
            keys2[c]=key[c];
        updatekeyboard();
}

void updatekeyboard()
{
        int c,d;
//        printf("Updatekeyboard %02X %i\n",sysvia.pcr,IC32&8);
//        if ((sysvia.pcr&0xC)==4)
//        {
                if (IC32&8)
                {
//                        printf("Keyless scan!\n");
                        for (d=0;d<((MASTER)?13:10);d++)
                        {
                                for (c=1;c<8;c++)
                                {
                                        if (bbckey[d][c])
                                        {
                                                sysca2high();
//                                                sysvia.ifr|=1;
                                                return;
                                        }
                                }
                        }
                        sysca2low();
                }
                else
                {
                        if (keycol<((MASTER)?13:10))
                        {
                                for (c=1;c<8;c++)
                                {
                                        if (bbckey[keycol][c])
                                        {
                                                sysca2high();
                                                return;
                                        }
                                }
                        }
                        sysca2low();
                }
//        }
}

void initDIPS(uint8_t dips)
{
        int c;
        for (c=9;c>=2;c--)
        {
                if (dips&1)
                   presskey(0,c);
                else
                   releasekey(0,c);
                dips>>=1;
        }
}
