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

/*Sound emulation*/

#include "gfx.h"
#include <stdio.h>
#include <go32.h>
#include <pc.h>
#include "sound.h"

int noiseFB;
int freq[4],vol[4];
int actualfreq[4];
int lasttone;
int currvolpos=0;
unsigned char volpos[4096],volcycle[4096],volchan[4096];

void dosound()
{
        int c,d;
        int time=numcycles/90; /*Roughly 8khz samples*/
        int state[4]={0,0,0,0};
        int timer[4]={0,0,0,0};
        int noisefreq[4]={0x1FF,0x2FF,0x3FF,freq[1]};
        unsigned char temp;
        int tempy;
        char buffer[65536];
        numcycles=0;
        for (c=1;c<4;c++)
        {
                if (freq[c])
                {
                        tempy=(4000000/(32*freq[c]));
                        actualfreq[c]=(int)((((double)22050.0/(double)tempy)/2.0)+0.5);
                }
        }
        #if 0
        numcycles=0;
        for (c=1;c<4;c++)
        {
                if (freq[c])
                   actualfreq[c]=(22050/(4000000/(32*freq[c])));//22050/(125000/freq[c]);
                else
                   actualfreq[c]=0;
        }
        #endif
/*        if (noisefreq[freq[0]&3])
           actualfreq[0]=22050/(125000 / noisefreq[freq[0]&3]);
        else
           actualfreq[0]=0;*/
        state[1]=(vol[1]&127)>>2;
        state[2]=(vol[2]&127)>>2;
        state[3]=(vol[3]&127)>>2;
        if (!noiseFB)
           state[0]=(vol[0]&127)>>1;
        else
           state[0]=(vol[0]*((random()&63)-32))>>7;
        while (time>0)
        {
        for (c=0;c<65536;c++)
        {
                #if 0
                temp=/*state[0]+*/state[1]+state[2]+state[3];
                buffer[c]=temp;
                for (d=0;d<4;d++)
                {
                        timer[d]++;
                        if (timer[d]==actualfreq[d] && !(d==0&&!noiseFB))
                        {
                                state[d]=-state[d];
                                if (d==0 && noiseFB)
                                   state[0]=(vol[0]*((random()&63)-32))>>7;
                                timer[d]=0;
                        }
                        else if (d==0&&!noiseFB)
                        {
                                if (timer[d]==(actualfreq[d]>>3))
                                   state[d]=-state[d];
                                if (timer[d]>actualfreq[d])
                                {
                                        state[d]=-state[d];
                                        timer[d]=0;
                                }
                        }
                #endif
                temp=state[1]+state[2]+state[3];
                buffer[c]=temp;
                for (d=1;d<4;d++)
                {
                        timer[d]++;
                        if (timer[d]==actualfreq[d])
                        {
                                timer[d]=0;
                                state[d]=-state[d];
                        }
                }
        }
        fwrite(buffer,(time>65536)?65536:time,1,soundfile);
        time-=65536;
        }

}

int mix(unsigned long buffer, unsigned long length)
{
        char tempbuffer[length];
        char buff2[length];
        int c,d,e=0;
        int time=length;
        int state[4]={0,0,0,0};
        int timer[4]={0,0,0,0};
        int noisefreq[4]={0xFF,0x1FF,0x3FF,freq[1]};
        char temp;
        currvolpos=0;
        for (c=0;c<length;c+=4)
        {
                tempbuffer[c]=tempbuffer[c+2]=0x80;
                tempbuffer[c+1]=tempbuffer[c+3]=0;
        }
        numcycles=0;
        for (c=1;c<4;c++)
        {
                if (freq[c])
                   actualfreq[c]=44100/(125000/freq[c]);
                else
                   actualfreq[c]=0;
        }
        if (noisefreq[freq[0]&3])
        {
                actualfreq[0]=11025/(125000 / noisefreq[freq[0]&3]);
                actualfreq[0]>>=2;
        }
        else
           actualfreq[0]=0;
        state[1]=(vol[1])>>2;
        state[2]=(vol[2])>>2;
        state[3]=(vol[3])>>2;
        if (!noiseFB)
           state[0]=(vol[0]&127)>>2;
        else
           state[0]=(vol[0]*((random()&63)-32))>>7;
        for (c=0;c<length;c++)
        {
                temp=state[0]+state[1]+state[2]+state[3];
                if (freq[0]==3)
                   temp-=state[1];
                tempbuffer[c]=temp;
                if (volcycle[e]==(length*90))
                {
                        if (volchan[e]>0)
                        {
                                state[volchan[e]&3]=(volpos[e])>>2;
                                e++;
                        }
                }
                for (d=0;d<4;d++)
                {
                        timer[d]++;
                        if (timer[d]==actualfreq[d] && !(d==0&&!noiseFB))
                        {
                                state[d]=-state[d];
                                if (d==0 && noiseFB)
                                   state[0]=(vol[0]*((random()&63)-32))>>7;
                                timer[d]=0;
                        }
                        else if (d==0&&!noiseFB)
                        {
                                if (timer[d]==(actualfreq[d]))
                                   state[d]=-state[d];
                                if (timer[d]>(actualfreq[d]<<3))
                                {
                                        state[d]=-state[d];
                                        timer[d]=0;
                                }
                        }
                }
        }

        _oldseg=_dos_ds;
        _farsetsel(_dos_ds);
        for (c=0;c<length;c++)
        {
                _farnspokeb(buffer+c,tempbuffer[c]^0x80);
        }
        memset(volpos,0,4096);
        memset(volcycle,0,4096);
        memset(volchan,0,4096);
        return 0;
}

SAMPLE *sine[3];
void initsnd()
{
        int c;
        for (c=0;c<4;c++)
        {
                freq[c]=0x2FF;
                vol[c]=0;
        }
        freq[1]=0x3FF;
        vol[1]=15;
        freq[2]=0x3FF;
        vol[2]=15;
        freq[3]=0x3FF;
        vol[3]=15;
        lasttone=0;
        noiseFB=0;
}

//8C
//11
//90
void soundwrite(unsigned char val)
{
        unsigned char temp;
        if (val&0x80)
        {
                switch ((val>>4) & 0x7)
                {
                        case 0: /*Tone 3 freq*/
                        freq[3]=(freq[3] & 0x2f0) | (val & 0xf);
                        if (!freq[3])
                           freq[3]=1;
                        lasttone=3;
                        break;

                        case 1: /*Tone 3 vol */
                        if (logsound)
                           dosound();
                        vol[3]=(15-(val & 15))<<3;
                        if (!freq[3])
                           freq[3]=1;
                        if (currvolpos<4096)
                        {
                                volpos[currvolpos]=vol[3];
                                volcycle[currvolpos]=numcycles;
                                volchan[currvolpos]=3;
                                currvolpos++;
                        }
                        if (soundon)
                           adjustsample(sine[2],(vol[3]*4)|16,127,((4000000 / (32 * freq[3])))*1000);
                        lasttone=3;
                        break;

                        case 2: /*Tone 2 freq*/
                        freq[2]=(freq[2] & 0x2f0) | (val & 0xf);
                        if (!freq[2])
                           freq[2]=1;
                        lasttone=2;
                        break;

                        case 3: /*Tone 2 vol */
                        if (logsound)
                           dosound();
                        vol[2]=(15-(val & 15))<<3;
                        if (!freq[2])
                           freq[2]=1;
                        if (currvolpos<4096)
                        {
                                volpos[currvolpos]=vol[2];
                                volcycle[currvolpos]=numcycles;
                                volchan[currvolpos]=2;
                                currvolpos++;
                        }
                        if (soundon)
                           adjustsample(sine[1],(vol[2]*4)|16,127,((4000000 / (32 * freq[2])))*1000);
                        lasttone=2;
                        break;

                        case 4: /*Tone 1 freq*/
                        freq[1]=(freq[1] & 0x2f0) | (val & 0xf);
                        if (!freq[1])
                           freq[1]=1;
                        lasttone=1;
                        break;

                        case 5: /*Tone 1 vol */
                        if (logsound)
                           dosound();
                        vol[1]=(15-(val & 15))<<3;
                        if (!freq[1])
                           freq[1]=1;
                        if (currvolpos<4096)
                        {
                                volpos[currvolpos]=vol[1];
                                volcycle[currvolpos]=numcycles;
                                volchan[currvolpos]=1;
                                currvolpos++;
                        }
                        if (soundon)
                           adjustsample(sine[0],(vol[1]*4)|16,127,((4000000 / (32 * freq[1])))*1000);
                        lasttone=1;
                        break;

                        case 6: /*Noise control*/
                        if (logsound)
                           dosound();
                        freq[0]=val & 3;
                        noiseFB=(val>>2)&1;
                        break;

                        case 7: /*Noise vol */
                        if (logsound)
                           dosound();
                        vol[0]=(15-(val & 15))<<3;
                        if (currvolpos<4096)
                        {
                                volpos[currvolpos]=vol[0];
                                volcycle[currvolpos]=numcycles;
                                volchan[currvolpos]=0;
                                currvolpos++;
                        }
                        break;
                }
        }
        else
        {
                if (logsound)
                   dosound();
                temp=freq[lasttone] & 15;
                temp |= (val & 0x3f)<<4;
                freq[lasttone]=temp;
        }
}
