/*B-em v2.0 by Tom Walker
  ADFS disc support (really all double-density formats)*/

#include <stdio.h>
#include "b-em.h"

FILE *adff[2];
uint8_t trackinfoa[2][2][20*256];
int adl[2];
int adfsectors[2],adfsize[2],adftrackc[2];

int adfsector,adftrack,adfside,adfdrive;
int adfread,adfreadpos,adfwrite,adfreadaddr;
int adftime;
int adfnotfound;
int adfrsector=0;
int adfformat=0;

void adf_reset()
{
        adff[0]=adff[1]=0;
        adl[0]=adl[1]=0;
        adfnotfound=0;
}

void adf_load(int drive, char *fn)
{
        writeprot[drive]=0;
        adff[drive]=fopen(fn,"rb+");
        if (!adff[drive])
        {
                adff[drive]=fopen(fn,"rb");
                if (!adff[drive]) return;
                writeprot[drive]=1;
        }
        fwriteprot[drive]=writeprot[drive];
        fseek(adff[drive],-1,SEEK_END);
        if (ftell(adff[drive])>700*1024)
        {
                adl[drive]=1;
                adfsectors[drive]=5;
                adfsize[drive]=1024;
        }
        else
        {
                adl[drive]=0;
                adfsectors[drive]=16;
                adfsize[drive]=256;
        }
        drives[drive].seek=adf_seek;
        drives[drive].readsector=adf_readsector;
        drives[drive].writesector=adf_writesector;
        drives[drive].readaddress=adf_readaddress;
        drives[drive].poll=adf_poll;
        drives[drive].format=adf_format;
}

void adl_load(int drive, char *fn)
{
        writeprot[drive]=0;
        adff[drive]=fopen(fn,"rb+");
        if (!adff[drive])
        {
                adff[drive]=fopen(fn,"rb");
                if (!adff[drive]) return;
                writeprot[drive]=1;
        }
        fwriteprot[drive]=writeprot[drive];
        adl[drive]=1;
        drives[drive].seek=adf_seek;
        drives[drive].readsector=adf_readsector;
        drives[drive].writesector=adf_writesector;
        drives[drive].readaddress=adf_readaddress;
        drives[drive].poll=adf_poll;
        drives[drive].format=adf_format;
        adfsectors[drive]=16;
        adfsize[drive]=256;
}

void adl_loadex(int drive, char *fn, int sectors, int size)
{
        writeprot[drive]=0;
        adff[drive]=fopen(fn,"rb+");
        if (!adff[drive])
        {
                adff[drive]=fopen(fn,"rb");
                if (!adff[drive]) return;
                writeprot[drive]=1;
        }
        fwriteprot[drive]=writeprot[drive];
        fseek(adff[drive],-1,SEEK_END);
        adl[drive]=1;
        drives[drive].seek=adf_seek;
        drives[drive].readsector=adf_readsector;
        drives[drive].writesector=adf_writesector;
        drives[drive].readaddress=adf_readaddress;
        drives[drive].poll=adf_poll;
        drives[drive].format=adf_format;
        adfsectors[drive]=sectors;
        adfsize[drive]=size;
}

void adf_close(int drive)
{
        if (adff[drive]) fclose(adff[drive]);
        adff[drive]=NULL;
}

void adf_seek(int drive, int track)
{
        if (!adff[drive]) return;
//        rpclog("Seek %i %i %i %i\n",drive,track,adfsectors[drive],adfsize[drive]);
        adftrackc[drive]=track;
        if (adl[drive])
        {
                fseek(adff[drive],track*adfsectors[drive]*adfsize[drive]*2,SEEK_SET);
                fread(trackinfoa[drive][0],adfsectors[drive]*adfsize[drive],1,adff[drive]);
                fread(trackinfoa[drive][1],adfsectors[drive]*adfsize[drive],1,adff[drive]);
        }
        else
        {
                fseek(adff[drive],track*adfsectors[drive]*adfsize[drive],SEEK_SET);
                fread(trackinfoa[drive][0],adfsectors[drive]*adfsize[drive],1,adff[drive]);
        }
}
void adf_writeback(int drive, int track)
{
        if (!adff[drive]) return;
        if (adl[drive])
        {
                fseek(adff[drive],track*adfsectors[drive]*adfsize[drive]*2,SEEK_SET);
                fwrite(trackinfoa[drive][0],adfsectors[drive]*adfsize[drive],1,adff[drive]);
                fwrite(trackinfoa[drive][1],adfsectors[drive]*adfsize[drive],1,adff[drive]);
        }
        else
        {
                fseek(adff[drive],track*adfsectors[drive]*adfsize[drive],SEEK_SET);
                fwrite(trackinfoa[drive][0],adfsectors[drive]*adfsize[drive],1,adff[drive]);
        }
}

