/*B-em v2.2 by Tom Walker
  IDE emulation*/
#include <stdio.h>
#include "b-em.h"
#include "ide.h"

int ide_enable;
int ide_count;

static struct
{
        uint8_t atastat;
        uint8_t error,status;
        int secount,sector,cylinder,head,drive,cylprecomp;
        uint8_t command;
        uint8_t fdisk;
        int pos,pos2;
        int spt,hpc;
} ide;

static uint16_t ide_buffer[256];
static uint8_t *ide_bufferb;
static uint8_t  ide_buffer2[256];
static FILE *hdfile[2] = {NULL, NULL};

void ide_close()
{
        if (hdfile[0]) fclose(hdfile[0]);
        if (hdfile[1]) fclose(hdfile[1]);
}

void ide_init(void)
{
        char s[256];
        ide.pos2 = 1;
        ide.atastat = 0x40;
        ide_count = 0;
        if (!hdfile[0])
        {
                sprintf(s, "%shd4.hdf", exedir);
                hdfile[0] = fopen(s, "rb+");
                if (!hdfile[0])
                {
                        hdfile[0] = fopen(s, "wb");
                        putc(0, hdfile[0]);
                        fclose(hdfile[0]);
                        hdfile[0] = fopen(s, "rb+");
                }
        }
        if (!hdfile[1])
        {
                sprintf(s, "%shd5.hdf", exedir);
                hdfile[1] = fopen(s, "rb+");
                if (!hdfile[1])
                {
                        hdfile[1] = fopen(s, "wb");
                        putc(0, hdfile[1]);
                        fclose(hdfile[1]);
                        hdfile[1] = fopen(s, "rb+");
                }
        }
        ide_bufferb = (uint8_t *)ide_buffer;
        ide.spt = 63;
        ide.hpc = 16;

        ide.atastat  = 0x40;
        ide.error    = 0;
        ide.secount  = 1;
        ide.sector   = 1;
        ide.head     = 0;
        ide.cylinder = 0;
}

void ide_write(uint16_t addr, uint8_t val)
{
        if (!ide_enable) return;
//        bem_debugf("Write IDE %04X %02X %04X\n",addr,val,pc);
        switch (addr & 0xF)
        {
                case 0x0:
                ide_bufferb[ide.pos] = val;
                ide.pos2 = ide.pos + 1;
                ide.pos += 2;
                if (ide.pos >= 512)
                {
                        ide.pos = 0;
                        ide.atastat  = 0x80;
                        ide_count = 1000;
                }
                return;
                case 0x8:
                ide_bufferb[ide.pos2] = val;
                return;
                case 0x1:
                ide.cylprecomp = val;
                return;
                case 0x2:
                ide.secount = val;
                return;
                case 0x3:
                ide.sector = val;
                return;
                case 0x4:
                ide.cylinder = (ide.cylinder & 0xFF00) | val;
                return;
                case 0x5:
                ide.cylinder = (ide.cylinder & 0xFF) | (val << 8);
                return;
                case 0x6:
                ide.head  = val & 0xF;
                ide.drive = (val >> 4) & 1;
                return;
                case 0x7: /*Command register*/
                ide.command = val;
                ide.error = 0;
//                bem_debugf("IDE command %02X\n",val);
                switch (val)
                {
                        case 0x10: /*Restore*/
                        case 0x70: /*Seek*/
                        ide.atastat  = 0x40;
                        ide_count = 100;
                        return;
                        case 0x20: /*Read sector*/
                        ide.atastat  = 0x80;
                        ide_count = 200;
                        return;
                        case 0x30: /*Write sector*/
                        ide.atastat = 0x08 | 0x40;
                        ide.pos = 0;
                        return;
                        case 0x40: /*Read verify*/
                        ide.atastat  = 0x80;
                        ide_count = 200;
                        return;
                        case 0x50: /*Format track*/
                        ide.atastat = 0x08;
                        ide.pos = 0;
                        return;
                        case 0x91: /*Set parameters*/
                        ide.atastat  = 0x80;
                        ide_count = 200;
                        return;
                        case 0xA1: /*Identify packet device*/
                        case 0xE3: /*Idle*/
                        ide.atastat  = 0x80;
                        ide_count = 200;
                        return;
                        case 0xEC: /*Identify device*/
                        ide.atastat  = 0x80;
                        ide_count = 200;
                        return;
                }
                bem_debugf("Bad IDE command %02X\n", val);
                exit(-1);
                return;
        }
}

static int indexcount=0;

