/*B-em v2.2 by Tom Walker
  1770 FDC emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"
#include "6502.h"
#include "wd1770.h"
#include "disc.h"
#include "model.h"

#define ABS(x) (((x)>0)?(x):-(x))

void wd1770_callback();
void wd1770_data(uint8_t dat);
void wd1770_spindown();
void wd1770_finishread();
void wd1770_notfound();
void wd1770_datacrcerror();
void wd1770_headercrcerror();
void wd1770_writeprotect();
int  wd1770_getdata(int last);

struct
{
        uint8_t command, sector, track, status, data;
        uint8_t ctrl;
        int curside,curtrack;
        int density;
        int written;
        int stepdir;
} wd1770;

int byte;

void wd1770_reset()
{
        nmi = 0;
        wd1770.status = 0;
        motorspin = 0;
//        bem_debug("Reset 1770\n");
        fdc_time = 0;
        if (WD1770)
        {
                fdc_callback       = wd1770_callback;
                fdc_data           = wd1770_data;
                fdc_spindown       = wd1770_spindown;
                fdc_finishread     = wd1770_finishread;
                fdc_notfound       = wd1770_notfound;
                fdc_datacrcerror   = wd1770_datacrcerror;
                fdc_headercrcerror = wd1770_headercrcerror;
                fdc_writeprotect   = wd1770_writeprotect;
                fdc_getdata        = wd1770_getdata;
        }
        motorspin = 45000;
}

void wd1770_spinup()
{
        wd1770.status |= 0x80;
        motoron = 1;
        motorspin = 0;
}

void wd1770_spindown()
{
        wd1770.status &= ~0x80;
        motoron = 0;
}

void wd1770_setspindown()
{
        motorspin = 45000;
}

#define track0 (wd1770.curtrack ? 4 : 0)

void wd1770_write(uint16_t addr, uint8_t val)
{
//        bem_debugf("Write 1770 %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0xFE80:
//                        bem_debugf("Write CTRL FE80 %02X\n",val);
                wd1770.ctrl = val;
                curdrive = (val & 2) ? 1 : 0;
                wd1770.curside =  (wd1770.ctrl & 4) ? 1 : 0;
                wd1770.density = !(wd1770.ctrl & 8);
                break;
                case 0xFE24:
//                        bem_debugf("Write CTRL FE24 %02X\n",val);
                wd1770.ctrl = val;
                curdrive = (val & 2) ? 1 : 0;
                wd1770.curside =  (wd1770.ctrl & 16) ? 1 : 0;
                wd1770.density = !(wd1770.ctrl & 0x20);
                break;
                case 0xFE84:
                case 0xFE28:
                if (wd1770.status & 1 && (val >> 4) != 0xD) { bem_debug("Command rejected\n"); return; }
//                bem_debugf("FDC command %02X %i %i %i\n",val,wd1770.curside,wd1770.track,wd1770.sector);
                wd1770.command = val;
                if ((val >> 4) != 0xD)/* && !(val&8)) */wd1770_spinup();
                switch (val >> 4)
                {
                        case 0x0: /*Restore*/
                        wd1770.status = 0x80 | 0x21 | track0;
                        disc_seek(curdrive, 0);
                        break;

                        case 0x1: /*Seek*/
                        wd1770.status = 0x80 | 0x21 | track0;
                        disc_seek(curdrive, wd1770.data);
                        break;

                        case 0x2:
                        case 0x3: /*Step*/
                        wd1770.status = 0x80 | 0x21 | track0;
                        wd1770.curtrack += wd1770.stepdir;
                        if (wd1770.curtrack < 0) wd1770.curtrack = 0;
                        disc_seek(curdrive, wd1770.curtrack);
                        break;

                        case 0x4:
                        case 0x5: /*Step in*/
                        wd1770.status = 0x80 | 0x21 | track0;
                        wd1770.curtrack++;
                        disc_seek(curdrive, wd1770.curtrack);
                        wd1770.stepdir = 1;
                        break;
                        case 0x6:
                        case 0x7: /*Step out*/
                        wd1770.status = 0x80 | 0x21 | track0;
                        wd1770.curtrack--;
                        if (wd1770.curtrack < 0) wd1770.curtrack = 0;
                        disc_seek(curdrive, wd1770.curtrack);
                        wd1770.stepdir = -1;
                        break;

                        case 0x8: /*Read sector*/
                        wd1770.status = 0x80 | 0x1;
                        disc_readsector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
                        //printf("Read sector %i %i %i %i %i\n",curdrive,wd1770.sector,wd1770.track,wd1770.curside,wd1770.density);
                        byte = 0;
                        break;
                        case 0xA: /*Write sector*/
                        wd1770.status = 0x80 | 0x1;
                        disc_writesector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
                        byte = 0;
                        nmi |= 2;
                        wd1770.status |= 2;

//Carlo Concari: wait for first data byte before starting sector write
                        wd1770.written = 0;
                        break;
                        case 0xC: /*Read address*/
                        wd1770.status = 0x80 | 0x1;
                        disc_readaddress(curdrive, wd1770.track, wd1770.curside, wd1770.density);
                        byte = 0;
                        break;
                        case 0xD: /*Force interrupt*/
//                        bem_debug("Force interrupt\n");
                        fdc_time = 0;
                        wd1770.status = 0x80 | track0;
                        nmi = (val & 8) ? 1 : 0;
                        wd1770_spindown();
                        break;
                        case 0xF: /*Write track*/
                        wd1770.status = 0x80 | 0x1;
                        disc_format(curdrive, wd1770.track, wd1770.curside, wd1770.density);
                        break;

                        default:
//                                bem_debugf("Bad 1770 command %02X\n",val);
                        fdc_time = 0;
                        nmi = 1;
                        wd1770.status = 0x90;
                        wd1770_spindown();
                        break;
/*                        bem_debugf("Bad 1770 command %02X\n", val);
                        dumpregs();
                        mem_dump();
                        exit(-1);*/
                }
                break;
                case 0xFE85:
                case 0xFE29:
                wd1770.track = val;
                break;
                case 0xFE86:
                case 0xFE2A:
                wd1770.sector = val;
                break;
                case 0xFE87:
                case 0xFE2B:
                nmi &= ~2;
                wd1770.status &= ~2;
                wd1770.data = val;
                wd1770.written = 1;
                break;
        }
}

