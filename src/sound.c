/*B-em 1.3 by Tom Walker*/
/*Sound emulation*/

#define NOISEBUFFER 32768
static signed char snperiodic[2][NOISEBUFFER];
int rectpos=0,rectdir=0;
int vgmsamples;
int soundfilter=0;
void dumpsound();
#include <allegro.h>
#include <stdio.h>
#include "sound.h"

int soundbuflen;
float volslog[16]=
{
        0.00000f,0.59715f,0.75180f,0.94650f,
        1.19145f,1.50000f,1.88835f,2.37735f,
        2.99295f,3.76785f,4.74345f,5.97165f,
        7.51785f,9.46440f,11.9194f,15.0000f
};

FILE *soundf;
//FILE *slog;
AUDIOSTREAM *as;
#define SNCLOCK (4000000>>5)


SAMPLE *snsample[4];
FILE *biglog;

int soundwave;
int us;
int logging=0;
FILE *snlog,*snlog2;
unsigned char snfreqhi[4],snfreqlo[4];
unsigned char snvol[4];
unsigned char snnoise;
int curfreq[4];
int lasttone;
int soundon=1;
int curwave=0;

signed short snwaves[5][32]=
{
        {
                127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
                -127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127,-127

        },
        {
                -120,-112,-104,-96,-88,-80,-72,-64,-56,-48,-40,-32,-24,-16,-8,0,
                8,16,24,32,40,48,56,64,72,80,88,96,104,112,120,127
        },
        {
                8,16,24,32,40,48,56,64,72,80,88,96,104,112,120,127,
                120,112,104,96,88,80,72,64,56,48,40,32,24,16,8,0
        },
        {
                0x00,0x19,0x31,0x4A,0x61,0x78,0x8E,0xA2,0xB5,0xC5,0xD4,0xE1,0xEC,0xF4,0xFB,0xFE,
                0xFF,0xFE,0xFB,0xF4,0xEC,0xE1,0xD4,0xC5,0xB5,0xA2,0x8E,0x78,0x61,0x4A,0x31,0x19,
        }
};

void updaterectwave(int d)
{
        int c;
        c>>=3;
        for (c=0;c<d;c++) snwaves[4][c]=snperiodic[1][c]=127;
        for (;c<32;c++)   snwaves[4][c]=snperiodic[1][c]=-127;
}

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
int soundiniteded;
static signed char snperiodic[2][NOISEBUFFER];
static signed char snperiodic2[32] =
{
      -127,-127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
       127, 127,127,127,127,127,127,127,127,127,127,127,127,127,127,127
};

static unsigned char snnoises[NOISEBUFFER];

fixed sncount[4],snlatch[4],snstat[4];
int sntone[4];
int snvols[3125<<1][4],snnoise2[3125<<1];
fixed snlatchs[3125<<1][4];
int snline=0,snlinec=0;

void logvols()
{
        int c;
        for (c=0;c<2;c++)
        {
        snvols[snline][0]=snvol[0];
        snvols[snline][1]=snvol[1];
        snvols[snline][2]=snvol[2];
        snvols[snline][3]=snvol[3];
        snlatchs[snline][0]=snlatch[0];
        snlatchs[snline][1]=snlatch[1];
        snlatchs[snline][2]=snlatch[2];
        snlatchs[snline][3]=snlatch[3];
        snnoise2[snline]=snnoise;
        snline++;
  //      rpclog("Log %i\n",snline);
        }
//        return;
//        if (snline>=(soundbuflen>>1)) snline=0;
        snlinec++;
        if (snlinec==312)
        {
                snvols[snline][0]=snvol[0];
                snvols[snline][1]=snvol[1];
                snvols[snline][2]=snvol[2];
                snvols[snline][3]=snvol[3];
                snlatchs[snline][0]=snlatch[0];
                snlatchs[snline][1]=snlatch[1];
                snlatchs[snline][2]=snlatch[2];
                snlatchs[snline][3]=snlatch[3];
                snnoise2[snline]=snnoise;
                snlinec=0;
                snline++;
//        rpclog("Log2 %i\n",snline);
        }
        if (snline>=3125) snline=0;
//        rpclog("SNline now %i\n",snline);
}

void drawsound(BITMAP *b)
{
//        textprintf_ex(b,font,0,16,7,0,"%i %04X",snlatch[1],snlatch[1]);
//        textprintf_ex(b,font,0,24,7,0,"%i %04X",snlatch[2],snlatch[2]);
//        textprintf_ex(b,font,0,32,7,0,"%i %04X",snlatch[3],snlatch[3]);
}

signed short lastbuffer[4]={0,0,0,0};

