/*B-em 0.6 by Tom Walker*/
/*Sound emulation*/

int soundfilter=0;
void dumpsound();
#include <allegro.h>
#include <stdio.h>
#include "sound.h"

FILE *soundf;
//FILE *slog;
AUDIOSTREAM *as;
#define SNCLOCK (4000000>>5)

#define NOISEBUFFER 32768

SAMPLE *snsample[4];
FILE *biglog;

int soundwave;
int us;
int logging=0;
FILE *snlog;
unsigned char snfreqhi[4],snfreqlo[4];
unsigned char snvol[4];
unsigned char snnoise;
int curfreq[4];
int lasttone;
int soundon=1;
int curwave=0;

unsigned char snwaves[4][32]=
{
        {
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
        },
        {
                0x08,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,
                0x88,0x90,0x98,0xA0,0xA8,0xB0,0xB8,0xC0,0xC8,0xD0,0xD8,0xE0,0xE8,0xF0,0xF8,0
        },
        {
                0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,
                0xF0,0xE0,0xD0,0xC0,0xB0,0xA0,0x90,0x80,0x70,0x60,0x50,0x40,0x30,0x20,0x10,0x00
        },
        {
                0x00,0x19,0x31,0x4A,0x61,0x78,0x8E,0xA2,0xB5,0xC5,0xD4,0xE1,0xEC,0xF4,0xFB,0xFE,
                0xFF,0xFE,0xFB,0xF4,0xEC,0xE1,0xD4,0xC5,0xB5,0xA2,0x8E,0x78,0x61,0x4A,0x31,0x19,
        }
};

void updatesoundwave()
{
        memcpy(snsample[1]->data,snwaves[soundwave],32);
        memcpy(snsample[2]->data,snwaves[soundwave],32);
        memcpy(snsample[3]->data,snwaves[soundwave],32);
}

static unsigned char snsamples[32] =    /* a simple sine (sort of) wave */
{
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
//      0x08,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,
//      0x88,0x90,0x98,0xA0,0xA8,0xB0,0xB8,0xC0,0xC8,0xD0,0xD8,0xE0,0xE8,0xF0,0xF8,0
};

int soundinited=0;

