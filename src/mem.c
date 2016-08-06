/*B-em v2.2 by Tom Walker
  ROM handling*/

#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"
#include "6502.h"
#include "mem.h"

uint8_t *ram, *rom, *os;
uint8_t ram_fe30, ram_fe34;
int romused[16]   = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int swram[16]     = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void mem_init()
{
        bem_debug("mem_init\n");
        ram = (uint8_t *)malloc(64 * 1024);
        rom = (uint8_t *)malloc(16 * 16384);
        os  = (uint8_t *)malloc(16384);
        memset(ram, 0, 64 * 1024);
}

void mem_reset()
{
        memset(romused,0, sizeof(romused));
        memset(swram,  0, sizeof(swram));
        memset(ram,    0, 64 * 1024);
        memset(rom,    0, 16 * 16384);
        memset(os,     0, 16384);
}

void mem_close()
{
        if (ram) free(ram);
        if (rom) free(rom);
        if (os)  free(os);
}

void mem_dump()
{
        FILE *f=fopen("ram.dmp","wb");
        fwrite(ram,64*1024,1,f);
        fclose(f);
        f=fopen("swram.dmp","wb");
        fwrite(&rom[4*16384],16384,1,f);
        fwrite(&rom[5*16384],16384,1,f);
        fwrite(&rom[6*16384],16384,1,f);
        fwrite(&rom[7*16384],16384,1,f);
        fclose(f);
}

void mem_loadswroms()
{
        FILE *f;
        int c = 15;
        struct al_ffblk ffblk;
//        memset(rom,0,16*16384);

        if (al_findfirst("*.rom", &ffblk, FA_ALL) != 0) return;
        do
        {
                while (romused[c] && c >= 0) c--;
                if (c >= 0)
                {
                        bem_debugf("Loading %s to slot %i\n",ffblk.name,c);
                        if ((f = fopen(ffblk.name, "rb")))
                        {
                                fread(rom + (c * 16384), 16384, 1, f);
                                fclose(f);
                                romused[c] = 1;
                                c--;
                        }
                        else
                                bem_errorf("unable to load rom file '%s': %s\n", strerror(errno));
                }
        } while (c >= 0 && !al_findnext(&ffblk));
        al_findclose(&ffblk);

#ifndef WIN32
        if (al_findfirst("*.ROM", &ffblk, FA_ALL) != 0) return;
        do
        {
                while (romused[c] && c >= 0) c--;
                if (c >= 0)
                {
                        bem_debugf("Loading %s to slot %i\n",ffblk.name,c);
                        if ((f = fopen(ffblk.name, "rb")))
                        {
                                fread(rom + (c * 16384), 16384, 1, f);
                                fclose(f);
                                romused[c] = 1;
                                c--;
                        }
                        else
                                bem_errorf("unable to load rom file '%s': %s\n", strerror(errno));
                }
        } while (c >= 0 && !al_findnext(&ffblk));
        al_findclose(&ffblk);
#endif
}

void mem_fillswram()
{
        int c;
        for (c = 0; c < 16; c++)
        {
                if (!romused[c]) swram[c] = 1;
        }
}

void mem_loadroms(char *os_name, char *romdir)
{
        char path[512], p2[512];
        FILE *f;

        if (os_name[0])
        {
                bem_debugf("Reading OS file %s\n", os_name);
                f = fopen(os_name, "rb");
                if (!f)
                {
                        bem_errorf("Failed to load roms/%s: %s\n", os_name, strerror(errno));
                        exit(-1);
                }
                fread(os, 16384, 1, f);
                fclose(f);
        }
        getcwd(p2, 511);
        sprintf(path, "%sroms/%s", exedir, romdir);
        chdir(path);
        mem_loadswroms();
        chdir(p2);
}

void mem_clearroms()
{
        int c;
        memset(rom, 0, 16 * 16384);
        for (c = 0; c < 16; c++) romused[c] = 0;
        for (c = 0; c < 16; c++) swram[c] = 0;
}

int mem_romsetup_os01()
{
        int c;
        FILE *f;
        struct al_ffblk ffblk;

        f=fopen("os01", "rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/os01: %s", strerror(errno));
                return -1;
        }
        fread(os, 16384, 1, f);
        fclose(f);

        chdir("a01");
        if (!al_findfirst("*.rom", &ffblk, FA_ALL))
        {
                f=fopen(ffblk.name, "rb");
                if (f)
                {
                        fread(rom, 16384, 1, f);
                        fclose(f);
                }
                al_findclose(&ffblk);
                memcpy(rom + 16384,  rom, 16384);
                memcpy(rom + 32768,  rom, 32768);
                memcpy(rom + 65536,  rom, 65536);
                memcpy(rom + 131072, rom, 131072);
        }

        chdir("..");
        for (c = 0; c < 16; c++) romused[c] = 1;
        for (c = 0; c < 16; c++) swram[c] = 0;
        return 0;
}

int mem_romsetup_bplus128()
{
        swram[12] = swram[13] = 1;
        swram[0]  = swram[1]  = 1;
        romused[12] = romused[13] = 1;
        romused[0]  = romused[1]  = 1;
        return 0;
}

int mem_romsetup_master128()
{
        FILE *f;
//        printf("ROM setup Master 128\n");
        memset(rom, 0, 16 * 16384);
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = romused[8] = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[9]  = romused[10] = romused[11] = 1;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
        f=fopen("master/mos3.20", "rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/master/mos.320: %s", strerror(errno));
                return -1;
        }
        fread(os, 16384, 1, f);
        fread(rom + (9 * 16384), 7 * 16384, 1, f);
        fclose(f);
        return 0;
}

int mem_romsetup_master128_35()
{
        FILE *f;
//        printf("ROM setup Master 128\n");
        memset(rom, 0, 16 * 16384);
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = romused[8] = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[9]  = romused[10] = romused[11] = 1;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
        f=fopen("master/mos3.50", "rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/master/mos.350: %s", strerror(errno));
                return -1;
        }
        fread(os, 16384, 1, f);
        fread(rom + (9 * 16384), 7 * 16384, 1, f);
        fclose(f);
        return 0;
}

int mem_romsetup_mastercompact()
{
        FILE *f;
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[8]  = romused[9]  = romused[10] = romused[11] = 0;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
//        printf("Master compact init\n");
        f=fopen("compact/os51","rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/compact/os51: %s", strerror(errno));
                return -1;
        }
        fread(os,16384,1,f);
        fclose(f);
        f=fopen("compact/basic48","rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/compact/basic48: %s", strerror(errno));
                return -1;
        }
        fread(rom+(14*16384),16384,1,f);
        fclose(f);
        f=fopen("compact/adfs210","rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/compact/adfs210: %s", strerror(errno));
                return -1;
        }
        fread(rom+(13*16384),16384,1,f);
        fclose(f);
        f=fopen("compact/utils","rb");
        if (!f)
        {
                bem_errorf("Failed to load roms/compact/utils: %s", strerror(errno));
                return -1;
        }
        fread(rom+(15*16384),16384,1,f);
        fclose(f);
        return 0;
}

void mem_savestate(FILE *f)
{
        putc(ram_fe30, f);
        putc(ram_fe34, f);
        fwrite(ram, 64  * 1024, 1, f);
        fwrite(rom, 256 * 1024, 1, f);
}

void mem_loadstate(FILE *f)
{
        writemem(0xFE30, getc(f));
        writemem(0xFE34, getc(f));
        fread(ram, 64  * 1024, 1, f);
        fread(rom, 256 * 1024, 1, f);
}
