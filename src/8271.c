/*B-em 1.4 by Tom Walker*/
/*8271 FDC emulation*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"
#include "8271.h"
#include "fditoraw.h"

#define BYTETIME 200

#define SIDES 2
#define TRACKS 80
#define SECTORS 16
#define SECTORSIZE 256

#define SIDE ((drvctrloutp>>5) & 1)

int delayreadop=0;
int idmarks=0;
int waitforindex=0;
int trackskips;
int readflash;
int inreadop=0,discrawpos=0;
int discaltered[2];
int soundiniteded=0;
int adfs[2];
int sides[2];
int nmiwait=0;
int output;
int nmi;
int driveled=0;
int discside=0;
int curdisc=0;
int dsd=0;
unsigned char statusreg=0;
unsigned char datareg=0;
unsigned char resultreg=0;
unsigned char error=0;
unsigned char discs[2][SIDES][SECTORS*TRACKS][SECTORSIZE];
unsigned char discraw[2][2][80][0x10000];
unsigned char discrawdd[2][2][80][0x20000];
int disctracklen[2][2][80];
int disctracklendd[2][2][80];
int discmaxtracks[2];
int discindexes[2][2][80];
unsigned char discid[6];
unsigned short discbuffer=0;
int byteswritten=0;
int fdiin[2];

int firstwriteint=0;
int ddnoise=1;
void (*docommand)();
void (*doint)();

void loaddiscsamps()
{
        seeksmp=load_wav("seek.wav");
        seek2smp=load_wav("seek2.wav");
        seek3smp=load_wav("seek3.wav");
        stepsmp=load_wav("step.wav");
        motorsmp=load_wav("motor.wav");
        motoroffsmp=load_wav("motoroff.wav");
        motoronsmp=load_wav("motoron.wav");
        if (!seeksmp) { printf("no seeksmp"); exit(-1); }
        if (!seek2smp) { printf("no seek2"); exit(-1); }
        if (!seek3smp) { printf("no seek3"); exit(-1); }
        if (!stepsmp) { printf("no step"); exit(-1); }
        if (!motorsmp) { printf("no motorsmp"); exit(-1); }
        if (!motoronsmp) { printf("no motoronsmp"); exit(-1); }
        if (!motoroffsmp) { printf("no motoroffsmp"); exit(-1); }
}

void set8271poll(int time)
{
        discint=time;
//        printf("Poll time now %i\n",discint);
        disctime=0;
}

void error8271(unsigned char err)
{
//        rpclog("error %02X\n",err);
        if (!nmi)
        {
                error=err;
                statusreg=0x80;
                set8271poll(50);
        }
}

void motoroff()
{
        if (!driveled) return;
//        printf("Motor off %i\n",soundiniteded);
        driveled=0;
        stop_sample(motorsmp);
        play_sample(motoroffsmp,96,127,1000,0);
//        if (soundiniteded) play_sample(motoroffsmp,255,127,1000,0);
}

void setspindown()
{
//        printf("Set spindown\n");
        if (ddnoise)
        {
                set8271poll(1000000);
                doint=motoroff;
        }
}

int load8271ssd(char *fn, int disc)
{
        FILE *ff=fopen(fn,"rb");
        int c,d,e,f;
        int eof=0,temp;
        if (!ff)
           return -1;
        sides[disc]=1;
        adfs[disc]=0;
        fdiin[disc]=0;
        discmaxtracks[disc]=80;
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
        sides[disc]=2;
        adfs[disc]=0;
        fdiin[disc]=0;
        discmaxtracks[disc]=80;
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

FDI *fdi;

unsigned char decodefm(unsigned short dat)
{
        unsigned char temp;
        temp=0;
        if (dat&0x0001) temp|=1;
        if (dat&0x0004) temp|=2;
        if (dat&0x0010) temp|=4;
        if (dat&0x0040) temp|=8;
        if (dat&0x0100) temp|=16;
        if (dat&0x0400) temp|=32;
        if (dat&0x1000) temp|=64;
        if (dat&0x4000) temp|=128;
        return temp;
}

#if FDIEN
int load8271fdi(char *fn, int disc)
{
        unsigned char fdihead[512],trackhead[16],sectorid[4];
        char id[]="Formatted Disk Image file  ";
        int c,d,e;
        int lasttrack;
        unsigned short mfmbuf[0x10000];
        unsigned short tracktiming[0x10000];
        unsigned char *mfmbufb;
        int tracklen,tempi;
        int indexoffset,mr;
        unsigned char temp;
        unsigned short databuffer;
        int idleft,idbitsleft;
        int datleft,datbitsleft;
        FILE *f=fopen(fn,"rb");
        fdiin[disc]=1;
        fdi=fditoraw_open(f);
        lasttrack=fdi2raw_get_last_track(fdi);
//        printf("%i tracks\n",lasttrack);
//        exit(-1);
        for (d=0;d<lasttrack;d++)
        {
                e=fdi2raw_loadtrack(fdi,mfmbuf,tracktiming,d,&tracklen,&indexoffset,&mr,0);
//                printf("Track %i : outlen %i tracklen %i\n",d,e,tracklen);
                if (!e) /*Blank track - as found on Acornsoft protection*/
                   memset(mfmbuf,0,0x20000);
                mfmbufb=(unsigned char *)mfmbuf;
                for (c=0;c<tracklen;c++)
                    discraw[disc][0][d][c]=mfmbufb[c^1];
                disctracklen[disc][0][d]=tracklen;
                discindexes[disc][0][d]=indexoffset;
                e=fdi2raw_loadtrack(fdi,mfmbuf,tracktiming,d,&tracklen,&indexoffset,&mr,1);