void adf_readsector(int drive, int sector, int track, int side, int density)
{
        adfsector=sector;
        adftrack=track;
        adfside=side;
        adfdrive=drive;
        if (adfsize[drive]!=256) adfsector--;
//        printf("ADFS Read sector %i %i %i %i   %i\n",drive,side,track,sector,adftrackc[drive]);

        if (!adff[drive] || (side && !adl[drive]) || !density || (track!=adftrackc[drive]))
        {
                adfnotfound=500;
                return;
        }
        adfread=1;
        adfreadpos=0;
}

void adf_writesector(int drive, int sector, int track, int side, int density)
{
        adfsector=sector;
        adftrack=track;
        adfside=side;
        adfdrive=drive;
        if (adfsize[drive]!=256) adfsector--;
//        printf("ADFS Write sector %i %i %i %i\n",drive,side,track,sector);

        if (!adff[drive] || (side && !adl[drive]) || !density || (track!=adftrackc[drive]))
        {
                adfnotfound=500;
                return;
        }
        adfwrite=1;
        adfreadpos=0;
}

void adf_readaddress(int drive, int track, int side, int density)
{
        adfdrive=drive;
        adftrack=track;
        adfside=side;
//        rpclog("Read address %i %i %i\n",drive,side,track);

        if (!adff[drive] || (side && !adl[drive]) || !density || (track!=adftrackc[drive]))
        {
                adfnotfound=500;
                return;
        }
        adfreadaddr=1;
        adfreadpos=0;
}

void adf_format(int drive, int track, int side, int density)
{
        adfdrive=drive;
        adftrack=track;
        adfside=side;

        if (!adff[drive] || (side && !adl[drive]) || !density || track!=adftrackc[drive])
        {
                adfnotfound=500;
                return;
        }
        adfsector=0;
        adfreadpos=0;
        adfformat=1;
}

void adf_poll()
{
        int c;
        adftime++;
        if (adftime<16) return;
        adftime=0;

        if (adfnotfound)
        {
                adfnotfound--;
                if (!adfnotfound)
                {
//                        rpclog("Not found!\n");
                        fdcnotfound();
                }
        }
        if (adfread && adff[adfdrive])
        {
//                if (!adfreadpos) rpclog("%i\n",adfsector*adfsize[adfdrive]);
                fdcdata(trackinfoa[adfdrive][adfside][(adfsector*adfsize[adfdrive])+adfreadpos]);
                adfreadpos++;
                if (adfreadpos==adfsize[adfdrive])
                {
//                        rpclog("Read %i bytes\n",adfreadpos);
                        adfread=0;
                        fdcfinishread();
                }
        }
        if (adfwrite && adff[adfdrive])
        {
                if (writeprot[adfdrive])
                {
                        fdcwriteprotect();
                        adfwrite=0;
                        return;
                }
//                printf("Write data %i\n",adfreadpos);
                c=fdcgetdata(adfreadpos==255);
                if (c==-1)
                {
//                        printf("Data overflow!\n");
//                        exit(-1);
                }
                trackinfoa[adfdrive][adfside][(adfsector*adfsize[adfdrive])+adfreadpos]=c;
                adfreadpos++;
                if (adfreadpos==adfsize[adfdrive])
                {
                        adfwrite=0;
                        fdcfinishread();
                        adf_writeback(adfdrive,adftrack);
                }
        }
        if (adfreadaddr && adff[adfdrive])
        {
                switch (adfreadpos)
                {
                        case 0: fdcdata(adftrack); break;
                        case 1: fdcdata(adfside); break;
                        case 2: fdcdata(adfrsector+(adfsize[adfdrive]!=256)?1:0); break;
                        case 3: fdcdata((adfsize[adfdrive]==256)?1:((adfsize[adfdrive]==512)?2:3)); break;
                        case 4: fdcdata(0); break;
                        case 5: fdcdata(0); break;
                        case 6:
                        adfreadaddr=0;
                        fdcfinishread();
//                        rpclog("Read addr - %i %i %i 1 0 0\n",adftrack,adfside,adfsector);
                        adfrsector++;
                        if (adfrsector==adfsectors[adfdrive]) adfrsector=0;
                        break;
                }
                adfreadpos++;
        }
        if (adfformat && adff[adfdrive])
        {
                if (writeprot[adfdrive])
                {
                        fdcwriteprotect();
                        adfformat=0;
                        return;
                }
                trackinfoa[adfdrive][adfside][(adfsector*adfsize[adfdrive])+adfreadpos]=0;
                adfreadpos++;
                if (adfreadpos==adfsize[adfdrive])
                {
                        adfreadpos=0;
                        adfsector++;
                        if (adfsector==adfsectors[adfdrive])
                        {
                                adfformat=0;
                                fdcfinishread();
                                adf_writeback(adfdrive,adftrack);
                        }
                }
        }
}
