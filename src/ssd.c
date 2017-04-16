/*B-em v2.2 by Tom Walker
  SSD/DSD disc handling*/
#include <stdio.h>
#include "b-em.h"
#include "ssd.h"
#include "disc.h"

static FILE *ssd_f[2];
static uint8_t trackinfo[2][2][10*256];
static int dsd[2],ssd_trackc[2];
int writeprot[2],fwriteprot[2];

static int ssd_sector, ssd_track,   ssd_side,    ssd_drive;
static int ssd_inread, ssd_inwrite, ssd_readpos, ssd_inreadaddr;
static int ssd_time;
static int ssd_notfound;
static int ssd_rsector = 0;
static int ssd_informat = 0;

static void ssd_seek(int drive, int track)
{
        if (!ssd_f[drive]) return;
//        printf("Seek :%i to %i\n",drive,track);
        if (track < 0)
            track = 0;
        else  if (track > 79)
            track = 79;
        ssd_trackc[drive] = track;
        if (dsd[drive])
        {
                fseek(ssd_f[drive], track * 20 * 256, SEEK_SET);
                fread(trackinfo[drive][0],10 * 256, 1, ssd_f[drive]);
                fread(trackinfo[drive][1],10 * 256, 1, ssd_f[drive]);
        }
        else
        {
                fseek(ssd_f[drive], track * 10 * 256, SEEK_SET);
                fread(trackinfo[drive][0], 10 * 256, 1, ssd_f[drive]);
        }
}

static void ssd_writeback(int drive, int track)
{
        if (!ssd_f[drive]) return;
        if (dsd[drive])
        {
                fseek(ssd_f[drive], track * 20 * 256, SEEK_SET);
                fwrite(trackinfo[drive][0], 10 * 256, 1, ssd_f[drive]);
                fwrite(trackinfo[drive][1], 10 * 256, 1, ssd_f[drive]);
        }
        else
        {
                fseek(ssd_f[drive], track * 10 * 256, SEEK_SET);
                fwrite(trackinfo[drive][0], 10 * 256, 1, ssd_f[drive]);
        }
}

static void ssd_readsector(int drive, int sector, int track, int side, int density)
{
        ssd_sector = sector;
        ssd_track  = track;
        ssd_side   = side;
        ssd_drive  = drive;
//        printf("Read sector %i %i %i %i\n",drive,side,track,sector);
        
        if (!ssd_f[drive] || (side && !dsd[drive]) || density || track != ssd_trackc[drive] || sector >= 10)
        {
                ssd_notfound = 500;
///                printf("Not found!\n");
                return;
        }
        ssd_inread  = 1;
        ssd_readpos = 0;
//        printf("GO\n");
}

static void ssd_writesector(int drive, int sector, int track, int side, int density)
{
        ssd_sector = sector;
        ssd_track  = track;
        ssd_side   = side;
        ssd_drive  = drive;
//        printf("Write sector %i %i %i %i\n",drive,side,track,sector);
        
        if (!ssd_f[drive] || (side && !dsd[drive]) || density || track != ssd_trackc[drive] || sector >= 10)
        {
                ssd_notfound=500;
                return;
        }
        ssd_inwrite = 1;
        ssd_readpos = 0;
        ssd_time    = -1000;
}

static void ssd_readaddress(int drive, int track, int side, int density)
{
        ssd_track = track;
        ssd_side  = side;
        ssd_drive = drive;
//        printf("Read address %i %i %i %i\n",drive,track,side,density);

        if (!ssd_f[drive] || (side && !dsd[drive]) || density || track != ssd_trackc[drive])
        {
                ssd_notfound = 500;
                return;
        }
        ssd_rsector    = 0;
        ssd_readpos    = 0;
        ssd_inreadaddr = 1;
}

static void ssd_format(int drive, int track, int side, int density)
{
        ssd_track = track;
        ssd_side  = side;
        ssd_drive = drive;

        if (!ssd_f[drive] || (side && !dsd[drive]) || density || track != ssd_trackc[drive])
        {
                ssd_notfound = 500;
                return;
        }
        ssd_sector   = 0;
        ssd_readpos  = 0;
        ssd_informat = 1;
}

