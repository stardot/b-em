/*B-em v2.0 by Tom Walker
  FDI disc support
  Interfaces with fdi2raw.c*/

#include <stdio.h>
#include <stdint.h>
#include "b-em.h"
#include "fdi2raw.h"

FILE *fdif[2];
FDI *fdih[2];
uint8_t ftrackinfo[2][2][2][65536];
uint8_t fditiming[65536];
int fdisides[2];
int ftracklen[2][2][2];
int ftrackindex[2][2][2];
int fdilasttrack[2];
int fdids[2];
int fdipos;
int fdirevs;

int fdisector,fditrack,fdiside,fdidrive,fdidensity;
int fdiread,fdiwrite,fdireadpos,fdireadaddr;
int fditime;
int fdinotfound;
int fdirsector=0;

uint16_t CRCTable[256];

void setupcrc(uint16_t poly, uint16_t rvalue)
{
        int c = 256, bc;
        uint16_t crctemp;

        while(c--)
        {
                crctemp = c << 8;
                bc = 8;

                while(bc--)
                {
                        if(crctemp&0x8000)
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

void fdi_reset()
{
        printf("FDI reset\n");
        fdif[0]=fdif[1]=0;
        fdids[0]=fdids[1]=0;
        fdinotfound=0;
        setupcrc(0x1021, 0xcdb4);
}

void fdi_load(int drive, char *fn)
{
        writeprot[drive]=fwriteprot[drive]=1;
        fdif[drive]=fopen(fn,"rb");
        if (!fdif[drive]) return;
        fdih[drive]=fdi2raw_header(fdif[drive]);
        if (!fdih[drive]) printf("Failed to load!\n");
        fdilasttrack[drive]=fdi2raw_get_last_track(fdih[drive]);
        fdisides[drive]=(fdilasttrack[drive]>83)?1:0;
        printf("Last track %i\n",fdilasttrack[drive]);
        drives[drive].seek=fdi_seek;
        drives[drive].readsector=fdi_readsector;
        drives[drive].writesector=fdi_writesector;
        drives[drive].readaddress=fdi_readaddress;
        drives[drive].poll=fdi_poll;
        drives[drive].format=fdi_format;
}

void fdi_close(int drive)
{
        if (fdih[drive]) fdi2raw_header_free(fdih[drive]);
        if (fdif[drive]) fclose(fdif[drive]);
        fdif[drive]=NULL;
}

void fdi_seek(int drive, int track)
{
        int c;
        if (!fdif[drive]) return;
//        printf("Track start %i\n",track);
        if (track<0) track=0;
        if (track>fdilasttrack[drive]) track=fdilasttrack[drive]-1;
        c=fdi2raw_loadtrack(fdih[drive],ftrackinfo[drive][0][0],fditiming,track<<fdisides[drive],&ftracklen[drive][0][0],&ftrackindex[drive][0][0],NULL,0);
        if (!c) memset(ftrackinfo[drive][0][0],0,ftracklen[drive][0][0]);
        c=fdi2raw_loadtrack(fdih[drive],ftrackinfo[drive][0][1],fditiming,track<<fdisides[drive],&ftracklen[drive][0][1],&ftrackindex[drive][0][1],NULL,1);
        if (!c) memset(ftrackinfo[drive][0][1],0,ftracklen[drive][0][1]);
        if (fdisides[drive])
        {
                c=fdi2raw_loadtrack(fdih[drive],ftrackinfo[drive][1][0],fditiming,(track<<fdisides[drive])+1,&ftracklen[drive][1][0],&ftrackindex[drive][1][0],NULL,0);
                if (!c) memset(ftrackinfo[drive][1][0],0,ftracklen[drive][1][0]);
                c=fdi2raw_loadtrack(fdih[drive],ftrackinfo[drive][1][1],fditiming,(track<<fdisides[drive])+1,&ftracklen[drive][1][1],&ftrackindex[drive][1][1],NULL,1);
                if (!c) memset(ftrackinfo[drive][1][1],0,ftracklen[drive][1][1]);
        }
        else
        {
                memset(ftrackinfo[drive][1][0],0,65536);
                memset(ftrackinfo[drive][1][1],0,65536);
                ftracklen[drive][1][0]=ftracklen[drive][1][1]=10000;
                ftrackindex[drive][1][0]=ftrackindex[drive][1][1]=100;
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
        fdirevs=0;
        fdisector=sector;
        fditrack=track;
        fdiside=side;
        fdidrive=drive;
        fdidensity=density;
        printf("Read sector %i %i %i %i\n",drive,side,track,sector);

        fdiread=1;
        fdireadpos=0;
}

void fdi_writesector(int drive, int sector, int track, int side, int density)
{
        fdirevs=0;
        fdisector=sector;
        fditrack=track;
        fdiside=side;
        fdidrive=drive;
        printf("Write sector %i %i %i %i\n",drive,side,track,sector);

        if (!fdif[drive] || (side && !fdids[drive]) || density)
        {
                fdinotfound=500;
                return;
        }
        fdiwrite=1;
        fdireadpos=0;
}

void fdi_readaddress(int drive, int track, int side, int density)
{
        fdirevs=0;
        fditrack=track;
        fdiside=side;
        fdidensity=density;
        fdidrive=drive;
        printf("Read address %i %i %i\n",drive,side,track);

        fdireadaddr=1;
        fdireadpos=0;
}

void fdi_format(int drive, int track, int side, int density)
{
        fdirevs=0;
        fditrack=track;
        fdiside=side;
        fdidensity=density;
        fdidrive=drive;
        printf("Format %i %i %i\n",drive,side,track);

        fdiwrite=1;
        fdireadpos=0;
}

uint16_t fdibuffer;
static int pollbytesleft=0,pollbitsleft=0;
static int readidpoll=0,readdatapoll=0,fdinextsector=0,inreadop=0;
uint8_t fdisectordat[1026];
int lastfdidat[2],sectorcrc[2];
static int sectorsize,fdcsectorsize;
static int ddidbitsleft=0;

uint8_t decodefm(uint16_t dat)
{
        uint8_t temp;
        temp=0;
        if (dat&0x0001) temp|=1;
        if (dat&0x0004) temp|=2;
        if (dat&0x0010) temp|=4;
        if (dat&0x0040) temp|=8;
        if (dat&0x0100) temp|=16;
        if (dat&0x0400) temp|=32;
        if (dat&0x1000) temp|=64;
        if (dat&0x4000) temp|=128;
        return temp;
}

static uint16_t crc;

void calccrc(uint8_t byte)
{
        crc = (crc << 8) ^ CRCTable[(crc >> 8)^byte];
}

void fdi_poll()
{
        int tempi,c;
        if (fdipos>=ftracklen[fdidrive][fdiside][fdidensity])
        {
//                printf("Looping! %i\n",fdipos);
                fdipos=0;
        }
        tempi=ftrackinfo[fdidrive][fdiside][fdidensity][((fdipos>>3)&0xFFFF)^1]&(1<<(7-(fdipos&7)));
        fdipos++;
        fdibuffer<<=1;
        fdibuffer|=(tempi?1:0);
        if (fdiwrite)
        {
                fdiwrite=0;
                fdcwriteprotect();
                return;
        }
        if (!fdiread && !fdireadaddr) return;
        if (fdipos==ftrackindex[fdidrive][fdiside][fdidensity])
        {
                fdirevs++;
                if (fdirevs==3)
                {
                        printf("Not found!\n");
                        fdcnotfound();
                        fdiread=fdireadaddr=0;
                        return;
                }
        }
        if (pollbitsleft)
        {
                pollbitsleft--;
                if (!pollbitsleft)
                {
                        pollbytesleft--;
                        if (pollbytesleft) pollbitsleft=16; /*Set up another word if we need it*/
                        if (readidpoll)
                        {
                                fdisectordat[5-pollbytesleft]=decodefm(fdibuffer);
                                if (fdireadaddr && pollbytesleft>1) fdcdata(fdisectordat[5-pollbytesleft]);
                                if (!pollbytesleft)
                                {
                                        if ((fdisectordat[0]==fditrack && fdisectordat[2]==fdisector) || fdireadaddr)
                                        {
                                                crc=(fdidensity)?0xcdb4:0xffff;
                                                calccrc(0xFE);
                                                for (c=0;c<4;c++) calccrc(fdisectordat[c]);
                                                if ((crc>>8)!=fdisectordat[4] || (crc&0xFF)!=fdisectordat[5])
                                                {
//                                                        printf("Header CRC error : %02X %02X %02X %02X\n",crc>>8,crc&0xFF,fdisectordat[4],fdisectordat[5]);
//                                                        dumpregs();
//                                                        exit(-1);
                                                        inreadop=0;
                                                        if (fdireadaddr)
                                                        {
                                                                fdcdata(fdisector);
                                                                fdcfinishread();
                                                        }
                                                        else             fdcheadercrcerror();
                                                        return;
                                                }
                                                if (fdisectordat[0]==fditrack && fdisectordat[2]==fdisector && fdiread && !fdireadaddr)
                                                {
                                                        fdinextsector=1;
                                                        readidpoll=0;
                                                        sectorsize=(1<<(fdisectordat[3]+7))+2;
                                                        fdcsectorsize=fdisectordat[3];
                                                }
                                                if (fdireadaddr)
                                                {
                                                        fdcfinishread();
                                                        fdireadaddr=0;
                                                }
                                        }
                                }
                        }
                        if (readdatapoll)
                        {
                                if (pollbytesleft>1)
                                {
                                        calccrc(decodefm(fdibuffer));
                                }
                                else
                                   sectorcrc[1-pollbytesleft]=decodefm(fdibuffer);
                                if (!pollbytesleft)
                                {
                                        fdiread=0;
                                        if ((crc>>8)!=sectorcrc[0] || (crc&0xFF)!=sectorcrc[1])// || (fditrack==79 && fdisect==4 && fdcside&1))
                                        {
                                                printf("Data CRC error : %02X %02X %02X %02X %i %04X %02X%02X %i\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1],fdipos,crc,sectorcrc[0],sectorcrc[1],ftracklen[0][0][fdidensity]);
                                                inreadop=0;
                                                fdcdata(decodefm(lastfdidat[1]));
                                                fdcfinishread();
                                                fdcdatacrcerror();
                                                readdatapoll=0;
                                                return;
                                        }
//                                        printf("End of FDI read %02X %02X %02X %02X\n",crc>>8,crc&0xFF,sectorcrc[0],sectorcrc[1]);
                                        fdcdata(decodefm(lastfdidat[1]));
                                        fdcfinishread();
                                }
                                else if (lastfdidat[1]!=0)
                                   fdcdata(decodefm(lastfdidat[1]));
                                lastfdidat[1]=lastfdidat[0];
                                lastfdidat[0]=fdibuffer;
                                if (!pollbytesleft)
                                   readdatapoll=0;
                        }
                }
        }
        if (fdibuffer==0x4489 && fdidensity)
        {
//                rpclog("Found sync\n");
                ddidbitsleft=17;
        }

        if (fdibuffer==0xF57E && !fdidensity)
        {
                pollbytesleft=6;
                pollbitsleft=16;
                readidpoll=1;
        }
        if ((fdibuffer==0xF56F || fdibuffer==0xF56A) && !fdidensity)
        {
                if (fdinextsector)
                {
                        pollbytesleft=sectorsize;
                        pollbitsleft=16;
                        readdatapoll=1;
                        fdinextsector=0;
                        crc=0xffff;
                        if (fdibuffer==0xF56A) calccrc(0xF8);
                        else                   calccrc(0xFB);
                        lastfdidat[0]=lastfdidat[1]=0;
                }
        }
        if (ddidbitsleft)
        {
                ddidbitsleft--;
                if (!ddidbitsleft)
                {
//                        printf("ID bits over %04X %02X %i\n",fdibuffer,decodefm(fdibuffer),fdipos);
                        if (decodefm(fdibuffer)==0xFE)
                        {
//                                printf("Sector header\n");
                                pollbytesleft=6;
                                pollbitsleft=16;
                                readidpoll=1;
                        }
                        else if (decodefm(fdibuffer)==0xFB)
                        {
//                                printf("Data header\n");
                                if (fdinextsector)
                                {
                                        pollbytesleft=sectorsize;
                                        pollbitsleft=16;
                                        readdatapoll=1;
                                        fdinextsector=0;
                                        crc=0xcdb4;
                                        if (fdibuffer==0xF56A) calccrc(0xF8);
                                        else                   calccrc(0xFB);
                                        lastfdidat[0]=lastfdidat[1]=0;
                                }
                        }
                }
        }
}
