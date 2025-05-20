/*B-em v2.2 by Tom Walker
  1770 FDC emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"
#include "ddnoise.h"
#include "6502.h"
#include "wd1770.h"
#include "disc.h"
#include "led.h"
#include "model.h"

/*
 * WD1770 status regsiter flags.  There are sometimes more than two names
 * for the same value because there are variations between the type 1
 * commands (seeks and steps) and the others.
 */
enum {
    WDS_MOTOR_ON      = 0x80,
    WDS_WRITE_PROTECT = 0x40,
    WDS_SPUN_UP       = 0x20,
    WDS_DELETED_DATA  = 0x20,
    WDS_SEEK_ERROR    = 0x10,
    WDS_NOT_FOUND     = 0x10,
    WDS_CRC_ERROR     = 0x08,
    WDS_TRACK0        = 0x04,
    WDS_LOST_DATA     = 0x04,
    WDS_DATA_REQ      = 0x02,
    WDS_INDEX_PULSE   = 0x02,
    WDS_BUSY          = 0x01
};

struct
{
    uint8_t command, oldcmd, sector, track, status, data, resetting;
    int curside;
    int density;
    int stepdir;
    uint8_t cmd_started, in_gap;
} wd1770;

static const uint8_t nmi_on_completion[5] = {
    1, // WD1770_ACORN
    1, // WD1770_MASTER
    0, // WD1770_OPUS
    0, // WD1770_SOLIDISK
    1  // WD1770_WATFORD
};

static const unsigned step_times[4] = { 6000, 12000, 20000, 30000 };
#define SETTLE_TIME (15 * 2000) /* 15ms in 2Mhz clock pulses */

static int bytenum;

static void wd1770_short_spindown(void)
{
    motorspin = 15000;
    fdc_time = 0;
}

void wd1770_setspindown()
{
    motorspin = 45000;
}

void wd1770_spinup()
{
    wd1770.status |= WDS_MOTOR_ON;
    if (!motoron) {
        motoron = 1;
        motorspin = 0;
        led_update((curdrive == 0) ? LED_DRIVE_0 : LED_DRIVE_1, true, 0);
        ddnoise_spinup();
        for (int i = 0; i < NUM_DRIVES; i++)
            if (drives[i].spinup)
                drives[i].spinup(i);
    }
    wd1770_setspindown();
}

static void wd1770_spindown()
{
    wd1770.status &= ~WDS_MOTOR_ON;
    if (motoron) {
        motoron = 0;
        led_update(LED_DRIVE_0, false, 0);
        led_update(LED_DRIVE_1, false, 0);
        ddnoise_spindown();
        for (int i = 0; i < NUM_DRIVES; i++)
            if (drives[i].spindown)
                drives[i].spindown(i);
    }
}

static void wd1770_begin_seek(unsigned cmd, const char *cmd_desc, int tracks)
{
    log_debug("wd1770: begin %s of %d tracks", cmd_desc, tracks);
    disc_seekrelative(curdrive, tracks, step_times[cmd & 3], (cmd & 0x04) ? SETTLE_TIME : 0);
    wd1770.status |= WDS_SPUN_UP;
}

static int data_count = 0;

static void wd1770_begin_read_sector(const char *variant)
{
    log_debug("wd1770: %s read sector drive=%d side=%d track=%d sector=%d dens=%d", variant, curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
    data_count = 0;
    wd1770.status = WDS_MOTOR_ON|WDS_BUSY;
    wd1770.in_gap = 0;
    disc_readsector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density|DISC_FLAG_DELD);
    bytenum = 0;
}

static void wd1770_begin_write_sector(const char *variant)
{
    log_debug("wd1770: %s write sector drive=%d side=%d track=%d sector=%d dens=%d", variant, curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
    wd1770.status = WDS_MOTOR_ON|WDS_DATA_REQ|WDS_BUSY;
    wd1770.in_gap = 0;
    unsigned flags = wd1770.density;
    if (wd1770.command & 1) /* write deleted data */
        flags |= DISC_FLAG_DELD;
    disc_writesector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, flags);
    bytenum = 0;
    nmi |= 2;
}

