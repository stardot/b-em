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

/*Sound emulation*/

#include <allegro.h>
#include <stdio.h>
#include <go32.h>
#include <pc.h>
#include "sound.h"

#define SNCLOCK (4000000>>5)

#define NOISEBUFFER 1024

int us;
int logging=0;
FILE *snlog;
unsigned char snfreqhi[4],snfreqlo[4];
unsigned char snvol[4];
unsigned char snnoise;
int curfreq[4];
int lasttone;
int soundon;

static unsigned char snsamples[32] =    /* a simple sine (sort of) wave */
{
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

int soundinited=0;

static unsigned char snperiodic[NOISEBUFFER] =
{
      0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static unsigned char snnoises[NOISEBUFFER];

SAMPLE *snsample[4];

void initsnd()
{
      int c;
      install_sound(DIGI_AUTODETECT,MIDI_NONE,0);
      reserve_voices(8,0);
      for (c=0;c<NOISEBUFFER;c++)
          snnoises[c]=(random()&128)?255:0/*random()&255*/;
      for (c=32;c<NOISEBUFFER;c++)
          snperiodic[c]=snperiodic[c&31];
            for (c=1;c<4;c++)
            {
                  snsample[c]=create_sample(8,0,320,32);
                  if (snsample[c])
                     memcpy(snsample[c]->data,snsamples,32);
                  else
                     exit(-1);
                  play_sample(snsample[c],(soundinited)?0:63,127,(SNCLOCK/0x3FF)*100,TRUE);
            }
                  snsample[0]=create_sample(8,0,160,NOISEBUFFER);
                  if (snsample[0])
                     memcpy(snsample[0]->data,snperiodic,NOISEBUFFER);
                  else
                     exit(-1);
                  play_sample(snsample[0],(soundinited)?0:127,127,100*100,TRUE);
      soundinited=1;
}

void soundwrite(unsigned char data)
{
      int freq;
      int c;
      if (soundon)
      {
      if (data&0x80)
      {
            switch (data&0x70)
            {
                  case 0:
                  snfreqlo[3]=data&0xF;
                  lasttone=3;
                  break;
                  case 0x10:
                  data&=0xF;
                  snvol[3]=0xF-data;
                  adjust_sample(snsample[3],snvol[3]<<4,127,curfreq[3]*100,TRUE);
                  break;
                  case 0x20:
                  snfreqlo[2]=data&0xF;
                  lasttone=2;
                  break;
                  case 0x30:
                  data&=0xF;
                  snvol[2]=0xF-data;
                  adjust_sample(snsample[2],snvol[2]<<4,127,curfreq[2]*100,TRUE);
                  break;
                  case 0x40:
                  snfreqlo[1]=data&0xF;
                  lasttone=1;
                  break;
                  case 0x50:
                  data&=0xF;
                  snvol[1]=0xF-data;
                  adjust_sample(snsample[1],snvol[1]<<4,127,curfreq[1]*100,TRUE);
                  break;
                  case 0x60:
                  snnoise=data&0xF;
                  if ((data&3)==3)
                     curfreq[0]=curfreq[1]>>3;
                  else
                  {
                        switch (data&3)
                        {
                              case 0:
                              curfreq[0]=1000;
                              break;
                              case 1:
                              curfreq[0]=500;
                              break;
                              case 2:
                              curfreq[0]=250;
                              break;
                        }
                  }
                  if (!(data&4))
                     memcpy(snsample[0]->data,snperiodic,NOISEBUFFER);
                  else
                     memcpy(snsample[0]->data,snnoises,NOISEBUFFER);
                  adjust_sample(snsample[0],snvol[0]<<4,127,curfreq[0]*100,TRUE);
                  break;
                  case 0x70:
                  data&=0xF;
                  snvol[0]=0xF-data;
                  adjust_sample(snsample[0],snvol[0]<<4,127,curfreq[0]*100,TRUE);
                  break;
            }
      }
      else
      {
            snfreqhi[lasttone]=data&0x3F;
            freq=snfreqlo[lasttone]|(snfreqhi[lasttone]<<4);
            if (freq)
            {
                  curfreq[lasttone]=(SNCLOCK/freq);
                  if ((snnoise&3)==3&&lasttone==1)
                  {
                        curfreq[0]=curfreq[1]>>3;
                        adjust_sample(snsample[0],snvol[0]<<4,127,curfreq[0]*100,TRUE);
                  }
                  adjust_sample(snsample[lasttone],snvol[lasttone]<<4,127,(SNCLOCK/freq)*100,TRUE);
            }
            else
            {
                  curfreq[lasttone]=0;
                  adjust_sample(snsample[lasttone],0,127,0,TRUE);
            }
      }
      }
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
