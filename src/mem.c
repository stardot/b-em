/*B-em v2.0 by Tom Walker
  ROM handling*/

#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"

uint8_t *ram,*rom,*os;
int romused[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
extern int romsel;

void initmem()
{
        ram=(uint8_t *)malloc(128*1024);
        rom=(uint8_t *)malloc(16*16384);
        os=(uint8_t *)malloc(16384);
        memset(ram,0,128*1024);
}

void resetmem()
{
        memset(romused,0,16);
        memset(swram,0,16);
        memset(ram,0,128*1024);
        memset(rom,0,16*16384);
}

void closemem()
{
        free(ram);
        free(rom);
        free(os);
}

void dumpram()
{
        FILE *f=fopen("ram.dmp","wb");
        fwrite(ram,128*1024,1,f);
        fclose(f);
        f=fopen("swram.dmp","wb");
        fwrite(&rom[romsel],16384,1,f);
        fclose(f);
}

void loadswroms()
{
        FILE *f;
        int c=15;
        struct al_ffblk ffblk;
//        memset(rom,0,16*16384);

        if (al_findfirst("*.rom",&ffblk,FA_ALL)!=0) return;
        do
        {
                while (romused[c] && c>=0) c--;
                if (c>=0)
                {
                        printf("Loading %s to slot %i\n",ffblk.name,c);
                        f=fopen(ffblk.name,"rb");
                        fread(rom+(c*16384),16384,1,f);
                        fclose(f);
                        romused[c]=1;
                        c--;
                }
        } while (c>=0 && !al_findnext(&ffblk));
        al_findclose(&ffblk);

#ifndef WIN32
        if (al_findfirst("*.ROM",&ffblk,FA_ALL)!=0) return;
        do
        {
                while (romused[c] && c>=0) c--;
                if (c>=0)
                {
                        printf("Loading %s to slot %i\n",ffblk.name,c);
                        f=fopen(ffblk.name,"rb");
                        fread(rom+(c*16384),16384,1,f);
                        fclose(f);
                        romused[c]=1;
                        c--;
                }
        } while (c>=0 && !al_findnext(&ffblk));
        al_findclose(&ffblk);
#endif
}

void fillswram()
{
        int c;
        for (c=0;c<16;c++)
        {
                if (!romused[c]) swram[c]=1;
        }
}

void loadroms(MODEL m)
{
        char path[512],p2[512];
        FILE *f;
        if (m.os[0])
        {
                memset(rom,0,16*16384);
                rpclog("Reading OS file %s\n",m.os);
                f=fopen(m.os,"rb");
                if (!f) rpclog("Failed!\n");
                fread(os,16384,1,f);
                fclose(f);
        }
        getcwd(p2,511);
        sprintf(path,"%sroms/%s",exedir,m.romdir);
        chdir(path);
        loadswroms();
        chdir(p2);
}

void romsetup_os01()
{
        int c;
        FILE *f;
        struct al_ffblk ffblk;

        f=fopen("os01","rb");
        fread(os,16384,1,f);
        fclose(f);

        chdir("a01");
        if (!al_findfirst("*.rom",&ffblk,FA_ALL))
        {
                printf("Loading %s to slot %i\n",ffblk.name,c);
                f=fopen(ffblk.name,"rb");
                fread(rom,16384,1,f);
                fclose(f);
                al_findclose(&ffblk);
                memcpy(rom+16384,rom,16384);
                memcpy(rom+32768,rom,32768);
                memcpy(rom+65536,rom,65536);
                memcpy(rom+131072,rom,131072);
        }

        chdir("..");
        for (c=0;c<16;c++) romused[c]=1;
        for (c=0;c<16;c++) swram[c]=0;
}

void romsetup_bplus128()
{
        memset(rom,0,16*16384);
        swram[12]=swram[13]=1;
        swram[0]=swram[1]=1;
        romused[12]=romused[13]=1;
        romused[0]=romused[1]=1;
}

void romsetup_master128()
{
        FILE *f;
        swram[0]=swram[1]=swram[2]=swram[3]=0;
        swram[4]=swram[5]=swram[6]=swram[7]=1;
        swram[8]=swram[9]=swram[10]=swram[11]=0;
        swram[12]=swram[13]=swram[14]=swram[15]=0;
        romused[0]=romused[1]=romused[2]=romused[3]=0;
        romused[4]=romused[5]=romused[6]=romused[7]=1;
        romused[8]=romused[9]=romused[10]=romused[11]=1;
        romused[12]=romused[13]=romused[14]=romused[15]=1;
        memset(rom,0,16*16384);
        f=fopen("master/mos3.20","rb");
        fread(os,16384,1,f);
        fread(rom+(9*16384),7*16384,1,f);
        fclose(f);
}

void romsetup_mastercompact()
{
        FILE *f;
        swram[4]=swram[5]=swram[6]=swram[7]=1;
        romused[4]=romused[5]=romused[6]=romused[7]=1;
        romused[8]=romused[9]=romused[10]=romused[11]=1;
        romused[12]=romused[13]=romused[14]=romused[15]=1;
        printf("Master compact init\n");
        f=fopen("compact/os51.rom","rb");
        fread(os,16384,1,f);
        fclose(f);
        f=fopen("compact/basic48.rom","rb");
        fread(rom+(14*16384),16384,1,f);
        fclose(f);
        f=fopen("compact/adfs210.rom","rb");
        fread(rom+(13*16384),16384,1,f);
        fclose(f);
        f=fopen("compact/utils.rom","rb");
        fread(rom+(15*16384),16384,1,f);
        fclose(f);
}
