/*B-em 0.6 by Tom Walker*/
/*8271 FDC emulation*/

#include <stdio.h>
#include <allegro.h>
#include "8271.h"

#define BYTETIME 160

#define SIDES 2
#define TRACKS 80
#define SECTORS 16
#define SECTORSIZE 256

#define SIDE ((drvctrloutp>>5) & 1)

int nmiwait=0;
int output;
int nmi;
int driveled=0;
char discname[2][260]={"test.ssd","uridium.ssd"};
int discside=0;
int curdisc=0;
int dsd=0;
unsigned char statusreg=0;
unsigned char datareg=0;
unsigned char resultreg=0;
unsigned char error=0;
unsigned char discs[2][SIDES][SECTORS*TRACKS][SECTORSIZE];

int ddnoise=1;
SAMPLE *seeksmp;
SAMPLE *stepsmp;
SAMPLE *motorsmp;
void (*docommand)();
void (*doint)();

void loaddiscsamps()
{
        seeksmp=load_wav("seek.wav");
        stepsmp=load_wav("step.wav");
        motorsmp=load_wav("motor.wav");
}

void set8271poll(int time)
{
        discint=time;
//        printf("Poll time now %i\n",discint);
        disctime=0;
}

void error8271(unsigned char err)
{
        //printf("error %02X\n",err);
        if (!nmi)
        {
                error=err;
                statusreg=0x80;
                set8271poll(50);
        }
}

void motoroff()
{
//        printf("Motor off\n");
        driveled=0;
        stop_sample(motorsmp);
}

void setspindown()
{
//        printf("Set spindown\n");
        set8271poll(2000000);
        doint=motoroff;
}

int load8271ssd(char *fn, int disc)
{
        FILE *ff=fopen(fn,"rb");
        int c,d,e,f;
        int eof=0,temp;
        if (!ff)
           return -1;
                for (e=0;e<TRACKS;e++)
                {
                        for (d=0;d<10;d++)
                        {
                                for (c=0;c<SECTORSIZE;c++)
                                {
                                        if (!eof)
                                        {
                                                temp=getc(ff);
                                                if (temp==EOF)
                                                {
                                                        discs[disc][0][(e*16)+d][c]=0;
                                                        discs[disc][1][(e*16)+d][c]=0;
                                                        eof=1;
                                                }
                                                else
                                                {
                                                        discs[disc][0][(e*16)+d][c]=temp;
                                                        discs[disc][1][(e*16)+d][c]=0;
                                                }
                                        }
                                        else
                                        {
                                                discs[disc][0][(e*16)+d][c]=0;
                                                discs[disc][1][(e*16)+d][c]=0;
                                        }
                                }
                        }
                }
        fclose(ff);
        dsd=0;
        return 0;
}

int load8271dsd(char *fn, int disc)
{
        FILE *ff=fopen(fn,"rb");
        int c,d,e,f;
        int eof=0,temp;
        if (!ff)
           return -1;
//        for (f=0;f<SIDES;f++)
//        {
//                for (e=0;e<TRACKS;e++)
//                {
//                        for (f=0;f<SIDES;f++)
//                        {
                        for (d=0;d<TRACKS;d++)
                        {
                                for (f=0;f<2;f++)
                                {
                                for (e=0;e<10;e++)
                                {
                                for (c=0;c<SECTORSIZE;c++)
                                {
                                        if (!eof)
                                        {
                                                temp=getc(ff);
                                                if (temp==EOF)
                                                {
                                                        discs[disc][f][(d*16)+e][c]=0;
                                                        eof=1;
                                                }
                                                else
                                                   discs[disc][f][(d*16)+e][c]=temp;
                                        }
                                        else
                                           discs[disc][f][(d*16)+e][c]=0;
                                }
                                }
                                }
                        }
//                        }
//                }
//        }
        fclose(ff);
        dsd=1;
        return 0;
}

void empty8271disc(int disc)
{
        int c,d,e,f;
        for (f=0;f<SIDES;f++)
        {
//                for (e=0;e<TRACKS;e++)
//                {
                        for (d=0;d<SECTORS*TRACKS;d++)
                        {
                                for (c=0;c<SECTORSIZE;c++)
                                {
                                        discs[disc][f][d][c]=0;
                                }
                        }
//                }
        }
}

void dumpdisc()
{
        FILE *f=fopen("disc.dmp","wb");
        fwrite(discs[0],80*16*256,1,f);
        fclose(f);
}

