/*B-em v2.2 by Tom Walker
  1770 FDC emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"
#include "ddnoise.h"
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
    uint8_t in_gap;
} wd1770;

static uint8_t nmi_on_completion[5] = {
    1, // WD1770_ACORN
    1, // WD1770_MASTER
    0, // WD1770_OPUS
    0, // WD1770_SOLIDISK
    1  // WD1770_WATFORD
};

static int bytenum;

void wd1770_reset()
{
    nmi = 0;
    wd1770.status = 0;
    motorspin = 0;
    log_debug("wd1770: reset 1770");
    fdc_time = 0;
    if (fdc_type >= FDC_ACORN) {
        fdc_callback       = wd1770_callback;
        fdc_data           = wd1770_data;
        fdc_spindown       = wd1770_spindown;
        fdc_finishread     = wd1770_finishread;
        fdc_notfound       = wd1770_notfound;
        fdc_datacrcerror   = wd1770_datacrcerror;
        fdc_headercrcerror = wd1770_headercrcerror;
        fdc_writeprotect   = wd1770_writeprotect;
        fdc_getdata        = wd1770_getdata;
        motorspin = 45000;
    }
}

void wd1770_spinup()
{
    wd1770.status |= 0x80;
    if (!motoron) {
        motoron = 1;
        motorspin = 0;
        ddnoise_spinup();
    }
}

void wd1770_spindown()
{
    wd1770.status &= ~0x80;
    if (motoron) {
        motoron = 0;
        ddnoise_spindown();
    }
}

void wd1770_setspindown()
{
    motorspin = 45000;
}

static void short_spindown(void)
{
    motorspin = 15000;
    fdc_time = 0;
}

#define track0 (wd1770.curtrack ? 0 : 4)

static int data_count = 0;

static void begin_read_sector(const char *variant)
{
    log_debug("wd1770: %s read sector drive=%d side=%d track=%d sector=%d dens=%d", variant, curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
    data_count = 0;
    wd1770.status = 0x80 | 0x1;
    wd1770.in_gap = 0;
    disc_readsector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
    bytenum = 0;
}

static void begin_write_sector(const char *variant)
{
    log_debug("wd1770: %s write sector drive=%d side=%d track=%d sector=%d dens=%d", variant, curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
    wd1770.status = 0x80 | 0x1;
    disc_writesector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
    bytenum = 0;
    nmi |= 2;
    wd1770.status |= 2;
    //Carlo Concari: wait for first data byte before starting sector write
    wd1770.written = 0;
}

static void write_1770(uint16_t addr, uint8_t val)
{
    switch (addr & 0x03)
    {
    case 0:
        nmi &= ~1;
        if (wd1770.status & 1 && (val >> 4) != 0xD) {
            log_debug("wd1770: command %02X rejected", val);
            return;
        }
        if ((val >> 4) != 0xD)/* && !(val&8)) */
            wd1770_spinup();
        switch (val >> 4)
        {
        case 0x0: /*Restore*/
            log_debug("wd1770: restore");
            wd1770.curtrack = 0;
            wd1770.status = 0x80 | 0x21 | track0;
            disc_seek(curdrive, 0);
            break;

        case 0x1: /*Seek*/
            log_debug("wd1770: seek track=%02d\n", wd1770.track);
            wd1770.curtrack = wd1770.data;
            wd1770.status = 0x80 | 0x21 | track0;
            disc_seek(curdrive, wd1770.curtrack);
            break;

        case 0x2:
        case 0x3: /*Step*/
            log_debug("wd1770: step");
            wd1770.status = 0x80 | 0x21 | track0;
            wd1770.curtrack += wd1770.stepdir;
            if (wd1770.curtrack < 0)
                wd1770.curtrack = 0;
            disc_seek(curdrive, wd1770.curtrack);
            break;

        case 0x4:
        case 0x5: /*Step in*/
            log_debug("wd1770: step in");
            wd1770.status = 0x80 | 0x21 | track0;
            wd1770.curtrack++;
            disc_seek(curdrive, wd1770.curtrack);
            wd1770.stepdir = 1;
            break;

        case 0x6:
        case 0x7: /*Step out*/
            log_debug("wd1770: step out");
            wd1770.status = 0x80 | 0x21 | track0;
            wd1770.curtrack--;
            if (wd1770.curtrack < 0)
                wd1770.curtrack = 0;
            disc_seek(curdrive, wd1770.curtrack);
            wd1770.stepdir = -1;
            break;

        case 0x8: /*Read sector*/
            begin_read_sector("start single");
            break;

        case 0x9: /* read multiple sectors*/
            begin_read_sector("start multiple");
            break;

        case 0xA: /*Write sector*/
            begin_write_sector("start single");
            break;

        case 0xB: /*write multiple sectors */
            begin_write_sector("start multiple");
            break;

        case 0xC: /*Read address*/
            log_debug("wd1770: read address side=%d track=%d dens=%d", wd1770.curside, wd1770.track, wd1770.density);
            wd1770.status = 0x80 | 0x1;
            disc_readaddress(curdrive, wd1770.track, wd1770.curside, wd1770.density);
            bytenum = 0;
            break;

        case 0xD: /*Force interrupt*/
            log_debug("wd1770: force interrupt");
            disc_abort(curdrive);
            if (wd1770.status & 0x01)
                wd1770.status &= ~1;
            else
                wd1770.status = 0x80 | 0x20 | track0;
            if (((val & 0xc) || (wd1770.command >> 4) == 0xB) && nmi_on_completion[fdc_type - FDC_ACORN])
                nmi = 1;
            wd1770_setspindown();
            break;

        case 0xF: /*Write track*/
            log_debug("wd1770: write track side=%d track=%d dens=%d, ctrl=%d\n", wd1770.curside, wd1770.track, wd1770.density, wd1770.ctrl);
            wd1770.status = 0x80 | 0x1;
            disc_format(curdrive, wd1770.track, wd1770.curside, wd1770.density);
            break;

        default:
            log_debug("wd1770: bad WD1770 command %02X",val);
            if (nmi_on_completion[fdc_type - FDC_ACORN])
                nmi = 1;
            wd1770.status = 0x90;
            short_spindown();
            break;
        }
        wd1770.command = val;
        break;

    case 1: // Track register
        log_debug("wd1770: write track register, track=%02x", val);
        wd1770.track = val;
        break;

    case 2: // Sector register
        log_debug("wd1770: write sector register, sector=%02x", val);
        wd1770.sector = val;
        break;

    case 3: // Data register
        nmi &= ~2;
        wd1770.status &= ~2;
        wd1770.data = val;
        wd1770.written = 1;
        break;
    }
}