void updatebuffer(signed short *buffer, int len)
{
        int c,d,diff[1024];
        unsigned char oldlast[4];
        float tempf,tempf2;
        unsigned short *sbuf=buffer;
        int sidcount=0;
        int lcount=0;
//        rpclog("update\n");
        for (d=0;d<len;d++)
        {
//                rpclog("Update pos %i %i\n",d,len);
//                printf("Latch : %i %i %i\n",snlatch[1],snlatch[2],snlatch[3]);
/*                lcount++;
                if (lcount==625)
                {
                        lcount=0;
                        buffer[d]=buffer[d-1];
                }
                else
                {*/
                buffer[d]=0;
                for (c=0;c<3;c++)
                {
                        c++;
                        if (snlatchs[d][c]>256) buffer[d]+=(snwaves[curwave][snstat[c]]*volslog[snvols[d][c]]);
                        else                    buffer[d]+=volslog[snvols[d][c]]*128;
                        sncount[c]-=8192;
                        while ((int)sncount[c]<0  && snlatchs[d][c])
                        {
                                sncount[c]+=snlatchs[d][c];
                                snstat[c]++;
                                snstat[c]&=31;
                        }
                        c--;
                }
                if (!(snnoise2[d]&4))
                {
                        if (curwave==4) buffer[d]+=(snperiodic[1][snstat[0]&31]*volslog[snvols[d][0]]);
                        else            buffer[d]+=(snperiodic[0][snstat[0]]*volslog[snvols[d][0]]);
                }
                else              buffer[d]+=(snnoises[snstat[0]]*volslog[snvols[d][0]]);
                sncount[0]-=512;
                while ((int)sncount[0]<0 && snlatchs[d][0])
                {
                        sncount[0]+=snlatchs[d][0];
                        snstat[0]++;
//                        if (snstat[0]==30) snstat[0]=0;
//                        snstat[0]&=32767;
                }
                if (!(snnoise2[d]&4))
                {
                        while (snstat[0]>=30) snstat[0]-=30;
                }
                else
                   snstat[0]&=32767;
                tempf=tempf2=0;
                if (soundfilter&1) tempf+=(float)lastbuffer[0]*((float)11/(float)16);
                if (soundfilter&1) buffer[d]+=tempf;
                lastbuffer[1]=lastbuffer[0];
                lastbuffer[0]=buffer[d];
                if (soundfilter&2) tempf2-=(float)lastbuffer[2]*((float)7/(float)16);
                if (soundfilter&2) buffer[d]+=tempf2;
                lastbuffer[3]=lastbuffer[2];
                lastbuffer[2]=buffer[d];
                sidcount++;
                if (sidcount==624)
                {
                        sidcount=0;
                        if (!rectdir)
                        {
                                rectpos++;
                                if (rectpos==30) rectdir=1;
                        }
                        else
                        {
                                rectpos--;
                                if (rectpos==1) rectdir=0;
                        }
                        updaterectwave(rectpos);
                }
//                }
        }
        for (d=0;d<len;d++)
        {
//                rpclog("Flipping %i %i\n",d,len);
                sbuf[d]^=0x8000;
        }
//        snline=0;
}

