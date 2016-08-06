/*B-em v2.2 by Tom Walker
  ADFS disc support (really all double-density formats)*/

#include <stdio.h>
#include "b-em.h"
#include "adf.h"
#include "disc.h"

static FILE *adf_f[2];
static uint8_t trackinfoa[2][2][20*256];
static int adl[2];
static int adf_sectors[2], adf_size[2], adf_trackc[2];
static int adf_dblstep[2];

static int adf_sector,   adf_track,   adf_side,    adf_drive;
static int adf_inread,   adf_readpos, adf_inwrite, adf_inreadaddr;
static int adf_time;
static int adf_notfound;
static int adf_rsector=0;
static int adf_informat=0;

void adf_init()
{
        adf_f[0] = adf_f[1] = 0;
        adl[0] = adl[1] = 0;
        adf_notfound = 0;
}

void adf_load(int drive, char *fn)
{
        writeprot[drive] = 0;
        adf_f[drive] = fopen(fn, "rb+");
        if (!adf_f[drive])
        {
                adf_f[drive] = fopen(fn, "rb");
                if (!adf_f[drive]) return;
                writeprot[drive] = 1;
        }
        fwriteprot[drive] = writeprot[drive];
        fseek(adf_f[drive], -1, SEEK_END);
        if (ftell(adf_f[drive]) > (700 * 1024))
        {
                adl[drive] = 1;
                adf_sectors[drive] = 5;
                adf_size[drive] = 1024;
        }
        else
        {
                adl[drive] = 0;
                adf_sectors[drive] = 16;
                adf_size[drive] = 256;
        }
        drives[drive].seek        = adf_seek;
        drives[drive].readsector  = adf_readsector;
        drives[drive].writesector = adf_writesector;
        drives[drive].readaddress = adf_readaddress;
        drives[drive].poll        = adf_poll;
        drives[drive].format      = adf_format;
        adf_dblstep[drive] = 0;
}

void adl_load(int drive, char *fn)
{
        writeprot[drive] = 0;
        adf_f[drive] = fopen(fn, "rb+");
        if (!adf_f[drive])
        {
                adf_f[drive] = fopen(fn, "rb");
                if (!adf_f[drive]) return;
                writeprot[drive] = 1;
        }
        fwriteprot[drive] = writeprot[drive];
        adl[drive] = 1;
        drives[drive].seek        = adf_seek;
        drives[drive].readsector  = adf_readsector;
        drives[drive].writesector = adf_writesector;
        drives[drive].readaddress = adf_readaddress;
        drives[drive].poll        = adf_poll;
        drives[drive].format      = adf_format;
        adf_sectors[drive] = 16;
        adf_size[drive] = 256;
        adf_dblstep[drive] = 0;
}

void adl_loadex(int drive, char *fn, int sectors, int size, int dblstep)
{
        writeprot[drive] = 0;
        adf_f[drive] = fopen(fn, "rb+");
        if (!adf_f[drive])
        {
                adf_f[drive] = fopen(fn, "rb");
                if (!adf_f[drive]) return;
                writeprot[drive] = 1;
        }
        fwriteprot[drive] = writeprot[drive];
        fseek(adf_f[drive], -1, SEEK_END);
        adl[drive] = 1;
        drives[drive].seek        = adf_seek;
        drives[drive].readsector  = adf_readsector;
        drives[drive].writesector = adf_writesector;
        drives[drive].readaddress = adf_readaddress;
        drives[drive].poll        = adf_poll;
        drives[drive].format      = adf_format;
        adf_sectors[drive] = sectors;
        adf_size[drive] = size;
        adf_dblstep[drive] = dblstep;
}

void adf_close(int drive)
{
        if (adf_f[drive]) fclose(adf_f[drive]);
        adf_f[drive] = NULL;
}

