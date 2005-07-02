/*B-em 0.8 by Tom Walker*/
/*1770 emulator*/
#include <allegro.h>
#include <stdio.h>

int discaltered[2]={0,0};
int sides[2];
int adfs[2]={0,0};
unsigned short pc;
int motorofff;
int ddnoise;

BITMAP *buffer;

#define SIDES 2
#define TRACKS 80
#define SECTORS 16
#define SECTORSIZE 256

SAMPLE *seeksmp;
SAMPLE *seek2smp;
SAMPLE *seek3smp;
SAMPLE *stepsmp;
SAMPLE *motorsmp;

int output;
unsigned char discs[2][SIDES][SECTORS*TRACKS][SECTORSIZE];
int endcommand=0;
int curside,curdisc;
int nmi;
int discint;

int sectorpos;
struct
{
        unsigned char track,sector,data,command,control,status;
        int curtrack,cursector,dat;
} wd1770;

int load1770adfs(char *fn, int disc)
{
        int c,d,e,f;
        int len;
        FILE *ff=fopen(fn,"rb");
        if (!ff) return -1;
        fseek(ff,-1,SEEK_END);
        len=ftell(ff);
        fseek(ff,0,SEEK_SET);
        if (len>(80*16*256)) sides[disc]=2;
        else                 sides[disc]=1;
        adfs[disc]=1;
        for (d=0;d<80;d++)
        {
                for (c=0;c<sides[disc];c++)
                {
//                        printf("Track %i side %i at %i\n",d,c,ftell(ff));
                        for (e=0;e<16;e++)
                        {
                                for (f=0;f<256;f++)
                                {
                                        if (!feof(ff)) discs[disc][c][(d*16)+e][f]=getc(ff);
                                        else           discs[disc][c][(d*16)+e][f]=0;
                                }
                        }
                }
        }
        fclose(ff);
}

char discname[2][260];

void checkdiscchanged(int disc)
{
        FILE *ff;
        int c,d,e,f;
        if (!discaltered[disc]) return;
//        alert("Writing disc",discname[disc],NULL,"OK",NULL,0,0);
        discaltered[disc]=0;
        ff=fopen(discname[disc],"wb");
        for (c=0;c<80;c++)
        {
                for (d=0;d<sides[disc];d++)
                {
                        for (e=0;e<(adfs[disc]?16:10);e++)
                        {
                                for (f=0;f<256;f++)
                                {
                                        putc(discs[disc][d][(c*16)+e][f],ff);
                                }
                        }
                }
        }
        fclose(ff);
}

void dumpdisc2()
{
        FILE *f=fopen("d:/djgpp/b-em6/disc.ssd","wb");
        int c,d,e;
        for (c=0;c<80;c++)
        {
                for (d=0;d<10;d++)
                {
                        for (e=0;e<256;e++)
                        {
                                putc(discs[0][0][(c*16)+d][e],f);
                        }
                }
        }
        fclose(f);
}

void reset1770()
{
        wd1770.control=0xFF;
        wd1770.status=0x80;
        discint=0;
        nmi=0;
        stop_sample(motorsmp);
        motorofff=0;
}

void reset1770s()
{
        wd1770.control=0xFF;
        wd1770.status=0x80;
        discint=0;
        nmi=0;
//        stop_sample(motorsmp);
        motorofff=0;
}

void set1770poll(int c)
{
        discint=c;
}

void set1770spindown()
{
        set1770poll(2000000);
        motorofff=1;
}

void play1770seek(int target)
{
        int dif=ABS(target-wd1770.curtrack);
        if (!dif)
        {
                set1770poll(100);
        }
        else if (dif==1)
        {
                play_sample(stepsmp,127,127,1000,0);
                set1770poll(100);
        }
        else if (dif<7)
        {
                play_sample(seeksmp,127,127,1000,0);
                set1770poll(1000);
        }
        else if (dif<35)
        {
                play_sample(seek3smp,127,127,1000,0);
                set1770poll(1200000);
        }
        else
        {
                play_sample(seek2smp,127,127,1000,0);
                set1770poll(2000000);
        }
//        printf("dif %i discint %i\n",dif,discint);
}

void start1770command(addr)
{
        wd1770.status=0x81;
//        wd1770.status|=1;
        endcommand=0;
        if (ddnoise)
        {
                stop_sample(motorsmp);
                play_sample(motorsmp,127,127,1000,TRUE);
        }
        motorofff=0;
        switch (wd1770.command>>4)
        {
                case 0: if (ddnoise) play1770seek(0); else set1770poll(2000); break; /*Restore*/
                case 1: if (ddnoise) play1770seek(wd1770.data); else set1770poll(2000); break; /*Seek*/
                case 5: if (ddnoise) play_sample(stepsmp,127,127,1000,FALSE); set1770poll(2000); break; /*Step in*/
                case 8: wd1770.status&=~4; if (ddnoise) { play1770seek(wd1770.track); } else { set1770poll(2000); } sectorpos=0; wd1770.curtrack=wd1770.track; wd1770.cursector=wd1770.sector; break; /*Read sector*/
                case 0xA: wd1770.status&=~4; if (ddnoise) { play1770seek(wd1770.track); } else { set1770poll(2000); } sectorpos=-1; wd1770.curtrack=wd1770.track; wd1770.cursector=wd1770.sector; wd1770.dat=0; break; /*Write sector*/
                default:
                printf("Bad 1770 command %01X\n",wd1770.command>>4);
                dumpregs();
                exit(-1);
        }
}