void initsnd()
{
      int c;
      FILE *f;
      unsigned short *p;
/*      for (c=0;c<32;c++)
      {
                snwaves[3][c]=fsin((c<<2)<<16)>>8;
                printf("%i=%i\n",c,fsin((c<<2)<<16)>>8);
      }*/
//      if (soundon)
//      {
//        atexit(dumpsound);
//          soundf=fopen("sound.pcm","wb");
      reserve_voices(4,0);
      if (install_sound(DIGI_DIRECTX(0),MIDI_NONE,0))
      {
                if (install_sound(DIGI_AUTODETECT,MIDI_NONE,0))
                {
                        soundon=0;
                        return;
                }
      }
      soundiniteded=1;
      f=fopen("sn76489.dat","rb");
      fread(snnoises,32768,1,f);
      fclose(f);
      for (c=0;c<NOISEBUFFER;c++)
          snnoises[c]=snnoises[c]*255;
      for (c=0;c<NOISEBUFFER;c++)
          snperiodic[0][c]=snperiodic2[c&31];
      for (c=0;c<32;c++)
          snwaves[3][c]-=128;
        as=play_audio_stream(soundbuflen,16,0,31250,255,127);
                        p=0;
                        while (!p)
                        {
                                p=(unsigned short *)get_audio_stream_buffer(as);
                                yield_timeslice();
                        }
     for (c=0;c<soundbuflen;c++) p[c]=0x8000;
     free_audio_stream_buffer(as);

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
      snlatch[0]=snlatch[1]=snlatch[2]=snlatch[3]=0x3FF<<6;
      snvol[0]=snvol[1]=snvol[2]=snvol[3]=8;
      sncount[0]=sncount[1]=sncount[2]=sncount[3]=0;
      snnoise=3;
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

unsigned char vgmdat[1024]; /*Data for VGM writing*/
int vgmpos=0;
unsigned char firstdat;
unsigned char lastdat;
void soundwrite(unsigned char data)
{
        int freq;
        int c;
        if (logging && vgmpos!=1024 && data!=lastdat)
        {
                vgmdat[vgmpos++]=0x50;
                vgmdat[vgmpos++]=data;
        }
        lastdat=data;
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
//                        printf("Vol 3 %i\n",snvol[3]);
                        break;
                        case 0x20:
                        snfreqlo[2]=data&0xF;
                        lasttone=2;
                        break;
                        case 0x30:
                        data&=0xF;
                        snvol[2]=0xF-data;
//                        printf("Vol 2 %i\n",snvol[2]);
                        break;
                        case 0x40:
                        snfreqlo[1]=data&0xF;
                        lasttone=1;
                        break;
                        case 0x50:
                        data&=0xF;
                        snvol[1]=0xF-data;
//                        printf("Vol 1 %i\n",snvol[1]);
                        break;
                        case 0x60:
//                        printf("SNNOISE %01X\n",data&0xF);
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
                                        snlatch[0]=128<<7;
                                        curfreq[0]=SNCLOCK/256;
                                        snlatch[0]=0x400;
                                        sncount[0]=0;
                                        break;
                                        case 1:
                                        snlatch[0]=256<<7;
                                        curfreq[0]=SNCLOCK/512;
                                        snlatch[0]=0x800;
                                        sncount[0]=0;
                                        break;
                                        case 2:
                                        snlatch[0]=512<<7;
                                        curfreq[0]=SNCLOCK/1024;
                                        snlatch[0]=0x1000;
                                        sncount[0]=0;
                                        break;
                                        case 3:
                                        snlatch[0]=snlatch[1];
//                                        printf("SN 0 latch %04X\n",snlatch[0]);
                                        sncount[0]=0;
                                }
                                if (snnoise&4) snlatch[0]<<=1;
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
                                        snlatch[0]=128<<7;
                                        curfreq[0]=SNCLOCK/256;
                                        snlatch[0]=0x400;
                                        sncount[0]=0;
                                        break;
                                        case 1:
                                        snlatch[0]=256<<7;
                                        curfreq[0]=SNCLOCK/512;
                                        snlatch[0]=0x800;
                                        sncount[0]=0;
                                        break;
                                        case 2:
                                        snlatch[0]=512<<7;
                                        curfreq[0]=SNCLOCK/1024;
                                        snlatch[0]=0x1000;
                                        sncount[0]=0;
                                        break;
                                        case 3:
                                        snlatch[0]=snlatch[1];
//                                        printf("SN 0 latch %04X\n",snlatch[0]);
                                        sncount[0]=0;
                                }
                                if (snnoise&4) snlatch[0]<<=1;
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
        if (snlog2)
           fclose(snlog2);
        vgmsamples=vgmpos=0;
        logging=1;
        snlog=fopen("temp.vgm","wb");
        snlog2=fopen(fn,"wb");
        putc('V',snlog);
        putc('g',snlog);
        putc('m',snlog);
        putc(' ',snlog);
        /*We don't know file length yet so just store 0*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*Version number - 1.50*/
        putc(0x50,snlog); putc(1,snlog); putc(0,snlog); putc(0,snlog);
        /*Clock speed - 4mhz*/
        putc(4000000&255,snlog);
        putc(4000000>>8,snlog);
        putc(4000000>>16,snlog);
        putc(4000000>>24,snlog);
        /*We don't have an FM chip*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*We don't have an GD3 tag*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*We don't know total samples*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*No looping*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*50hz. This is true even in NTSC mode as the sound log is always updated at 50hz*/
        putc(50,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*White noise feedback pattern & length*/
        putc(3,snlog); putc(0,snlog); putc(15,snlog); putc(0,snlog);
        /*We don't have an FM chip*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*We don't have an FM chip*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*Data offset*/
        putc(0xC,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        /*Reserved*/
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
        putc(0,snlog); putc(0,snlog); putc(0,snlog); putc(0,snlog);
}

void stopsnlog()
{
        int c,len;
        unsigned char buffer[32];
        putc(0x66,snlog);
        len=ftell(snlog);
        fclose(snlog);
        snlog=fopen("temp.vgm","rb");
        for (c=0;c<4;c++) putc(getc(snlog),snlog2);
        putc(len,snlog2);
        putc(len>>8,snlog2);
        putc(len>>16,snlog2);
        putc(len>>24,snlog2);
        for (c=0;c<4;c++) getc(snlog);
        for (c=0;c<16;c++) putc(getc(snlog),snlog2);
        putc(vgmsamples,snlog2);
        putc(vgmsamples>>8,snlog2);
        putc(vgmsamples>>16,snlog2);
        putc(vgmsamples>>24,snlog2);
        for (c=0;c<4;c++) getc(snlog);
        while (!feof(snlog))
        {
                putc(getc(snlog),snlog2);
        }
        fclose(snlog2);
        fclose(snlog);
        remove("temp.vgm");
//        printf("%08X samples\n",vgmsamples);
        logging=0;
}

void logsound()
{
        if (vgmpos) fwrite(vgmdat,vgmpos,1,snlog);
        putc(0x63,snlog);
        vgmsamples+=882;
        vgmpos=0;
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

void savesoundstate(FILE *f)
{
        fwrite(snlatch,16,1,f);
        fwrite(sncount,16,1,f);
        fwrite(snstat,16,1,f);
        fwrite(snvol,4,1,f);
        putc(snnoise,f);
}

void loadsoundstate(FILE *f)
{
        fread(snlatch,16,1,f);
        fread(sncount,16,1,f);
        fread(snstat,16,1,f);
        fread(snvol,4,1,f);
        snnoise=getc(f);
}