uint8_t wd1770_read(uint16_t addr)
{
//        bem_debugf("Read 1770 %04X %04X\n",addr,pc);
        switch (addr)
        {
                case 0xFE84:
                case 0xFE28:
                nmi &= ~1;
//                bem_debugf("Status %02X\n",wd1770.status);
                return wd1770.status;
                case 0xFE85:
                case 0xFE29:
                return wd1770.track;
                case 0xFE86:
                case 0xFE2A:
                return wd1770.sector;
                case 0xFE87:
                case 0xFE2B:
                nmi &= ~2;
                wd1770.status &= ~2;
//                bem_debugf("Read data %02X %04X\n",wd1770.data,pc);
                return wd1770.data;
        }
        return 0xFE;
}

void wd1770_callback()
{
//        bem_debugf("FDC callback %02X\n",wd1770.command);
        fdc_time = 0;
        switch (wd1770.command >> 4)
        {
                case 0: /*Restore*/
                wd1770.curtrack = wd1770.track = 0;
                wd1770.status = 0x80;
                wd1770_setspindown();
                nmi |= 1;
//                disc_seek(curdrive,0);
                break;
                case 1: /*Seek*/
                wd1770.curtrack = wd1770.track = wd1770.data;
                wd1770.status = 0x80 | track0;
                wd1770_setspindown();
                nmi |= 1;
//                disc_seek(curdrive,wd1770.curtrack);
                break;
                case 3: /*Step*/
                case 5: /*Step in*/
                case 7: /*Step out*/
                wd1770.track = wd1770.curtrack;
                case 2: /*Step*/
                case 4: /*Step in*/
                case 6: /*Step out*/
                wd1770.status = 0x80 | track0;
                wd1770_setspindown();
                nmi |= 1;
                break;

                case 8: /*Read sector*/
                wd1770.status = 0x80;
                wd1770_setspindown();
                nmi |= 1;
                break;
                case 0xA: /*Write sector*/
                wd1770.status = 0x80;
                wd1770_setspindown();
                nmi |= 1;
                break;
                case 0xC: /*Read address*/
                wd1770.status = 0x80;
                wd1770_setspindown();
                nmi |= 1;
                wd1770.sector = wd1770.track;
                break;
                case 0xF: /*Write tracl*/
                wd1770.status = 0x80;
                wd1770_setspindown();
                nmi |= 1;
                break;
        }
}

void wd1770_data(uint8_t dat)
{
        wd1770.data = dat;
        wd1770.status |= 2;
        nmi |= 2;
}

void wd1770_finishread()
{
        fdc_time = 200;
}

void wd1770_notfound()
{
//        bem_debug("Not found\n");
        fdc_time = 0;
        nmi = 1;
        wd1770.status = 0x90;
        wd1770_spindown();
}

void wd1770_datacrcerror()
{
//        bem_debug("Data CRC\n");
        fdc_time = 0;
        nmi = 1;
        wd1770.status = 0x88;
        wd1770_spindown();
}

void wd1770_headercrcerror()
{
//        bem_debug("Header CRC\n");
        fdc_time = 0;
        nmi = 1;
        wd1770.status = 0x98;
        wd1770_spindown();
}

int wd1770_getdata(int last)
{
//        bem_debug("Disc get data\n");
        if (!wd1770.written) return -1;
        if (!last)
        {
                nmi |= 2;
                wd1770.status |= 2;
        }
        wd1770.written = 0;
        return wd1770.data;
}

void wd1770_writeprotect()
{
        fdc_time = 0;
        nmi = 1;
        wd1770.status = 0xC0;
        wd1770_spindown();
}
