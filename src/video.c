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

/*Video emulation*/

#include "bbctext.h"
#include <stdlib.h>
#include <stdio.h>
#include <allegro.h>
#include "vias.h"
#include "mem.h"
#include "6502.h"
#include "video.h"
#include "sound.h"
#include "serial.h"

#define CURSOR

int logging;
int frametime=40000;
int scanlinesframe=312;
int mono=1;
int frameskip,fskip;

PALETTE beebpal =
{
      {0,0,0},
      {63,0,0},
      {0,63,0},
      {63,63,0},
      {0,0,63},
      {63,0,63},
      {0,63,63},
      {63,63,63},
      {63,63,63},
      {0,63,63},
      {63,0,63},
      {0,0,63},
      {63,63,0},
      {63,0,63},
      {0,63,63},
      {63,63,63}
};

PALETTE monopal =
{
      {0,0,0},
      {16,16,16},
      {24,24,24},
      {40,40,40},
      {8,8,8},
      {24,24,24},
      {32,32,32},
      {56,56,56},
      {0,0,0},
      {16,16,16},
      {24,24,24},
      {40,40,40},
      {8,8,8},
      {24,24,24},
      {32,32,32},
      {56,56,56},
};

int driveled;
int us;

int flashfreq=0,flash=0;
int videoline=0;
int tempx,tempy,curaddr;
unsigned short scrlen;
int tingtable[32768][2];
typedef long long EightByteType;
#define physx SCREEN_W
#define physy SCREEN_H
#define drawbox(a,b,c,d,e,f) rectfill(a,b,c,d,e,f)

void updateframeskip()
{
      frametime=scanlinesframe*128;
      fskip=frametime*frameskip;
}

int time;
int slowdown;
int screenlen[4]={0x4000,0x5000,0x2000,0x2800};

int mode2table[256][8];

unsigned char VideoULA_ControlReg=0x9c;

int VideoULA_Palette[16]={0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};

unsigned char CRTCControlReg=0;
unsigned char CRTC_HorizontalTotal=127;     /* R0 */
unsigned char CRTC_HorizontalDisplayed=40;  /* R1 */
unsigned char CRTC_HorizontalSyncPos=98;    /* R2 */
unsigned char CRTC_SyncWidth=0x28;          /* R3 */
unsigned char CRTC_VerticalTotal=38;        /* R4 */
unsigned char CRTC_VerticalTotalAdjust=0;   /* R5 */
unsigned char CRTC_VerticalDisplayed=25;    /* R6 */
unsigned char CRTC_VerticalSyncPos=34;      /* R7 */
unsigned char CRTC_InterlaceAndDelay=0;     /* R8 */
unsigned char CRTC_ScanLinesPerChar=7;      /* R9 */
unsigned char CRTC_CursorStart=0;           /* R10 */
unsigned char CRTC_CursorEnd=0;             /* R11 */
unsigned char CRTC_ScreenStartHigh=6;       /* R12 */
unsigned char CRTC_ScreenStartLow=0;        /* R13 */
unsigned char CRTC_CursorPosHigh=0;         /* R14 */
unsigned char CRTC_CursorPosLow=0;          /* R15 */
unsigned char CRTC_LightPenHigh=0;          /* R16 */
unsigned char CRTC_LightPenLow=0;           /* R17 */

void Drawteletextscreen();

int mode2,mode0;

int addrtable[256];

unsigned int offsets[]={0x4000,0x6000,0x3000,0x5800};
int multable[128];

inline void drawchar(int cr, int x, int y, int col, int backcol, char *chrset)
{
      int xx,yy;
      if (cr<32)
      {
            for (yy=0;yy<10;yy++)
                  for (xx=0;xx<6;xx++)
                      b->line[y+yy][x+xx]=backcol;
            return;
      }
      cr-=32;
      cr&=127;
      cr=multable[cr];
      for (yy=0;yy<10;yy++)
      {
            for (xx=0;xx<6;xx++)
            {
                  if (chrset[cr++])
                     b->line[y+yy][x+xx]=col;
                  else
                     b->line[y+yy][x+xx]=backcol;
            }
      }
}