void reset8271(int reload)
{
        int c;
        statusreg=0;
        resultreg=0;
        disctime=0;
        discint=0;
        if (reload)
        {
        //printf("Disc name %s\n",discname);
        for (c=0;c<strlen(discname[0]);c++)
        {
                if (discname[0][c]=='.')
                {
                        c++;
                        break;
                }
        }
        if (c==strlen(discname[0]))
        {
                empty8271disc(0);
                return;
        }
        if (discname[0][c]=='d'||discname[0][c]=='D')
           load8271dsd(discname[0],0);
        else
           load8271ssd(discname[0],0);

        for (c=0;c<strlen(discname[1]);c++)
        {
                if (discname[1][c]=='.')
                {
                        c++;
                        break;
                }
        }
        if (c==strlen(discname[1]))
        {
                empty8271disc(1);
                return;
        }
        if (discname[1][c]=='d'||discname[1][c]=='D')
           load8271dsd(discname[1],1);
        else
           load8271ssd(discname[1],1);
//        atexit(dumpdisc);
        }
        motoroff();
}

char str[40];

static inline void NMI()
{
//        //printf("NMI %i\n",statusreg&8);
        if (statusreg & 8)
        {
                nmi=1;
                nmiwait=8;
        }
        else
           nmi=0;
}

int presentparam;
int params;
int command;

typedef struct FDCCOMMAND
{
        int command;
        int params;
        int mask;
        void (*proc)();
        void (*intproc)();
} FDCCOMMAND;

unsigned char drvctrloutp;   /*Drive control output port*/
unsigned char modereg;       /*Mode register*/
unsigned char parameters[8]; /*Should be more than enough*/
int selects[2];              /*Drive selection*/
int curtrack[2];             /*Current disc track*/
int cursec[2];
int sectorsleft;
int sectorlen;
int byteinsec;
int curr8271drv;

void doselects()
{
        selects[0]=command&0x40;
        selects[1]=command&0x80;
        drvctrloutp&=0x3F;
        drvctrloutp|=selects[0]|selects[1];
}

void specify()
{
        /*Should do something here?*/
}

void readdrvstatus()
{
        resultreg=0x80;
        if (selects[0])
           resultreg|=0x4;
        if (selects[1])
           resultreg|=0x40;
        resultreg|=8;
        if (curtrack[0]==0)
           resultreg|=2;
        //printf("Read drive status %02X\n",resultreg);
        statusreg|=0x10;
}

void readspecial()
{
        unsigned char retval=-1;
        doselects();
        switch(parameters[0])
        {
                case 0x23:
                retval=drvctrloutp;
                break;
                default:
                set_gfx_mode(GFX_TEXT,0,0,0,0);
                //printf("Unimplemented 8271 read special register - param %X\n",parameters[0]);
                exit(-1);
                break;
        }
        resultreg=retval;
        statusreg|=0x10;
        //printf("RSR NMI\n");
        NMI();
//        closegfx();
//        //printf("Unimplemented 8271 read special register - param %X\n",parameters[0]);
//        exit(-1);
}

void writespecial()
{
        doselects();
        //printf("Write special %02X %02X\n",parameters[0],parameters[1]);
        switch(parameters[0])
        {
                case 0x17:
                modereg=parameters[1];
                break;
                case 0x23:
                drvctrloutp=parameters[1];
                selects[0]=parameters[1]&0x40;
                selects[1]=parameters[1]&0x80;
                break;
                default:
                set_gfx_mode(GFX_TEXT,0,0,0,0);
                //printf("Unimplemented 8271 write special register - params %X %X\n",parameters[0],parameters[1]);
                exit(-1);
                break;
        }
}

void seek()
{
        int drv=-1;
        char s[40];
        if (!driveled)
        {
                driveled=1;
                if (ddnoise)
                   play_sample(motorsmp,255,127,1000,TRUE);
        }
        doselects();
        if (selects[0])
           drv=0;
        if (selects[1])
           drv=1;
        if (drv==-1)
        {
                error8271(0x10);
                return;
        }
        curtrack[0]=parameters[0];
        statusreg=0x80;
        //printf("Seek NMI\n");
        NMI();
        set8271poll(100);
        if (ddnoise)
           play_sample(seeksmp,255,127,1000,0);
}

void seekint()
{
        char s[40];
        statusreg=0x18;
        //printf("Seek NMI 2\n");
        NMI();
        resultreg=0;
        setspindown();
}

int bytesread=0;

void readvarlen()
{
        char s[40];
        int drv=-1;
        if (!driveled)
        {
                if (ddnoise)
                   play_sample(motorsmp,255,127,1000,TRUE);
                driveled=1;
        }
        doselects();
        if (selects[0])
           drv=0;
        if (selects[1])
           drv=1;
        if (drv==-1)
        {
                error8271(0x10);
                return;
        }
        curr8271drv=drv;
        if (curtrack[0]!=parameters[0] && ddnoise)
           play_sample(stepsmp,255,127,1000,0);
        curtrack[0]=parameters[0];
        cursec[0]=parameters[1];
        sectorsleft=parameters[2]&0x1F;
        //printf("Reading track %i sector %i num of sectors %i\n",curtrack[0],cursec[0],sectorsleft);
        sectorlen=1<<(7+((parameters[2]>>5)&7));
        byteinsec=0;
        statusreg=0x80;
        //printf("Read NMI\n");
        NMI();
        set8271poll(BYTETIME);
        bytesread=0;
}

