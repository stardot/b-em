/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.4a


              All of this code is written by Tom Walker
         You may use SMALL sections from this program (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*VIA emulation (user + system)*/

#include <allegro.h>
#include <stdio.h>
#include "6502.h"
#include "vias.h"
#include "sound.h"
#include "video.h"

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

int Cycles;
int orbwrites=0,orareads=0;
unsigned char IC32State=0;
unsigned char UIC32State=0;
int printwrites=0;
char bbckey[10][8];
unsigned char sdbval;
int extracycles=0;
FILE *printer;
int callbacks=0;
int firsttime=1;
VIAState SysVIA;
VIAState UserVIA;
int printtarget;
int printertime=0;
int firetrackhack;
static inline void updateIFRtopbit()
{
        if (SysVIA.ifr&(SysVIA.ier&0x7f))
           SysVIA.ifr|=0x80;
        else
           SysVIA.ifr&=0x7f;
//        intStatus&=~(1<<sysVia);
//        intStatus|=((SysVIA.ifr & 128)?(1<<sysVia):0);
        if (SysVIA.ifr&0x80)
           intStatus=1;
        else if (!firetrackhack)
           intStatus=0;
}

void SysCA2(int value)
{
        SysVIA.ifr|=1;
        updateIFRtopbit();
}

void SysCB1(int value)
{
        SysVIA.ifr|=0x10;
        updateIFRtopbit();
}

static inline void checkkey()
{
        int Oldflag=(SysVIA.ifr & 1);
        Oldflag=Oldflag;
        if ((Keysdown>0) && ((SysVIA.pcr & 0xc)==4))
        {
                if ((IC32State & 8)==8)
                {
                        SysVIA.ifr|=1;
                        updateIFRtopbit();
                }
                else
                {
                        if (KBDCol<10)
                        {
                                int presrow;
                                for(presrow=1;presrow<8;presrow++)
                                {
                                        if (bbckey[KBDCol][presrow])
                                        {
                                                SysVIA.ifr|=1;
                                                updateIFRtopbit();
                                        }
                                }
                        }
                }
        }
}

void presskey(int row, int col)
{
        if (!bbckey[col][row] && row!=0)
           Keysdown++;
        bbckey[col][row]=1;
        checkkey();
}

static inline void IC32Write(unsigned char Value)
{
        int bit;
        int oldval=IC32State;
        oldval=oldval;
        bit=Value & 7;
        if (Value & 8)
           IC32State|=(1<<bit);
        else
           IC32State&=0xff-(1<<bit);
        if ((!(oldval & 1)) && ((IC32State & 1)))  soundwrite(sdbval);
        scrlenindex=((IC32State&16)?2:0)|((IC32State&32)?1:0);
//        scrlenindex=(IC32State&48)>>4;
        checkkey();
}

static inline void SlowDataBusWrite(unsigned char Value)
{
        sdbval=Value;
        if (!(IC32State & 8))
        {
                KBDRow=(Value>>4) & 7;
                KBDCol=(Value & 0xf);
                checkkey();
        }
        if (!(IC32State & 1))
        {
                soundwrite(sdbval);
        }
}

static inline int KbdOP()
{
        if ((KBDCol>9) || (KBDRow>7)) return(0);
        return(bbckey[KBDCol][KBDRow]);
}

static inline int SlowDataBusRead()
{
  int result=(SysVIA.ora & SysVIA.ddra);
  if (KbdOP()) result|=128;
  return(result);
}

void SysVIATriggerCA1Int(int value)
{
        if (!((SysVIA.pcr & 1) ^ value))
        {
                SysVIA.ifr|=2; /* CA1 */
                updateIFRtopbit();
                SysVIA.ira=(SysVIA.pal & (SysVIA.ddra ^ 255)) | (SysVIA.ora & SysVIA.ddra);
        }

//        if ((SysVIA.pcr & 1)==value)
//        {
//                SysVIA.ifr|=2; /* CA1 */
//                updateIFRtopbit();
/*                if (SysVIA.ier&2)
                {
                        SysVIA.ifr|=0x80;
                        intStatus++;
                }*/
//        }
}

static inline void sysclearint(unsigned char val)
{
        SysVIA.ifr=(SysVIA.ifr&~val)&0x7F;
        if (SysVIA.ifr&SysVIA.ier)
           SysVIA.ifr|=0x80;
}

void SVIAWrite(int Address, int Value)
{
        switch (Address)
        {
                case ORB:
                SysVIA.orb=Value;
                SysVIA.ifr&=~0x18;
//                if ((SysVIA.pcr&0xE0)==0x20||(SysVIA.pcr&0xE0)==0x60)
//                   SysVIA.ifr&=0xEF;
//                else
//                   SysVIA.ifr&=0xE7;
                updateIFRtopbit();
                IC32Write(Value);
                Value&=SysVIA.ddrb;
                SysVIA.pbl&=~SysVIA.ddrb;
                SysVIA.pbl|=Value;
                break;

                case ORA:
                case ORAnh:
                SysVIA.ifr&=~0x3;
                updateIFRtopbit();
//                if ((SysVIA.pcr&0xE)==0x2||(SysVIA.pcr&0xE)==0x6)
//                   SysVIA.ifr&=0xFD;
//                else
//                   SysVIA.ifr&=0xFC;
//                updateIFRtopbit();
                SysVIA.ora=Value;
                SlowDataBusWrite(Value);
                Value&=SysVIA.ddra;
                SysVIA.pal&=~SysVIA.ddra;
                SysVIA.pal|=Value;
                break;

                case DDRB:
                SysVIA.ddrb=Value & 0xff;
                SysVIA.pbl=~Value & 0xf0;
                SysVIA.pbl|=(Value & SysVIA.orb);
                break;

                case DDRA:
                SysVIA.ddra=Value & 0xff;
                SysVIA.pbl=~Value & 0xff;
                SysVIA.pbl|=(Value & SysVIA.ora);
                break;

                case T1LL:
                case T1CL:
                SysVIA.timer1l&=0xff00;
                SysVIA.timer1l|=(Value & 0xff);
                break;

                case T1LH:
                SysVIA.timer1l&=0xff;
                SysVIA.timer1l|=(Value<<8);
                break;

                case T1CH:
                SysVIA.timer1l&=0xff;
                SysVIA.timer1l|=(Value & 0xff)<<8;
                SysVIA.timer1c=SysVIA.timer1l;
                SysVIA.ifr &=0xbf; /* clear timer 1 ifr */
                if (SysVIA.acr & 128)
                {
                        SysVIA.orb&=0x7f;
                        SysVIA.irb&=0x7f;
                }
                updateIFRtopbit();
                SysVIA.timer1hasshot=0;
                break;

                case T2CL:
                SysVIA.timer2l&=0xff00;
                SysVIA.timer2l|=(Value & 0xff);
                break;

                case T2CH:
                SysVIA.timer2l&=0xff;
                SysVIA.timer2l|=(Value & 0xff)<<8;
                SysVIA.timer2c=SysVIA.timer2l;
                SysVIA.ifr &=0xdf; /* clear timer 2 ifr */
                updateIFRtopbit();
                SysVIA.timer2hasshot=0;
                break;

                case SR:
                SysVIA.sr=Value;
                break;

                case ACR:
                SysVIA.acr=Value & 0xff;
                SysVIA.pale=Value&1;
                SysVIA.pble=Value&2;
                break;

                case PCR:
                SysVIA.pcr=Value & 0xff;
                break;

                case IER:
                if (Value & 0x80)
                   SysVIA.ier|=Value & 0xff;
                else
                   SysVIA.ier&=~(Value & 0xff);
                SysVIA.ier&=0x7f;
                updateIFRtopbit();
                break;

                case IFR:
                SysVIA.ifr&=~(Value & 0xff);
                updateIFRtopbit();
                break;
        }
}

int SysVIARead(int Address)
{
        int tmp;
        switch (Address)
        {
                case DDRA:
                return SysVIA.ddra;
                break;

                case DDRB:
                return SysVIA.ddrb;
                break;

                case T1LL:
                return SysVIA.timer1l&0xFF;
                break;

                case T1LH:
                return SysVIA.timer1l>>8;
                break;

                case SR:
                return SysVIA.sr;
                break;

                case T1CL:
                SysVIA.ifr&=0xBF;
                updateIFRtopbit();
                return SysVIA.timer1c&0xFF;
                break;

                case T1CH:
                return SysVIA.timer1c>>8;
                break;

                case T2CL:
                SysVIA.ifr&=0xDF;
                updateIFRtopbit();
                return SysVIA.timer2c&0xFF;
                break;

                case T2CH:
                return SysVIA.timer2c>>8;
                break;

                case ACR:
                return SysVIA.acr;
                break;

                case PCR:
                return SysVIA.pcr;
                break;

                case IER:
                return SysVIA.ier|0x80;
                break;

                case IFR:
                updateIFRtopbit();
                return SysVIA.ifr;
                break;

                case ORB:
                if (SysVIA.acr&2)
                {
                        tmp=((SysVIA.orb & SysVIA.ddrb) | (SysVIA.irb & ~SysVIA.ddrb))|48|192;
                }
                else
                {
                        tmp=((SysVIA.orb & SysVIA.ddrb) | (SysVIA.pbl & ~SysVIA.ddrb))|48|192;
                }
                if (!(key_shifts&KB_NUMLOCK_FLAG)&&key[KEY_INSERT])
                   tmp^=16;
//                if ((SysVIA.pcr&0xE0)==0x20||(SysVIA.pcr&0xE0)==0x60)
//                   SysVIA.ifr&=0xEF;
//                else
//                   SysVIA.ifr&=0xE7;
                updateIFRtopbit();
                return tmp;

                case ORA:
                sysclearint(0x2);
//                if ((SysVIA.pcr&0xE)==0x2||(SysVIA.pcr&0xE)==0x6)
//                   SysVIA.ifr&=0xFD;
//                else
//                   SysVIA.ifr&=0xFC;
                updateIFRtopbit();
                if (SysVIA.acr&1)
                   return SysVIA.ira;
                case ORAnh:
                return SlowDataBusRead();
                break;
        }
        return 0xff;
}

void SysVIA_poll_real()
{
        if (SysVIA.timer1c<0)
        {
                SysVIA.timer1c=SysVIA.timer1l;
                if ((SysVIA.timer1hasshot==0) || (SysVIA.acr & 0x40))
                {
                        SysVIA.ifr|=0x40;
                        updateIFRtopbit();
                        if (SysVIA.acr & 0x80)
                        {
                                SysVIA.orb^=0x80;
                                SysVIA.irb^=0x80;
                        }
                }
                SysVIA.timer1hasshot=1;
        }

        if (SysVIA.timer2c<0)
        {
                SysVIA.timer2c=SysVIA.timer2l;
                if (SysVIA.timer2hasshot==0)
                {
                        SysVIA.ifr|=0x20;
                        updateIFRtopbit();
                }
                SysVIA.timer2hasshot=1;
        }
}

static inline void updateusrIFRtopbit()
{
        if (UserVIA.ifr&(UserVIA.ier&0x7f))
           UserVIA.ifr|=0x80;
        else
           UserVIA.ifr&=0x7f;
//        if (UserVIA.ifr&0x80)
//           intStatus=1;
        intStatus&=~(1<<sysVia);
        intStatus|=((UserVIA.ifr & 128)?(1<<sysVia):0);
}

static inline void setuserCA1()
{
        UserVIA.ifr|=2;
        updateusrIFRtopbit();
//        UserVIA.ifr|=0x80;
//        intStatus=1;
//        updateusrIFRtopbit();
/*        if (UserVIA.ier&2)
        {
                UserVIA.ifr|=0x80;
                intStatus=1;
        }
        else
           UserVIA.ifr&=0x7F;*/
}

static inline void UIC32Write(unsigned char Value)
{
        int bit;
        int oldval=UIC32State;
        oldval=oldval;
        bit=Value & 7;
        if (Value & 8)
           UIC32State|=(1<<bit);
        else
           UIC32State&=0xff-(1<<bit);
//        checkkey();
}

int printertime;

static inline void USlowDataBusWrite(unsigned char Value)
{
}

static inline int USlowDataBusRead()
{
        return 0xFF;
}

void UVIAWrite(int Address, int Value)
{
        switch (Address)
        {
                case 0:
                UserVIA.orb=Value & 0xff;
                orbwrites++;
                UIC32Write(Value);
                if ((UserVIA.ifr & 1) && ((UserVIA.pcr & 2)==0))
                {
                        UserVIA.ifr&=0xfe;
                        updateusrIFRtopbit();
                };
//                if ((UserVIA.pcr&0xE0)==0x20||(UserVIA.pcr&0xE0)==0x60)
//                   UserVIA.ifr&=0xEF;
//                else
//                   UserVIA.ifr&=0xE7;
//                updateusrIFRtopbit();
                break;

                case 1:
                UserVIA.ora=Value & 0xff;
                UserVIA.ifr&=0xfc;
                printwrites++;
                updateusrIFRtopbit();
//                fputc(Value,printer);
//                if (Value==0xA)
//                   fputc(0xD,printer);
                printertime=50;
//                if ((UserVIA.pcr&0xE0)==0x20||(UserVIA.pcr&0xE0)==0x60)
//                   UserVIA.ifr&=0xEF;
//                else
//                   UserVIA.ifr&=0xE7;
//                updateusrIFRtopbit();
                break;

                case 2:
                UserVIA.ddrb=Value & 0xff;
                break;

                case 3:
                UserVIA.ddra=Value & 0xff;
                break;

                case 4:
                case 6:
                UserVIA.timer1l&=0xff00;
                UserVIA.timer1l|=(Value & 0xff);
                break;

                case 5:
                UserVIA.timer1l&=0xff;
                UserVIA.timer1l|=(Value & 0xff)<<8;
                UserVIA.timer1c=UserVIA.timer1l;
                UserVIA.ifr &=0xbf; /* clear timer 1 ifr */
                if (UserVIA.acr & 128)
                {
                        UserVIA.orb&=0x7f;
                        UserVIA.irb&=0x7f;
                };
                updateusrIFRtopbit();
                UserVIA.timer1hasshot=0;
                break;

                case 7:
                UserVIA.timer1l&=0xff;
                UserVIA.timer1l|=(Value & 0xff)<<8;
                break;

                case 8:
                UserVIA.timer2l&=0xff00;
                UserVIA.timer2l|=(Value & 0xff);
                break;

                case 9:
                UserVIA.timer2l&=0xff;
                UserVIA.timer2l|=(Value & 0xff)<<8;
                UserVIA.timer2c=UserVIA.timer2l;
                UserVIA.ifr &=0xdf; /* clear timer 2 ifr */
                updateusrIFRtopbit();
                UserVIA.timer2hasshot=0;
                break;

                case 10:
                break;

                case 11:
                UserVIA.acr=Value & 0xff;
                break;

                case 12:
                UserVIA.pcr=Value & 0xff;
                break;

                case 13:
                UserVIA.ifr&=~(Value & 0xff);
                updateusrIFRtopbit();
                break;

                case 14:
                if (Value & 0x80)
                   UserVIA.ier|=Value & 0xff;
                else
                   UserVIA.ier&=~(Value & 0xff);
                UserVIA.ier&=0x7f;
                updateusrIFRtopbit();
                break;

                case 15:
                UserVIA.ora=Value & 0xff;
                break;
        }
}

int UVIARead(int Address)
{
        int tmp;
        switch (Address)
        {
                case 0:
                tmp=UserVIA.orb & UserVIA.ddrb;
                tmp |= 32;
//                if ((UserVIA.pcr&0xE0)==0x20||(UserVIA.pcr&0xE0)==0x60)
//                   UserVIA.ifr&=0xEF;
//                else
//                   UserVIA.ifr&=0xE7;
//                updateusrIFRtopbit();
                return tmp;
                break;

                case 2:
                return UserVIA.ddrb;
                break;

                case 3:
                return UserVIA.ddra;
                break;

                case 4:
                tmp=UserVIA.timer1c;
                UserVIA.ifr&=0xbf;
                updateusrIFRtopbit();
                return tmp & 0xff;
                break;

                case 5:
                tmp=UserVIA.timer1c>>8;
                return tmp & 0xff;
                break;

                case 6:
                return UserVIA.timer1l & 0xff;
                break;

                case 7:
                return (UserVIA.timer1l>>8) & 0xff;
                break;

                case 8:
                tmp=UserVIA.timer2c;
                UserVIA.ifr&=0xdf;
                updateusrIFRtopbit();
                return tmp & 0xff;
                break;

                case 9:
                return (UserVIA.timer2c>>8) & 0xff;
                break;

                case 13:
                updateusrIFRtopbit();
                return UserVIA.ifr;
                break;

                case 14:
                return UserVIA.ier | 0x80;
                break;

                case 1:
                UserVIA.ifr&=0xfc;
                updateusrIFRtopbit();
//                if ((UserVIA.pcr&0xE0)==0x20||(UserVIA.pcr&0xE0)==0x60)
//                   UserVIA.ifr&=0xEF;
//                else
//                   UserVIA.ifr&=0xE7;
//                updateusrIFRtopbit();
                case 15:
                orareads++;
                return USlowDataBusRead();
                break;
        }
        return 0xff;
}

void UVIAReset()
{
        UserVIA.ora=0x80;
        UserVIA.orb=0xff;
        UserVIA.ira=UserVIA.irb=0xff;
        UserVIA.ddra=UserVIA.ddrb=0;
        UserVIA.acr=0;
        UserVIA.pcr=0;
        UserVIA.ifr=0;
        UserVIA.ier=0x80;
        UserVIA.timer1l=UserVIA.timer2l=0xffff;
        UserVIA.timer1c=UserVIA.timer2c=0xffff;
        UserVIA.timer1hasshot=0;
        UserVIA.timer2hasshot=0;
        printtarget=5000000;
}

void SVIAReset()
{
        SysVIA.ora=0x80;
        SysVIA.orb=0x00;
        SysVIA.ira=0x00;
        SysVIA.irb=0xf0;
        SysVIA.ddra=SysVIA.ddrb=0;
        SysVIA.acr=0;
        SysVIA.pcr=0;
        SysVIA.ifr=0;
        SysVIA.ier=0;
        SysVIA.timer1l=SysVIA.timer2l=0xffff;
        SysVIA.timer1c=SysVIA.timer2c=0xffff;
        SysVIA.timer1hasshot=0;
        SysVIA.timer2hasshot=0;
        KBDCol=KBDRow=Keysdown=0;
        oddcycle=0;
}

void UserVIA_poll_real()
{
        if (UserVIA.timer1c<0)
        {
                UserVIA.timer1c=UserVIA.timer1l;
                if ((UserVIA.timer1hasshot==0) || (UserVIA.acr & 0x40))
                {
                        UserVIA.ifr|=0x40;
                        if (UserVIA.ier&UserVIA.ifr)
                        {
                                UserVIA.ifr|=0x80;
                                intStatus=1;
                        }
                        else
                           UserVIA.ifr&=0x7F;
//                        updateusrIFRtopbit();
                        if (UserVIA.acr & 0x80)
                        {
                                UserVIA.orb^=0x80;
                                UserVIA.irb^=0x80;
                        }
                }
                UserVIA.timer1hasshot=1;
        }
        if (UserVIA.timer2c<0)
        {
                UserVIA.timer2c=UserVIA.timer2l;
                if (!(UserVIA.ifr&=0x20))
                {
                        UserVIA.ifr|=0x20;
                        if (UserVIA.ier&UserVIA.ifr)
                        {
                                UserVIA.ifr|=0x80;
                                intStatus=1;
                        }
                        else
                           UserVIA.ifr&=0x7F;
//                        updateusrIFRtopbit();
                }
        }
}

void pollprinter()
{
        if (firsttime)
        {
                printtarget=50;
                printertime=0;
                firsttime=0;
                return;
        }
        printertime=0;
        callbacks=55;
        setuserCA1();
        printtarget=1000000;
        printertime=0;
}