inline void drawcharhigh(int cr, int x, int y, int col, int backcol, char *chrset)
{
      int xx,yy;
      if (cr<32)
      {
            for (yy=0;yy<10;yy++)
                  for (xx=0;xx<6;xx++)
                      b->line[y+yy][x+xx]=backcol;
            return;
      }
      cr-=32;
      cr&=127;
      cr=multable[cr];
      for (yy=0;yy<10;yy+=2)
      {
            for (xx=0;xx<6;xx++)
            {
                  if (chrset[cr++])
                  {
                        b->line[y+yy][x+xx]=col;
                        b->line[y+yy+1][x+xx]=col;
                  }
                  else
                  {
                        b->line[y+yy][x+xx]=backcol;
                        b->line[y+yy+1][x+xx]=backcol;
                  }
            }
      }
}

inline void drawcharlow(int cr, int x, int y, int col, int backcol, char *chrset)
{
      int xx,yy;
      if (cr<32)
      {
            for (yy=0;yy<10;yy++)
                  for (xx=0;xx<6;xx++)
                      b->line[y+yy][x+xx]=backcol;
            return;
      }
      cr-=32;
      cr&=127;
      cr=multable[cr]+30;
      for (yy=0;yy<10;yy+=2)
      {
            for (xx=0;xx<6;xx++)
            {
                  if (chrset[cr++])
                  {
                        b->line[y+yy][x+xx]=col;
                        b->line[y+yy+1][x+xx]=col;
                  }
                  else
                  {
                        b->line[y+yy][x+xx]=backcol;
                        b->line[y+yy+1][x+xx]=backcol;
                  }
            }
      }
}

static inline unsigned int WrapAddr(int in)
{
        if (in<0x8000)
           return in;
        in+=offsets[(IC32State & 0x30)>>4];
        in&=0x7fff;
        return in;
}

static inline char *BeebMemPtrWithWrap(int a, int n)
{
        static char tmpBuf[1024];
        char *tmpBufPtr;
        int EndAddr=a+n-1;
        int toCopy;

        a=WrapAddr(a);
        EndAddr=WrapAddr(EndAddr);

        if (a<=EndAddr)
        {
                return (char *)ram+a;
        }

        toCopy=0x8000-a;
        if (toCopy>n)
           toCopy=n;
        if (toCopy>0)
           memcpy(tmpBuf,ram+a,toCopy);
        tmpBufPtr=tmpBuf+toCopy;
        toCopy=n-toCopy;
        if (toCopy>0)
           memcpy(tmpBufPtr,ram+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */

        return tmpBuf;
}

static inline unsigned int WrapAddrMo7(int in)
{
        if (in<0x8000)
           return in;
        in+=0x7c00;
        in&=0x7fff;
        return in;
}

static inline char *BeebMemPtrWithWrapMo7(int a, int n)
{
        static char tmpBuf[1024];
        char *tmpBufPtr;
        int EndAddr=a+n-1;
        int toCopy;

        a=WrapAddrMo7(a);
        EndAddr=WrapAddrMo7(EndAddr);

        if (a<=EndAddr)
        {
                return (char *)ram+a;
        }

        toCopy=0x8000-a;
        if (toCopy>n)
           return (char *)ram+a;
        if (toCopy>0)
           memcpy(tmpBuf,ram+a,toCopy);
        tmpBufPtr=tmpBuf+toCopy;
        toCopy=n-toCopy;
        if (toCopy>0)
           memcpy(tmpBufPtr,ram+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */
        return tmpBuf;
}

void DrawMode2()
{
        unsigned char temp;
        int tempy;
        int left,right;
        int c,d,tempaddr;
        int xoffset=200-(CRTC_HorizontalDisplayed<<1);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        for (d=0;d<CRTC_VerticalDisplayed<<3;d++)
        {
                tempaddr=(d&7)+((CRTC_HorizontalDisplayed<<3)*(d>>3))+VideoState.Addr;
                if (tempaddr>0x7FFF)
                {
                        tempaddr+=offsets[(IC32State & 0x30)>>4];
                        tempaddr&=0x7FFF;
                }
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
                        if (modela)
                           tempaddr&=0x3FFF;
                        temp=ram[tempaddr];
                        b->line[d+yoffset][(c<<2)+xoffset]=b->line[d+yoffset][(c<<2)+1+xoffset]=VideoULA_Palette[mode2table[temp][0]];
                        b->line[d+yoffset][(c<<2)+2+xoffset]=b->line[d+yoffset][(c<<2)+3+xoffset]=VideoULA_Palette[mode2table[temp][1]];
                        tempaddr+=8;
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }
                }
        }
        if (driveled)
           rectfill(b,384,0,400,8,254);
        else
           rectfill(b,384,0,400,8,0);
        blit(b,screen,0,0,0,0,400,300);
}