static void wd1770_write_fdc(uint16_t addr, uint8_t val)
{
    switch (addr & 0x03)
    {
    case 0: // Command register.
        if ((val & 0xf0) != 0xd0) {
            if (wd1770.status & WDS_BUSY) {
                log_warn("wd1770: attempt to write %s register with %02X when device busy rejected", "command", val);
                break;
            }
            wd1770.status |= WDS_BUSY;
            wd1770_spinup();
        }
        log_debug("wd1770: write command register, cmd=%02X", val);
        nmi &= ~1;
        wd1770.cmd_started = 0;
        wd1770.command = val;
        fdc_time = 32;
        break;

    case 1: // Track register
        if (wd1770.status & WDS_BUSY)
            log_warn("wd1770: attempt to write %s register with %02X when device busy rejected", "track", val);
        else {
            log_debug("wd1770: write track register, track=%02x", val);
            wd1770.track = val;
        }
        break;

    case 2: // Sector register
        if (wd1770.status & WDS_BUSY)
            log_warn("wd1770: attempt to write %s register with %02X when device busy rejected", "sector", val);
        else {
            log_debug("wd1770: write sector register, sector=%02x", val);
            wd1770.sector = val;
        }
        break;

    case 3: // Data register
        nmi &= ~2;
        wd1770.status &= ~WDS_DATA_REQ;
        wd1770.data = val;
        break;
    }
}

static void wd1770_maybe_reset(uint8_t val)
{
    if (val)
        wd1770.resetting = 0;
    else if (!wd1770.resetting) {
        wd1770_reset();
        wd1770.resetting = 1;
    }
}

/*
 * Process the drive selection bits common to the Acorn WD1770
 * interface in the Master and the Acorn WD1770 interface as used on
 * the B+ and as a daughter board for the BBC B.
 */

static void wd1770_wctl_adrive(uint8_t val)
{
    curdrive = (val & 0x02) ? 1 : 0;
    if (motoron) {
        led_update(LED_DRIVE_0, val & 0x01, 0);
        led_update(LED_DRIVE_1, val & 0x02, 0);
    }
}

/*
 * Process a write to the control latch for the Acorn WD1770 interface
 * as fitted to the B+ and as a daughter board for the BBC B.
 */

static void wd1770_wctl_acorn(uint8_t val)
{
    log_debug("wd1770: write acorn-style ctrl %02X", val);
    wd1770_maybe_reset(val & 0x20);
    wd1770_wctl_adrive(val);
    wd1770.curside =  (val & 0x04) ? 1 : 0;
    wd1770.density = (val & 0x08) ? 0 : DISC_FLAG_MFM;
}

/*
 * Process a write to the control latch for the Acorn WD1770 interface
 * as fitted to the BBC Master.
 */

static void wd1770_wctl_master(uint8_t val)
{
    log_debug("wd1770: write master-style ctrl %02X", val);
    wd1770_maybe_reset(val & 0x04);
    wd1770_wctl_adrive(val);
    wd1770.curside =  (val & 0x10) ? 1 : 0;
    wd1770.density = (val & 0x20) ? 0 : DISC_FLAG_MFM;
}

/*
 * Process the drive selection bit common to the non-Acorn WD1770
 * interfaces.
 */

static void wd1770_wctl_sdrive(uint8_t val)
{
    if (val) {
        curdrive = 1;
        if (motoron) {
            led_update(LED_DRIVE_0, false, 0);
            led_update(LED_DRIVE_1, true, 0);
        }
    }
    else {
        curdrive = 0;
        if (motoron) {
            led_update(LED_DRIVE_0, true, 0);
            led_update(LED_DRIVE_1, false, 0);
        }
    }
}

/*
 * Process a write to the control latch for the Opus WD1770
 * interface
 */

static void wd1770_wctl_opus(uint8_t val)
{
    log_debug("wd1770: write opus-style ctrl %02X", val);
    wd1770_wctl_sdrive(val & 0x01);
    wd1770.curside =  (val & 0x02) ? 1 : 0;
    wd1770.density = (val & 0x40) ? DISC_FLAG_MFM : 0;
}