uint8_t ide_read(uint16_t addr)
{
        uint8_t temp;
        if (!ide_enable) return 0xFC;
//        bem_debugf("Read IDE %04X %04X %02X\n",addr,pc,ide.atastat);
        switch (addr&0xF)
        {
                case 0x0:
                temp = ide_bufferb[ide.pos];
                ide.pos2 = ide.pos + 1;
                ide.pos += 2;

                if (ide.pos >= 512)
                {
                        ide.pos = 0;
                        ide.atastat = 0x40;
                        if (ide.command == 0x20)
                        {
                                ide.secount--;
                                if (ide.secount)
                                {
                                        ide.atastat = 0x08;
                                        ide.sector++;
                                        if (ide.sector == (ide.spt + 1))
                                        {
                                                ide.sector = 1;
                                                ide.head++;
                                                if (ide.head == ide.hpc)
                                                {
                                                        ide.head = 0;
                                                        ide.cylinder++;
                                                }
                                        }
                                        ide.atastat  = 0x80;
                                        ide_count = 200;
                                }
                        }
                }
                return temp;
                case 0x8:
                temp = ide_bufferb[ide.pos2];
                return temp;
                case 0x1:
                return ide.error;
                case 0x2:
                return ide.secount;
                case 0x3:
                return ide.sector;
                case 0x4:
                return ide.cylinder & 0xFF;
                case 0x5:
                return ide.cylinder >> 8;
                case 0x6:
                return ide.head | (ide.drive << 4);
                case 0x7:
                indexcount++;
                if (indexcount == 199)
                {
                        indexcount = 0;
                        return ide.atastat | 2;
                }
                return ide.atastat;
        }
        return 0xFF;
}

void ide_callback()
{
        int addr, c;

        switch (ide.command)
        {
                case 0x10: /*Restore*/
                case 0x70: /*Seek*/
                ide.atastat = 0x40;
                return;
                case 0x20: /*Read sectors*/
                addr = ((((ide.cylinder * ide.hpc) + ide.head) * ide.spt) + (ide.sector)) * 256;
                fseek(hdfile[ide.drive], addr, SEEK_SET);
                memset(ide_buffer, 0, 512);
                fread(ide_buffer2, 256, 1, hdfile[ide.drive]);
                for (c = 0; c < 256; c++) ide_bufferb[c << 1] = ide_buffer2[c];
                ide.pos = 0;
                ide.atastat = 0x08 | 0x40;
                return;
                case 0x30: /*Write sector*/
                addr = ((((ide.cylinder * ide.hpc) + ide.head) * ide.spt) + (ide.sector)) * 256;
                fseek(hdfile[ide.drive], addr, SEEK_SET);
                for (c = 0; c < 256; c++) ide_buffer2[c] = ide_bufferb[c << 1];
                fwrite(ide_buffer2, 256, 1, hdfile[ide.drive]);
                ide.secount--;
                if (ide.secount)
                {
                        ide.atastat = 0x08 | 0x40;
                        ide.pos = 0;
                        ide.sector++;
                        if (ide.sector == (ide.spt + 1))
                        {
                                ide.sector = 1;
                                ide.head++;
                                if (ide.head == ide.hpc)
                                {
                                        ide.head = 0;
                                        ide.cylinder++;
                                }
                        }
                }
                else
                   ide.atastat = 0x40;
                return;
                case 0x40: /*Read verify*/
                ide.pos = 0;
                ide.atastat = 0x40;
                return;
                case 0x50: /*Format track*/
                addr = (((ide.cylinder * ide.hpc) + ide.head) * ide.spt) * 256;
                fseek(hdfile[ide.drive], addr, SEEK_SET);
                memset(ide_bufferb, 0, 512);
                for (c = 0; c < ide.secount; c++)
                {
                        fwrite(ide_buffer, 256, 1, hdfile[ide.drive]);
                }
                ide.atastat = 0x40;
                return;
                case 0x91: /*Set parameters*/
                ide.spt = ide.secount;
                ide.hpc = ide.head + 1;
                ide.atastat = 0x40;
                return;
                case 0xA1:
                case 0xE3:
                ide.atastat = 0x41;
                ide.error = 4;
                return;
                case 0xEC:
                memset(ide_buffer, 0, 512);
                ide_buffer[1] = 101; /*Cylinders*/
                ide_buffer[3] = 16;  /*Heads*/
                ide_buffer[6] = 63;  /*Sectors*/
                for (addr = 10; addr < 20; addr++)
                    ide_buffer[addr] = 0x2020;
                for (addr = 23; addr < 47; addr++)
                    ide_buffer[addr] = 0x2020;
                ide_bufferb[46^1] = 'v'; /*Firmware version*/
                ide_bufferb[47^1] = '2';
                ide_bufferb[48^1] = '.';
                ide_bufferb[49^1] = '1';
                ide_bufferb[54^1] = 'B'; /*Drive model*/
                ide_bufferb[55^1] = '-';
                ide_bufferb[56^1] = 'e';
                ide_bufferb[57^1] = 'm';
                ide_bufferb[58^1] = ' ';
                ide_bufferb[59^1] = 'H';
                ide_bufferb[60^1] = 'D';
                ide_buffer[50] = 0x4000; /*Capabilities*/
                ide_buffer[53] = 1;
                ide_buffer[56] = ide.spt;
                ide_buffer[55] = ide.hpc;
                ide_buffer[54] = (101 * 16 * 63) / (ide.spt * ide.hpc);
                ide_buffer[57] = (101 * 16 * 63) & 0xFFFF;
                ide_buffer[58] = (101 * 16 * 63) >> 16;
                ide.pos = 0;
                ide.atastat = 0x08;
                return;
        }
}