#define GETMODE5PIXEL(pixel)                                    \
                        bits = ( temp >> ( 3 - pixel )) & 0x11; \
                        switch ( bits )                         \
                        {                                       \
                                case 0x11 :                     \
                                        col = 10;               \
                                        break;                  \
                                case 0x10 :                     \
                                        col = 8;                \
                                        break;                  \
                                case 0x01 :                     \
                                        col = 2;                \
                                        break;                  \
                                case 0x00 :                     \
                                        col = 0;                \
                                        break;                  \
                        }
//#define GETMODE5PIXEL(p) col=mode5table[temp][p];
int bytesread;

void DrawMode5(int mode1)
{
        int c,d,e,tempaddr,f;
        unsigned char temp;
        int bits,col;
        int xoffset=200-(CRTC_HorizontalDisplayed<<1);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        if (!mode1)
           xoffset=200-(CRTC_HorizontalDisplayed<<2);
        tempaddr=VideoState.Addr;
        for (d=0;d<CRTC_VerticalDisplayed<<3;d+=8)
        {
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
                        if (modela)
                           tempaddr&=0x3FFF;
                        for (f=0;f<8;f++)
                        {
                              temp=ram[tempaddr+f]&255;
                              if (!mode1) //Mode 5
                              {
                                    for (e=0;e<4;e++)
                                        b->line[d+f+yoffset][(c<<3)+(e<<1)+xoffset]=b->line[d+f+yoffset][(c<<3)+(e<<1)+1+xoffset]=VideoULA_Palette[mode2table[temp][e]];
                              }
                              else //Mode 1
                              {
                                    for (e=0;e<4;e++)
                                        b->line[d+f+yoffset][(c<<2)+e+xoffset]=VideoULA_Palette[mode2table[temp][e]&15];
                              }
                        }
                        tempaddr+=8;
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }
                }
        }
        if (driveled)
           rectfill(b,384,0,400,8,254);
        else
           rectfill(b,384,0,400,8,0);
        blit(b,screen,0,0,0,0,400,300);
}

