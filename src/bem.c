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

/*bem.c - Main loop*/

unsigned char CRTC_VerticalTotal;
int scanlinesframe;
unsigned char CRTC_ScanLinesPerChar;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <allegro.h>
#include <pc.h>
#include "6502.h"
#include "mem.h"
#include "vias.h"
#include "video.h"
#include "scan2bbc.h"
#include "disk.h"
#include "sound.h"
#include "adc.h"
#include "8271.h"
#include "serial.h"
#include "codes.h"
//#include "snd.h"

int ddnoise;
int oldczvn=0;
int frameskip,fskip;
int slowdown;
int us=0;
int TotalCycles;
int mono;

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
int gfxc[8]={GFX_MODEX,GFX_MODEX,GFX_MODEX,GFX_MODEX,GFX_VESA2L,GFX_VESA2L,GFX_VESA2L,GFX_VESA2L};

PALETTE beebpal;

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
        int c,cc;
        int row,col;
        for (c=0;c<128;c++)
        {
                if (key[c]!=keys2[c] && c!=KEY_F11)
                {
                        if (TranslateKey(codeconvert[c],&row,&col)>0)
                        {
                                if (key[c])
                                   presskey(row,col);
                                else
                                   releasekey(row,col);
                        }
                }
        }
        if (key[KEY_RSHIFT]||key[KEY_LSHIFT])
           presskey(0,0);
        else
           releasekey(0,0);
        if (key[KEY_LCONTROL]||key[KEY_RCONTROL])
           presskey(0,1);
        else
           releasekey(0,1);
        for (c=0;c<128;c++)
            keys2[c]=key[c];
//        memcpy(keys2,key,128);
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
        rungui();
}

int refresh;
int printon=0;

void readconfig()
{
        char temp[40],temp2[40];
        int c;
        FILE *cfg;
        printf("B-Em V0.4\nReading config file...");
        cfg=fopen("b-em.cfg","r");
        frameskip=fgetc(cfg);
        fskip=40000*frameskip;
        soundon=fgetc(cfg);
        mono=fgetc(cfg);
        scanlinedraw=fgetc(cfg);
        us=getc(cfg);
        modela=getc(cfg);
        ddnoise=getc(cfg);
        c=0;
        while (c<260)
        {
               discname[c]=fgetc(cfg);
               if (!discname[c])
                  break;
               c++;
        }
        fclose(cfg);
}

void writeconfig()
{
        FILE *cfg=fopen("b-em.cfg","wb");
        fputc(frameskip,cfg);
        fputc(soundon,cfg);
        fputc(mono,cfg);
        fputc(scanlinedraw,cfg);
        fputc(us,cfg);
        fputc(modela,cfg);
        fputc(ddnoise,cfg);
        fputs(discname,cfg);
        fputc(0,cfg);
        fclose(cfg);
}

int main(int argc, char *argv[])
{
        int c,viadelay;
        FILE *f;
        truecycles=1;
        readconfig();
        LOCK_VARIABLE(pcsecs);
        LOCK_FUNCTION(eachsec);
        printf("Loading *CAT list..");
        loadfilelist();
        initstuff();
        printf("\nIniting timer...");
        install_timer();
        printf("\nIniting keyboard...");
        install_keyboard();
        install_mouse();
        if (soundon)
        {
               printf("\nIniting BBC sound...");
               initsnd();
        }
        printf("Reading command line options...");
        diskenabled=1;
        memset(keys2,0,128);
        slowdown=1;
        for (c=1;c<argc;c++)
        {
                if (!strcmp(argv[c],"-modelb"))
                   modela=0;
                if (!strcmp(argv[c],"-us"))
                   us=1;
                if (!strcmp(argv[c],"-scanline"))
                   scanlinedraw=1;
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
                        printf("DIPS are %i\n",dips);
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
                if (!strcmp(argv[c],"-printer"))
                   printon=1;
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
        initadc();
        reset8271();
        printf("\nIniting 6502...");
        init6502();
        loaddiscsamps();
        printf("\nPress a key to go");
//        readkey();
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
        singlestep=0;
//        newmix=mix;
        install_int_ex(eachsec,MSEC_TO_TIMER(100));
        install_int_ex(timeit,MSEC_TO_TIMER(10));
        while (!quit)
        {
                do6502();
                checkkeys();
                if (key[KEY_F12]) /*F12 - reset*/
                {
                        if (key[KEY_LCONTROL]||key[KEY_RCONTROL])
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
                if (key[KEY_F11])
                   domenu();
        }
        writeconfig();
        if (soundfile)
           fclose(soundfile);
        if (printer)
           fclose(printer);
        return 0;
}