static void write_ctrl_acorn(uint8_t val)
{
    log_debug("wd1770: write acorn-style ctrl %02X", val);
    if (val & 0x20)
        wd1770_reset();
    wd1770.ctrl = val;
    curdrive = (val & 0x02) ? 1 : 0;
    wd1770.curside =  (wd1770.ctrl & 0x04) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x08);
}

static void write_ctrl_master(uint8_t val)
{
    log_debug("wd1770: write master-style ctrl %02X", val);
    if (val & 0x04)
        wd1770_reset();
    wd1770.ctrl = val;
    curdrive = (val & 2) ? 1 : 0;
    wd1770.curside =  (wd1770.ctrl & 0x10) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x20);
}

static void write_ctrl_opus(uint8_t val)
{
    log_debug("wd1770: write opus-style ctrl %02X", val);
    wd1770.ctrl = val;
    curdrive = (val & 0x01);
    wd1770.curside =  (wd1770.ctrl & 0x02) ? 1 : 0;
    wd1770.density = (wd1770.ctrl & 0x40);
}

static void write_ctrl_stl(uint8_t val)
{
    log_debug("wd1770: write solidisk-style ctrl %02X", val);
    wd1770.ctrl = val;
    curdrive = (val & 0x01);
    wd1770.curside =  (wd1770.ctrl & 0x02) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x04);
}

static void write_ctrl_watford(uint8_t val)
{
    log_debug("wd1770: write watford-style ctrl %02X", val);
    if (val & 0x80)
        wd1770_reset();
    wd1770.ctrl = val;
    curdrive = (val & 0x04) ? 1 : 0;
    wd1770.curside =  (wd1770.ctrl & 0x02) ? 1 : 0;
    wd1770.density = !(val & 0x01);
}

void wd1770_write(uint16_t addr, uint8_t val)
{
    switch (fdc_type)
    {
    case FDC_NONE:
    case FDC_I8271:
        log_warn("wd1770: write to WD1770 when no WD1770 present, %04x=%02x\n", addr, val);
        break;
    case FDC_ACORN:
        if (addr & 0x0004)
            write_1770(addr, val);
        else
            write_ctrl_acorn(val);
        break;
    case FDC_MASTER:
        if (addr & 0x0008)
            write_1770(addr, val);
        else
            write_ctrl_master(val);
        break;
    case FDC_OPUS:
        if (addr & 0x0004)
            write_ctrl_opus(val);
        else
            write_1770(addr, val);
        break;
    case FDC_STL:
        if (addr & 0x0004)
            write_ctrl_stl(val);
        else
            write_1770(addr, val);
        break;
    case FDC_WATFORD:
        log_debug("wd1770: write to watford WD1770 board: %04x=%02x, pc=%04x", addr, val, pc);
        if (addr & 0x0004)
            write_1770(addr, val);
        else
            write_ctrl_watford(val);
        break;
    default:
        log_warn("wd1770: write to unrecognised fdc type %d: %04x=%02x\n", fdc_type, addr, val);
    }
}