void Drawteletextscreen()
{
        int x,y,addr=VideoState.Addr&0x3FF;
        int realx,realy=0;
        int colour=7;
        int backcolour=0;
        int dblheight=0;
        int lastdblheight=0;
        int dblheighten=0;
        int dbl=0;
        int sepgfx=0;
        unsigned char temp;
        char *chrset;
        char chrstring[2]={0,0};
        int xoffset=200-(CRTC_HorizontalDisplayed*3);
        int yoffset=150-(CRTC_VerticalDisplayed*5);
        text_mode(0);
        for (y=0;y<CRTC_VerticalDisplayed;y++)
        {
                colour=7;
                backcolour=0;
                dblheight=0;
                realx=0;
                dblheighten=0;
                chrset=teletext_characters;
                for (x=0;x<CRTC_HorizontalDisplayed;x++)
                {
                        if (modela)
                           temp=ram[addr+0x3C00];
                        else
                           temp=ram[addr+0x7C00];
                        addr++;
                        addr&=0x3FF;
                        if (temp&0x80)
                        {
                              switch (temp)
                              {
                                    case 129: /*Change colour - no graphics*/
                                    case 130:
                                    case 131:
                                    case 132:
                                    case 133:
                                    case 134:
                                    case 135:
                                    colour=temp&7;
                                    chrset=teletext_characters;
                                    break;

                                    case 140: /*Double height off*/
                                    dblheight=0;
                                    break;

                                    case 141: /*Double height on*/
                                    dblheight=1;
                                    dblheighten=1;
                                    if (!lastdblheight)
                                       dbl=1;
                                    break;

                                    case 145: /*Change colour - graphics*/
                                    case 146:
                                    case 147:
                                    case 148:
                                    case 149:
                                    case 150:
                                    case 151:
                                    colour=temp&7;
                                    if (sepgfx)
                                       chrset=teletext_separated_graphics;
                                    else
                                       chrset=teletext_graphics;
                                    break;

                                    case 153: //unseparated graphics
                                    chrset=teletext_graphics;
                                    sepgfx=0;
                                    break;

                                    case 154: //Separated graphics
                                    chrset=teletext_separated_graphics;
                                    sepgfx=1;
                                    break;

                                    case 156: /*Black background*/
                                    backcolour=0;
                                    break;

                                    case 157: /*New background*/
                                    backcolour=colour;
                                    break;
                              }
                              temp&=127;
                        }
                        if (!dblheight)
                           drawchar(temp,realx+xoffset,realy+yoffset,colour,backcolour,chrset);
                        else if (dbl==1)
                           drawcharhigh(temp,realx+xoffset,realy+yoffset,colour,backcolour,chrset);
                        else
                           drawcharlow(temp,realx+xoffset,realy+yoffset,colour,backcolour,chrset);
                        realx+=6;
                }
                lastdblheight=dblheighten;
                if (dbl==1)
                   dbl=2;
                else if (dbl==2)
                   dbl=1;
                realy+=10;
        }
        if (driveled)
           rectfill(b,384,0,400,8,254);
        else
           rectfill(b,384,0,400,8,0);
        blit(b,screen,0,0,0,0,400,300);
}

#define GETMODE0PIXEL(pixel) col=((temp>>(7-pixel))&0x1)*15;

void DrawMode0()
{
        unsigned short tempaddr;
        int c,d,e;
        unsigned char temp;
        unsigned char col;
        int xoffset=200-(CRTC_HorizontalDisplayed<<2);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        if (mode0)
           xoffset=200-(CRTC_HorizontalDisplayed<<1);
        for (d=0;d<CRTC_VerticalDisplayed<<3;d++)
        {
                tempaddr=(d&7)+((CRTC_HorizontalDisplayed<<3)*(d/8))+VideoState.Addr;
                if (modela)
                   tempaddr&=0x3FFF;
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
                        temp=ram[tempaddr];
                        tempaddr+=8;
                        if (!mode0)
                        {
                              for (e=0;e<8;e++)
                                  b->line[d+yoffset][(c<<3)+e+xoffset]=VideoULA_Palette[mode2table[temp][e]];
                        }
                        else
                        {
                              for (e=0;e<8;e+=2)
                                  b->line[d+yoffset][(c<<2)+(e>>1)+xoffset]=VideoULA_Palette[mode2table[temp][e]];
                        }
                }
        }
        if (driveled)
           rectfill(b,384,0,400,8,254);
        else
           rectfill(b,384,0,400,8,0);
        blit(b,screen,0,0,0,0,400,300);
}

inline void startframe()
{
        int tmphigh=CRTC_ScreenStartHigh;
        if (VideoState.IsTeletext)
        {
                tmphigh^=0x20;
                tmphigh+=0x74;
                tmphigh&=255;
                VideoState.Addr=CRTC_ScreenStartLow+(tmphigh<<8);
        }
        else
        {
                VideoState.Addr=(CRTC_ScreenStartLow+(CRTC_ScreenStartHigh<<8))<<3;
        }
        if (VideoState.IsTeletext)
           scrlen=0x400;
        else
           scrlen=screenlen[scrlenindex];
}