//                printf("Track %i : outlen %i tracklen %i MFM\n",d,e,tracklen);
                if (!e) /*Blank track - as found on Acornsoft protection*/
                   memset(mfmbuf,0,0x20000);
                mfmbufb=(unsigned char *)mfmbuf;
                for (c=0;c<tracklen;c++)
                    discrawdd[disc][0][d][c]=mfmbufb[c^1];
                disctracklendd[disc][0][d]=tracklen;
        }
//        exit(-1);
        fdi2raw_loadtrack(fdi,mfmbuf,tracktiming,0,&tracklen,&indexoffset,&mr,0);
//        printf("Tracklen %i indexoffset %i mr %i\n",tracklen,indexoffset,mr);
//        printf("End of track : %i\n",ftell(f));
        fclose(f);
        discmaxtracks[disc]=lasttrack;
//        printf("Lasttrack %i\n",lasttrack);
//        if (lasttrack<50) discmaxtracks[disc]=40;
//        else              discmaxtracks[disc]=80;
/*f=fopen("arc.ssd","wb");
        for (e=0;e<lasttrack;e++)
        {
        printf("Track %i\n",e);
        databuffer=0;
        tracklen=disctracklen[0][0][e]/8;
        printf("tracklen %i\n",tracklen);
        idleft=idbitsleft=0;
        for (c=0;c<tracklen;c++)
        {
                for (d=0;d<8;d++)
                {
                        tempi=discraw[0][0][e][c]&(1<<(7-d));
//                        printf("%04X %i %04X\n",mfmbuf[c],(tempi)?1:0,databuffer,1<<(7-d));
                        databuffer<<=1;
                        databuffer|=(tempi?1:0);
                        if (idbitsleft)
                        {
                                idbitsleft--;
                                if (!idbitsleft)
                                {
                                        sectorid[4-idleft]=decodefm(databuffer);
//                                        printf("Found data - %04X %02X\n",databuffer,decodefm(databuffer));
                                        idleft--;
                                        if (idleft) idbitsleft=16;
                                        else
                                        {
                                                printf("Found sector - %02X %02X %02X %02X\n",sectorid[0],sectorid[1],sectorid[2],sectorid[3]);
                                        }
                                }
                        }
                        if (datbitsleft)
                        {
                                datbitsleft--;
                                if (!datbitsleft)
                                {
                                        putc(decodefm(databuffer),f);
                                        datleft--;
                                        if (datleft) datbitsleft=16;
                                }
                        }
                        if (databuffer==0xF56F ||
                            databuffer==0xF57E)
                        {
                                printf("Found mark %04X at %i %i\n",databuffer,c,d);
                                if (databuffer==0xF57E)
                                {
                                        idleft=4;
                                        idbitsleft=16;
                                }
                                else
                                {
                                        datleft=256;
                                        datbitsleft=16;
                                }
                        }
                }
        }
        }*/
        fclose(f);
//        exit(-1);
}
#endif
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
int inttrack[2];             /*What the 8271 thinks the track is - necessary for some Acornsoft protections*/
int cursec[2];
int sectorsleft;
int sectorlen;
int byteinsec;
int curr8271drv;

void reset8271(int reload)
{
        int c;
        delayreadop=0;
        motoroff();
        statusreg=0;
        resultreg=0;
        disctime=0;
        discint=0;
        inttrack[0]=curtrack[0]=0;
        if (reload)
        {
        //printf("Disc name %s\n",discname);
        for (c=(strlen(discname[0])-1);c>=0;c--)
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
#if FDIEN
        else if (discname[0][c]=='f'||discname[0][c]=='F')
           load8271fdi(discname[0],0);
#endif
        else if (discname[0][c]=='a'||discname[0][c]=='A')
           load1770adfs(discname[0],0);
        else
           load8271ssd(discname[0],0);

        for (c=(strlen(discname[1])-1);c>=0;c--)
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
#if FDIEN
        else if (discname[1][c]=='f'||discname[1][c]=='F')
           load8271fdi(discname[1],1);
#endif
        else if (discname[1][c]=='a'||discname[1][c]=='A')
           load1770adfs(discname[1],1);
        else
           load8271ssd(discname[1],1);
//        atexit(dumpdisc);
        }
}

