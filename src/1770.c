/*B-em v2.0 by Tom Walker
  1770 FDC emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"

#define ABS(x) (((x)>0)?(x):-(x))
int motoron,fdctime,disctime,motorspin;

void callback1770();
void data1770(uint8_t dat);
void spindown1770();
void finishread1770();
void notfound1770();
void datacrcerror1770();
void headercrcerror1770();
void writeprotect1770();
int getdata1770(int last);

struct
{
        uint8_t command,sector,track,status,data;
        uint8_t ctrl;
        int curside,curtrack;
        int density;
        int written;
        int stepdir;
} wd1770;

int byte;

void reset1770()
{
        nmi=0;
        wd1770.status=0;
        motorspin=0;
//        printf("Reset 1770\n");
        fdctime=0;
        if (WD1770)
        {
                fdccallback=callback1770;
                fdcdata=data1770;
                fdcspindown=spindown1770;
                fdcfinishread=finishread1770;
                fdcnotfound=notfound1770;
                fdcdatacrcerror=datacrcerror1770;
                fdcheadercrcerror=headercrcerror1770;
                fdcwriteprotect=writeprotect1770;
                fdcgetdata=getdata1770;
        }
        motorspin=45000;
}

void spinup1770()
{
        wd1770.status|=0x80;
        motoron=1;
        motorspin=0;
}

void spindown1770()
{
        wd1770.status&=~0x80;
        motoron=0;
}

void setspindown1770()
{
        motorspin=45000;
}

#define track0 (wd1770.curtrack?4:0)

void write1770(uint16_t addr, uint8_t val)
{
//        rpclog("Write 1770 %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0xFE80:
                wd1770.ctrl=val;
                if (val&2) curdrive=1;
                else       curdrive=0;
                wd1770.curside=(wd1770.ctrl&4)?1:0;
                wd1770.density=!(wd1770.ctrl&8);
                break;
                case 0xFE24:
                        //printf("Write CTRL %02X\n",val);
                wd1770.ctrl=val;
                if (val&2) curdrive=1;
                else       curdrive=0;
                wd1770.curside=(wd1770.ctrl&16)?1:0;
                wd1770.density=!(wd1770.ctrl&0x20);
                break;
                case 0xFE84:
                case 0xFE28:
                if (wd1770.status&1 && (val>>4)!=0xD) { printf("Command rejected\n"); return; }
//                printf("FDC command %02X %i %i %i\n",val,wd1770.curside,wd1770.track,wd1770.sector);
                wd1770.command=val;
                if ((val>>4)!=0xD)/* && !(val&8)) */spinup1770();
                switch (val>>4)
                {
                        case 0x0: /*Restore*/
                        wd1770.status=0x80|0x21|track0;
                        disc_seek(curdrive,0);
                        break;

                        case 0x1: /*Seek*/
                        wd1770.status=0x80|0x21|track0;
                        disc_seek(curdrive,wd1770.data);
                        break;

                        case 0x2:
                        case 0x3: /*Step*/
                        wd1770.status=0x80|0x21|track0;
                        wd1770.curtrack+=wd1770.stepdir;
                        if (wd1770.curtrack<0) wd1770.curtrack=0;
                        disc_seek(curdrive,wd1770.curtrack);
                        break;

                        case 0x4:
                        case 0x5: /*Step in*/
                        wd1770.status=0x80|0x21|track0;
                        wd1770.curtrack++;
                        disc_seek(curdrive,wd1770.curtrack);
                        wd1770.stepdir=1;
                        break;
                        case 0x6:
                        case 0x7: /*Step out*/
                        wd1770.status=0x80|0x21|track0;
                        wd1770.curtrack--;
                        if (wd1770.curtrack<0) wd1770.curtrack=0;
                        disc_seek(curdrive,wd1770.curtrack);
                        wd1770.stepdir=-1;
                        break;

                        case 0x8: /*Read sector*/
                        wd1770.status=0x80|0x1;
                        disc_readsector(curdrive,wd1770.sector,wd1770.track,wd1770.curside,wd1770.density);
                        byte=0;
                        break;
                        case 0xA: /*Write sector*/
                        wd1770.status=0x80|0x1;
                        disc_writesector(curdrive,wd1770.sector,wd1770.track,wd1770.curside,wd1770.density);
                        byte=0;
                        nmi|=2;
                        wd1770.status|=2;
                        break;
                        case 0xC: /*Read address*/
                        wd1770.status=0x80|0x1;
                        disc_readaddress(curdrive,wd1770.track,wd1770.curside,wd1770.density);
                        byte=0;
                        break;
                        case 0xD: /*Force interrupt*/
//                        printf("Force interrupt\n");
                        fdctime=0;
                        wd1770.status=0x80|track0;
                        nmi=(val&8)?1:0;
                        spindown1770();
                        break;
                        case 0xF: /*Write track*/
                        wd1770.status=0x80|0x1;
                        disc_format(curdrive,wd1770.track,wd1770.curside,wd1770.density);
                        break;

                        default:
//                                printf("Bad 1770 command %02X\n",val);
                        fdctime=0;
                        nmi=1;
                        wd1770.status=0x90;
                        spindown1770();
                        break;
/*                        printf("Bad 1770 command %02X\n",val);
                        dumpregs();
                        dumpram();
                        exit(-1);*/
                }
                break;
                case 0xFE85:
                case 0xFE29:
                wd1770.track=val;
                break;
                case 0xFE86:
                case 0xFE2A:
                wd1770.sector=val;
                break;
                case 0xFE87:
                case 0xFE2B:
                nmi&=~2;
                wd1770.status&=~2;
                wd1770.data=val;
                wd1770.written=1;
                break;
        }
}