void DrawMode2line(int line)
{
        int c,tempaddr;
        unsigned char temp;
        int tempy,left,right;
        int xoffset=200-(CRTC_HorizontalDisplayed<<1);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed<<3)*(line>>3))+VideoState.Addr;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=ram[tempaddr];
                tempaddr+=8;
                left=mode2table[temp][0];
                right=mode2table[temp][1];
                b->line[line+yoffset][(c<<2)+xoffset]=b->line[line+yoffset][(c<<2)+1+xoffset]=VideoULA_Palette[left];
                b->line[line+yoffset][(c<<2)+2+xoffset]=b->line[line+yoffset][(c<<2)+3+xoffset]=VideoULA_Palette[right];
        }
}

void DrawMode5line(int line, int mode1)
{
        int tempy,tempaddr,c;
        unsigned char temp;
        unsigned char bits;
        unsigned char col;
        int xoffset=200-(CRTC_HorizontalDisplayed<<1);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        if (!mode1)
           xoffset=200-(CRTC_HorizontalDisplayed<<2);
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed<<3)*(line>>3))+VideoState.Addr;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=ram[tempaddr];
                tempaddr+=8;
                if (!mode1) //Mode 5
                {
                        col=mode2table[temp][0];
                        b->line[line+yoffset][(c<<3)+xoffset]=VideoULA_Palette[(int)col];
                        b->line[line+yoffset][(c<<3)+1+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][1];
                        b->line[line+yoffset][(c<<3)+2+xoffset]=VideoULA_Palette[(int)col];
                        b->line[line+yoffset][(c<<3)+3+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][2];
                        b->line[line+yoffset][(c<<3)+4+xoffset]=VideoULA_Palette[(int)col];
                        b->line[line+yoffset][(c<<3)+5+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][3];
                        b->line[line+yoffset][(c<<3)+6+xoffset]=VideoULA_Palette[(int)col];
                        b->line[line+yoffset][(c<<3)+7+xoffset]=VideoULA_Palette[(int)col];
                }
                else //Mode 1
                {
                        col=mode2table[temp][0];
                        b->line[line+yoffset][(c<<2)+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][1];
                        b->line[line+yoffset][((c<<2)+1)+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][2];
                        b->line[line+yoffset][((c<<2)+2)+xoffset]=VideoULA_Palette[(int)col];
                        col=mode2table[temp][3];
                        b->line[line+yoffset][((c<<2)+3)+xoffset]=VideoULA_Palette[(int)col];
                }
        }
}

void DrawMode0line(int line)
{
        int c,tempaddr,d;
        unsigned char temp,col;
        int xoffset=200-(CRTC_HorizontalDisplayed<<1);
        int yoffset=150-(CRTC_VerticalDisplayed<<2);
        if (!mode0)
           xoffset=200-(CRTC_HorizontalDisplayed<<2);
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed*8)*(line>>3))+VideoState.Addr;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=ram[tempaddr];
                tempaddr+=8;
                if (!mode0)
                {
                        b->line[line+yoffset][(c<<3)+xoffset]=VideoULA_Palette[mode2table[temp][0]];
                        b->line[line+yoffset][(c<<3)+xoffset+1]=VideoULA_Palette[mode2table[temp][1]];
                        b->line[line+yoffset][(c<<3)+xoffset+2]=VideoULA_Palette[mode2table[temp][2]];
                        b->line[line+yoffset][(c<<3)+xoffset+3]=VideoULA_Palette[mode2table[temp][3]];
                        b->line[line+yoffset][(c<<3)+xoffset+4]=VideoULA_Palette[mode2table[temp][4]];
                        b->line[line+yoffset][(c<<3)+xoffset+5]=VideoULA_Palette[mode2table[temp][5]];
                        b->line[line+yoffset][(c<<3)+xoffset+6]=VideoULA_Palette[mode2table[temp][6]];
                        b->line[line+yoffset][(c<<3)+xoffset+7]=VideoULA_Palette[mode2table[temp][7]];
                }
                else
                {
                        b->line[line+yoffset][(c<<2)+xoffset]=VideoULA_Palette[mode2table[temp][0]];
                        b->line[line+yoffset][(c<<2)+xoffset+1]=VideoULA_Palette[mode2table[temp][2]];
                        b->line[line+yoffset][(c<<2)+xoffset+2]=VideoULA_Palette[mode2table[temp][4]];
                        b->line[line+yoffset][(c<<2)+xoffset+3]=VideoULA_Palette[mode2table[temp][6]];
                }
        }
}

