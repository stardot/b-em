/*B-em v2.2 by Tom Walker
  FDI disc support
  Interfaces with fdi2raw.c*/

#include <stdio.h>
#include <stdint.h>
#include "b-em.h"
#include "fdi.h"
#include "fdi2raw.h"
#include "disc.h"

static FILE *fdi_f[2];
static FDI  *fdi_h[2];
static uint8_t fdi_trackinfo[2][2][2][65536];
static uint8_t fdi_timing[65536];
static int fdi_sides[2];
static int fdi_tracklen[2][2][2];
static int fdi_trackindex[2][2][2];
static int fdi_lasttrack[2];
static int fdi_ds[2];
static int fdi_pos;
static int fdi_revs;

static int fdi_sector, fdi_track,   fdi_side,    fdi_drive, fdi_density;
static int fdi_inread, fdi_inwrite, fdi_readpos, fdi_inreadaddr;
static int fdi_notfound;

static uint16_t CRCTable[256];

static void fdi_setupcrc(uint16_t poly, uint16_t rvalue)
{
        int c = 256, bc;
        uint16_t crctemp;

        while(c--)
        {
                crctemp = c << 8;
                bc = 8;

                while(bc--)
                {
                        if(crctemp & 0x8000)
                        {
                                crctemp = (crctemp << 1) ^ poly;
                        }
                        else
                        {
                                crctemp <<= 1;
                        }
                }

                CRCTable[c] = crctemp;
        }
}

void fdi_init()
{
//        printf("FDI reset\n");
        fdi_f[0]  = fdi_f[1]  = 0;
        fdi_ds[0] = fdi_ds[1] = 0;
        fdi_notfound = 0;
        fdi_setupcrc(0x1021, 0xcdb4);
}

void fdi_load(int drive, char *fn)
{
        writeprot[drive] = fwriteprot[drive] = 1;
        fdi_f[drive] = fopen(fn, "rb");
        if (!fdi_f[drive]) return;
        fdi_h[drive] = fdi2raw_header(fdi_f[drive]);
//        if (!fdih[drive]) printf("Failed to load!\n");
        fdi_lasttrack[drive] = fdi2raw_get_last_track(fdi_h[drive]);
        fdi_sides[drive] = (fdi_lasttrack[drive]>83) ? 1 : 0;
//        printf("Last track %i\n",fdilasttrack[drive]);
        drives[drive].seek        = fdi_seek;
        drives[drive].readsector  = fdi_readsector;
        drives[drive].writesector = fdi_writesector;
        drives[drive].readaddress = fdi_readaddress;
        drives[drive].poll        = fdi_poll;
        drives[drive].format      = fdi_format;
}

void fdi_close(int drive)
{
        if (fdi_h[drive]) fdi2raw_header_free(fdi_h[drive]);
        if (fdi_f[drive]) fclose(fdi_f[drive]);
        fdi_f[drive] = NULL;
}

void fdi_seek(int drive, int track)
{
        int c;
        if (!fdi_f[drive]) return;
//        printf("Track start %i\n",track);
        if (track < 0) track = 0;
        if (track > fdi_lasttrack[drive]) track = fdi_lasttrack[drive] - 1;
        c = fdi2raw_loadtrack(fdi_h[drive], (uint16_t *)fdi_trackinfo[drive][0][0], (uint16_t *)fdi_timing, track << fdi_sides[drive], &fdi_tracklen[drive][0][0], &fdi_trackindex[drive][0][0], NULL, 0);
        if (!c) memset(fdi_trackinfo[drive][0][0], 0, fdi_tracklen[drive][0][0]);
        c = fdi2raw_loadtrack(fdi_h[drive], (uint16_t *)fdi_trackinfo[drive][0][1], (uint16_t *)fdi_timing, track << fdi_sides[drive], &fdi_tracklen[drive][0][1], &fdi_trackindex[drive][0][1], NULL, 1);
        if (!c) memset(fdi_trackinfo[drive][0][1], 0, fdi_tracklen[drive][0][1]);
        if (fdi_sides[drive])
        {
                c = fdi2raw_loadtrack(fdi_h[drive], (uint16_t *)fdi_trackinfo[drive][1][0], (uint16_t *)fdi_timing, (track << fdi_sides[drive]) + 1, &fdi_tracklen[drive][1][0], &fdi_trackindex[drive][1][0], NULL, 0);
                if (!c) memset(fdi_trackinfo[drive][1][0], 0, fdi_tracklen[drive][1][0]);
                c = fdi2raw_loadtrack(fdi_h[drive], (uint16_t *)fdi_trackinfo[drive][1][1], (uint16_t *)fdi_timing, (track << fdi_sides[drive]) + 1, &fdi_tracklen[drive][1][1], &fdi_trackindex[drive][1][1], NULL, 1);
                if (!c) memset(fdi_trackinfo[drive][1][1], 0, fdi_tracklen[drive][1][1]);
        }
        else
        {
                memset(fdi_trackinfo[drive][1][0], 0, 65536);
                memset(fdi_trackinfo[drive][1][1], 0, 65536);
                fdi_tracklen[drive][1][0]   = fdi_tracklen[drive][1][1]   = 10000;
                fdi_trackindex[drive][1][0] = fdi_trackindex[drive][1][1] = 100;
        }
//        printf("SD Track %i Len %i Index %i %i\n",track,ftracklen[drive][0][0],ftrackindex[drive][0][0],c);
//        printf("DD Track %i Len %i Index %i %i\n",track,ftracklen[drive][0][1],ftrackindex[drive][0][1],c);
}

