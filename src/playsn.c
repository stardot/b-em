#include <allegro.h>
#include <stdio.h>

int freqlo[3],freqhi[3],vol[4],noise;
int freq[4];

FILE *sn;
int speed,clock,delay;

SAMPLE *square[3];
SAMPLE *noisesmp;

#define NOISEBUFFER 4096

static unsigned char snperiodic[NOISEBUFFER] =
{
      0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

static unsigned char snnoises[NOISEBUFFER];

static unsigned char squarewave[32]=
{
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

void loadsn(char *fn)
{
        char magicst[4]={'T','I','S','N'};
        int c;
        sn=fopen(fn,"rb");
        for (c=0;c<4;c++)
        {
                if (getc(sn)!=magicst[c])
                {
                        printf("Not an SN file\n");
                        exit(-1);
                }
        }
        speed=getc(sn);
        if (getc(sn)!=1)
        {
                fclose(sn);
                printf("%s uses more than one sound chip. PLAYSN only supports one\n",fn);
                exit(-1);
        }
        clock=getc(sn);
        clock|=(getc(sn)<<8);
        clock|=(getc(sn)<<16);
        getc(sn);
        for (c=0;c<6;c++)
            getc(sn);
        clock>>=5;
        delay=1000/speed;
}

void startplaying()
{
        int c;
        allegro_init();
        install_sound(DIGI_AUTODETECT,MIDI_NONE,0);
        for (c=0;c<NOISEBUFFER;c++)
            snnoises[c]=random()&255;
        for (c=32;c<NOISEBUFFER;c++)
            snperiodic[c]=snperiodic[c&31];
        for (c=0;c<3;c++)
        {
                square[c]=create_sample(8,0,320,32);
                if (square[c])
                   memcpy(square[c]->data,squarewave,32);
                play_sample(square[c],0,127,100,TRUE);
        }
        noisesmp=create_sample(8,0,160,NOISEBUFFER);
        if (noisesmp)
           memcpy(noisesmp->data,snperiodic,NOISEBUFFER);
        play_sample(noisesmp,0,127,100,TRUE);
}

void updatesn()
{
readagain:
        freqlo[0]=getc(sn);
        freqhi[0]=getc(sn);
        vol[0]=getc(sn)<<4;
        freqlo[1]=getc(sn);
        freqhi[1]=getc(sn);
        vol[1]=getc(sn)<<4;
        freqlo[2]=getc(sn);
        freqhi[2]=getc(sn);
        vol[2]=getc(sn)<<4;
        noise=getc(sn);
        vol[3]=getc(sn)<<4;
        if (feof(sn))
        {
                fseek(sn,16,SEEK_SET);
                goto readagain;
        }
        freq[0]=freqlo[0]|(freqhi[0]<<4);
        freq[0]=clock/freq[0];
        freq[1]=freqlo[1]|(freqhi[1]<<4);
        freq[1]=clock/freq[1];
        freq[2]=freqlo[2]|(freqhi[2]<<4);
        freq[2]=clock/freq[2];
        switch (noise&3)
        {
                case 0:
                freq[3]=1000;
                break;

                case 1:
                freq[3]=500;
                break;

                case 2:
                freq[3]=250;
                break;

                case 3:
                freq[3]=freq[0];
                break;
        }
        if (!(noise&4))
        {
                memcpy(noisesmp->data,snperiodic,NOISEBUFFER);
                freq[3]>>=3;
        }
        else
           memcpy(noisesmp->data,snnoises,NOISEBUFFER);

        adjust_sample(square[0],vol[0],127,freq[0]*100,TRUE);
        adjust_sample(square[1],vol[1],127,freq[1]*100,TRUE);
        adjust_sample(square[2],vol[2],127,freq[2]*100,TRUE);
        adjust_sample(noisesmp,vol[3],127,freq[3]*100,TRUE);
}

int main(int argc, char *argv[])
{
        int thing;
        if (argc<2)
        {
                printf("Syntax : playsn chewn.sn\n");
                exit(-1);
        }
        loadsn(argv[1]);
        startplaying();
        install_timer();
        while (!kbhit())
        {
                updatesn();
                rest(delay);
        }
        return 0;
}
