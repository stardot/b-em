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

/*video.c - Video emulation*/

#include <stdlib.h>
#include <stdio.h>
#include "gfx.h"
#include "vias.h"
#include "mem.h"
//#include "teletext.h"
#include "6502.h"
#include "video.h"
#include "sound.h"
#include "serial.h"

#define CURSOR

int us;
unsigned char lastbyte;
int tapedelay;
int slowdown=1;
int time;
int printertime,printtarget;
int videocycles=4;
FILE *tape;
char *st;
int discside;
int flashfreq=0,flash=0;
int videoline=0;
int tempx,tempy,curaddr;
unsigned short scrlen;
int tingtable[32768][2];
int magicnum;
typedef long long EightByteType;

typedef union
{
        unsigned char data[8];
        EightByteType eightbyte;
} EightUChars;

typedef union
{
        unsigned char data[16];
        EightByteType eightbytes[2];
} SixteenUChars;

int screenlen[4]={0x4000,0x5000,0x2000,0x2800};

BMP *tempb;
FONT *dblhigh;
int mode2table[256][2];
int mode5table[256][4];
int FastTable_Valid=0;
typedef void (*LineRoutinePtr)(void);
LineRoutinePtr LineRoutine;
//static int NColsLookup[]={16, 4, 2, 0, 0, 16, 4, 2};
unsigned char VideoULA_ControlReg=0x9c;
int VideoULA_Palette[16]={0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};
unsigned char Flashmap[16]={0,1,2,3,4,5,6,7,7,6,5,4,3,2,1,0};
//int flash=0;
unsigned char CRTCControlReg=0;
unsigned char CRTC_HorizontalTotal=127;     /* R0 */
unsigned char CRTC_HorizontalDisplayed=80;  /* R1 */
unsigned char CRTC_HorizontalSyncPos=98;    /* R2 */
unsigned char CRTC_SyncWidth=0x28;          /* R3 */
unsigned char CRTC_VerticalTotal=38;        /* R4 */
unsigned char CRTC_VerticalTotalAdjust=0;   /* R5 */
unsigned char CRTC_VerticalDisplayed=32;    /* R6 */
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

int curtrack[2];             /*Current disc track*/
int cursec[2];
int sectorsleft;
int sectorlen;
int byteinsec;

int mode2;
#if 0
typedef struct
{
        int Addr;       /* Address of start of next visible character line in beeb memory  - raw */
        int PixmapLine; /* Current line in the pixmap */
        int PreviousFinalPixmapLine; /* The last pixmap line on the previous frame */
        int IsTeletext; /* This frame is a teletext frame - do things differently */
        unsigned char *DataPtr;  /* Pointer into host memory of video data */
        int CharLine;   /* 6845 counts in characters vertically - 0 at the top , incs by 1 - -1 means we are in the bit before the actual display starts */
        int InCharLine; /* Scanline within a character line - counts down*/
        int InCharLineUp; /* Scanline within a character line - counts up*/
        int VSyncState; // Cannot =0 in MSVC $NRM; /* When >0 VSync is high */
} VideoStateT;
VideoStateT VideoState;
#endif
//static unsigned char Mode7Font[3][96][9];
//static int Mode7DoubleHeightFlags[80];
int Mode7FlashTrigger=25;
int Video_RefreshFrequency=1;
int FrameNum;
int FastTable_valid;
int maxx,maxy,minx,miny;

//mode 7 - every 40 cycles
//mode 5 - every 4 cycles
//mode 2 - every 2 cycles

unsigned int offsets[]={0x4000,0x6000,0x3000,0x5800};

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
        for (d=0;d<CRTC_VerticalDisplayed<<3;d++)
        {
                tempaddr=(d&7)+((CRTC_HorizontalDisplayed<<3)*(d>>3))+VideoState.Addr;
//                VideoState.DataPtr=BeebMemPtrWithWrap(tempaddr,CRTC_HorizontalDisplayed<<3);
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }

                tempy=0;
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
//                        if (screencheck[tempaddr])
//                        {
//                                screencheck[tempaddr]=0;
                                temp=ram[tempaddr];
//                                temp=VideoState.DataPtr[tempy];//ram[tempaddr];
                                b->line[d][c<<2]=b->line[d][(c<<2)+1]=VideoULA_Palette[mode2table[temp][0]];
                                b->line[d][(c<<2)+2]=b->line[d][(c<<2)+3]=VideoULA_Palette[mode2table[temp][1]];