static void ssd_poll()
{
        int c;
//        printf("POLL %i\n",ssdtime);
        ssd_time++;
        if (ssd_time < 16) return;
        ssd_time = 0;
        
        if (ssd_notfound)
        {
                ssd_notfound--;
                if (!ssd_notfound)
                {
                        fdc_notfound();
                }
        }
        if (ssd_inread && ssd_f[ssd_drive])
        {
//                printf("Read %i\n",ssdreadpos);
                fdc_data(trackinfo[ssd_drive][ssd_side][(ssd_sector << 8) + ssd_readpos]);
                ssd_readpos++;
                if (ssd_readpos == 256)
                {
                        ssd_inread = 0;
                        fdc_finishread();
                }
        }
        if (ssd_inwrite && ssd_f[ssd_drive])
        {
                if (writeprot[ssd_drive])
                {
                        fdc_writeprotect();
                        ssd_inwrite = 0;
                        return;
                }
//                printf("Write data %i\n",ssdreadpos);
                c = fdc_getdata(ssd_readpos == 255);
                if (c == -1)
                {
//                        printf("Data overflow!\n");
//                        exit(-1);
                }
                trackinfo[ssd_drive][ssd_side][(ssd_sector << 8) + ssd_readpos] = c;
                ssd_readpos++;
                if (ssd_readpos == 256)
                {
                        ssd_inwrite = 0;
                        fdc_finishread();
                        ssd_writeback(ssd_drive, ssd_track);
                }
        }
        if (ssd_inreadaddr && ssd_f[ssd_drive])
        {
                switch (ssd_readpos)
                {
                        case 0: fdc_data(ssd_track);   break;
                        case 1: fdc_data(ssd_side);    break;
                        case 2: fdc_data(ssd_rsector); break;
                        case 3: fdc_data(1);           break;
                        case 4: fdc_data(0);           break;
                        case 5: fdc_data(0);           break;
                        case 6:
                        ssd_inreadaddr = 0;
                        fdc_finishread();
                        ssd_rsector++;
                        if (ssd_rsector == 10) ssd_rsector=0;
                        break;
                }
                ssd_readpos++;
        }
        if (ssd_informat && ssd_f[ssd_drive])
        {
                if (writeprot[ssd_drive])
                {
                        fdc_writeprotect();
                        ssd_informat = 0;
                        return;
                }
                trackinfo[ssd_drive][ssd_side][(ssd_sector << 8) + ssd_readpos] = 0;
                ssd_readpos++;
                if (ssd_readpos == 256)
                {
                        ssd_readpos = 0;
                        ssd_sector++;
                        if (ssd_sector == 10)
                        {
                                ssd_informat = 0;
                                fdc_finishread();
                                ssd_writeback(ssd_drive, ssd_track);
                        }
                }
        }
}

static void ssd_abort()
{
    ssd_inread = ssd_inwrite = ssd_inreadaddr = ssd_informat = 0;
}

void ssd_init()
{
        ssd_f[0] = ssd_f[1] = 0;
        dsd[0] = dsd[1] = 0;
        ssd_notfound = 0;
}

void ssd_load(int drive, char *fn)
{
        writeprot[drive] = 0;
        ssd_f[drive] = fopen(fn, "rb+");
        if (!ssd_f[drive])
        {
                ssd_f[drive] = fopen(fn, "rb");
                if (!ssd_f[drive])
		{
		    log_warn("ssd: unable to open SSD disc image '%s': %s", fn, strerror(errno));
		    return;
		}
                writeprot[drive] = 1;
        }
        fwriteprot[drive] = writeprot[drive];
        dsd[drive] = 0;
        drives[drive].seek        = ssd_seek;
        drives[drive].readsector  = ssd_readsector;
        drives[drive].writesector = ssd_writesector;
        drives[drive].readaddress = ssd_readaddress;
        drives[drive].poll        = ssd_poll;
        drives[drive].format      = ssd_format;
        drives[drive].abort       = ssd_abort;
}

void dsd_load(int drive, char *fn)
{
        writeprot[drive] = 0;
        ssd_f[drive] = fopen(fn, "rb+");
        if (!ssd_f[drive])
        {
                ssd_f[drive] = fopen(fn, "rb");
                if (!ssd_f[drive])
		{
		    log_warn("ssd: unable to open DSD disc image '%s': %s", fn, strerror(errno));
		    return;
		}
                writeprot[drive] = 1;
        }
        fwriteprot[drive] = writeprot[drive];
        dsd[drive] = 1;
        drives[drive].seek        = ssd_seek;
        drives[drive].readsector  = ssd_readsector;
        drives[drive].writesector = ssd_writesector;
        drives[drive].readaddress = ssd_readaddress;
        drives[drive].poll        = ssd_poll;
        drives[drive].format      = ssd_format;
        drives[drive].abort       = ssd_abort;
}

void ssd_close(int drive)
{
        if (ssd_f[drive]) fclose(ssd_f[drive]);
        ssd_f[drive] = NULL;
}