void write1770(unsigned short addr, unsigned char val)
{
//        if (addr!=0xFE87) printf("1770 write %04X %02X %04X\n",addr,val,pc);
        switch (addr)
        {
                case 0xFE80: /*B+ Control register*/
                wd1770.control=val;
                if (val&0x20) reset1770();
                if (val&1) curdisc=0;
                if (val&2) curdisc=1;
                if (val&4) curside=1; else curside=0;
                return;
                case 0xFE24: /*Master Control register*/
                wd1770.control=val;
                if (val&4) reset1770();
                if (val&1) curdisc=0;
                if (val&2) curdisc=1;
                if (val&16) curside=1; else curside=0;
                return;
                case 0xFE84: /*Command*/
                case 0xFE28:
//                printf("Command write %02X\n",val);
                if (wd1770.status&1 && (val>>4)!=0xD) return;
                wd1770.command=val;
                start1770command(addr);
//                printf("discint %i\n",discint);
                return;
                case 0xFE85: /*Track register*/
                case 0xFE29:
                wd1770.track=val;
                return;
                case 0xFE86: /*Sector register*/
                case 0xFE2A:
                wd1770.sector=val;
                return;
                case 0xFE87: /*Data register*/
                case 0xFE2B:
                wd1770.dat=1;
//                printf("Data register write %02X\n",val);
                wd1770.status&=~2;
                nmi&=~2;
                wd1770.data=val;
                return;
        }
        printf("Bad 1770 write %04X %02X\n",addr,val);
        dumpregs();
        exit(-1);
}

unsigned char read1770(unsigned short addr)
{
//        printf("1770 read %04X %04X\n",addr,pc);
        switch (addr)
        {
                case 0xFE80: /*Control*/
                case 0xFE24:
                return wd1770.control;
                case 0xFE84: /*Status register*/
                case 0xFE28:
                nmi&=~1;
//                printf("Read status %02X\n",wd1770.status);
                return wd1770.status;
                case 0xFE85: /*Track register*/
                case 0xFE29:
                return wd1770.track;
                case 0xFE86: /*Sector register*/
                case 0xFE2A:
                return wd1770.sector;
                case 0xFE87: /*Data register*/
                case 0xFE2B:
                wd1770.status&=~2;
                nmi&=~2;
                return wd1770.data;
        }
        printf("Bad 1770 read %04X\n",addr);
        dumpregs();
        exit(-1);
}

void poll1770()
{
        if (motorofff)
        {
                motorofff=0;
                stop_sample(motorsmp);
                return;
        }
        switch (wd1770.command>>4)
        {
                case 0: /*Restore*/
                wd1770.track=wd1770.curtrack=0;
                wd1770.status&=~1;
                wd1770.status|=4;
                nmi|=1;
                set1770spindown();
                break;

                case 1: /*Seek*/
                if ((wd1770.command&4) && (sides[curdisc]==1) && curside) /*Seek error*/
                {
                        wd1770.status&=~5;
                        wd1770.status|=0x10;
                        nmi|=1;
                        set1770spindown();
                }
                else
                {
                        wd1770.track=wd1770.curtrack=wd1770.data;
                        wd1770.status&=~5;
                        if (!wd1770.curtrack) wd1770.status|=4;
                        nmi|=1;
                        set1770spindown();
                }
                break;

                case 5: /*Step in*/
                wd1770.curtrack++;
                wd1770.track=wd1770.curtrack;
                wd1770.status&=~5;
                nmi|=1;
                set1770spindown();
                break;

                case 8: /*Read sector*/
                if ((sides[curdisc]==1) && curside) /*Seek error*/
                {
                        wd1770.status&=~5;
                        wd1770.status|=0x10;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
//                printf("Read sector %03i\n",sectorpos);
                if (endcommand)
                {
                        sectorpos=0;
                        wd1770.status&=~1;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
                wd1770.data=discs[curdisc][curside][(wd1770.curtrack*16)+wd1770.cursector][sectorpos++];
                sectorpos&=255;
                if (!sectorpos) endcommand=1;
                nmi|=2;
                wd1770.status|=2;
                set1770poll(100);
//                output=1;
                break;

                case 0xA: /*Write sector*/
                if ((sides[curdisc]==1) && curside) /*Seek error*/
                {
                        wd1770.status&=~5;
                        wd1770.status|=0x10;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
                if (sectorpos==-1)
                {
                        nmi|=2;
                        wd1770.status|=2;
                        set1770poll(200);
                        sectorpos=0;
                        return;
                }
//                if (!sectorpos) dumpram2();
//                dumpregs();
//                exit(-1);
//                printf("Write sector %02X %02X %i\n",sectorpos,wd1770.data,endcommand);
                if (endcommand)
                {
                        dumpdisc2();
                        sectorpos=0;
                        wd1770.status&=~1;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
                if (!wd1770.dat)
                {
                        printf("Missing data\n");
                        dumpregs();
                        exit(-1);
                }
                discaltered[curdisc]=1;
                wd1770.dat=0;
                discs[curdisc][curside][(wd1770.curtrack*16)+wd1770.cursector][sectorpos++]=wd1770.data;
                sectorpos&=255;
                if (!sectorpos)
                {
//                        dumpdisc2();
                        sectorpos=0;
                        wd1770.status&=~1;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
                nmi|=2;
                wd1770.status|=2;
                set1770poll(200);
                break;

                default:
                printf("Bad 1770 callback command %01X\n",wd1770.command>>4);
                dumpregs();
                exit(-1);
        }
}