void fdi_writeback(int drive, int track)
{
        return;
}

void fdi_readsector(int drive, int sector, int track, int side, int density)
{
        fdi_revs = 0;
        fdi_sector  = sector;
        fdi_track   = track;
        fdi_side    = side;
        fdi_drive   = drive;
        fdi_density = density;
//        printf("Read sector %i %i %i %i\n",drive,side,track,sector);

        fdi_inread  = 1;
        fdi_readpos = 0;
}

void fdi_writesector(int drive, int sector, int track, int side, int density)
{
        fdi_revs = 0;
        fdi_sector = sector;
        fdi_track  = track;
        fdi_side   = side;
        fdi_drive  = drive;
//        printf("Write sector %i %i %i %i\n",drive,side,track,sector);

        if (!fdi_f[drive] || (side && !fdi_ds[drive]) || density)
        {
                fdi_notfound = 500;
                return;
        }
        fdi_inwrite = 1;
        fdi_readpos = 0;
}

void fdi_readaddress(int drive, int track, int side, int density)
{
        fdi_revs = 0;
        fdi_track   = track;
        fdi_side    = side;
        fdi_density = density;
        fdi_drive   = drive;
//        printf("Read address %i %i %i\n",drive,side,track);

        fdi_inreadaddr = 1;
        fdi_readpos    = 0;
}

void fdi_format(int drive, int track, int side, int density)
{
        fdi_revs = 0;
        fdi_track   = track;
        fdi_side    = side;
        fdi_density = density;
        fdi_drive   = drive;
//        printf("Format %i %i %i\n",drive,side,track);

        fdi_inwrite = 1;
        fdi_readpos = 0;
}

static uint16_t fdi_buffer;
static int pollbytesleft=0,pollbitsleft=0;
static int readidpoll=0,readdatapoll=0,fdi_nextsector=0,inreadop=0;
static uint8_t fdi_sectordat[1026];
static int lastfdidat[2],sectorcrc[2];
static int sectorsize,fdc_sectorsize;
static int ddidbitsleft=0;

static uint8_t decodefm(uint16_t dat)
{
        uint8_t temp;
        temp = 0;
        if (dat & 0x0001) temp |= 1;
        if (dat & 0x0004) temp |= 2;
        if (dat & 0x0010) temp |= 4;
        if (dat & 0x0040) temp |= 8;
        if (dat & 0x0100) temp |= 16;
        if (dat & 0x0400) temp |= 32;
        if (dat & 0x1000) temp |= 64;
        if (dat & 0x4000) temp |= 128;
        return temp;
}

static uint16_t crc;

static void calccrc(uint8_t byte)
{
        crc = (crc << 8) ^ CRCTable[(crc >> 8)^byte];
}

