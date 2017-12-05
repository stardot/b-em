/*B-em v2.2 by Tom Walker
  ROM handling*/

#include <allegro.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"
#include "6502.h"
#include "mem.h"

uint8_t *ram, *rom, *os;
uint8_t ram_fe30, ram_fe34;

#define ROM_SLOTS 16
#define ROM_SIZE  16384

int romused[ROM_SLOTS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int swram[ROM_SLOTS]   = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void mem_init()
{
        log_debug("mem_init\n");
        ram = (uint8_t *)malloc(64 * 1024);
        rom = (uint8_t *)malloc(ROM_SLOTS * ROM_SIZE);
        os  = (uint8_t *)malloc(ROM_SIZE);
        memset(ram, 0, 64 * 1024);
}

void mem_reset()
{
        memset(romused,0, sizeof(romused));
        memset(swram,  0, sizeof(swram));
        memset(ram,    0, 64 * 1024);
        memset(rom,    0, ROM_SLOTS * ROM_SIZE);
        memset(os,     0, ROM_SIZE);
}

void mem_close()
{
        if (ram) free(ram);
        if (rom) free(rom);
        if (os)  free(os);
}

void mem_dump()
{
	FILE *f;

	if ((f=fopen("ram.dmp","wb")))
	{
		fwrite(ram,64*1024,1,f);
		fclose(f);
	}
	else
		log_error("mem: unable to open ram dump file: %s", strerror(errno));

        if ((f=fopen("swram.dmp","wb")))
	{
		fwrite(&rom[4*ROM_SIZE],ROM_SIZE,1,f);
		fwrite(&rom[5*ROM_SIZE],ROM_SIZE,1,f);
		fwrite(&rom[6*ROM_SIZE],ROM_SIZE,1,f);
		fwrite(&rom[7*ROM_SIZE],ROM_SIZE,1,f);
		fclose(f);
	}
	else
		log_error("mem: unable to open swram dump file: %s", strerror(errno));
}

static int load_sw_rom(const char *name, int slot)
{
	FILE *f;

	log_debug("Loading %s to slot %i\n", name, slot);
	if ((f = fopen(name, "rb")))
	{
		fread(rom + (slot * ROM_SIZE), ROM_SIZE, 1, f);
		fclose(f);
		romused[slot] = 1;
		return 1;
	}
	else
	{
		log_error("mem: unable to open rom file '%s': %s", name, strerror(errno));
		return 0;
	}
}

static int scan_rom_dir(const char *pat, char **files, int file_cnt)
{
        struct al_ffblk ffblk;

        if (al_findfirst(pat, &ffblk, FA_ALL) == 0)
	{
		do {
			log_debug("mem: found ROM file '%s'", ffblk.name);
			files[file_cnt++] = strdup(ffblk.name);
			if (file_cnt == ROM_SLOTS)
			{
				log_warn("mem: too many ROM files");
				break;
			}
		} while (!al_findnext(&ffblk));
		al_findclose(&ffblk);
	}
	return file_cnt;
}

void mem_loadswroms()
{
        char *file, *rom_files[ROM_SLOTS];
	int file_cnt, file_no, slot_no;

	// Build an array of all ROM files.

	file_cnt = scan_rom_dir("*.rom", rom_files, 0);
#ifndef WIN32
	if (file_cnt < ROM_SLOTS)
		file_cnt = scan_rom_dir("*.ROM", rom_files, file_cnt);
#endif
	// Next process files which start with a slot number.

	for (file_no = 0; file_no < file_cnt; file_no++)
	{
		file = rom_files[file_no];
		if (isdigit(*file))
		{
			log_debug("mem: numbered ROM %s", file);
			slot_no = atoi(file);
			if (slot_no < ROM_SLOTS)
			{
				if (romused[slot_no])
					log_warn("mem: slot %d is already used - unable to load %s into it", slot_no, file);
				else
				{
					load_sw_rom(file, slot_no);
					free(file);
					rom_files[file_no] = NULL; // mark done.
				}
			}
			else
				log_warn("mem: slot %d is out of range for %s", slot_no, file);
		}
	}

	// Go back and process the rest.

	slot_no = 15;
	for (file_no = 0; file_no < file_cnt; file_no++)
	{
		if ((file = rom_files[file_no]))
		{
			log_debug("mem: anywhere ROM %s", file);
			while (romused[slot_no])
			{
				if (slot_no == 0)
					return; // should never happen.
				slot_no--;
			}
			load_sw_rom(file, slot_no);
			free(file);
		}
	}
}

void mem_fillswram()
{
        int c;
        for (c = 0; c < ROM_SLOTS; c++)
        {
                if (!romused[c]) swram[c] = 1;
        }
}

int mem_findswram(int n) {
    int c;

    for (c = 0; c <= ROM_SLOTS; c++)
        if (swram[c])
            if (n-- <= 0)
                return c;
    return -1;
}

static void load_os_rom(const char *os_name)
{
	FILE *f;

	log_debug("Reading OS file %s", os_name);
	if ((f = fopen(os_name, "rb")) == NULL)
	{
		log_fatal("mem: unable to load OS ROM %s: %s", os_name, strerror(errno));
		exit(1);
	}
	fread(os, ROM_SIZE, 1, f);
	fclose(f);
}

void mem_loadroms(char *os_name, char *romdir)
{
        char path[512], p2[512];

        if (os_name[0])
		load_os_rom(os_name);

        getcwd(p2, 511);
        sprintf(path, "%sroms/%s", exedir, romdir);
        chdir(path);
        mem_loadswroms();
        chdir(p2);
}

void mem_clearroms()
{
        int c;
        memset(rom, 0, ROM_SLOTS * ROM_SIZE);
        for (c = 0; c < ROM_SLOTS; c++) romused[c] = 0;
        for (c = 0; c < ROM_SLOTS; c++) swram[c] = 0;
}

void mem_romsetup_os01()
{
        int c;
        struct al_ffblk ffblk;

	load_os_rom("os01");

        chdir("a01");
        if (!al_findfirst("*.rom", &ffblk, FA_ALL))
        {
		if (load_sw_rom(ffblk.name, 0))
		{
			memcpy(rom + ROM_SIZE,  rom, ROM_SIZE);
			memcpy(rom + 32768,  rom, 32768);
			memcpy(rom + 65536,  rom, 65536);
			memcpy(rom + 131072, rom, 131072);
		}
                al_findclose(&ffblk);
        }

        chdir("..");
        for (c = 0; c < ROM_SLOTS; c++) romused[c] = 1;
        for (c = 0; c < ROM_SLOTS; c++) swram[c] = 0;
}

void mem_romsetup_bplus128()
{
        swram[12] = swram[13] = 1;
        swram[0]  = swram[1]  = 1;
        romused[12] = romused[13] = 1;
        romused[0]  = romused[1]  = 1;
}

static void load_os_master(const char *os_name)
{
	FILE *f;

	log_debug("Reading OS file %s", os_name);
	if ((f = fopen(os_name, "rb")) == NULL)
	{
		log_fatal("mem: unable to load OS ROM %s: %s", os_name, strerror(errno));
		exit(1);
	}
        fread(os, ROM_SIZE, 1, f);
        fread(rom + (9 * ROM_SIZE), 7 * ROM_SIZE, 1, f);
	fclose(f);
}

void mem_romsetup_master128()
{
        memset(rom, 0, ROM_SLOTS * ROM_SIZE);
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = romused[8] = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[9]  = romused[10] = romused[11] = 1;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
	load_os_master("master/mos3.20");
}

void mem_romsetup_master128_35()
{
        memset(rom, 0, ROM_SLOTS * ROM_SIZE);
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = romused[8] = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[9]  = romused[10] = romused[11] = 1;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
	load_os_master("master/mos3.50");
}

void mem_romsetup_mastercompact()
{
        swram[0]  = swram[1]  = swram[2]  = swram[3]  = 0;
        swram[4]  = swram[5]  = swram[6]  = swram[7]  = 1;
        swram[8]  = swram[9]  = swram[10] = swram[11] = 0;
        swram[12] = swram[13] = swram[14] = swram[15] = 0;
        romused[0]  = romused[1]  = romused[2]  = romused[3]  = 0;
        romused[4]  = romused[5]  = romused[6]  = romused[7]  = 1;
        romused[8]  = romused[9]  = romused[10] = romused[11] = 0;
        romused[12] = romused[13] = romused[14] = romused[15] = 1;
	load_os_rom("compact/os51");
	load_sw_rom("compact/basic48", 14);
	load_sw_rom("compact/adfs210", 13);
        load_sw_rom("compact/utils", 15);
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