void adf_seek(int drive, int track)
{
        if (!adf_f[drive]) return;
//        bem_debugf("Seek %i %i %i %i %i %i\n",drive,track,adfsectors[drive],adfsize[drive],adl[drive],adfsectors[drive]*adfsize[drive]);
        if (adf_dblstep[drive]) track /= 2;
        adf_trackc[drive] = track;
        if (adl[drive])
        {
                fseek(adf_f[drive], track * adf_sectors[drive] * adf_size[drive] * 2, SEEK_SET);
                fread(trackinfoa[drive][0], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
                fread(trackinfoa[drive][1], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
        }
        else
        {
                fseek(adf_f[drive], track * adf_sectors[drive] * adf_size[drive], SEEK_SET);
                fread(trackinfoa[drive][0], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
        }
}
void adf_writeback(int drive, int track)
{
        if (!adf_f[drive]) return;
        if (adf_dblstep[drive]) track /= 2;
        if (adl[drive])
        {
                fseek(adf_f[drive], track * adf_sectors[drive] * adf_size[drive] * 2, SEEK_SET);
                fwrite(trackinfoa[drive][0], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
                fwrite(trackinfoa[drive][1], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
        }
        else
        {
                fseek(adf_f[drive], track *  adf_sectors[drive] * adf_size[drive], SEEK_SET);
                fwrite(trackinfoa[drive][0], adf_sectors[drive] * adf_size[drive], 1, adf_f[drive]);
        }
}

void adf_readsector(int drive, int sector, int track, int side, int density)
{
        adf_sector = sector;
        adf_track  = track;
        adf_side   = side;
        adf_drive  = drive;
        if (adf_size[drive] != 256) adf_sector--;
//        printf("ADFS Read sector %i %i %i %i   %i\n",drive,side,track,sector,adftrackc[drive]);

        if (!adf_f[drive] || (side && !adl[drive]) || !density || (track != adf_trackc[drive]))
        {
//                printf("Not found! %08X (%i %i) %i (%i %i)\n",adff[drive],side,adl[drive],density,track,adftrackc[drive]);
                adf_notfound=500;
                return;
        }
//        printf("Found\n");
        adf_inread  = 1;
        adf_readpos = 0;
}

void adf_writesector(int drive, int sector, int track, int side, int density)
{
//        if (adfdblstep[drive]) track/=2;
        adf_sector = sector;
        adf_track  = track;
        adf_side   = side;
        adf_drive  = drive;
        if (adf_size[drive] != 256) adf_sector--;
//        printf("ADFS Write sector %i %i %i %i\n",drive,side,track,sector);

        if (!adf_f[drive] || (side && !adl[drive]) || !density || (track != adf_trackc[drive]))
        {
                adf_notfound = 500;
                return;
        }
        adf_inwrite = 1;
        adf_readpos = 0;
}

void adf_readaddress(int drive, int track, int side, int density)
{
        if (adf_dblstep[drive]) track /= 2;
        adf_drive = drive;
        adf_track = track;
        adf_side  = side;
//        bem_debugf("Read address %i %i %i\n",drive,side,track);

        if (!adf_f[drive] || (side && !adl[drive]) || !density || (track != adf_trackc[drive]))
        {
                adf_notfound=500;
                return;
        }
        adf_inreadaddr = 1;
        adf_readpos    = 0;
}

void adf_format(int drive, int track, int side, int density)
{
        if (adf_dblstep[drive]) track /= 2;
        adf_drive = drive;
        adf_track = track;
        adf_side  = side;

        if (!adf_f[drive] || (side && !adl[drive]) || !density || track != adf_trackc[drive])
        {
                adf_notfound = 500;
                return;
        }
        adf_sector  = 0;
        adf_readpos = 0;
        adf_informat  = 1;
}

void adf_poll()
{
        int c;
        adf_time++;
        if (adf_time<16) return;
        adf_time=0;

        if (adf_notfound)
        {
                adf_notfound--;
                if (!adf_notfound)
                {
//                        bem_debug("Not found!\n");
                        fdc_notfound();
                }
        }
        if (adf_inread && adf_f[adf_drive])
        {
//                if (!adfreadpos) bem_debugf("%i\n",adfsector*adfsize[adfdrive]);
                fdc_data(trackinfoa[adf_drive][adf_side][(adf_sector * adf_size[adf_drive]) + adf_readpos]);
                adf_readpos++;
                if (adf_readpos == adf_size[adf_drive])
                {
//                        bem_debugf("Read %i bytes\n",adfreadpos);
                        adf_inread = 0;
                        fdc_finishread();
                }
        }
        if (adf_inwrite && adf_f[adf_drive])
        {
                if (writeprot[adf_drive])
                {
                        fdc_writeprotect();
                        adf_inwrite = 0;
                        return;
                }
//                printf("Write data %i\n",adfreadpos);
                c = fdc_getdata(adf_readpos == 255);
                if (c == -1)
                {
//Carlo Concari: do not write if data not ready yet
                          return;
//                        printf("Data overflow!\n");
//                        exit(-1);
                }
                trackinfoa[adf_drive][adf_side][(adf_sector * adf_size[adf_drive]) + adf_readpos] = c;
                adf_readpos++;
                if (adf_readpos == adf_size[adf_drive])
                {
                        adf_inwrite = 0;
                        fdc_finishread();
                        adf_writeback(adf_drive, adf_track);
                }
        }
        if (adf_inreadaddr && adf_f[adf_drive])
        {
                switch (adf_readpos)
                {
                        case 0: fdc_data(adf_track); break;
                        case 1: fdc_data(adf_side); break;
                        case 2: fdc_data(adf_rsector + (adf_size[adf_drive] != 256) ? 1 : 0); break;
                        case 3: fdc_data((adf_size[adf_drive] == 256) ? 1 : ((adf_size[adf_drive] == 512) ? 2 : 3)); break;
                        case 4: fdc_data(0); break;
                        case 5: fdc_data(0); break;
                        case 6:
                        adf_inreadaddr = 0;
                        fdc_finishread();
//                        bem_debugf("Read addr - %i %i %i %i 1 0 0\n",adfdrive,adftrack,adfside,adfsector);
                        adf_rsector++;
                        if (adf_rsector == adf_sectors[adf_drive]) adf_rsector=0;
                        break;
                }
                adf_readpos++;
        }
        if (adf_informat && adf_f[adf_drive])
        {
                if (writeprot[adf_drive])
                {
                        fdc_writeprotect();
                        adf_informat = 0;
                        return;
                }
                trackinfoa[adf_drive][adf_side][(adf_sector * adf_size[adf_drive]) + adf_readpos] = 0;
                adf_readpos++;
                if (adf_readpos == adf_size[adf_drive])
                {
                        adf_readpos = 0;
                        adf_sector++;
                        if (adf_sector == adf_sectors[adf_drive])
                        {
                                adf_informat = 0;
                                fdc_finishread();
                                adf_writeback(adf_drive, adf_track);
                        }
                }
        }
}