void fdi_poll()
{
        int tempi, c;
        if (fdi_pos >= fdi_tracklen[fdi_drive][fdi_side][fdi_density])
        {
//                printf("Looping! %i\n",fdipos);
                fdi_pos = 0;
        }
        tempi = fdi_trackinfo[fdi_drive][fdi_side][fdi_density][((fdi_pos >> 3) & 0xFFFF) ^ 1] & (1 << (7 - (fdi_pos & 7)));
        fdi_pos++;
        fdi_buffer<<=1;
        fdi_buffer|=(tempi?1:0);
        if (fdi_inwrite)
        {
                fdi_inwrite=0;
                fdc_writeprotect();
                return;
        }
        if (!fdi_inread && !fdi_inreadaddr) return;
        if (fdi_pos == fdi_trackindex[fdi_drive][fdi_side][fdi_density])
        {
                fdi_revs++;
                if (fdi_revs == 3)
                {
//                        printf("Not found!\n");
                        fdc_notfound();
                        fdi_inread = fdi_inreadaddr = 0;
                        return;
                }
        }
        if (pollbitsleft)
        {
                pollbitsleft--;
                if (!pollbitsleft)
                {
                        pollbytesleft--;
                        if (pollbytesleft) pollbitsleft = 16; /*Set up another word if we need it*/
                        if (readidpoll)
                        {
                                fdi_sectordat[5 - pollbytesleft] = decodefm(fdi_buffer);
                                if (fdi_inreadaddr && pollbytesleft > 1) fdc_data(fdi_sectordat[5 - pollbytesleft]);
                                if (!pollbytesleft)
                                {
                                        if ((fdi_sectordat[0] == fdi_track && fdi_sectordat[2] == fdi_sector) || fdi_inreadaddr)
                                        {
                                                crc = (fdi_density) ? 0xcdb4 : 0xffff;
                                                calccrc(0xFE);
                                                for (c = 0; c < 4; c++) calccrc(fdi_sectordat[c]);
                                                if ((crc >> 8) != fdi_sectordat[4] || (crc & 0xFF) != fdi_sectordat[5])
                                                {
//                                                        printf("Header CRC error : %02X %02X %02X %02X\n",crc>>8,crc&0xFF,fdisectordat[4],fdisectordat[5]);
//                                                        dumpregs();
//                                                        exit(-1);
                                                        inreadop = 0;
                                                        if (fdi_inreadaddr)
                                                        {
                                                                fdc_data(fdi_sector);
                                                                fdc_finishread();
                                                        }
                                                        else             fdc_headercrcerror();
                                                        return;
                                                }
                                                if (fdi_sectordat[0] == fdi_track && fdi_sectordat[2] == fdi_sector && fdi_inread && !fdi_inreadaddr)
                                                {
                                                        fdi_nextsector = 1;
                                                        readidpoll = 0;
                                                        sectorsize = (1 << (fdi_sectordat[3] + 7)) + 2;
                                                        fdc_sectorsize = fdi_sectordat[3];
                                                }
                                                if (fdi_inreadaddr)
                                                {
                                                        fdc_finishread();
                                                        fdi_inreadaddr = 0;
                                                }
                                        }
                                }
                        }
                        if (readdatapoll)
                        {
                                if (pollbytesleft > 1)
                                {
                                        calccrc(decodefm(fdi_buffer));
                                }
                                else
                                   sectorcrc[1 - pollbytesleft] = decodefm(fdi_buffer);
                                if (!pollbytesleft)
                                {
                                        fdi_inread = 0;
                                        if ((crc >> 8) != sectorcrc[0] || (crc & 0xFF) != sectorcrc[1])// || (fditrack==79 && fdisect==4 && fdc_side&1))
                                        {
//                                                printf("Data CRC error : %02X %02X %02X %02X %i %04X %02X%02X %i\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1],fdipos,crc,sectorcrc[0],sectorcrc[1],ftracklen[0][0][fdidensity]);
                                                inreadop = 0;
                                                fdc_data(decodefm(lastfdidat[1]));
                                                fdc_finishread();
                                                fdc_datacrcerror();
                                                readdatapoll = 0;
                                                return;
                                        }
//                                        printf("End of FDI read %02X %02X %02X %02X\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1]);
                                        fdc_data(decodefm(lastfdidat[1]));
                                        fdc_finishread();
                                }
                                else if (lastfdidat[1] != 0)
                                   fdc_data(decodefm(lastfdidat[1]));
                                lastfdidat[1] = lastfdidat[0];
                                lastfdidat[0] = fdi_buffer;
                                if (!pollbytesleft)
                                   readdatapoll = 0;
                        }
                }
        }
        if (fdi_buffer == 0x4489 && fdi_density)
        {
//                bem_debug("Found sync\n");
                ddidbitsleft = 17;
        }

        if (fdi_buffer == 0xF57E && !fdi_density)
        {
                pollbytesleft = 6;
                pollbitsleft  = 16;
                readidpoll    = 1;
        }
        if ((fdi_buffer == 0xF56F || fdi_buffer == 0xF56A) && !fdi_density)
        {
                if (fdi_nextsector)
                {
                        pollbytesleft  = sectorsize;
                        pollbitsleft   = 16;
                        readdatapoll   = 1;
                        fdi_nextsector = 0;
                        crc = 0xffff;
                        if (fdi_buffer == 0xF56A) calccrc(0xF8);
                        else                      calccrc(0xFB);
                        lastfdidat[0] = lastfdidat[1] = 0;
                }
        }
        if (ddidbitsleft)
        {
                ddidbitsleft--;
                if (!ddidbitsleft)
                {
//                        printf("ID bits over %04X %02X %i\n",fdibuffer,decodefm(fdibuffer),fdipos);
                        if (decodefm(fdi_buffer) == 0xFE)
                        {
//                                printf("Sector header\n");
                                pollbytesleft = 6;
                                pollbitsleft  = 16;
                                readidpoll    = 1;
                        }
                        else if (decodefm(fdi_buffer) == 0xFB)
                        {
//                                printf("Data header\n");
                                if (fdi_nextsector)
                                {
                                        pollbytesleft  = sectorsize;
                                        pollbitsleft   = 16;
                                        readdatapoll   = 1;
                                        fdi_nextsector = 0;
                                        crc = 0xcdb4;
                                        if (fdi_buffer == 0xF56A) calccrc(0xF8);
                                        else                      calccrc(0xFB);
                                        lastfdidat[0] = lastfdidat[1] = 0;
                                }
                        }
                }
        }
}