int scancount=0;
int refresh=0;
int videodelay;

void doscreen()
{
        int c;
        if (!scanlinedraw||VideoState.IsTeletext)
        {
                if (slowdown)
                   while (time<2);

                time=0;
                startframe();
                videoaddr=VideoState.Addr;
                if (VideoState.IsTeletext)
                   Drawteletextscreen();
                else
                {
                        switch(VideoULA_ControlReg&0xFE)
                        {
                                case 0xF4: case 0xF5: case 0x14:
                                DrawMode2();
                                break;
                                case 0xC4: case 0xC5: case 0xE0: case 0x64:
                                DrawMode5(0);
                                break;
                                case 0xD8: case 0xD9: case 0x18:
                                DrawMode5(1);
                                break;
                                default:
                                DrawMode0();
                                break;
                        }
                }
        }
        else
        {
                startframe();
                scancount++;
                if (scancount>scanlinesframe)
                {
                        if (slowdown)
                           while (time<2);
                        if (VideoState.IsTeletext)
                           Drawteletextscreen();
                        else
                           blit(b,screen,0,0,0,0,400,300);
                        refresh++;
                        scancount=0;
                }
                if ((scancount<(CRTC_VerticalDisplayed<<3)+CRTC_VerticalSyncPos)&&!VideoState.IsTeletext&&(scancount>CRTC_VerticalSyncPos))
                {
                        switch(VideoULA_ControlReg&0xFE)
                        {
                                case 0xF4: case 0xF5:
                                DrawMode2line(scancount-CRTC_VerticalSyncPos);
                                break;
                                case 0xC4: case 0xC5: case 0xE0: case 0x64: case 0x04:
                                DrawMode5line(scancount-CRTC_VerticalSyncPos,0);
                                break;
                                case 0xD8: case 0xD9: case 0x18:
                                DrawMode5line(scancount-CRTC_VerticalSyncPos,1);
                                break;
                                default:
                                DrawMode0line(scancount-CRTC_VerticalSyncPos);
                                break;
                        }
                }
        }
}

void MakeMode2Table()
{
        int temp,left,right,temp2,c,col,bits;
        for (temp=0;temp<256;temp++)
        {
                temp2=temp;
                for (c=0;c<8;c++)
                {
                     left=0;
                     if (temp2&2)
                        left|=1;
                     if (temp2&8)
                        left|=2;
                     if (temp2&32)
                        left|=4;
                     if (temp2&128)
                        left|=8;
                     mode2table[temp][c]=left;
                     temp2<<=1;
                }
        }
}

void CRTCWrite(unsigned int Address, unsigned char Value)
{
        int c;
        Value&=0xff;
        if (Address & 1)
        {
                switch (CRTCControlReg)
                {
                        case 0:
                        CRTC_HorizontalTotal=Value;
                        break;

                        case 1:
                        CRTC_HorizontalDisplayed=Value;
                        clear(b);
                        for (c=0;c<256;c++) /*Rebuild table*/
                            addrtable[c]=(c>>3)*(Value<<3);
                        break;

                        case 2:
                        CRTC_HorizontalSyncPos=Value;
                        break;

                        case 3:
                        CRTC_SyncWidth=Value;
                        break;

                        case 4:
                        scanlinesframe=(Value+1)*(CRTC_ScanLinesPerChar+1);
                        CRTC_VerticalTotal=Value;
                        frametime=scanlinesframe*128;
                        fskip=frametime*frameskip;
                        break;

                        case 5:
                        CRTC_VerticalTotalAdjust=Value;
                        break;

                        case 6:
                        CRTC_VerticalDisplayed=Value;
                        clear(b);
                        break;

                        case 7:
                        CRTC_VerticalSyncPos=Value;
                        clear(b);
                        break;

                        case 8:
                        CRTC_InterlaceAndDelay=Value;
                        break;

                        case 9:
                        if (VideoState.IsTeletext)
                           Value>>=1;
                        scanlinesframe=(Value+1)*(CRTC_VerticalTotal+1);
                        CRTC_ScanLinesPerChar=Value;
                        frametime=scanlinesframe*128;
                        fskip=frametime*frameskip;
                        break;

                        case 10:
                        CRTC_CursorStart=Value;
                        break;

                        case 11:
                        CRTC_CursorEnd=Value;
                        break;

                        case 12:
                        CRTC_ScreenStartHigh=Value;
                        break;

                        case 13:
                        CRTC_ScreenStartLow=Value;
                        break;

                        case 14:
                        CRTC_CursorPosHigh=Value;
                        break;

                        case 15:
                        CRTC_CursorPosLow=Value;
                        break;

                        case 16:
                        CRTC_LightPenHigh=Value;
                        break;

                        case 17:
                        CRTC_LightPenLow=Value;
                        break;
                }
        }
        else
          CRTCControlReg=Value & 0x1f;
}