/*
 * Process a write to the control latch for the Solidisk WD1770
 * interface.
 */
;
static void wd1770_wctl_stl(uint8_t val)
{
    log_debug("wd1770: write solidisk-style ctrl %02X", val);
    wd1770_wctl_sdrive(val & 0x01);
    wd1770.curside =  (val & 0x02) ? 1 : 0;
    wd1770.density = (val & 0x04) ? 0 : DISC_FLAG_MFM;
}

/*
 * Process a write to the control latch for the Watford Electronics
 * WD1770 interface.
 */

static void wd1770_wctl_watford(uint8_t val)
{
    log_debug("wd1770: write watford-style ctrl %02X", val);
    wd1770_maybe_reset(val & 0x08);
    wd1770_wctl_sdrive(val & 0x04);
    wd1770.curside =  (val & 0x02) ? 1 : 0;
    wd1770.density = (val & 0x01) ? 0 : DISC_FLAG_MFM;
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
            wd1770_write_fdc(addr, val);
        else
            wd1770_wctl_acorn(val);
        break;
    case FDC_MASTER:
        if (addr & 0x0008)
            wd1770_write_fdc(addr, val);
        else
            wd1770_wctl_master(val);
        break;
    case FDC_OPUS:
        if (addr & 0x0004)
            wd1770_wctl_opus(val);
        else
            wd1770_write_fdc(addr, val);
        break;
    case FDC_STL:
        if (addr & 0x0004)
            wd1770_wctl_stl(val);
        else
            wd1770_write_fdc(addr, val);
        break;
    case FDC_WATFORD:
        if (addr & 0x0004)
            wd1770_write_fdc(addr, val);
        else
            wd1770_wctl_watford(val);
        break;
    default:
        log_warn("wd1770: write to unrecognised fdc type %d: %04x=%02x\n", fdc_type, addr, val);
    }
}

static uint8_t wd1770_status(void)
{
    uint8_t status = wd1770.status;
    if (!(wd1770.command & 0x80)) { /* TYpe 1 commands. */
        if (drives[curdrive].curtrack == 0)
            status |= WDS_TRACK0; /* track0 signal */
        if (motoron && drives[curdrive].isindex)
            status |= WDS_INDEX_PULSE; /* index signal */
    }
    return status;
}

static uint8_t wd1770_read_fdc(uint16_t addr)
{
    switch (addr & 0x03)
    {
    case 0: // Status register.
        nmi &= ~1;
        return wd1770_status();

    case 1: // Track register.
        return wd1770.track;

    case 2: // Sector register
        return wd1770.sector;

    case 3: // Data register.
        nmi &= ~2;
        wd1770.status &= ~WDS_DATA_REQ;
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
                return wd1770_read_fdc(addr);
            break;
        case FDC_MASTER:
            if (addr & 0x0008)
                return wd1770_read_fdc(addr);
            break;
        case FDC_OPUS:
            if (!(addr & 0x0004))
                return wd1770_read_fdc(addr);
            break;
        case FDC_STL:
            return wd1770_read_fdc(addr);
        case FDC_WATFORD:
            return wd1770_read_fdc(addr);
        default:
            log_warn("wd1770: read from unrecognised FDC type %d, addr=%04X", fdc_type, addr);
    }
    return 0xFE;
}

static void wd1770_completed(void)
{
    wd1770.status &= ~WDS_BUSY;
    wd1770_setspindown();
    if (nmi_on_completion[fdc_type - FDC_ACORN])
        nmi |= 1;
}

