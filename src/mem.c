/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.3


              All of this code is (C)opyright Tom Walker 1999
          You may use SMALL sections from this file (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*mem.c - Memory emulation*/

#include <dir.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gfx.h"
#include "6502.h"
#include "video.h"
#include "vias.h"
#include "mem.h"
#include "8271.h"

int us;
int tingtable[32768][2];
BMP *b;
int VideoULA_Palette[16];
int mode2;
int mode2table[256][2];
unsigned char ram[65536];
unsigned char roms[16][16384];

int Cycles;
void changerom(int newrom)
{
        currom=newrom;
        memcpy(ram+0x8000,roms[currom],0x4000);
}

void writemem(unsigned short address,unsigned char value)
{
        #ifdef MEMLOGFILE
        char s[40];
        sprintf(s,"memwrite %X %X %X\n",address,value,pc);
        fputs(s,logfile);
        #endif

        int c,d;
        char s[40];
        if (address > 0x8000)
        {
                if (address>0xFE0F&&address<0xFE20)
                {
                        writeserial(address,value);
                        return;
                }

                if (address>0xFE07&&address<0xFE10)
                {
                        writeacai(address,value);
                        return;
                }

                if ((address&~0x1F)==0xFEC0)
                {
                        writeadc(address,value);
                        return;
                }

                if ((address & ~0xf)==0xfe40 || (address & ~0xf)==0xfe50)
                {
                        SVIAWrite((address & 0xf),value);
                        return;
                }

                if ((address & ~0xf)==0xfe60 || (address & ~0xf)==0xfe70)
                {
                       UVIAWrite((address & 0xf),value);
                       return;
                }

                if ((address & ~0xf)==0xfe20)
                {
                        ULAWrite(address & 0xf, value);
                        return;
                }

                if ((address & ~0x15)==0xfe00)
                {
                        CRTCWrite(address & 0x1, value);
                        return;
                }

                if ((address&~0xF)==0xfe30)
                {
                        changerom(value & 0xf);
                        return;
                }

                if ((address&~0x1F)==0xFE80)
                {
                        write8271(address,value);
                        return;
                }
                return;
        }
        oldaddr=address;
        oldvalue=value;
        if (modela)
            ram[address&0x3FFF]=value;
        else
            ram[address]=value;
}

void initmem()
{
        int c;
        int romslot=0xF;
        FILE *f;
        char *cc=malloc(16384);
        struct ffblk ff;
        int finished=0;
        for (c=0;c<65536;c++)
            ram[c]=0;

        if (chdir("roms"))
           perror("roms");
        if (f=fopen((us)?"usos":"os","rb"),f==NULL)
        {
                printf("Could not open OS rom file\n");
                printf("Make sure that is is called 'os' and\n");
                printf("in the roms directory\n");
                exit(-1);
        }
        if (fread(ram+0xC000,1,16384,f)!=16384) {
                printf("Could not read OS\n");
                exit(-1);
        }
        fclose(f);

        findfirst("*.rom",&ff,0);
        while (romslot >= 0 && !finished)
        {
                f=fopen(ff.ff_name,"rb");
                fread(cc,16384,1,f);
                fclose(f);
                memcpy(roms[romslot],cc,16384);
                romslot--;
                finished = findnext(&ff);
        }
        if (romslot==0xf)
        {
                printf("Error : No ROMs found\n");
                exit(-1);
        }
        currom=0xF;
        memcpy(ram+0x8000,roms[0xF],0x4000);
        fclose(f);
        if (chdir(".."))
           perror("..");
}