static uint8_t read_1770(uint16_t addr)
{
    switch (addr & 0x03)
    {
    case 0: // Status register.
        nmi &= ~1;
        //log_debug("wd1770: status %02X", wd1770.status);
        return wd1770.status;

    case 1: // Track register.
        return wd1770.track;

    case 2: // Sector register
        return wd1770.sector;

    case 3: // Data register.
        nmi &= ~2;
        wd1770.status &= ~2;
//        log_debug("wd1770: read data %02X %04X\n",wd1770.data,pc);
        return wd1770.data;
    }
    log_debug("wd1770: returning unmapped status");
    return 0xFE;
}

uint8_t wd1770_read(uint16_t addr)
{
    switch (fdc_type)
    {
        case FDC_NONE:
        case FDC_I8271:
            log_warn("wd1770: read from WD1770 when no WD1770 present, addr=%04X", addr);
            break;
        case FDC_ACORN:
            if (addr & 0x0004)
                return read_1770(addr);
            break;
        case FDC_MASTER:
            if (addr & 0x0008)
                return read_1770(addr);
            break;
        case FDC_OPUS:
            if (!(addr & 0x0004))
                return read_1770(addr);
            break;
        case FDC_STL:
            return read_1770(addr);
        case FDC_WATFORD:
            return read_1770(addr);
        default:
            log_warn("wd1770: read from unrecognised FDC type %d, addr=%04X", fdc_type, addr);
    }
    return 0xFE;
}

void wd1770_callback()
{
    log_debug("wd1770: fdc callback %02X",wd1770.command);
    fdc_time = 0;
    switch (wd1770.command >> 4)
    {
    case 0: /*Restore*/
    case 1: /*Seek*/
    case 3: /*Step*/
    case 5: /*Step in*/
    case 7: /*Step out*/
        wd1770.track = wd1770.curtrack;
    case 2: /*Step*/
    case 4: /*Step in*/
    case 6: /*Step out*/
        if (wd1770.command & 0x04 && !disc_verify(curdrive, wd1770.curtrack, wd1770.density))
            wd1770.status = 0x90 | track0;
        else
            wd1770.status = 0x80 | track0;
        wd1770_setspindown();
        if (nmi_on_completion[fdc_type - FDC_ACORN])
            nmi |= 1;
        break;

    case 8: /*Read sector*/
        wd1770.status = 0x80;
        wd1770_setspindown();
        if (nmi_on_completion[fdc_type - FDC_ACORN])
            nmi |= 1;
        break;

    case 9:
        if (wd1770.in_gap) {
            wd1770.sector++;
            begin_read_sector("continue multiple");
        } else {
            log_debug("wd1770: multi-sector read, inter-sector gap");
            wd1770.in_gap = 1;
            fdc_time = 5000;
        }
        break;
    case 0xA: /*Write sector*/
        wd1770.status = 0x80;
        wd1770_setspindown();
        if (nmi_on_completion[fdc_type - FDC_ACORN])
            nmi |= 1;
        break;

    case 0xB:
        if (wd1770.in_gap) {
            wd1770.sector++;
            begin_write_sector("continue multiple");
        } else {
            log_debug("wd1770: multi-sector write, inter-sector gap");
            wd1770.in_gap = 1;
            fdc_time = 5000;
        }
        break;

    case 0xC: /*Read address*/
        wd1770.status = 0x80;
        wd1770_setspindown();
        if (nmi_on_completion[fdc_type - FDC_ACORN])
            nmi |= 1;
        wd1770.sector = wd1770.track;
        break;

    case 0xD: /* force interrupt */
        break;

    case 0xF: /*Write tracl*/
        wd1770.status = 0x80;
        wd1770_setspindown();
        if (nmi_on_completion[fdc_type - FDC_ACORN])
            nmi |= 1;
        break;
    }
}

void wd1770_data(uint8_t dat)
{
    if (wd1770.status & 0x01) {
        wd1770.data = dat;
        wd1770.status |= 2;
        nmi |= 2;
    }
}

void wd1770_finishread()
{
    log_debug("wd1770: data i/o finished");
    fdc_time = 200;
}

void wd1770_notfound()
{
    log_debug("wd1770: not found");
    nmi |= nmi_on_completion[fdc_type - FDC_ACORN];
    wd1770.status = 0x90;
    short_spindown();
}

void wd1770_datacrcerror()
{
    log_debug("wd1770: data CRC error");
    nmi = nmi_on_completion[fdc_type - FDC_ACORN];
    wd1770.status = 0x88;
    short_spindown();
}

void wd1770_headercrcerror()
{
    log_debug("wd1770: header CRC error");
    nmi = nmi_on_completion[fdc_type - FDC_ACORN];
    wd1770.status = 0x98;
    short_spindown();
}

int wd1770_getdata(int last)
{
    //log_debug("wd1770: disc get data");
    if (!wd1770.written) {
        log_debug("wd1770: getdata: no data in register");
        return -1;
    }
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
    nmi = nmi_on_completion[fdc_type - FDC_ACORN];
    wd1770.status = 0xC0;
    wd1770_setspindown();
}