static void wd1770_cmd_next(unsigned cmd)
{
    switch(cmd >> 4) {
        case 0x0: /*Restore*/
        case 0x1: /*Seek*/
        case 0x3: /*Step*/
        case 0x5: /*Step in*/
        case 0x7: /*Step out*/
            wd1770.track = drives[curdrive].curtrack;
            // FALLTHROUGH
        case 0x2: /*Step*/
        case 0x4: /*Step in*/
        case 0x6: /*Step out*/
            wd1770.status &= ~WDS_BUSY;
            if (cmd & 0x04 && !disc_verify(curdrive, wd1770.track, wd1770.density|DISC_FLAG_DELD))
                wd1770.status |= WDS_SEEK_ERROR; /* not verified */
            wd1770_setspindown();
            if (nmi_on_completion[fdc_type - FDC_ACORN])
                nmi |= 1;
            break;

        case 0x8: /* Read single sector */
        case 0xA: /* Write single sector*/
        case 0xE: /* Read track  */
        case 0xF: /* Write track */
            wd1770_completed();
            break;

        case 0x9:
            if (wd1770.status & (WDS_WRITE_PROTECT|WDS_NOT_FOUND|WDS_CRC_ERROR)) /* error */
                wd1770_completed();
            else if (wd1770.in_gap) {
                wd1770.sector++;
                wd1770_begin_read_sector("continue multiple");
            }
            else {
                log_debug("wd1770: multi-sector read, inter-sector gap");
                wd1770.in_gap = 1;
                fdc_time = 5000;
            }
            break;

        case 0xB:
            if (wd1770.status & (WDS_WRITE_PROTECT|WDS_NOT_FOUND|WDS_CRC_ERROR)) /* error */
                wd1770_completed();
            else if (wd1770.in_gap) {
                wd1770.sector++;
                wd1770_begin_write_sector("continue multiple");
            }
            else {
                log_debug("wd1770: multi-sector write, inter-sector gap");
                wd1770.in_gap = 1;
                fdc_time = 5000;
            }
            break;

        case 0xC: /*Read address*/
            wd1770_completed();
            wd1770.sector = wd1770.track;
            break;

        case 0xD:
            if (wd1770.status & WDS_BUSY)
                wd1770.status &= ~WDS_BUSY;
            else if (motoron)
                wd1770.status = WDS_MOTOR_ON|WDS_SPUN_UP;
            else
                wd1770.status = 0;
            if ((wd1770.oldcmd & 0xc) && nmi_on_completion[fdc_type - FDC_ACORN])
                nmi |= 1;
            wd1770_setspindown();
            break;
    }
}

static void wd1770_cmd_start(unsigned cmd)
{
    switch(cmd >> 4) {
        case 0x0: /*Restore*/
            wd1770.status = WDS_MOTOR_ON|WDS_SPUN_UP|WDS_BUSY;
            disc_seek0(curdrive, step_times[cmd & 3], (cmd & 0x04) ? SETTLE_TIME : 0);
            break;

        case 0x1: /*Seek*/
            wd1770_begin_seek(cmd, "seek", wd1770.data - wd1770.track);
            break;

        case 0x2:
        case 0x3: /*Step*/
            wd1770_begin_seek(cmd, "step", wd1770.stepdir);
            break;

        case 0x4:
        case 0x5: /*Step in*/
            wd1770.stepdir = 1;
            wd1770_begin_seek(cmd, "step in", 1);
            break;

        case 0x6:
        case 0x7: /*Step out*/
            wd1770.stepdir = -1;
            wd1770_begin_seek(cmd, "step out", -1);
            break;

        case 0x8: /*Read sector*/
            wd1770_begin_read_sector("begin single");
            break;

        case 0x9: /* read multiple sectors*/
            wd1770_begin_read_sector("begin multiple");
            break;

        case 0xA: /*Write sector*/
            wd1770_begin_write_sector("begin single");
            break;

        case 0xB: /*write multiple sectors */
            wd1770_begin_write_sector("begin multiple");
            break;

        case 0xC: /*Read address*/
            log_debug("wd1770: read address side=%d track=%d dens=%d", wd1770.curside, wd1770.track, wd1770.density);
            wd1770.status = WDS_MOTOR_ON|WDS_BUSY;
            disc_readaddress(curdrive, wd1770.curside, wd1770.density);
            bytenum = 0;
            break;

        case 0xD:
            log_debug("wd1770: force interrupt, status=%02X", wd1770.status);
            disc_abort(curdrive);
            fdc_time = 200;
            break;

        case 0xE: /* read track */
            log_debug("wd1770: read track side=%d track=%d dens=%d\n", wd1770.curside, wd1770.track, wd1770.density);
            wd1770.status = WDS_MOTOR_ON|WDS_DATA_REQ|WDS_BUSY;
            nmi |= 2;
            disc_readtrack(curdrive, wd1770.curside, wd1770.density);
            break;

        case 0xF: /*Write track*/
            log_debug("wd1770: write track side=%d track=%d dens=%d\n", wd1770.curside, wd1770.track, wd1770.density);
            wd1770.status = WDS_MOTOR_ON|WDS_DATA_REQ|WDS_BUSY;
            nmi |= 2;
            disc_writetrack(curdrive, wd1770.curside, wd1770.density);
            break;
    }
    wd1770.oldcmd = wd1770.command;
    wd1770.cmd_started = 1;
}