void readdoneint()
{
//        printf("Read done\n");
        setspindown();
        statusreg=0x18;
        //printf("Read NMI 3\n");
        NMI();
}

void readint()
{
        int done=0;
        char s[40];
//        output=1;
        if (sectorsleft==-1)
        {
                set8271poll(20000);
                doint=readdoneint;
                return;
        }
        bytesread++;
        if (cursec[0]<(800))
           datareg=discs[curdisc][SIDE][cursec[0]+(curtrack[0]*16)][byteinsec];
        else
        {
                error8271(0x1E);
                return;
        }
        //printf("Data %02X ",datareg);
        resultreg=0;
        byteinsec++;
        if (byteinsec>=256)
        {
                byteinsec=0;
                sectorsleft--;
                if (sectorsleft)
                {
                        cursec[0]++;
                        if (cursec[0]==10)
                        {
                                done=1;
                                sectorsleft=-1;
                                cursec[0]=0;
//                                curtrack[0]++;
//                                play_sample(stepsmp,255,127,1000,0);
                        }
                }
                else
                {
                        done=1;
//                        play_sample(stepsmp,255,127,1000,0);
                        sectorsleft=-1;
                }
        }

        statusreg=0x8C;
        if (done)
           statusreg=0x9C;
        //printf("Read NMI 2 %i %i\n",sectorsleft,byteinsec);
        NMI();
        set8271poll(BYTETIME);
//        output=1;
}

#define COMMS 6
FDCCOMMAND fdccomms[COMMS+1] =
{
        {0x2C,0,0x3F,readdrvstatus,NULL},
        {0x3A,2,0x3F,writespecial,NULL},
        {0x3D,1,0x3F,readspecial,NULL},
        {0x35,4,0xFF,specify,NULL},
        {0x29,1,0x3F,seek,seekint},
        {0x13,3,0x3F,readvarlen,readint},
        {0xFF,0,0x00,NULL,NULL}
};

FDCCOMMAND getcommand(unsigned char comm)
{
        int c;
        for (c=0;c<COMMS;c++)
        {
                if ((comm&fdccomms[c].mask)==fdccomms[c].command)
                {
                        return fdccomms[c];
                }
        }
        return fdccomms[COMMS];
}

void commandwrite(unsigned char val)
{
        FDCCOMMAND comm;
        presentparam=0;
        statusreg|=0x90;
        //printf("Command write NMI\n");
        NMI();
        if (val&0x40) curdisc=0;
        if (val&0x80) curdisc=1;
        comm=getcommand(val);
        if (!comm.proc)
        {
                docommand=NULL;
                doint=NULL;
                error8271(0x10);
//                closegfx();
//                //printf("Unrecognized 8271 command %X\n",val);
//                exit(-1);
//                return;

        }
//        if (val==0x53) output=1;
        params=comm.params;
        docommand=comm.proc;
        doint=comm.intproc;
        //printf("Command is %02X params %i %i\n",val,params,comm.params);
        command=val;
        if (!params)
        {
                statusreg&=0x7E;
                NMI();
                docommand();
        }
}

void paramwrite(unsigned char val)
{
        if (presentparam<params)
        {
                parameters[presentparam]=val;
                presentparam++;
                statusreg&=0xFE;
                NMI();
                if (presentparam==params)
                {
                        statusreg&=0x7E;
                        NMI();
                        docommand();
                }
        }
}

unsigned char read8271(unsigned short addr)
{
        char s[40];
        unsigned char val;
        switch (addr&7)
        {
                case 0:
                val=statusreg;
/*                if (nmiwait)
                {
                        //printf("nmiwait %i\n",nmiwait);
                        nmiwait--;
                        if (nmiwait<4) val&=~0x80;
                }*/
                break;
                case 1:
                statusreg&=~18;
                NMI();
                val=resultreg;
                resultreg=0;
                break;
                case 4:
                statusreg&=~0xC;
                NMI();
                val=datareg;
                break;
                default:
                val=0;
                break;
        }
        return val;
}

void write8271(unsigned short addr, unsigned char val)
{
        char s[40];
        //printf("Writing %04X %02X\n",addr,val);
        switch(addr&7)
        {
                case 0:
                commandwrite(val);
                break;
                case 1:
                paramwrite(val);
                break;
                case 2:
                reset8271(0);
                break;
                case 4:
                statusreg&=~0xC;
                NMI();
                datareg=val;
                break;
        }
}

void poll8271()
{
        char s[40];
//        printf("Poll\n");
        if (doint==motoroff)
        {
                motoroff();
                discint=0;
                return;
        }
        statusreg|=8;
        NMI();
        discint=0;
        disctime=0;
        if (error)
        {
                resultreg=error;
                statusreg=0x18;
                error=0;
                NMI();
                driveled=0;
        }
        else
        {
                if (doint)
                   doint();
        }
}