int CRTCRead(int Address)
{
        if (Address & 1)
        {
                switch (CRTCControlReg)
                {
                        case 14:
                        return CRTC_CursorPosHigh;
                        break;

                        case 15:
                        return CRTC_CursorPosLow;
                        break;

                        case 16:
                        return CRTC_LightPenHigh;
                        break;

                        case 17:
                        return CRTC_LightPenLow;
                        break;
                }
        }
        return 0;
}

void ULAWrite(int Address, int Value)
{
        int c;
        int oldulareg;
        if (Address & 1)
        {
                VideoULA_Palette[(Value & 0xf0)>>4]=(Value & 0xf) ^ 7;
        }
        else
        {
                oldulareg=VideoULA_ControlReg;
                VideoULA_ControlReg=Value;
                if (VideoULA_ControlReg&2)
                   VideoState.IsTeletext=1;
                else
                   VideoState.IsTeletext=0;
                if ((VideoULA_ControlReg&0xFE)==0xF4)
                   mode2=1;
                else
                   mode2=0;
                if (VideoULA_ControlReg&0x10)
                   mode0=1;
                else
                   mode0=0;
                flash=1-(VideoULA_ControlReg&1);
                if (!mono)
                {
                        if (flash)
                        {
                                 for (c=0;c<8;c++)
                                     beebpal[c+8]=beebpal[c+16];
                        }
                        else
                        {
                                 for (c=0;c<8;c++)
                                     beebpal[c+8]=beebpal[c+24];
                        }
                        set_palette(beebpal);
                }
                else
                {
                        if (flash)
                        {
                                 for (c=0;c<8;c++)
                                     monopal[c+8]=monopal[c+16];
                        }
                        else
                        {
                                 for (c=0;c<8;c++)
                                     monopal[c+8]=monopal[c+24];
                        }
                        set_palette(monopal);
                }
        }
}

int ULARead(int Address)
{
        return Address;
}

void initvideo()
{
        int c;
        if (set_gfx_mode(GFX_VESA2L,400,300,0,0))
        {
               if (set_gfx_mode(GFX_MODEX,400,300,0,0))
               {
                     printf("400x300 video mode not available\n");
                     exit(-1);
               }
        }
        for (c=16;c<256;c++)
        {
               beebpal[c]=beebpal[c&15];
               monopal[c]=monopal[c&15];
        }
        beebpal[255].r=beebpal[255].g=beebpal[255].b=63;
        beebpal[254].r=63;
        beebpal[254].g=beebpal[254].b=0;
        monopal[255].r=monopal[255].g=monopal[255].b=63;
        monopal[254].r=63;
        monopal[254].g=monopal[254].b=0;
        if (mono)
           set_palette(monopal);
        else
           set_palette(beebpal);
        b=create_bitmap(400,300);
        clear(b);
        VideoState.DataPtr=BeebMemPtrWithWrapMo7(0x3000,640);
        mode2=0;
        scrlenindex=0;
        VideoState.PixmapLine=0;
        VideoState.PreviousFinalPixmapLine=255;
        MakeMode2Table();
        videoline=99;
        for (c=0;c<128;c++)
            multable[c]=c*60;
}

void initstuff()
{
        allegro_init();
}