static void wd1770_callback()
{
    log_debug("wd1770: fdc callback for cmd %02X", wd1770.command);
    fdc_time = 0;
    if (wd1770.cmd_started)
        wd1770_cmd_next(wd1770.command);
    else {
        wd1770.cmd_started = 1;
        wd1770_cmd_start(wd1770.command);
    }
}

void wd1770_data(uint8_t dat)
{
    if (wd1770.status & WDS_BUSY) {
        if (wd1770.status & WDS_DATA_REQ) { // Register already full.
            log_debug("wd1770: data overrun, %02X discarded", dat);
            wd1770.status |= WDS_LOST_DATA; // Set lost data (overrun).
        }
        else {
            wd1770.data = dat;
            wd1770.status |= WDS_DATA_REQ;
            nmi |= 0x02;
        }
    }
}

static void wd1770_finishread(bool deleted)
{
    log_debug("wd1770: data i/o finished, deleted=%u, nmi=%02X", deleted, nmi);
    wd1770.status &= WDS_MOTOR_ON|WDS_DATA_REQ|WDS_BUSY;
    if (deleted)
        wd1770.status |= WDS_DELETED_DATA;
    fdc_time = 100;
}

static void wd1770_fault(uint8_t flags, const char *desc)
{
    log_debug("wd1770: %s", desc);
    wd1770.status |= flags;
    if (nmi_on_completion[fdc_type - FDC_ACORN])
        fdc_time = 200;
    else {
        wd1770_short_spindown();
        wd1770.status &= ~WDS_BUSY;
    }
}

static void wd1770_notfound()
{
    wd1770_fault(WDS_NOT_FOUND, "not found");
}

static void wd1770_datacrcerror(bool deleted)
{
    uint8_t flags = WDS_CRC_ERROR;
    if (deleted)
        flags |= WDS_DELETED_DATA;
    wd1770_fault(flags, "data CRC error");
}

static void wd1770_headercrcerror()
{
    wd1770_fault(WDS_NOT_FOUND|WDS_CRC_ERROR, "header CRC error");
}

static int wd1770_getdata(int last)
{
    if (wd1770.status & WDS_DATA_REQ) {
        log_debug("wd1770: getdata: no data in register (underrun)");
        wd1770.status |= WDS_LOST_DATA;
        return -1;
    }
    else {
        if (!last) {
            nmi |= 0x02;
            wd1770.status |= WDS_DATA_REQ;
        }
        return wd1770.data;
    }
}

static void wd1770_writeprotect()
{
    wd1770_fault(WDS_WRITE_PROTECT, "write protect");
}

void wd1770_reset()
{
    if (fdc_type >= FDC_ACORN) { /* if FDC is a 1770 */
        log_debug("wd1770: reset 1770");
        nmi = 0;
        wd1770.command = 0;
        wd1770.status = 0;
        wd1770.sector = 1;
        motorspin = 0;
        fdc_time = 0;
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
        if (motoron)
            wd1770.status |= WDS_MOTOR_ON;
    }
}