uint8_t read1770(uint16_t addr)
{
        switch (addr)
        {
                case 0xFE84:
                case 0xFE28:
                nmi&=~1;
//                printf("Status %02X\n",wd1770.status);
                return wd1770.status;
                case 0xFE85:
                case 0xFE29:
                return wd1770.track;
                case 0xFE86:
                case 0xFE2A:
                return wd1770.sector;
                case 0xFE87:
                case 0xFE2B:
                nmi&=~2;
                wd1770.status&=~2;
//                printf("Read data %02X %04X\n",wd1770.data,pc);
                return wd1770.data;
        }
        return 0xFE;
}

void callback1770()
{
//        printf("FDC callback %02X\n",wd1770.command);
        fdctime=0;
        switch (wd1770.command>>4)
        {
                case 0: /*Restore*/
                wd1770.curtrack=wd1770.track=0;
                wd1770.status=0x80;
                setspindown1770();
                nmi|=1;
//                disc_seek(curdrive,0);
                break;
                case 1: /*Seek*/
                wd1770.curtrack=wd1770.track=wd1770.data;
                wd1770.status=0x80|track0;
                setspindown1770();
                nmi|=1;
//                disc_seek(curdrive,wd1770.curtrack);
                break;
                case 3: /*Step*/
                case 5: /*Step in*/
                case 7: /*Step out*/
                wd1770.track=wd1770.curtrack;
                case 2: /*Step*/
                case 4: /*Step in*/
                case 6: /*Step out*/
                wd1770.status=0x80|track0;
                setspindown1770();
                nmi|=1;
                break;

                case 8: /*Read sector*/
                wd1770.status=0x80;
                setspindown1770();
                nmi|=1;
                break;
                case 0xA: /*Write sector*/
                wd1770.status=0x80;
                setspindown1770();
                nmi|=1;
                break;
                case 0xC: /*Read address*/
                wd1770.status=0x80;
                setspindown1770();
                nmi|=1;
                wd1770.sector=wd1770.track;
                break;
                case 0xF: /*Write tracl*/
                wd1770.status=0x80;
                setspindown1770();
                nmi|=1;
                break;
        }
}

void data1770(uint8_t dat)
{
        wd1770.data=dat;
        wd1770.status|=2;
        nmi|=2;
}

void finishread1770()
{
        fdctime=200;
}

void notfound1770()
{
//        printf("Not found\n");
        fdctime=0;
        nmi=1;
        wd1770.status=0x90;
        spindown1770();
}

void datacrcerror1770()
{
//        printf("Data CRC\n");
        fdctime=0;
        nmi=1;
        wd1770.status=0x88;
        spindown1770();
}

void headercrcerror1770()
{
//        printf("Header CRC\n");
        fdctime=0;
        nmi=1;
        wd1770.status=0x98;
        spindown1770();
}

int getdata1770(int last)
{
//        printf("Disc get data\n");
        if (!wd1770.written) return -1;
        if (!last)
        {
                nmi|=2;
                wd1770.status|=2;
        }
        wd1770.written=0;
        return wd1770.data;
}

void writeprotect1770()
{
        fdctime=0;
        nmi=1;
        wd1770.status=0xC0;
        spindown1770();
}
