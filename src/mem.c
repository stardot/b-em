/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.4a


              All of this code is written by Tom Walker
          You may use SMALL sections from this file (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*Memory emulation*/

#include <dir.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <allegro.h>
#include "6502.h"
#include "video.h"
#include "vias.h"
#include "mem.h"
#include "8271.h"

int writeable[16];
int writeablerom=0;
int us;
int tingtable[32768][2];
BITMAP *b;
int VideoULA_Palette[16];
int mode2;
int mode2table[256][2];
unsigned char ram[65536];
unsigned char roms[16][16384];

int Cycles;
void changerom(int newrom)
{
        if (writeablerom)
           memcpy(roms[currom],ram+0x8000,0x4000);
        currom=newrom;
        writeablerom=writeable[currom];
        memcpy(ram+0x8000,roms[currom],0x4000);
}

void writemem(unsigned short address,unsigned char value)
{
        if (address > 0x8000)
        {
                if ((address<0xC000)&&writeablerom)
                {
                     ram[address]=value;
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

                if ((address & ~0xf)==0xfe60 || (address & ~0xf)==0xfe70 && !modela)
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
        for (currom=0;currom<16;currom++)
            writeable[currom]=0;
        writeable[0]=writeable[1]=1;
        currom=0xF;
        writeablerom=0;
        memcpy(ram+0x8000,roms[0xF],0x4000);
        fclose(f);
        if (chdir(".."))
           perror("..");
}