void reset8271s()
{
        int c;
        motoroff();
        statusreg=0;
        resultreg=0;
        disctime=0;
        discint=0;
        inttrack[0]=curtrack[0]=0;
}

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
//        rpclog("READ DRV %i ",curtrack[0]);
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
//        rpclog("%i\n",curtrack[0]);
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
//                set_gfx_mode(GFX_TEXT,0,0,0,0);
                printf("Unimplemented 8271 read special register - param %X\n",parameters[0]);
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
//        printf("Write special %02X %02X %04X %i\n",parameters[0],parameters[1],pc,ins);
        switch(parameters[0])
        {
                case 0x12:
                inttrack[0]=parameters[1];
                break;
                case 0x17:
                modereg=parameters[1];
                break;
                case 0x23:
                drvctrloutp=parameters[1];
                selects[0]=parameters[1]&0x40;
                selects[1]=parameters[1]&0x80;
                break;
                case 0x10: case 0x11: case 0x18: case 0x19: break;
                default:
                set_gfx_mode(GFX_TEXT,0,0,0,0);
                printf("Unimplemented 8271 write special register - params %X %X\n",parameters[0],parameters[1]);
                exit(-1);
                break;
        }
}

void seek()
{
        int drv=-1;
        int diff;
        char s[40];
        if (!driveled)
        {
                driveled=1;
                if (ddnoise)
                {
                        play_sample(motorsmp,96,127,1000,TRUE);
                        play_sample(motoronsmp,96,127,1000,0);
                }
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
        diff=parameters[0]-inttrack[0];
//        printf("Seek to %02X\n",parameters[0]);
//        if (parameters[0]==3) ins=0x80000000;
//        printf("Diff %i %i-%i %i\n",diff,inttrack[0],parameters[0],curtrack[0]);
        if (inttrack[0]!=parameters[0])// && parameters[0])
        {
                if (parameters[0])
                {
                        inttrack[0]+=diff;
                        curtrack[0]+=diff;
                }
                else
                   inttrack[0]=curtrack[0]=parameters[0];
        }
        if (curtrack[0]>=discmaxtracks[0])
           curtrack[0]=inttrack[0]=discmaxtracks[0]-1;
//        printf("Curtrack 0 now %i Inttrack 0 now %i\n",curtrack[0],inttrack[0]);
        statusreg=0x80;
        //printf("Seek NMI\n");
        NMI();
        //set8271poll(100);
//        printf("Seek over %i tracks %i\n",diff,ddnoise);
        if (ddnoise && diff)
        {
                if (diff==1)
                {
                        play_sample(stepsmp,96,127,1000,0);
                        set8271poll(1000);
                }
                else if (diff<7)
                {
                        play_sample(seeksmp,96,127,1000,0);
                        set8271poll(1000);
                }
                else if (diff<30)
                {
                        play_sample(seek3smp,96,127,1000,0);
                        set8271poll(500000);
                }
                else
                {
                        play_sample(seek2smp,96,127,1000,0);
                        set8271poll(1400000);
                }
        }
        else
           set8271poll(100);
}

void seekint()
{
        char s[40];
        statusreg=0x18;
//        printf("Seek NMI 2\n");
        NMI();
        resultreg=0;
        setspindown();
}

int bytesread=0;

void readdoneint()
{
//        rpclog("Read done %i\n",curtrack[0]);
//        if (curtrack[0]==54) output=1;
//        output=1;
//        timetolive=500;
        setspindown();
        statusreg=0x18;
        //printf("Read NMI 3\n");
        NMI();
}

void readint();

void readdelay()
{
//        printf("Read delay over\n");
        delayreadop=0;
}

void readvarlen()
{
        char s[40];
        int drv=-1,dif,diff;
        readflash|=1;
        if (!driveled)
        {
                if (ddnoise)
                {
                        play_sample(motorsmp,96,127,1000,TRUE);
                        play_sample(motoronsmp,96,127,1000,0);
                }
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
//        rpclog("%i %i %i\n",curtrack[0],parameters[0],inttrack[0]);
        dif=ABS(curtrack[0]-parameters[0]);
        diff=parameters[0]-inttrack[0];
//        if (curtrack[0]!=parameters[0] && ddnoise)
//           play_sample(stepsmp,255,127,1000,0);
        if (inttrack[0]!=parameters[0])// && parameters[0])
        {
                if (parameters[0])
                {
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                        inttrack[0]+=diff;
                        curtrack[0]+=diff;
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                }
                else
                {
                        inttrack[0]=curtrack[0]=parameters[0];
//                        printf("rv Seek to %i\n",curtrack[0]);
                }
        }
//        else
//           printf("No seeking - on track %i %i\n",curtrack[0],inttrack[0]);
        if (curtrack[0]>=discmaxtracks[0])
           curtrack[0]=inttrack[0]=discmaxtracks[0]-1;
//        printf("Readvar curtrack 0 now %i %i\n",curtrack[0],parameters[0]);
        cursec[0]=parameters[1];
        sectorsleft=parameters[2]&0x1F;
        sectorlen=1<<(7+((parameters[2]>>5)&7));
        byteinsec=0;
        statusreg=0x80;
//        rpclog("READ track %i %i sector %i sectorsleft %i sectorlen %i %i\n",curtrack[0],inttrack[0],cursec[0],sectorsleft,sectorlen,parameters[2]>>5);
//        printf("FDIIN %i\n",fdiin[drv]);
        //printf("Read NMI\n");
        NMI();
        if (fdiin[drv])
        {
                idmarks=0;
                trackskips=0;
                inreadop=1;
                doint=readdelay;
                delayreadop=1;
        }
        else
        {
                doint=readint;
                bytesread=0;
        }
        if (ddnoise)
        {
                if (!diff)
                   set8271poll(BYTETIME);
                else if (diff==1)
                {
                        play_sample(stepsmp,96,127,1000,0);
                        set8271poll(BYTETIME);
                }
                else if (diff<7)
                {
                        play_sample(seeksmp,96,127,1000,0);
                        set8271poll(BYTETIME*7);
                }
                else if (diff<30)
                {
                        play_sample(seek3smp,96,127,1000,0);
                        set8271poll(500000);
                }
                else
                {
                        play_sample(seek2smp,96,127,1000,0);
                        set8271poll(1400000);
                }
        }
        else
           set8271poll(BYTETIME);
/*        if (!sectorsleft)
        {
                inreadop=0;
                set8271poll(150);
                doint=readdoneint;
                statusreg=0x9C;
        }*/
}

void readvarlendel()
{
//        printf("DEL ");
        readvarlen();
        inreadop=2;
}

void readint()
{
        int done=0;
        char s[40];
        //return;
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

void writeint()
{
        int c;
        int done=0;
//        printf("Write int %i %i %i %i %03i\n",curdisc,SIDE,curtrack[0],cursec[0],byteinsec);
        if (firstwriteint) firstwriteint=0;
        else
        {
//        printf("Written %03i\n",byteinsec);
        discs[curdisc][SIDE][cursec[0]+(curtrack[0]*16)][byteinsec]=datareg;

        byteinsec++;
        if (byteinsec>=256)
        {
//                printf("\n");
//                for (c=0;c<256;c++) printf("%c",discs[0][0][0][c]);
//                printf("\n");
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
                        }
                }
                else
                {
                        done=1;
                        sectorsleft=-1;
                }
        }
        }
        if (sectorsleft<0)
        {
//                printf("End of command\n");
                statusreg=0x18;
                NMI();
                return;
//                exit(-1);
        }
        statusreg=0x8C;
        NMI();
        set8271poll(BYTETIME);
}

void writevarlen()
{
        int drv=-1;
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
        curtrack[0]=parameters[0];
        cursec[0]=parameters[1];
        sectorsleft=parameters[2]&0x1F;
        sectorlen=1<<(7+((parameters[2]>>5)&7));
        byteinsec=0;
        statusreg=0x80;
        NMI();
        set8271poll(1200);
        firstwriteint=1;
        discaltered[drv]=1;
//        printf("Write track %i sector %i length %i sectors\n",parameters[0],parameters[1],parameters[2]&0x1F);
//        exit(-1);
}

void verifyvarlen()
{
        int drv=-1,diff;
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
        diff=parameters[0]-inttrack[0];
//        if (curtrack[0]!=parameters[0] && ddnoise)
//           play_sample(stepsmp,255,127,1000,0);
        if (inttrack[0]!=parameters[0])// && parameters[0])
        {
                if (parameters[0])
                {
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                        inttrack[0]+=diff;
                        curtrack[0]+=diff;
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                }
                else
                   inttrack[0]=curtrack[0]=parameters[0];
        }
//        else
//           printf("No seeking - on track %i %i\n",curtrack[0],inttrack[0]);
        if (curtrack[0]>=discmaxtracks[0])
           curtrack[0]=inttrack[0]=discmaxtracks[0]-1;
//        printf("Verify curtrack\n");
        cursec[0]=parameters[1];
        statusreg=0x80;
        NMI();
        set8271poll(BYTETIME*300);
        firstwriteint=1;
}

void verifyint()
{
//        FILE *f=fopen("d:/djgpp/b-em6/disc.ssd","wb");
//        fwrite(&discs[0][0][0][0],256*10*80,1,f);
//        fclose(f);
        statusreg=0x18;
        NMI();
}

void readid()
{
        int diff;
        int drv=-1;
        if (selects[0])
           drv=0;
        if (selects[1])
           drv=1;
        if (drv==-1)
        {
                error8271(0x10);
                return;
        }
        diff=parameters[0]-inttrack[0];
        if (inttrack[0]!=parameters[0])// && parameters[0])
        {
                if (parameters[0])
                {
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                        inttrack[0]+=diff;
                        curtrack[0]+=diff;
//                        printf("%i %i %i\n",inttrack[0],curtrack[0],diff);
                }
                else
                   inttrack[0]=curtrack[0]=parameters[0];
        }
//        else
//           printf("No seeking - on track %i %i\n",curtrack[0],inttrack[0]);
//        curtrack[0]=parameters[0];
        sectorsleft=parameters[2];//&0x1F;
//        printf("Read ID track %i %i id fields\n",parameters[0],parameters[2]&0x1F);
        if (fdiin[drv])
        {
                waitforindex=1;
                inreadop=3;
                idmarks=0;
        }
        else
        {
                set8271poll(BYTETIME);
        }
        byteinsec=0;
        statusreg=0x80;
        NMI();
//        dumpregs();
//        exit(-1);
}

void readidint()
{
        if (!sectorsleft)
        {
                statusreg=0x18;
                NMI();
                return;
        }
        switch (byteinsec)
        {
                case 0:
                datareg=curtrack[0];
                break;
                case 1:
                datareg=SIDE;
                break;
                case 2:
                datareg=cursec[0];
                break;
                case 3:
                datareg=1;
                break;
        }
        byteinsec++;
        if (byteinsec>=4)
        {
                byteinsec=0;
                cursec[0]++;
                if (cursec[0]==10) cursec[0]=0;
                sectorsleft--;
        }
        set8271poll(BYTETIME);
        statusreg=0x8C;
        NMI();
}

void format()
{
        curtrack[0]=parameters[0];
        cursec[0]=0;
        sectorsleft=parameters[2]&0x1F;
        set8271poll(BYTETIME);
        byteinsec=0;
        statusreg=0x80;
        NMI();
//        printf("Format command\n");
//        printf("Track %i\n",parameters[0]);
//        printf("Number of sectors %i Record length %i\n",parameters[2]&0x1F,parameters[2]>>5);
//        dumpregs();
//        exit(-1);
}

void formatint()
{
        discs[curdisc][SIDE][cursec[0]+(curtrack[0]*16)][byteinsec]=0;
        byteinsec++;
        if (byteinsec==256)
        {
                byteinsec=0;
//                printf("Formatted track %i sector %i\n",curtrack[0],cursec[0]);
                cursec[0]++;
                sectorsleft--;
                if (!sectorsleft)
                {
                        statusreg=0x18;
                        NMI();
//                        printf("End of format command\n");
                        return;
                }
        }
        set8271poll(BYTETIME);
}

#define COMMS 11
FDCCOMMAND fdccomms[COMMS+1] =
{
        {0x2C,0,0x3F,readdrvstatus,NULL},
        {0x3A,2,0x3F,writespecial,NULL},
        {0x3D,1,0x3F,readspecial,NULL},
        {0x35,4,0xFF,specify,NULL},
        {0x29,1,0x3F,seek,seekint},
        {0x13,3,0x3F,readvarlen,/*readint*/NULL},
        {0x17,3,0x3F,readvarlendel,/*readint*/NULL},
        {0x0b,3,0x3F,writevarlen,writeint},
        {0x1f,3,0x3F,verifyvarlen,verifyint},
        {0x23,5,0x3F,format,formatint},
        {0x1B,3,0x3F,readid,readidint},
        {0xFF,0,0x00,NULL,NULL}
};

int pollbytesleft,pollbitsleft,readidpoll;
int ddidbitsleft,dataheader;
unsigned short databuffer=0;
unsigned char disccrc[2];
unsigned short crc;

void calccrc(unsigned char byte)
{
        int i;
        for (i = 0; i < 8; i++) {
                if (crc & 0x8000) {
                        crc <<= 1;
                        if (!(byte & 0x80)) crc ^= 0x1021;
                } else {
                        crc <<= 1;
                        if (byte & 0x80) crc ^= 0x1021;
                }
                byte <<= 1;
        }
}

void writefdcdata(unsigned char v)
{
        if (model>2)
        {
                write1770fdcdata(v);
        }
        else
        {
                datareg=v;
                resultreg=0;
                statusreg=0x8C;
                NMI();
        }
}

void endreadid()
{
        if (model>2)
        {
                end1770readid();
        }
        else
        {
                statusreg=0x9C;
                set8271poll(500);
                doint=readdoneint;
        }
}

void idcrcerror()
{
        if (model>2)
        {
                idcrcerror1770();
        }
        else
        {
                resultreg=0x0C;
                statusreg=0x18;
                NMI();
                inreadop=0;
        }
}

void datacrcerror()
{
        if (model>2)
        {
                datacrcerror1770();
        }
        else
        {
                resultreg=0x0E;
                statusreg=0x18;
                NMI();
                inreadop=0;
        }
}

void sectorerror()
{
        if (model>2)
        {
                sectorerror1770();
        }
        else
        {
                resultreg=0x18;
                statusreg=0x18;
                NMI();
                inreadop=0;
        }
}

/*What a mess!
  This function simulates the rotation of the disc and many of the functions
  of the 8271. This includes identifying syncs, decoding data, waiting for
  index etc*/
void pollbyte()
{
        int c;
        int tempi;
        /*Check to see if we've looped on ourselves
          This isn't looking for the end of the track as discs are circular*/
        if ((discrawpos>=disctracklen[curdisc][SIDE][curtrack[0]] && !ddensity) ||
            (discrawpos>=disctracklendd[curdisc][SIDE][curtrack[0]] && ddensity))
        {
                discrawpos=0;
                readflash|=2;
//                printf("Disc track length %i\n",disctracklen[curdisc][SIDE][curtrack[0]]);
        }
        /*If we are waiting for data (readvarlen/readid) and are not waiting
          for index, then enter this huge mess of code*/
        if (inreadop && !waitforindex && !delayreadop)
        {
                /*Pull next bit off disc*/
                if (ddensity)
                   tempi=discrawdd[curdisc][SIDE][curtrack[0]][discrawpos>>3]&(1<<(7-(discrawpos&7)));
                else
                   tempi=discraw[curdisc][SIDE][curtrack[0]][discrawpos>>3]&(1<<(7-(discrawpos&7)));
                databuffer<<=1;
                databuffer|=(tempi?1:0);
//                printf("%i %04X %02X %i %i %02X\n",discrawpos,databuffer,discraw[0][0][curtrack[0]][discrawpos>>3],curtrack[0],tempi,(1<<(7-(discrawpos&7))));
//                printf("%i %04X\n",pollbitsleft,databuffer);
                /*If we are actively reading data, then enter this lot*/
                if (pollbitsleft)
                {
                        pollbitsleft--;
                        if (!pollbitsleft)
                        {
                                /*Now we've pulled a word off disc (8 data bits and 8 clock bits)
                                  Now ready for decoding*/
                                pollbytesleft--;
                                if (pollbytesleft) pollbitsleft=16; /*Set up another word if we need it*/
                                /*If this is an ID we are reading then enter this lot*/
//                                printf("Found byte - %i %i %02X %02X %02X %02X %02X %02X\n",readidpoll,pollbytesleft,discid[0],discid[1],discid[2],discid[3],discid[4],discid[5]);
                                if (readidpoll)
                                {
                                        /*Decode and store*/
                                        discid[5-pollbytesleft]=decodefm(databuffer);
                                        /*If we are in a read ID command and this isn't a CRC byte
                                          then send to CPU*/
                                        if (inreadop==3 && pollbytesleft>1)
                                        {
                                                writefdcdata(decodefm(databuffer));
/*                                                datareg=decodefm(databuffer);
//                                                printf("Datareg %02X\n",datareg);
                                                resultreg=0;
                                                statusreg=0x8C;
                                                NMI();*/
                                        }
                                        /*If we've read the entire ID*/
                                        if (!pollbytesleft)
                                        {
                                                /*Calculate CRC*/
                                                crc=(ddensity)?0xcdb4:0xFFFF;
                                                calccrc(0xFE);
                                                for (c=0;c<4;c++) calccrc(discid[c]);
                                                if (((crc>>8)!=discid[4] || (crc&0xFF)!=discid[5]))
                                                {
//                                                        printf("CRC error : %04X %02X%02X\n",crc,discid[4],discid[5]);
                                                        idcrcerror();
                                                        return;
                                                        dumpregs();
                                                        exit(-1);
                                                }
                                                /*If we are in a read ID command*/
                                                if (inreadop==3)
                                                {
                                                        /*Mark off another sector ID read*/
                                                        sectorsleft--;
                                                        if (sectorsleft)
                                                        {
                                                                cursec[0]++;
//                                                                printf("READ ID continuing to sector %i %i\n",cursec[0],sectorsleft);
                                                                idmarks=0;
                                                        }
                                                        else
                                                        {
//                                                                printf("End of read ID command\n");
                                                                endreadid();
                                                                inreadop=0;
                                                        }
                                                }
//                                                printf("Sector ID : %02X %02X %02X %02X %02X %02X %i %i CRC %04X\n",discid[0],discid[1],discid[2],discid[3],discid[4],discid[5],inreadop,curtrack[0],crc);
                                                /*If we are in a readvar/readvardel command and this isn't the right track*/
                                                if (inttrack[0]!=discid[0] && inreadop<3 && inreadop)
                                                {
                                                        /*Then skip to the next track*/
                                                        /*This code is probably WRONG!!!*/
                                                        trackskips++;
                                                        if (trackskips==3)
                                                        {
//                                                                printf("Can not find track/sector\n");
                                                                sectorerror();
                                                                return;
//                                                                dumpregs();
//                                                                exit(-1);
                                                        }
                                                        curtrack[0]++;
                                                        inttrack[0]++;
                                                        discid[2]=0xFF;
//                                                        printf("Stepping to next track\n");
                                                        if (curtrack[0]>=discmaxtracks[0])
                                                        {
//                                                                printf("Error : %i %i\n",inttrack[0],discid[0]);
//                                                                printf("Error\n");
                                                                sectorerror();
                                                                curtrack[0]--;
                                                                inttrack[0]--;
//                                                                output=1;
                                                                timetolive=250;
                                                                return;
                                                                dumpregs();
                                                                exit(-1);
                                                        }
                                                }
                                        }
                                }
                                else /*This must be a readvar command*/
                                {
                                        if (pollbytesleft>1)
                                        {
                                                /*Decode data and send to CPU*/
                                                writefdcdata(decodefm(databuffer));
/*                                                datareg=decodefm(databuffer);
                                                resultreg=0;
                                                statusreg=0x8C;*/
                                                calccrc(decodefm(databuffer));
                                        }
                                        else /*Decode and store CRC*/
                                           disccrc[pollbytesleft^1]=decodefm(databuffer);
                                        /*If end of sector*/
                                        if (!pollbytesleft)
                                        {
//                                                printf("End of sector - CRC %02X %02X %04X\n",disccrc[0],disccrc[1],crc);
                                                if (((crc>>8)!=disccrc[0] || (crc&0xFF)!=disccrc[1]))
                                                {
//                                                        printf("CRC error - %04X %02X%02X\n",crc,disccrc[0],disccrc[1]);
                                                        datacrcerror();
                                                        return;
                                                }
                                                /*Mark off another sector read*/
                                                sectorsleft--;
                                                sectorsleft&=0x1F;
                                                if (sectorsleft)
                                                {
                                                        cursec[0]++;
//                                                        printf("READ continuing to sector %i %i\n",cursec[0],sectorsleft);
                                                        idmarks=0;
                                                }
                                                else
                                                {
//                                                        printf("End of read command\n");
                                                        endreadid();
/*                                                        statusreg=0x9C;
                                                        set8271poll(20000);
                                                        doint=readdoneint;*/
                                                        inreadop=0;
                                                }
                                        }
//                                        NMI();
                                }
                        }
                }
                else if (databuffer==0x4489 && ddensity)
                {
                        ddidbitsleft=17;
//                        printf("4489\n");
                }
                if (ddidbitsleft)
                {
                        ddidbitsleft--;
                        if (!ddidbitsleft)
                        {
                                if (decodefm(databuffer)==0xFE)
                                {
//                                        printf("Sector header\n");
                                        pollbytesleft=6;
                                        pollbitsleft=16;
                                        readidpoll=1;
                                }
                                else if (decodefm(databuffer)==0xFB)
                                {
                                        dataheader=1;
//                                        printf("Data header\n");
                                }
                        }
                }
                if (!pollbitsleft)
                {
                if (databuffer==0xF57E && !ddensity) /*Sector head ID*/
                {
                        /*Read next 6 bytes (sector ID)*/
                        pollbytesleft=6;
                        pollbitsleft=16;
                        readidpoll=1;
//                        printf("Found sector head ID\n");
                }
                if ((inreadop<3 && (databuffer==0xF56F || ((databuffer==0xF56A) && (inreadop==2))) && !ddensity) || dataheader) /*Data ID*/
                {
                        dataheader=0;
//                        printf("Data ID %i %i %i %i\n",cursec[0],curtrack[0],discid[0],discid[2]);
                        if (cursec[0]==discid[2])
                        {
                                /*If sector and track from last sector ID match*/
                                if (inttrack[0]==discid[0])
                                {
                                        readidpoll=0;
                                        /*Read sector - length of data + 2 CRC bytes*/
                                        if (model>2) sectorlen=1<<(7+(discid[3]&7));
                                        pollbytesleft=sectorlen+2;
                                        pollbitsleft=16;
//                                        printf("Reading found sector ID %i %i %i %i %02X %02X\n",discid[0],discid[1],discid[2],discid[3],ram[0xA6],ram[0xA7]);
                                        readflash|=1;
                                        crc=(ddensity)?0xcdb4:0xFFFF;
                                        if (databuffer==0xF56A) calccrc(0xF8);
                                        else                    calccrc(0xFB);
                                }
                                else
                                {
                                        /*We are on the wrong track - skip*/
                                        /*This code is probably wrong*/
                                        trackskips++;
                                        if (trackskips==3)
                                        {
//                                                printf("Can not find track/sector\n");
                                                sectorerror();
                                                return;
                                                dumpregs();
                                                exit(-1);
                                        }
                                        curtrack[0]++;
                                        inttrack[0]++;
                                        discid[2]=0xFF;
//                                        printf("Stepping to next track\n");
                                        if (curtrack[0]>=discmaxtracks[0])
                                        {
//                                                printf("Error : %i %i\n",inttrack[0],discid[0]);
                                                                sectorerror();
                                                                curtrack[0]--;
                                                                inttrack[0]--;
//                                                                output=1;
                                                                timetolive=250;
                                                                return;
                                                dumpregs();
                                                exit(-1);
                                        }
                                }
/*                                dumpregs();
                                exit(-1);*/
                        }
                }
                }
        }
        /*Move ahead one bit*/
        discrawpos++;
        if (delayreadop) return;
        /*See if we hit an index pulse/hole*/
        if (waitforindex && discrawpos==discindexes[0][0][curtrack[0]]) waitforindex=0;
        if (discrawpos==discindexes[0][0][curtrack[0]]) idmarks++;
        if (idmarks==2)
        {
//                printf("Failed to find sector\n");
                sectorerror();
                return;
                dumpregs();
                exit(-1);
        }
}

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

unsigned char i8271command;
void commandwrite(unsigned char val)
{
        FDCCOMMAND comm;
        i8271command=val;
        delayreadop=0;
        if (val==0xF5) return; /*Seems to be written on reset sometimes*/
        presentparam=0;
        statusreg|=0x90;
        //printf("Command write NMI\n");
        NMI();
        if (val&0x40) curdisc=0;
        if (val&0x80) curdisc=1;
        comm=getcommand(val);
        if (!comm.proc)
        {
/*                docommand=NULL;
                doint=NULL;
                error8271(0x10);
                return;*/
//                closegfx();
                printf("Unrecognized 8271 command %X\n",val);
                exit(-1);
                return;

        }
//        if (val==0x53) output=1;
        params=comm.params;
        docommand=comm.proc;
        if (comm.intproc) doint=comm.intproc;
        //printf("Command is %02X params %i %i\n",val,params,comm.params);
        command=val;
        if (!params)
        {
                statusreg&=0x7E;
                NMI();
                docommand();
//                rpclog("Running command %02X\n",command);
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
//                        rpclog("Running command %02X  %02X %02X %02X %02X\n",command,parameters[0],parameters[1],parameters[2],parameters[3]);
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
                statusreg&=~0x18;
                NMI();
                val=resultreg;
                resultreg=0;
                break;
                case 4:
                statusreg&=~0xC;
                NMI();
                val=datareg;
//                rpclog("Read data %02X\n",val);
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
//        rpclog("Write %04X %02X %04X %i\n",addr,val,pc,curtrack[0]);
//        if (output) exit(-1);
//        printf("Writing %04X %02X\n",addr,val);
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
//                printf("%03i write %02X\n",byteswritten++,datareg);
//                printf("%c",val);
                break;
        }
}

void poll8271()
{
        char s[40];
//        printf("Poll %08X %08X\n",doint,motoroff);
        if (doint==motoroff)
        {
                motoroff();
                discint=0;
                return;
        }
        if (!delayreadop)
        {
                statusreg|=8;
                NMI();
        }
        discint=0;
        disctime=0;
//        printf("NMI now %i %02X %02X %i\n",nmi,i8271command,statusreg,error);
        if (error)
        {
                resultreg=error;
                statusreg=0x18;
                error=0;
                NMI();
                driveled=0;
//                rpclog("Error %02X\n",error);
        }
        else
        {
                if (doint)
                   doint();
        }
}
