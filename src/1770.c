/*B-em 0.6 by Tom Walker*/
/*1770 emulator*/
#include <allegro.h>
#include <stdio.h>

int motorofff;
int ddnoise;

BITMAP *buffer;

#define SIDES 2
#define TRACKS 80
#define SECTORS 16
#define SECTORSIZE 256

SAMPLE *seeksmp;
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
        int curtrack,cursector;
} wd1770;

int load1770adfs(char *fn, int disc)
{
        int c,d,e,f,sides;
        int len;
        FILE *ff=fopen(fn,"rb");
        if (!ff) return -1;
        fseek(ff,-1,SEEK_END);
        len=ftell(ff);
        fseek(ff,0,SEEK_SET);
        if (len>(80*16*256)) sides=2;
        else                 sides=1;
        for (d=0;d<80;d++)
        {
                for (c=0;c<sides;c++)
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

void reset1770()
{
        wd1770.control=0xFF;
        wd1770.status=0x80;
        discint=0;
        nmi=0;
        stop_sample(motorsmp);
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

void start1770command()
{
        wd1770.status|=1;
        endcommand=0;
        if (ddnoise)
        {
                stop_sample(motorsmp);
                play_sample(motorsmp,255,127,1000,TRUE);
        }
        motorofff=0;
        switch (wd1770.command>>4)
        {
                case 0: set1770poll(2000); if (ddnoise) play_sample(seeksmp,255,127,1000,FALSE); break; /*Restore*/
                case 1: set1770poll(2000); if (ddnoise) play_sample(seeksmp,255,127,1000,FALSE); break; /*Seek*/
                case 5: set1770poll(2000); if (ddnoise) play_sample(stepsmp,255,127,1000,FALSE); break; /*Step in*/
                case 8: wd1770.status&=~4; set1770poll(2000); sectorpos=0; wd1770.curtrack=wd1770.track; wd1770.cursector=wd1770.sector; break; /*Read sector*/
                default:
                printf("Bad 1770 command %01X\n",wd1770.command>>4);
                dumpregs();
                exit(-1);
        }
}

void write1770(unsigned short addr, unsigned char val)
{
        switch (addr)
        {
                case 0xFE80: /*Control register*/
                wd1770.control=val;
                if (val&0x20) reset1770();
                if (val&1) curdisc=0;
                if (val&2) curdisc=1;
                if (val&4) curside=1; else curside=0;
                return;
                case 0xFE84: /*Command*/
                if (wd1770.status&1 && (val>>4)!=0xD) return;
                wd1770.command=val;
                start1770command();
                return;
                case 0xFE85: /*Track register*/
                wd1770.track=val;
                return;
                case 0xFE86: /*Sector register*/
                wd1770.sector=val;
                return;
                case 0xFE87: /*Data register*/
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
        switch (addr)
        {
                case 0xFE80: /*Control*/
                return wd1770.control;
                case 0xFE84: /*Status register*/
                nmi&=~1;
                return wd1770.status;
                case 0xFE85: /*Track register*/
                return wd1770.track;
                case 0xFE86: /*Sector register*/
                return wd1770.sector;
                case 0xFE87: /*Data register*/
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
                wd1770.track=wd1770.curtrack=wd1770.data;
                wd1770.status&=~5;
                if (!wd1770.curtrack) wd1770.status|=4;
                nmi|=1;
                set1770spindown();
                break;

                case 5: /*Step in*/
                wd1770.curtrack++;
                wd1770.track=wd1770.curtrack;
                wd1770.status&=~5;
                nmi|=1;
                set1770spindown();
                break;

                case 8: /*Read sector*/
                wd1770.data=discs[curdisc][curside][(wd1770.curtrack*16)+wd1770.cursector][sectorpos++];
                sectorpos&=255;
                if (endcommand)
                {
                        sectorpos=0;
                        wd1770.status&=~1;
                        nmi|=1;
                        set1770spindown();
                        return;
                }
                if (!sectorpos) endcommand=1;
                nmi|=2;
                wd1770.status|=2;
                set1770poll(200);
//                output=1;
                break;

                default:
                printf("Bad 1770 callback command %01X\n",wd1770.command>>4);
                dumpregs();
                exit(-1);
        }
}