//                        }
                        tempy+=8;
                        tempaddr+=8;
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }
                }
        }
        if (!(IC32State&64))
           drawstring(b,font,80,256,"CAPS LOCK",1);
        if (!(IC32State&128))
           drawstring(b,font,160,256,"SHIFT LOCK",1);
        if (motor)
           drawstring(b,font,0,256,"CASSETTE",1);
/*        if (vol[0])
           drawstring(b,font,0,264,"Noise gen",64+(vol[0]>>2));
        if (vol[1])
           drawstring(b,font,80,264,"Tone gen 1",64+(vol[1]>>2));
        if (vol[2])
           drawstring(b,font,160,264,"Tone gen 2",64+(vol[2]>>2));
        if (vol[3])
           drawstring(b,font,240,264,"Tone gen 3",64+(vol[3]>>2));
        if (stretch)
           stretched_blit(b,screen,0,0,320,272,0,0,physx,physy);
        else*/
           blit(b,screen,0,0,(physx>>1)-160,((physy>>1)-136)+((signed char)CRTC_VerticalTotalAdjust&7),640,400);
//        clearall(b,0);
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
        int c,d,tempaddr2,tempaddr,curstart,curend,tempy;
        char temp,bits,col=0;
        char s[80];
        int x,y;
        unsigned char backupram[2];
        //clearall(b,0);
        #ifdef CRPCURSOR
        curaddr=(CRTC_CursorPosLow|(CRTC_CursorPosHigh<<8))<<3;
        if (modela)
           curaddr&=0x3FFF;
        backupram[0]=ram[(curaddr+7)];
        backupram[1]=ram[(curaddr+7)+8];
        curstart = CRTC_CursorStart & 0x1f;
        curend = CRTC_CursorEnd;

        ram[(curaddr+7)]=0xFF;
        ram[(curaddr+7)+8]=0xFF;
        #endif

        for (d=0;d<CRTC_VerticalDisplayed<<3;d++)
        {
                tempy=0;
                tempaddr=(d&7)+((CRTC_HorizontalDisplayed<<3)*(d>>3))+VideoState.Addr;
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }
                if (modela)
                   tempaddr&=0x3FFF;

                //VideoState.DataPtr=BeebMemPtrWithWrap(tempaddr,CRTC_HorizontalDisplayed<<3);
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
                        if (screencheck[tempaddr])
                        {
                        temp=ram[tempaddr];//VideoState.DataPtr[tempy];
                        tempy+=8;
                        if (!mode1) //Mode 5
                        {
                                GETMODE5PIXEL(0)
                                b->line[d][(c<<3)+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                b->line[d][(c<<3)+1+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(1)
                                b->line[d][(c<<3)+2+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                b->line[d][(c<<3)+3+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(2)
                                b->line[d][(c<<3)+4+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                b->line[d][(c<<3)+5+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(3)
                                b->line[d][(c<<3)+6+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                                b->line[d][(c<<3)+7+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        }
                        else //Mode 1
                        {
                                GETMODE5PIXEL(0)
                                b->line[d][(c<<2)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(1)
                                b->line[d][((c<<2)+1)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(2)
                                b->line[d][((c<<2)+2)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                                GETMODE5PIXEL(3)
                                b->line[d][((c<<2)+3)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                        }
                        }
                        tempaddr+=8;
                        if (tempaddr>0x7FFF)
                        {
                                tempaddr+=offsets[(IC32State & 0x30)>>4];
                                tempaddr&=0x7FFF;
                        }
                }
                tempaddr+=32;
        }
        #ifdef CRPCURSOR
        ram[(curaddr+7)]=backupram[0];
        ram[(curaddr+7)+8]=backupram[1];
        #endif
        if (!(IC32State&64))
           drawstring(b,font,80,256,"CAPS LOCK",1);
        if (!(IC32State&128))
           drawstring(b,font,160,256,"SHIFT LOCK",1);
        if (motor)
           drawstring(b,font,0,256,"CASSETTE",1);
/*        if (vol[0])
           drawstring(b,font,0,264,"Noise gen",64+(vol[0]>>2));
        if (vol[1])
           drawstring(b,font,80,264,"Tone gen 1",64+(vol[1]>>2));
        if (vol[2])
           drawstring(b,font,160,264,"Tone gen 2",64+(vol[2]>>2));
        if (vol[3])
           drawstring(b,font,240,264,"Tone gen 3",64+(vol[3]>>2));
        sprintf(s,"%i %i %i %i %i %i",curtrack[0],cursec[0],sectorsleft,byteinsec,sectorlen,bytesread);
        drawstring(b,font,0,272,s,1);
        if (stretch)
           stretched_blit(b,screen,0,0,320,272,0,0,physx,physy);
        else*/
           blit(b,screen,0,0,(physx>>1)-160,((physy>>1)-136)+(signed char)CRTC_VerticalTotalAdjust,640,400);
}

void Drawteletextscreen()
{
        #if 1
        int c,d,temp=0x7C00,tempaddr=VideoState.Addr,colour=7,tempy,graphics=0,dbl=0,addr;
        int lastcol=0;
        int backcol=0;
        int seperated=0;
        char s[80];
        #ifdef CURSOR
        int tempx;
        #endif
        flashfreq++;
        if (flashfreq==25)
        {
                flashfreq=0;
                flash^=1;
        }
        charmode(MASKED);
        tempaddr=tempaddr;
        clearall(b,0);
        addr=VideoState.Addr;
        if (modela)
           addr&0x3FFF;
        for (d=0;d<CRTC_VerticalDisplayed;d++)
        {
/*                if (modela)
                   VideoState.Addr&=0x3FFF;
                if (log)
                {
                        sprintf(s,"Video read %X\n",VideoState.Addr);
                        fputs(s,logfile);
                }
//                if (VideoState.Addr>0x7FFF)
//                   VideoState.Addr=0x7C00;
                VideoState.Addr=WrapAddrMo7(VideoState.Addr);
                VideoState.DataPtr=&ram[VideoState.Addr];//BeebMemPtrWithWrapMo7(VideoState.Addr,40);*/
//                VideoState.Addr+=40;
                colour=7;
                graphics=0;
                lastcol=7;
                backcol=0;
                seperated=0;
                if (dbl==2)
                   dbl=0;
                if (dbl==1)
                   dbl++;

                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
//                VideoState.Addr=WrapAddrMo7(VideoState.Addr);
//                VideoState.DataPtr=&ram[VideoState.Addr];//BeebMemPtrWithWrapMo7(VideoState.Addr,40);
//                        VideoState.Addr++;
                        if (ram[addr]&0x80) //Handle control codes
                        {
                                switch (ram[addr])
                                {
                                        case 129://Change colour - no graphics
                                        case 130:
                                        case 131:
                                        case 132:
                                        case 133:
                                        case 134:
                                        case 135:
                                        colour=lastcol=ram[addr]-128;
                                        graphics=0;
                                        break;
                                        case 145://Change colour - graphics
                                        case 146:
                                        case 147:
                                        case 148:
                                        case 149:
                                        case 150:
                                        case 151:
                                        colour=lastcol=ram[addr]-144;
                                        graphics=1;
                                        break;
                                        case 140: //Double height off
                                        dbl=0;
                                        break;
                                        case 141: //Double height on
                                        if (!dbl)
                                           dbl=1;
                                        break;
                                        case 153: //unseperated graphics
                                        seperated=0;
                                        break;
                                        case 154: //Seperated graphics
                                        seperated=1;
                                        break;
                                        case 156: //Black background
                                        backcol=0;
                                        drawbox(b,c<<3,d<<3,320,(d<<3)+7,0);
                                        break;
                                        case 157: //New background
                                        backcol=lastcol;
                                        drawbox(b,c<<3,d<<3,320,(d<<3)+7,backcol);
                                        break;
                                }
                        }
                        else //Normal character
                        {
                                if (!graphics || (ram[addr]&128))
                                { //Normal text
                                        if (ram[addr]>31)
                                        {
//                                                sprintf(st,"%c",VideoState.DataPtr[c]);
                                                if (backcol && dbl<2)
                                                {
                                                        charmode(MASKED);
                                                        drawbox(b,c<<3,d<<3,(c<<3)+8,(d<<3)+((dbl)?15:7),backcol);
                                                }
                                                if (dbl==2)
                                                   drawchar(b,dblhigh,c<<3,(d<<3)-8,ram[addr],colour);
                                                else if (dbl)
                                                   drawchar(b,dblhigh,c<<3,d<<3,ram[addr],colour);
                                                else
                                                   drawchar(b,font,c<<3,d<<3,ram[addr],colour);
                                        }
                                        else
                                        {
                                                if (backcol)
                                                   drawbox(b,c<<3,d<<3,(c<<3)+8,(d<<3)+8,backcol);
                                        }
                                }
                                else if (!dbl)
                                { //Teletext graphics
                                        if (ram[addr]>31)
                                        {
                                                tempy=ram[addr];
                                                clearall(tempb,backcol);
                                                if (!seperated)
                                                {
                                                        if (tempy&1)
                                                           drawbox(tempb,0,0,3,2,colour);
                                                        if (tempy&2)
                                                           drawbox(tempb,4,0,7,2,colour);
                                                        if (tempy&4)
                                                           drawbox(tempb,0,3,3,4,colour);
                                                        if (tempy&8)
                                                           drawbox(tempb,4,3,7,4,colour);
                                                        if (tempy&16)
                                                           drawbox(tempb,0,5,3,7,colour);
                                                        if (tempy&64)
                                                           drawbox(tempb,4,5,7,7,colour);
                                                }
                                                else
                                                {
                                                        if (tempy&1)
                                                           drawbox(tempb,0,0,2,2,colour);
                                                        if (tempy&2)
                                                           drawbox(tempb,4,0,6,2,colour);
                                                        if (tempy&4)
                                                           drawbox(tempb,0,4,2,6,colour);
                                                        if (tempy&8)
                                                           drawbox(tempb,4,4,6,6,colour);
                                                }
                                                blit(tempb,b,0,0,c<<3,d<<3,8,8);
                                        }
                                }
                        }
                        temp++;
                        addr++;
                        if (!modela)
                        {
                                if (addr==0x8000)
                                {
                                        addr=0x7C00;
                                }
                        }
                        else
                        {
                                if (addr==0x4000)
                                {
                                        addr=0x3C00;
                                }
                        }
                }
//                if (CRTC_HorizontalDisplayed>40)
//                   addr+=(CRTC_HorizontalDisplayed-40);
        }
        if (singlestep)
        {
        sprintf(st,"A=%X X=%X Y=%X PC=%X S=%X   ",a,x,y,pc,s);
        drawstring(b,font,0,300,st,7);
        sprintf(st,"C%i Z%i I%i D%i B%i V%i N%i   ",p&1,p&2,p&4,p&8,p&0x10,p&0x40,p&0x80);
        drawstring(b,font,0,316,st,7);
        sprintf(st,"Last mem write : at %X value %c [%X]",oldaddr,oldvalue,oldvalue);
        drawstring(b,font,0,332,st,7);
        sprintf(st,"Last mem read  : at %X value %c [%X]",oldraddr,oldrvalue,oldrvalue);
        drawstring(b,font,0,348,st,7);
        sprintf(st,"Last instruc   : %X [%i]            ",lastins,lastins);
        drawstring(b,font,0,364,st,7);

        if (NMILock)
           sprintf(st,"NMI");
        else
           sprintf(st,"   ");
        drawstring(b,font,0,380,st,7);

        if (p&4)
           sprintf(st,"interrupt");
        else
           sprintf(st,"         ");
        drawstring(b,font,32,380,st,7);
        }
        #ifdef CURSOR
        //Draw cursor
        if ((VideoULA_ControlReg & 0xe0) == 0 || (CRTC_CursorStart & 0x40) == 0)
                goto drawitnow;
        temp=CRTC_CursorPosLow+((((CRTC_CursorPosHigh ^ 0x20) + 0x74) & 0xff)<<8);
        temp-=tempaddr;
        tempx=temp%40;
        tempy=temp/40;
//        sprintf(st,"_");
        if (flash)
           drawchar(b,font,tempx<<3,tempy<<3,'_',15);
 //          drawstring(b,font,tempx<<3,tempy<<3,st,15);
        drawitnow:
        #endif
        if (!(IC32State&64))
           drawstring(b,font,80,200,"CAPS LOCK",1);
        if (!(IC32State&128))
           drawstring(b,font,160,200,"SHIFT LOCK",1);
        if (motor)
           drawstring(b,font,0,200,"CASSETTE",1);
        sprintf(s,"%i %i   ",tapedelay,lastbyte);
        drawstring(b,font,0,208,s,1);
/*        if (vol[0])
           drawstring(b,font,0,208,"Noise gen",64+(vol[0]>>2));
        if (vol[1])
           drawstring(b,font,80,208,"Tone gen 1",64+(vol[1]>>2));
        if (vol[2])
           drawstring(b,font,160,208,"Tone gen 2",64+(vol[2]>>2));
        if (vol[3])
           drawstring(b,font,240,208,"Tone gen 3",64+(vol[3]>>2));
        sprintf(s,"%i %i %i %i %i %i %i   %i %i",discside,curtrack[0],cursec[0],sectorsleft,byteinsec,sectorlen,bytesread,printertime,printtarget);
        drawstring(b,font,0,216,s,1);
        if (stretch)
           stretched_blit(b,screen,0,0,320,400,0,0,physx,physy);
        else*/
/*        int c,d,addr=videoaddr;
        for (d=0;d<25;d++)
        {
                for (c=0;c<25;c++)
                {
                        drawchar(b,font,c<<3,d<<3,ram[addr++],7);
                        if (addr>0x7FFF)
                           addr=0x7C00;
                }
        }*/
           blit(b,screen,0,0,(physx>>1)-160-((CRTC_HorizontalSyncPos-51)<<3),((physy>>1)-112)+(signed char)CRTC_VerticalTotalAdjust,640,400);
        #else
        int x,y,addr=0x7C00;
        for (y=0;y<25;y++)
        {
                for (x=0;x<40;x++)
                {
                        drawchar(b,font,x<<3,y<<3,ram[addr++],7);
                }
        }
        blit(b,screen,0,0,0,0,320,200);
        #endif
}

#define GETMODE0PIXEL(pixel) col=((temp>>(7-pixel))&0x1)*15;

void DrawMode0()
{
        unsigned short tempaddr;
        int c,d;
        unsigned char temp;
        unsigned char col;
        //clearall(b,0);
        for (d=0;d<CRTC_VerticalDisplayed<<3;d++)
        {
                tempaddr=(d&7)+((CRTC_HorizontalDisplayed*8)*(d/8))+VideoState.Addr;
                if (modela)
                   tempaddr&=0x3FFF;
                for (c=0;c<CRTC_HorizontalDisplayed;c++)
                {
                        temp=ram[tempaddr];
                        tempaddr+=8;
                        GETMODE0PIXEL(0)
                        putpixel(b,c<<3,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(1)
                        putpixel(b,(c<<3)+1,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(2)
                        putpixel(b,(c<<3)+2,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(3)
                        putpixel(b,(c<<3)+3,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(4)
                        putpixel(b,(c<<3)+4,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(5)
                        putpixel(b,(c<<3)+5,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(6)
                        putpixel(b,(c<<3)+6,d,VideoULA_Palette[(int)col]);
                        GETMODE0PIXEL(7)
                        putpixel(b,(c<<3)+7,d,VideoULA_Palette[(int)col]);
                }
        }
        if (!(IC32State&64))
           drawstring(b,font,80,256,"CAPS LOCK",1);
        if (!(IC32State&128))
           drawstring(b,font,160,256,"SHIFT LOCK",1);
        if (motor)
           drawstring(b,font,0,256,"CASSETTE",1);
/*        if (vol[0])
           drawstring(b,font,0,264,"Noise gen",64+(vol[0]>>2));
        if (vol[1])
           drawstring(b,font,80,264,"Tone gen 1",64+(vol[1]>>2));
        if (vol[2])
           drawstring(b,font,160,264,"Tone gen 2",64+(vol[2]>>2));
        if (vol[3])
           drawstring(b,font,240,264,"Tone gen 3",64+(vol[3]>>2));
        if (stretch)
           stretched_blit(b,screen,0,0,640,272,0,0,physx,physy);
        else*/
           blit(b,screen,0,0,(physx>>1)-160,((physy>>1)-136)+(signed char)CRTC_VerticalTotalAdjust,640,400);
}

void startframe()
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
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed<<3)*(line/8))+VideoState.Addr;
        VideoState.DataPtr=BeebMemPtrWithWrap(tempaddr,CRTC_HorizontalDisplayed<<3);
        tempy=0;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=VideoState.DataPtr[tempy];//ram[tempaddr];
                tempy+=8;
                left=mode2table[temp][0];
                right=mode2table[temp][1];
                b->line[line][c<<2]=b->line[line][(c<<2)+1]=VideoULA_Palette[left];
                b->line[line][(c<<2)+2]=b->line[line][(c<<2)+3]=VideoULA_Palette[right];
        }
}

void DrawMode5line(int line, int mode1)
{
        int tempy,tempaddr,c;
        unsigned char temp;
        unsigned char bits;
        unsigned char col;
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed<<3)*(line/8))+VideoState.Addr;
        if (modela)
           tempaddr&=0x3FFF;
        VideoState.DataPtr=BeebMemPtrWithWrap(tempaddr,CRTC_HorizontalDisplayed<<3);
        tempy=0;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=VideoState.DataPtr[tempy];
                tempy+=8;
                if (!mode1) //Mode 5
                {
                        GETMODE5PIXEL(0)
                        b->line[line][(c<<3)+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        b->line[line][(c<<3)+1+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(1)
                        b->line[line][(c<<3)+2+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        b->line[line][(c<<3)+3+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(2)
                        b->line[line][(c<<3)+4+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        b->line[line][(c<<3)+5+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(3)
                        b->line[line][(c<<3)+6+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                        b->line[line][(c<<3)+7+((40-CRTC_HorizontalDisplayed)<<2)]=VideoULA_Palette[(int)col];
                }
                else //Mode 1
                {
                        GETMODE5PIXEL(0)
                        b->line[line][(c<<2)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(1)
                        b->line[line][((c<<2)+1)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(2)
                        b->line[line][((c<<2)+2)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                        GETMODE5PIXEL(3)
                        b->line[line][((c<<2)+3)+((80-CRTC_HorizontalDisplayed)<<1)]=VideoULA_Palette[(int)col];
                }
        }
}

void DrawMode0line(int line)
{
        int c,tempaddr;
        unsigned char temp,col;
        tempaddr=(line&7)+((CRTC_HorizontalDisplayed*8)*(line/8))+VideoState.Addr;
        if (modela)
           tempaddr&=0x3FFF;
        for (c=0;c<CRTC_HorizontalDisplayed;c++)
        {
                temp=ram[tempaddr];
                tempaddr+=8;
                GETMODE0PIXEL(0)
                putpixel(b,c<<3,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(1)
                putpixel(b,(c<<3)+1,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(2)
                putpixel(b,(c<<3)+2,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(3)
                putpixel(b,(c<<3)+3,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(4)
                putpixel(b,(c<<3)+4,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(5)
                putpixel(b,(c<<3)+5,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(6)
                putpixel(b,(c<<3)+6,line,VideoULA_Palette[(int)col]);
                GETMODE0PIXEL(7)
                putpixel(b,(c<<3)+7,line,VideoULA_Palette[(int)col]);
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
                                case 0xD8: case 0xD9:
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
//                if (!scancount)

//                scancount=(videodelay - 1900 - magicnum)/64;
                scancount++;
                if (scancount>((us)?262:304))
                {
                        if (slowdown)
                           while (time<2);
                        if (VideoState.IsTeletext)
                           Drawteletextscreen();
                        else
                           blit(b,screen,0,0,/*((CRTC_HorizontalTotal-CRTC_HorizontalDisplayed)<<2)*/((physx>>1)-160),((physy>>1)-128)+(signed char)CRTC_VerticalTotalAdjust+(CRTC_VerticalSyncPos-34),640,400);
                        refresh++;
                        scancount=0;
//                        clearall(b,0);
                }
                if (scancount<CRTC_VerticalDisplayed<<3&&!VideoState.IsTeletext)
                {
                        switch(VideoULA_ControlReg&0xFE)
                        {
                                case 0xF4: case 0xF5:
                                DrawMode2line(scancount);
                                break;
                                case 0xC4: case 0xC5: case 0xE0: case 0x64:
                                DrawMode5line(scancount,0);
                                break;
                                case 0xD8: case 0xD9:
                                DrawMode5line(scancount,1);
                                break;
                                default:
                                DrawMode0line(scancount);
                                break;
                        }
                }
                videoline=(CRTC_HorizontalTotal)*((VideoULA_ControlReg & 16)?1:2);//(CRTC_HorizontalTotal+1)*((VideoULA_ControlReg & 16)?1:2);
//                videoline=magicnum+1900;
        }
}

void MakeMode2Table()
{
        int temp,left,right;
        for (temp=0;temp<256;temp++)
        {
                left=right=0;
                if (temp&1)
                   right|=1;
                if (temp&2)
                   left|=1;
                if (temp&4)
                   right|=2;
                if (temp&8)
                   left|=2;
                if (temp&16)
                   right|=4;
                if (temp&32)
                   left|=4;
                if (temp&64)
                   right|=8;
                if (temp&128)
                   left|=8;
                mode2table[temp][0]=left;
                mode2table[temp][1]=right;
        }
}

void MakeMode5Table()
{
        int temp,p[4],c,bits;
        for (temp=0;temp<256;temp++)
        {
                for (c=0;c<4;c++)
                {
                        bits = ( temp >> ( 3 - c )) & 0x11;
                        switch ( bits )
                        {
                                case 0x11 :
                                        p[c] = 10;
                                        break;
                                case 0x10 :
                                        p[c] = 8;
                                        break;
                                case 0x01 :
                                        p[c] = 2;
                                        break;
                                case 0x00 :
                                        p[c] = 0;
                                        break;
                        }
                }
                mode5table[temp][0]=p[0];
                mode5table[temp][1]=p[1];
                mode5table[temp][2]=p[2];
                mode5table[temp][3]=p[3];
        }
}

void CRTCWrite(unsigned int Address, unsigned char Value)
{
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
                        FastTable_valid=0;
                        clearall(b,0);
                        break;

                        case 2:
                        CRTC_HorizontalSyncPos=Value;
                        break;

                        case 3:
                        CRTC_SyncWidth=Value;
                        break;

                        case 4:
                        CRTC_VerticalTotal=Value;
                        break;

                        case 5:
                        CRTC_VerticalTotalAdjust=Value;
                        break;

                        case 6:
                        CRTC_VerticalDisplayed=Value;
                        break;

                        case 7:
                        CRTC_VerticalSyncPos=Value;
                        clearall(b,0);
                        magicnum=(35-Value)*(CRTC_ScanLinesPerChar+1)*64;
                        break;

                        case 8:
                        CRTC_InterlaceAndDelay=Value;
                        break;

                        case 9:
                        CRTC_ScanLinesPerChar=Value;
                        magicnum=(35-Value)*(CRTC_ScanLinesPerChar+1)*64;
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
                FastTable_Valid=0;
        }
        else
        {
                oldulareg=VideoULA_ControlReg;
                VideoULA_ControlReg=Value;
                FastTable_Valid=0;
                if (VideoULA_ControlReg&2)
                {
                        VideoState.IsTeletext=1;
                        videocycles=80;
                }
                else
                {
                        VideoState.IsTeletext=0;
                        if (VideoULA_ControlReg&0x10)
                           videocycles=8;
                        else
                           videocycles=4;
                }
                if ((VideoULA_ControlReg&0xFE)!=(oldulareg&0xFE))
                   clearall(screen,0);
                if ((VideoULA_ControlReg&0xFE)==0xF4)
                   mode2=1;
                else
                   mode2=0;
                memset(screencheck,1,32768);
                flash=1-(VideoULA_ControlReg&1);
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
                setpal(beebpal);
        }
}

int ULARead(int Address)
{
        return Address;
}

void maketingtable()
{
        int c,d;
        int tempaddr;
        for (d=0;d<256;d++)
        {
                tempaddr=(d&7)+(80*(d/8))+0x3000;
                for (c=0;c<80;c++)
                {
                        tingtable[tempaddr][0]=c;
                        tingtable[tempaddr][1]=d;
                        tempaddr+=8;
                }
        }
}

void initvideo()
{
        tempb=createbmp(8,8);
        b=createbmp(640,400);
        VideoState.DataPtr=BeebMemPtrWithWrapMo7(0x3000,640);
        mode2=0;
        maxx=400;
        maxy=300;
        minx=miny=0;
        scrlenindex=0;
        VideoState.PixmapLine=0;
        VideoState.PreviousFinalPixmapLine=255;
        st=malloc(80);
        dblhigh=makefont8x16();
        MakeMode2Table();
        MakeMode5Table();
        maketingtable();
        videoline=99;
}
