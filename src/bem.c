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

/*bem.c - Main loop*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include <pc.h>
#include "6502.h"
#include "mem.h"
#include "vias.h"
#include "video.h"
#include "scan2bbc.h"
#include "disk.h"
#include "sound.h"
#include "serial.h"
#include "adc.h"
#include "8271.h"
int oldczvn=0;
int frameskip,fskip;
int slowdown;
int us=0;
#include "6502.c"

int firetrackhack=0;
int callbacks;
int printwrites;
int orbwrites;
int orareads;
FILE *printer;
FILE *tape;
int time=0;
int gfxmode;
int uefon;
char discname[260];
char uefname[260];
static int pcsecs=0;
int quit=0;
int ingui=0;
int tempx,tempy,curaddr;
void trapos();
char filename[40];
int dips=0;
int mix(unsigned long buffer, unsigned long length);
int buf[200];
int bufs=0,buff=0;
char keys2[256];

int card,xx,yy;
int gfxx[8]={320,320,320,320,400,640,640,800};
int gfxy[8]={200,240,300,400,300,400,480,600};
int gfxc[8]={MODEX,MODEX,MODEX,MODEX,VESA2L,VESA2L,VESA2L,VESA2L};

static void timeit()
{
        if (ingui==0)
           time++;
}

END_OF_FUNCTION(timeit);

static void eachsec()
{
        if (ingui==0)
           pcsecs++;
}

END_OF_FUNCTION(eachsec);

static inline void releasekey(int row, int col)
{
        if (bbckey[col][row] && row!=0)
           Keysdown--;
        bbckey[col][row]=0;
}

static inline int TranslateKey(int index, int *row, int *col)
{
        unsigned int    vkey=scan2bbc[index & 127];

   if (vkey==0xaa) return(-1);

        col[0] = vkey&15;
        row[0] = (vkey>>4)&15;

        return(row[0]);
}

static inline void checkkeys()
{
        int c;
        int row,col;
        for (c=0;c<128;c++)
        {
                if (keys[c]!=keys2[c])
                {
                        if (TranslateKey(c,&row,&col)>0)
                        {
                                if (keys[c])
                                   presskey(row,col);
                                else
                                   releasekey(row,col);
                        }
                }
        }
        if (keys[KEY_RSHIFT]||keys[KEY_LSHIFT])
           presskey(0,0);
        else
           releasekey(0,0);
        if (keys[KEY_CTRL])
           presskey(0,1);
        else
           releasekey(0,1);
        memcpy(keys2,keys,128);
}

void initDIPS()
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

void domenu()
{
        int temp;
        int c;
        FILE *ff;
        ingui=1;
        closegfx();
        printf("card=%i xx=%i yy=%i\n",card,xx,yy);
        printf("A=%X X=%X Y=%X S=%X P=%X PC=%X\n",a,x,y,s,p,pc);
        printf("C%i Z%i I%i D%i B%i V%i N%i\n",p&1,(p&2)>>1,(p&4)>>2,(p&8)>>3,(p&0x10)>>4,(p&0x40)>>6,(p&0x80)>>7);
        printf("CRTC Horizontal Total=%X [%i]\n",CRTC_HorizontalTotal,CRTC_HorizontalTotal);
        printf("CRTC Horizontal Displayed=%X [%i]\n",CRTC_HorizontalDisplayed,CRTC_HorizontalDisplayed);
        printf("CRTC Horizontal SyncPos=%X [%i]\n",CRTC_HorizontalSyncPos,CRTC_HorizontalSyncPos);
        printf("CRTC Sync Width=%X [%i]\n",CRTC_SyncWidth,CRTC_SyncWidth);
        printf("CRTC Vertical Total=%X [%i]\n",CRTC_VerticalTotal,CRTC_VerticalTotal);
        printf("CRTC Vertical Total Adjust=%X [%i]\n",CRTC_VerticalTotalAdjust,CRTC_VerticalTotalAdjust);
        printf("CRTC Vertical Displayed=%X [%i]\n",CRTC_VerticalDisplayed,CRTC_VerticalDisplayed);
        printf("CRTC Vertical SyncPos=%X [%i]\n",CRTC_VerticalSyncPos,CRTC_VerticalSyncPos);
        printf("CRTC Interlace And Delay=%X [%i]\n",CRTC_InterlaceAndDelay,CRTC_InterlaceAndDelay);
        printf("CRTC Scanlines Per Char=%X [%i]\n",CRTC_ScanLinesPerChar,CRTC_ScanLinesPerChar);
        printf("CRTC Cursor Start=%X [%i]\n",CRTC_CursorStart,CRTC_CursorStart);
        printf("CRTC Cursor End=%X [%i]\n",CRTC_CursorEnd,CRTC_CursorEnd);
        printf("CRTC Screen Start High=%X [%i]\n",CRTC_ScreenStartHigh,CRTC_ScreenStartHigh);
        printf("CRTC Screen Start Low=%X [%i]\n",CRTC_ScreenStartLow,CRTC_ScreenStartLow);
        printf("CRTC Cursor Pos High=%X [%i]\n",CRTC_CursorPosHigh,CRTC_CursorPosHigh);
        printf("CRTC Cursor Pos Low=%X [%i]\n",CRTC_CursorPosLow,CRTC_CursorPosLow);
        printf("CRTC Lightpen High=%X [%i]\n",CRTC_LightPenHigh,CRTC_LightPenHigh);
        printf("CRTC Lightpen Low=%X [%i]\n",CRTC_LightPenLow,CRTC_LightPenLow);
        printf("ULA=%X [%i]  ",VideoULA_ControlReg,VideoULA_ControlReg);
        printf("Sys VIA IC32=%X User VIA IC32=%X Scroll=%X\n",IC32State,UIC32State,IC32State&0x30);
        printf("%i %i %X %X\n\n",tempx,tempy,videoaddr,curaddr);
        printf("Press Q to quit, or any other key to return to the emulator");
        clearbuf();
        temp=waitkey()>>8;
        if (temp=='Q'||temp=='q')
           quit=1;
        if (temp=='d'||temp=='D')
        {
                ff=fopen("ram.dmp","wb");
                for (c=0;c<32768;c++)
                    putc(ram[c],ff);
                fclose(ff);
        }
        if (temp=='a'||temp=='A')
        {
                log=1;
                logfile=fopen("log.log","wt");
        }
        if (temp=='s'||temp=='S')
           savesnapshot();
        if (temp=='l'||temp=='L')
           loadsnapshot();
        if (temp=='r'||temp=='R')
           savepcx("scrshot.pcx",b,beebpal);
        if (temp=='c'||temp=='C')
        {
                closekey();
                printf("Please type disc name : ");
                scanf("%s",discname);
                if (chdir("discs"))
                {
                        perror("discs");
                        exit(-1);
                }
                if (discname[c]=='d'||discname[c]=='D')
                   load8271dsd(discname,0);
                else
                   load8271ssd(discname,0);
                if (chdir(".."))
                {
                        perror("..");
                        exit(-1);
                }
                initkey();
        }
        initgfx(card,xx,yy,0,0);
        setpal(beebpal);
        ingui=0;
}

int refresh;
int printon=0;

void readconfig()
{
        char temp[40],temp2[40];
        FILE *cfg;
        printf("B-Em V0.3\nReading config file...");
        cfg=fopen("b-em.cfg","r");
        fgets(temp,40,cfg);
        sscanf(temp,"FRAMESKIP=%s",temp2);
        frameskip=strtol(temp2,NULL,0);
        fgets(temp,40,cfg);
        sscanf(temp,"MODE=%s",temp2);
        gfxmode=strtol(temp2,NULL,0);
        fgets(temp,40,cfg);
        sscanf(temp,"%s",discname);
        fgets(temp,40,cfg);
        sscanf(temp,"%s",uefname);
        fgets(temp,40,cfg);
        sscanf(temp,"%i",&soundon);
        fgets(temp,40,cfg);
        sscanf(temp,"%i",&scanlinedraw);
        fgets(temp,40,cfg);
        sscanf(temp,"%i",&uefon);
        fclose(cfg);
        card=gfxc[gfxmode&7];
        xx=gfxx[gfxmode&7];
        yy=gfxy[gfxmode&7];
        printf("\nGraphics mode : %ix%i",xx,yy);
        printf("%i\n",soundon);
}

int main(int argc, char *argv[])
{
        int c,frameskip,viadelay;
        modela=0;
        truecycles=1;
        logsound=0;
        readconfig();
        LOCK_VARIABLE(pcsecs);
        LOCK_FUNCTION(eachsec);
        printf("Loading *CAT list..");
        loadfilelist();
        tomgfxinit();
        printf("\nIniting timer...");
        inittimer();
        printf("\nIniting keyboard...");
        initkey();
        printf("\nIniting BBC sound...");
        initsnd();
        printf("Reading command line options...");
        diskenabled=1;
        memset(keys2,0,128);
        frameskip=fskip=1;
        for (c=1;c<argc;c++)
        {
                if (!strcmp(argv[c],"-modela"))
                   modela=1;
                if (!strcmp(argv[c],"-modelb"))
                   modela=0;
                if (!strcmp(argv[c],"-us"))
                   us=1;
                if (!strcmp(argv[c],"-scanline"))
                   scanlinedraw=1;
                if (!strcmp(argv[c],"-logsound"))
                   logsound=1;
                if (!strcmp(argv[c],"-sound"))
                   soundon=1;
                if (!strcmp(argv[c],"-notfs"))
                   diskenabled=0;
                if (!strcmp(argv[c],"-dips"))
                {
                        c++;
                        if (c==argc)
                           break;
                        dips=strtol(argv[c],NULL,0);
                }
                if (!strcmp(argv[c],"-frameskip"))
                {
                        c++;
                        if (c==argc)
                           break;
                        frameskip=strtol(argv[c],NULL,0);
                        fskip=40000*frameskip;
                }
                if (!strcmp(argv[c],"-disc"))
                {
                        c++;
                        if (c==argc)
                           break;
                        strcpy(discname,argv[c]);
                }
                if (!strcmp(argv[c],"-uef"))
                {
                        c++;
                        if (c==argc)
                           break;
                        strcpy(uefname,argv[c]);
                        uefon=1;
                }
                if (!strcmp(argv[c],"-printer"))
                   printon=1;
                if (!strcmp(argv[c],"-firetrack"))
                   firetrackhack=1;
                if (!strcmp(argv[c],"-fast"))
                   slowdown=0;
        }
        printf("\n");
        if (modela)
           printf("Emulating BBC Model A\n");
        else
           printf("Emulating BBC Model B\n");
        if (us)
           printf("Emulating a US BBC\n");
        else
           printf("Emulating a UK BBC\n");
        if (!truecycles)
           printf("Fixed 6502 cycles\n");
        else
           printf("Accurate 6502 cycles\n");
        if (!scanlinedraw)
           printf("Scanline drawing disabled\n");
        else
           printf("Scanline drawing enabled\n");
        if (logsound)
        {
                soundfile=fopen("sound.pcm","wb");
                printf("Logging sound to sound.pcm\n");
        }
        if (soundon)
        {
                printf("\nIniting sound...");
                initsound(SB,0);
        }
        if (printon)
           printer=fopen("printer.txt","w");
        printf("\nIniting BBC RAM...");
        initmem();
        printf("\nTrapping OS...");
        trapos();
        log=0;
        loaded=0;
        stretch=0;
        hadcli=0;
        initserial();
        initadc();
        reset8271();
        resetacai();
        printf("\nIniting 6502...");
        init6502();
        if (uefon)
           inituef(uefname);
        printf("\nPress a key to go");
        waitkey();
        initvideo();
        UVIAReset();
        SVIAReset();
        reset8271();
        initDIPS();
        for (c=0;c<64;c++)
        {
                beebpal[64+c].r=c;
                beebpal[64+c].g=beebpal[64+c].b=0;
        }
        if (initgfx(card,xx,yy,0,0))
        {
                printf("Video error : %s\n",tomgfxerror);
                return -1;
        }
        setpal(beebpal);
        singlestep=0;
        newmix=mix;
        installint(eachsec,MSEC_TO_TIMER(100));
        installint(timeit,MSEC_TO_TIMER(10));
        while (!quit)
        {
                do6502();
                checkkeys();
                if (keys[KEY_F12]) /*F12 - reset*/
                {
                        if (keys[KEY_CTRL])
                        {
                                /*Hard reset - reset almost everything*/
                                for (c=0;c<32768;c++)
                                    ram[c]=0;
                                init6502();
                                UVIAReset();
                                SVIAReset();
                                reset8271();
                        }
                        else
                        {
                                /*Soft reset - reinitialise the CPU*/
                                init6502();
                                reset8271();
                        }
                }
                if (keys[KEY_F11])
                   domenu();
        }
        if (soundfile)
           fclose(soundfile);
        if (printer)
           fclose(printer);
        closegfx();
        printf("%i 6502 cycles - %i BBC seconds\n%i PC seconds\n",TotalCycles,TotalCycles/2000000,pcsecs/10);
        printf("%i screen refreshes\n",refresh);
        return 0;
}