static unsigned char snperiodic[NOISEBUFFER];
static unsigned char snperiodic2[32] =
{
      0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static unsigned char snnoises[NOISEBUFFER];

fixed sncount[4],snlatch[4],snstat[4];
int sntone[4];
int snvols[312][4],snvols2[312][4];

void copyvols()
{
        memcpy(snvols,snvols2,sizeof(snvols));
}

void logvols(int line)
{
        snvols2[311-line][0]=snvol[0];
        snvols2[311-line][1]=snvol[1];
        snvols2[311-line][2]=snvol[2];
        snvols2[311-line][3]=snvol[3];
}

void drawsound(BITMAP *b)
{
//        textprintf_ex(b,font,0,16,7,0,"%i %04X",snlatch[1],snlatch[1]);
//        textprintf_ex(b,font,0,24,7,0,"%i %04X",snlatch[2],snlatch[2]);
//        textprintf_ex(b,font,0,32,7,0,"%i %04X",snlatch[3],snlatch[3]);
}

unsigned char lastbuffer[4];

void updatebuffer(unsigned char *buffer, int len)
{
        int c,d,diff[1024];
        unsigned char oldlast[4];
        for (d=0;d<len;d++)
        {
                buffer[d]=0;
                for (c=0;c<3;c++)
                {
                        c++;
                        buffer[d]+=(snwaves[curwave][snstat[c]]*snvols[d>>1][c])>>6;
                        sncount[c]-=8192;
                        while ((int)sncount[c]<0)
                        {
                                sncount[c]+=snlatch[c];
                                snstat[c]++;
                                snstat[c]&=31;
                        }
                        c--;
                }
                if (!(snnoise&4)) buffer[d]+=(snperiodic[snstat[0]]*snvols[d>>1][0])>>6;
                else              buffer[d]+=(snnoises[snstat[0]]*snvols[d>>1][0])>>6;
                sncount[0]-=512;
                while ((int)sncount[0]<0)
                {
                        sncount[0]+=snlatch[0];
                        snstat[0]++;
                        snstat[0]&=32767;
                }
        }
//        fwrite(buffer,len,1,soundf);
        /*Sound filter emulation is actually always disabled. It doesn't work
          correctly, I was unable to find out how to actually emulate a low
          pass filter properly - the output sounds quite grainy*/
        memcpy(oldlast,lastbuffer,4);
        memcpy(lastbuffer,&buffer[len-4],4);
        if (soundfilter)
        {
                for (c=0;c<len;c++)
                {
                        buffer[c]>>=1;
                        if (!c) diff[c]=(oldlast[2]>>1)-(buffer[c]>>1);
                        else if (c==1) diff[c]=(oldlast[3]>>1)-(buffer[c]>>1);
                        else    diff[c]=(buffer[c-2]>>1)-(buffer[c]>>1);
                }
                for (c=0;c<len;c++) buffer[c]+=diff[c];
//                buffer[0]+=(oldlast[0]>>1);
//                buffer[1]+=(oldlast[1]>>1);
//                buffer[2]+=(oldlast[2]>>1);
//                buffer[3]+=(oldlast[3]>>1);
        }
}

void initsnd()
{
      int c;
      FILE *f;
/*      for (c=0;c<32;c++)
      {
                snwaves[3][c]=fsin((c<<2)<<16)>>8;
                printf("%i=%i\n",c,fsin((c<<2)<<16)>>8);
      }*/
//      if (soundon)
//      {
//        atexit(dumpsound);
//          soundf=fopen("sound.pcm","wb");
      reserve_voices(8,0);
      if (install_sound(DIGI_AUTODETECT,MIDI_NONE,0))
      {
                soundon=0;
                return;
      }
      f=fopen("sn76489.dat","rb");
      fread(snnoises,32768,1,f);
      fclose(f);
      for (c=0;c<NOISEBUFFER;c++)
          snnoises[c]=snnoises[c]*255;
      for (c=32;c<NOISEBUFFER;c++)
          snperiodic[c]=snperiodic2[c&31];
        as=play_audio_stream(624,8,0,31250,255,127);
/*            for (c=1;c<4;c++)
            {
                  snsample[c]=create_sample(8,0,320,32);
                  if (snsample[c])
                     memcpy(snsample[c]->data,snwaves[soundwave],32);
                  else
                     exit(-1);
//                  play_sample(snsample[c],(soundinited)?0:63,127,(SNCLOCK/0x3FF)*100,TRUE);
            }
                  snsample[0]=create_sample(8,0,160,NOISEBUFFER);
                  if (snsample[0])
                     memcpy(snsample[0]->data,snperiodic,NOISEBUFFER);
                  else
                     exit(-1);
//                  play_sample(snsample[0],(soundinited)?0:127,127,100*100,TRUE);*/
      snlatch[0]=snlatch[1]=snlatch[2]=snlatch[3]=0xFFFFF;
      soundinited=1;
//      }
//      slog=fopen("slog.pcm","wb");
}

void resetsound()
{
        int c;
        for (c=0;c<4;c++) stop_sample(snsample[c]);
        for (c=0;c<4;c++) play_sample(snsample[c],0,127,100*100,TRUE);
}

void closesnd()
{
        remove_sound();
}

int timesincelast=0;

void logsoundsomemore()
{
        float tempy;
        int c;
        int realfreq[4];
        int time[4];
        int state[4];
        unsigned char val;
        for (c=1;c<3;c++)
        {
                if (!snfreqlo[c] && !snfreqhi[c])
                   tempy=0;
                else
                   tempy=125000/(snfreqlo[c]|(snfreqhi[c]<<4));
                if (!tempy)
                   realfreq[c]=0;
                else
                   realfreq[c]=(int)((((double)31250.0/(double)tempy)/2.0)+0.5);
                state[c]=snvol[c]<<2;
                time[c]=0;
        }
        timesincelast>>=6;
        while (timesincelast)
        {
                val=0;
                for (c=1;c<3;c++)
                {
                        time[c]++;
                        if (time[c]>realfreq[c])
                        {
                                time[c]=0;
                                state[c]=-state[c];
                        }
                        val+=state[c];
                }
                putc(val,biglog);
                timesincelast--;
        }
}

unsigned char firstdat;
void soundwrite(unsigned char data)
{
      int freq;
      int c;
//      if (soundon)
//      {
//      if (biglog)
//         logsoundsomemore();
//      if ((data&0x90)!=0x90)
//      printf("Sound write %02X\n",data);
      if (data&0x80)
      {
                firstdat=data;
                switch (data&0x70)
                {
                        case 0:
                        snfreqlo[3]=data&0xF;
                        lasttone=3;
                        break;
                        case 0x10:
                        data&=0xF;
                        snvol[3]=0xF-data;
                        break;
                        case 0x20:
                        snfreqlo[2]=data&0xF;
                        lasttone=2;
                        break;
                        case 0x30:
                        data&=0xF;
                        snvol[2]=0xF-data;
                        break;
                        case 0x40:
                        snfreqlo[1]=data&0xF;
                        lasttone=1;
                        break;
                        case 0x50:
                        data&=0xF;
                        snvol[1]=0xF-data;
                        break;
                        case 0x60:
                        if ((data&3)!=(snnoise&3)) sncount[0]=0;
                        snnoise=data&0xF;
                        if ((data&3)==3)
                        {
                                curfreq[0]=curfreq[1]>>4;
                                snlatch[0]=snlatch[1];
//                                printf("SN 0 latch %04X\n",snlatch[0]);
                        }
                        else
                        {
                                switch (data&3)
                                {
                                        case 0:
                                        snlatch[0]=256<<7;
                                        curfreq[0]=SNCLOCK/256;
                                        snlatch[0]=0x800;
                                        sncount[0]=0;
                                        break;
                                        case 1:
                                        snlatch[0]=512<<7;
                                        curfreq[0]=SNCLOCK/512;
                                        snlatch[0]=0x1000;
                                        sncount[0]=0;
                                        break;
                                        case 2:
                                        snlatch[0]=1024<<7;
                                        curfreq[0]=SNCLOCK/1024;
                                        snlatch[0]=0x2000;
                                        sncount[0]=0;
                                        break;
                                        case 3:
                                        snlatch[0]=snlatch[1];
//                                        printf("SN 0 latch %04X\n",snlatch[0]);
                                        sncount[0]=0;
                                }
                        }
                        break;
                        case 0x70:
                        data&=0xF;
                        snvol[0]=0xF-data;
                        break;
                }
      }
      else
      {
                if ((firstdat&0x70)==0x60)
                {
                        if ((data&3)!=(snnoise&3)) sncount[0]=0;
                        snnoise=data&0xF;
                        if ((data&3)==3)
                        {
                                curfreq[0]=curfreq[1]>>4;
                                snlatch[0]=snlatch[1];
//                                printf("SN 0 latch %04X\n",snlatch[0]);
                        }
                        else
                        {
                                switch (data&3)
                                {
                                        case 0:
                                        snlatch[0]=256<<7;
                                        curfreq[0]=SNCLOCK/256;
                                        snlatch[0]=0x800;
                                        sncount[0]=0;
                                        break;
                                        case 1:
                                        snlatch[0]=512<<7;
                                        curfreq[0]=SNCLOCK/512;
                                        snlatch[0]=0x1000;
                                        sncount[0]=0;
                                        break;
                                        case 2:
                                        snlatch[0]=1024<<7;
                                        curfreq[0]=SNCLOCK/1024;
                                        snlatch[0]=0x2000;
                                        sncount[0]=0;
                                        break;
                                        case 3:
                                        snlatch[0]=snlatch[1];
//                                        printf("SN 0 latch %04X\n",snlatch[0]);
                                        sncount[0]=0;
                                }
                        }
                        return;
                }
            snfreqhi[lasttone]=data&0x3F;
            freq=snfreqlo[lasttone]|(snfreqhi[lasttone]<<4);
//            printf("Freq for channel %i now %04X\n",lasttone,freq);
            if (freq)
            {
                  c=(SNCLOCK/freq);
                  if (curfreq[lasttone]>9999 && c<10000)
                  {
//                        stop_sample(snsample[lasttone]);
//                        play_sample(snsample[lasttone],0,127,100*100,TRUE);
                  }
                  curfreq[lasttone]=c;
                  if ((snnoise&3)==3&&lasttone==1)
                  {
//                        curfreq[0]=curfreq[1]>>3;
                        snlatch[0]=freq<<6;
                        sncount[0]=0;
//                        printf("SN 0 latch %04X\n",snlatch[0]);
//                        adjust_sample(snsample[0],snvol[0]<<4,127,curfreq[0]*100,TRUE);
                  }
                  snlatch[lasttone]=freq<<6;
                  sncount[lasttone]=0;
//                  if (curfreq[lasttone]<10000)

//                     adjust_sample(snsample[lasttone],snvol[lasttone]<<4,127,(SNCLOCK/freq)*100,TRUE);
//                  else
//                     adjust_sample(snsample[lasttone],0,127,(SNCLOCK/freq)*100,TRUE);
            }
            else
            {
                  curfreq[lasttone]=0;
//                  adjust_sample(snsample[lasttone],0,127,10000,TRUE);
            }
      }
//      }
}

void startsnlog(char *fn)
{
        int c;
        if (snlog)
           fclose(snlog);
        logging=1;
        snlog=fopen(fn,"wb");
        putc('T',snlog);
        putc('I',snlog);
        putc('S',snlog);
        putc('N',snlog);
        if (us)
           putc(60,snlog);
        else
           putc(50,snlog);
        putc(1,snlog);

        putc(0,snlog);
        putc(9,snlog);
        putc(0x3D,snlog);
        for (c=0;c<7;c++)
            putc(0,snlog);
}

void stopsnlog()
{
        fclose(snlog);
        logging=0;
}

void logsound()
{
        putc(snfreqlo[1],snlog);
        putc(snfreqhi[1],snlog);
        putc(snvol[1],snlog);
        putc(snfreqlo[2],snlog);
        putc(snfreqhi[2],snlog);
        putc(snvol[2],snlog);
        putc(snfreqlo[3],snlog);
        putc(snfreqhi[3],snlog);
        putc(snvol[3],snlog);
        putc(snnoise,snlog);
        putc(snvol[0],snlog);
}

void dumpsound()
{
        FILE *f=fopen("sound.dmp","wb");
        fwrite(snsample[1]->data,32,1,f);
        fwrite(snsample[2]->data,32,1,f);
        fwrite(snsample[3]->data,32,1,f);
        fclose(f);
}

void mutesound()
{
        adjust_sample(snsample[0],0,127,curfreq[0]*100,TRUE);
        adjust_sample(snsample[1],0,127,curfreq[1]*100,TRUE);
        adjust_sample(snsample[2],0,127,curfreq[2]*100,TRUE);
        adjust_sample(snsample[3],0,127,curfreq[3]*100,TRUE);
}

void restoresound()
{
        adjust_sample(snsample[0],snvol[0]<<4,127,curfreq[0]*100,TRUE);
        adjust_sample(snsample[1],snvol[1]<<4,127,curfreq[1]*100,TRUE);
        adjust_sample(snsample[2],snvol[2]<<4,127,curfreq[2]*100,TRUE);
        adjust_sample(snsample[3],snvol[3]<<4,127,curfreq[3]*100,TRUE);
}
