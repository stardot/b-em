// Ported to b-em 04/08/2016
/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2006  Jon Welch

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/

/* SCSI Support for Beebem */
/* Based on code written by Y. Tanaka */
/* 26/12/2011 JGH: Disk images at DiscsPath, not AppPath */

/*

Offset  Description                 Access
+00     data                        R/W
+01     read status                 R
+02     write select                W
+03     write irq enable            W

*/

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"
#include "main.h"
#include "scsi.h"
#include "6502.h"
#include "led.h"

#define SCSI_INT_NUM 16

bool scsi_enabled = false;

typedef enum {
    busfree,
    selection,
    command,
    execute,
    scsiread,
    scsiwrite,
    status,
    message
} phase_t;

typedef struct {
    phase_t phase;
    bool sel;
    bool msg;
    bool cd;
    bool io;
    bool bsy;
    bool req;
    bool irq;
    unsigned char cmd[10];
    int status;
    int message;
    unsigned char buffer[0x800];
    int blocks;
    int next;
    int offset;
    int length;
    int lastwrite;
    int lun;
    int code;
    int sector;
} scsi_t;

typedef struct scsi_disc scsidisc;

struct scsi_disc {
    bool (*ReadSector)(scsidisc *disc, unsigned char *buf, unsigned block);
    bool (*WriteSector)(scsidisc *disc, unsigned char *buf, unsigned block);
    ALLEGRO_PATH *path;
    FILE *dat_fp, *dsc_fp;
    unsigned blocks;
    unsigned char geom[33];
};

#define SCSI_DRIVES 4

static scsi_t scsi;
static scsidisc SCSIDisc[SCSI_DRIVES];

static inline unsigned CalcSector(unsigned char *cmd)
{
    return ((scsi.cmd[1] & 0x1f) << 16) | (scsi.cmd[2] << 8) | scsi.cmd[3];
}

static void BusFree(void)
{
    scsi.msg = false;
    scsi.cd = false;
    scsi.io = false;
    scsi.bsy = false;
    scsi.req = false;
    scsi.irq = false;

    scsi.phase = busfree;
}

static void Selection(int data)
{
    scsi.bsy = true;
    scsi.phase = selection;
}

static void Command(void)
{
    scsi.phase = command;

    scsi.io = false;
    scsi.cd = true;
    scsi.msg = false;

    scsi.offset = 0;
    scsi.length = 6;
}

static void Status(void)
{
    scsi.phase = status;

    scsi.io = true;
    scsi.cd = true;
    scsi.req = true;
}

static bool DiscTestUnitReady(unsigned char *buf)
{
    log_debug("scsi lun %d: test unit ready", scsi.lun);
    if (SCSIDisc[scsi.lun].dat_fp == NULL)
        return false;
    return true;
}

static void TestUnitReady(void)
{
    bool status;

    status = DiscTestUnitReady(scsi.cmd);
    if (status) {
        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
    }
    Status();
}

static int DiscRequestSense(unsigned char *cdb, unsigned char *buf)
{
    int size;

    size = cdb[4];
    if (size == 0)
        size = 4;

    switch (scsi.code) {
        case 0x00:
            buf[0] = 0x00;
            buf[1] = 0x00;
            buf[2] = 0x00;
            buf[3] = 0x00;
            break;
        case 0x21:
            buf[0] = 0x21;
            buf[1] = (scsi.sector >> 16) & 0xff;
            buf[2] = (scsi.sector >> 8) & 0xff;
            buf[3] = (scsi.sector & 0xff);
            break;
    }

    log_debug("scsi lun %d: request sense returning %d for sector %d", scsi.lun, size, scsi.sector);
    scsi.code = 0x00;
    scsi.sector = 0x00;

    return size;
}

static void RequestSense(void)
{
    scsi.length = DiscRequestSense(scsi.cmd, scsi.buffer);

    if (scsi.length > 0) {
        scsi.offset = 0;
        scsi.blocks = 1;
        scsi.phase = scsiread;
        scsi.io = true;
        scsi.cd = false;

        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;

        scsi.req = true;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
        Status();
    }
}

static bool DiscFormat(unsigned char *buf)
{
    scsidisc *sd = &SCSIDisc[scsi.lun];
    ALLEGRO_PATH *path = sd->path;
    const char *cpath;
    if (path)
        al_set_path_extension(path, ".dat");
    else {
        char name[50];
        snprintf(name, sizeof(name), "scsi/scsi%d", scsi.lun);
        path = find_cfg_dest(name, ".dat");
        if (!path) {
            log_error("scsi lun %d: unable to find destination for %s", scsi.lun, name);
            return false;
        }
        sd->path = path;
    }
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    if (sd->dat_fp)
        fclose(sd->dat_fp);
    if (!(sd->dat_fp = fopen(cpath, "wb+"))) {
        log_error("scsi lun %d: unable to open %s: %s", scsi.lun, cpath, strerror(errno));
        return false;
    }
    return true;
}

static void Format(void)
{
    bool status;

    status = DiscFormat(scsi.cmd);
    if (status) {
        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
    }
    Status();
}

static bool ReadWriteNone(scsidisc *sd, unsigned char *buf, unsigned block)
{
    return false;
}

static bool ReadSectorSimple(scsidisc *sd, unsigned char *buf, unsigned block)
{
    log_debug("scsi lun %d: read sector %u", scsi.lun, block);
    if (fseek(sd->dat_fp, block * 256, SEEK_SET))
        return false;
    if (fread(buf, 256, 1, sd->dat_fp) != 1 && ferror(sd->dat_fp)) {
        log_warn("scsi lun %d: read error: %s", scsi.lun, strerror(errno));
        return false;
    }
    return true;
}

static bool ReadSectorPadded(scsidisc *sd, unsigned char *buf, unsigned block)
{
    unsigned char padbuf[512];
    unsigned char *end = padbuf + sizeof(padbuf);
    if (fseek(sd->dat_fp, block * sizeof(padbuf), SEEK_SET))
        return false;
    if (fread(padbuf, sizeof(padbuf), 1, sd->dat_fp) != 1 && ferror(sd->dat_fp)) {
        log_warn("scsi lun %d: read error: %s", scsi.lun, strerror(errno));
        return false;
    }
    for (unsigned char *ptr = padbuf; ptr < end; ptr += 2)
        *buf++ = *ptr;
    return true;
}

static void Read6(void)
{
    scsidisc *sd = &SCSIDisc[scsi.lun];
    unsigned sector = CalcSector(scsi.cmd);
    scsi.blocks = scsi.cmd[4];
    log_debug("scsi lun %d: read6, sector=%u, blocks=%d", scsi.lun, sector, scsi.blocks);
    if (scsi.blocks == 0)
        scsi.blocks = 0x100;
    if (sd->ReadSector(sd, scsi.buffer, sector)) {
        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;

        scsi.length = 256;
        scsi.offset = 0;
        scsi.next = sector + 1;

        scsi.phase = scsiread;
        scsi.io = true;
        scsi.cd = false;

        scsi.req = true;
        autoboot = 0;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
        Status();
    }
}

static bool WriteSectorSimple(scsidisc *sd, unsigned char *buf, unsigned block)
{
    log_debug("scsi lun %d: write sector %d", scsi.lun, block);
    if (fseek(sd->dat_fp, block * 256, SEEK_SET))
        return false;
    if (fwrite(buf, 256, 1, sd->dat_fp) != 1) {
        log_warn("scsi lun %d: write error: %s", scsi.lun, strerror(errno));
        return false;
    }
    return true;
}

static bool WriteSectorPadded(scsidisc *sd, unsigned char *buf, unsigned block)
{
    unsigned char padbuf[512];
    unsigned char *end = padbuf + sizeof(padbuf);
    log_debug("scsi lun %d: write sector %d", scsi.lun, block);
    if (fseek(sd->dat_fp, block * sizeof(padbuf), SEEK_SET))
        return false;
    for (unsigned char *ptr = padbuf; ptr < end; ptr += 2)
        *ptr = *buf++;
    if (fwrite(padbuf, sizeof(padbuf), 1, sd->dat_fp) != 1) {
        log_warn("scsi lun %d: write error: %s", scsi.lun, strerror(errno));
        return false;
    }
    return true;
}

static void Write6(void)
{
    scsi.blocks = scsi.cmd[4];
    if (scsi.blocks == 0)
        scsi.blocks = 0x100;

    scsi.length = 256;

    scsi.status = (scsi.lun << 5) | 0x00;
    scsi.message = 0x00;

    scsi.next = CalcSector(scsi.cmd) + 1;
    scsi.offset = 0;

    scsi.phase = scsiwrite;
    scsi.cd = false;

    scsi.req = true;
}

static void Translate(void)
{
    scsi.buffer[0] = scsi.cmd[3];
    scsi.buffer[1] = scsi.cmd[2];
    scsi.buffer[2] = scsi.cmd[1] & 0x1f;
    scsi.buffer[3] = 0x00;

    scsi.length = 4;

    scsi.offset = 0;
    scsi.blocks = 1;
    scsi.phase = scsiread;
    scsi.io = true;
    scsi.cd = false;

    scsi.status = (scsi.lun << 5) | 0x00;
    scsi.message = 0x00;

    scsi.req = true;
}

static void ModeSelect(void)
{

    scsi.length = scsi.cmd[4];
    scsi.blocks = 1;

    scsi.status = (scsi.lun << 5) | 0x00;
    scsi.message = 0x00;

    scsi.next = 0;
    scsi.offset = 0;

    scsi.phase = scsiwrite;
    scsi.cd = false;

    scsi.req = true;
}

static bool DiscStartStop(unsigned char *buf)
{
    if (buf[4] & 0x02) {
        // Eject Disc
        FILE *fp = SCSIDisc[scsi.lun].dat_fp;
        log_debug("scsi lun %d: eject", scsi.lun);
        if (fp)
            fflush(fp);
    }
    else
        log_debug("scsi lun %d: start", scsi.lun);
    return true;
}

static void StartStop(void)
{
    bool status;

    status = DiscStartStop(scsi.cmd);
    if (status) {
        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
    }
    Status();
}

static int DiscModeSense(unsigned char *cdb, unsigned char *buf)
{
    scsidisc *sd = &SCSIDisc[scsi.lun];
    int size = cdb[4];
    if (size == 0)
        size = 22;
    if (size > sizeof(sd->geom))
        size = sizeof(sd->geom);
    memcpy(buf, sd->geom, sizeof(sd->geom));
    return size;
}

static void ModeSense(void)
{
    scsi.length = DiscModeSense(scsi.cmd, scsi.buffer);

    if (scsi.length > 0) {
        scsi.offset = 0;
        scsi.blocks = 1;
        scsi.phase = scsiread;
        scsi.io = true;
        scsi.cd = false;

        scsi.status = (scsi.lun << 5) | 0x00;
        scsi.message = 0x00;

        scsi.req = true;
    }
    else {
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
        Status();
    }
}

static void Verify(void)
{
    int sector = CalcSector(scsi.cmd);
    if (sector < SCSIDisc[scsi.lun].blocks)
        scsi.status = (scsi.lun << 5) | 0x00;
    else {
        scsi.code = 0x21;
        scsi.sector = sector;
        scsi.status = (scsi.lun << 5) | 0x02;
        scsi.message = 0x00;
    }
    scsi.message = 0x00;
    Status();
}

static void Execute(void)
{
    scsi.phase = execute;
    scsi.lun = (scsi.cmd[1]) >> 5;

    if (scsi.cmd[0] <= 0x1f) {
        log_debug("scsi lun %d: Execute 0x%02x, Param 1=0x%02x, Param 2=0x%02x, Param 3=0x%02x, Param 4=0x%02x, Param 5=0x%02x, Phase = %d, PC = 0x%04x", scsi.lun, scsi.cmd[0], scsi.cmd[1], scsi.cmd[2], scsi.cmd[3], scsi.cmd[4], scsi.cmd[5], scsi.phase, pc);
    }
    else {
        log_debug("scsi lun %d: Execute 0x%02x, Param 1=0x%02x, Param 2=0x%02x, Param 3=0x%02x, Param 4=0x%02x, Param 5=0x%02x, Param 6=0x%02x, Param 7=0x%02x, Param 8=0x%02x, Param 9=0x%02x, Phase = %d, PC = 0x%04x", scsi.lun, scsi.cmd[0], scsi.cmd[1], scsi.cmd[2], scsi.cmd[3], scsi.cmd[4], scsi.cmd[5], scsi.cmd[6], scsi.cmd[7], scsi.cmd[8], scsi.cmd[9], scsi.phase, pc);
    }

    log_debug("scsi lun %d: turning on LED", scsi.lun);
    led_update(LED_HARD_DISK_0 + scsi.lun, 1, 20);

    switch (scsi.cmd[0]) {
        case 0x00:
            TestUnitReady();
            return;
        case 0x03:
            RequestSense();
            return;
        case 0x04:
            Format();
            return;
        case 0x08:
            Read6();
            return;
        case 0x0a:
            Write6();
            return;
        case 0x0f:
            Translate();
            return;
        case 0x15:
            ModeSelect();
            return;
        case 0x1a:
            ModeSense();
            return;
        case 0x1b:
            StartStop();
            return;
        case 0x2f:
            Verify();
            return;
    }

    scsi.status = (scsi.lun << 5) | 0x02;
    scsi.message = 0x00;
    Status();
}

static bool WriteGeometory(unsigned char *buf)
{
    scsidisc *sd = &SCSIDisc[scsi.lun];
    FILE *fp = sd->dsc_fp;
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_SET))
        return false;
    if (fwrite(buf, 22, 1, fp) != 1)
        return false;
    return true;
}

static void WriteData(int data)
{
    scsidisc *sd;

    scsi.lastwrite = data;

    switch (scsi.phase) {
        case busfree:
            if (scsi.sel) {
                Selection(data);
            }
            return;

        case selection:
            if (!scsi.sel) {
                Command();
                return;
            }
            break;

        case command:
            scsi.cmd[scsi.offset] = data;
            if (scsi.offset == 0) {
                if ((data >= 0x20) && (data <= 0x3f)) {
                    scsi.length = 10;
                }
            }
            scsi.offset++;
            scsi.length--;
            scsi.req = false;

            if (scsi.length == 0) {
                Execute();
                return;
            }
            return;

        case scsiwrite:
            scsi.buffer[scsi.offset] = data;
            scsi.offset++;
            scsi.length--;
            scsi.req = false;

            if (scsi.length > 0)
                return;

            switch (scsi.cmd[0]) {
                case 0x0a:
                case 0x15:
                case 0x2a:
                case 0x2e:
                    break;
                default:
                    Status();
                    return;
            }

            switch (scsi.cmd[0]) {
                case 0x0a:
                    sd = &SCSIDisc[scsi.lun];
                    if (!sd->WriteSector(sd, scsi.buffer, scsi.next - 1)) {
                        scsi.status = (scsi.lun << 5) | 0x02;
                        scsi.message = 0;
                        Status();
                        return;
                    }
                    break;
                case 0x15:
                    if (!WriteGeometory(scsi.buffer)) {
                        scsi.status = (scsi.lun << 5) | 0x02;
                        scsi.message = 0;
                        Status();
                        return;
                    }
                    break;
            }

            scsi.blocks--;

            if (scsi.blocks == 0) {
                Status();
                return;
            }
            scsi.length = 256;
            scsi.next++;
            scsi.offset = 0;
            return;
        default:
            log_warn("scsi: invalid phase %d in WriteData", scsi.phase);
    }
    BusFree();
}

void scsi_write(uint16_t addr, uint8_t value)
{
    //log_debug("scsi_write: addr=%02x, value=%02X, phase=%d", addr, value, scsi.phase);

    switch (addr & 0x03) {
        case 0x00:
            scsi.sel = true;
            WriteData(value);
            break;
        case 0x01:
            scsi.sel = true;
            break;
        case 0x02:
            scsi.sel = false;
            WriteData(value);
            break;
        case 0x03:
            scsi.sel = true;
            if (value == 0xff) {
                log_debug("scsi lun %d: set interrupt", scsi.lun);
                scsi.irq = true;
                interrupt |= 1 << (SCSI_INT_NUM);
                scsi.status = 0x00;
            }
            else {
                log_debug("scsi lun %d: clear interrupt", scsi.lun);
                scsi.irq = false;
                interrupt &= ~(1 << (SCSI_INT_NUM));
            }
            break;
    }
}

static void Message(void)
{
    scsi.phase = message;

    scsi.msg = true;
    scsi.req = true;
}

static int ReadData(void)
{
    scsidisc *sd;
    int data;

    switch (scsi.phase) {
        case status:
            data = scsi.status;
            scsi.req = false;
            Message();
            return data;

        case message:
            data = scsi.message;
            scsi.req = false;
            BusFree();
            return data;

        case scsiread:
            data = scsi.buffer[scsi.offset];
            scsi.offset++;
            scsi.length--;
            scsi.req = false;

            if (scsi.length == 0) {
                scsi.blocks--;
                if (scsi.blocks == 0) {
                    Status();
                    return data;
                }
                sd = &SCSIDisc[scsi.lun];
                if (sd->ReadSector(sd, scsi.buffer, scsi.next)) {
                    scsi.length = 256;
                    scsi.offset = 0;
                    scsi.next++;
                }
                else {
                    scsi.status = (scsi.lun << 5) | 0x02;
                    scsi.message = 0x00;
                    Status();
                }
            }
            return data;
        case selection:
            break;
        default:
            log_warn("scsi: invalid phase %d in ReadData", scsi.phase);
    }

    if (scsi.phase == busfree)
        return scsi.lastwrite;

    BusFree();
    return scsi.lastwrite;
}

uint8_t scsi_read(uint16_t addr)
{
    int data = 0xff;

    switch (addr & 0x03) {
        case 0x00:              // Data Register
            data = ReadData();
            break;
        case 0x01:              // Status Register
            data = 0x20;        // Hmmm.. don't know why req has to always be active ? If start at 0x00, ADFS lock up on entry
            if (scsi.cd)
                data |= 0x80;
            if (scsi.io)
                data |= 0x40;
            if (scsi.req)
                data |= 0x20;
            if (scsi.irq)
                data |= 0x10;
            if (scsi.bsy)
                data |= 0x02;
            if (scsi.msg)
                data |= 0x01;
            break;
        case 0x02:
            break;
        case 0x03:
            break;
    }

    //log_debug("scsi_read: addr=%02x, value=%02X, phase=%d", addr, data, scsi.phase);

    return data;
}

void scsi_reset(void)
{
    scsi.code = 0x00;
    scsi.sector = 0x00;
    BusFree();
}

static bool scsi_check_adfs(FILE *fp, unsigned off1, unsigned off2, const char *pattern, size_t len)
{
    char id1[10], id2[10];
    if (fseek(fp, off1, SEEK_SET))
        return false;
    if (fread(id1, len, 1, fp) != 1)
        return false;
    if (memcmp(id1+1, pattern, len-1))
        return false;
    if (fseek(fp, off2, SEEK_SET))
        return false;
    if (fread(id2, len, 1, fp) != 1)
        return false;
    if (memcmp(id1, id2, len))
        return false;
    return true;
}

static void scsi_select_simple(scsidisc *sd, int lun, const char *cpath, const char *msg)
{
    log_info("scsi lun %d: %s %s", lun, cpath, msg);
    sd->ReadSector  = ReadSectorSimple;
    sd->WriteSector = WriteSectorSimple;
}

static void scsi_select_padded(scsidisc *sd, int lun, const char *cpath, const char *msg)
{
    log_info("scsi lun %d: %s %s", lun, cpath, msg);
    sd->ReadSector  = ReadSectorPadded;
    sd->WriteSector = WriteSectorPadded;
}

static void scsi_init_lun(scsidisc *sd, int lun)
{
    ALLEGRO_PATH *path;
    char name[50];
    sd->ReadSector  = ReadWriteNone;
    sd->WriteSector = ReadWriteNone;
    sd->dat_fp = sd->dsc_fp = NULL;
    sd->blocks = 0;
    snprintf(name, sizeof(name), "scsi/scsi%d", lun);
    if ((path = find_cfg_file(name, ".dat"))) {
        sd->path = path;
        const char *cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        FILE *fp = fopen(cpath, "rb+");
        if (fp) {
            if (scsi_check_adfs(fp, 0x200, 0x6fa, "Hugo", 5))
                scsi_select_simple(sd, lun, cpath, "detected as simple (SCSI) format");
            else if (scsi_check_adfs(fp, 0x400, 0xdf4, "\0H\0u\0g\0o", 10))
                scsi_select_padded(sd, lun, cpath, "detected as padded (IDE) format");
            else
                scsi_select_simple(sd, lun, cpath, "selected as simple (SCSI) format by default");
            sd->dat_fp = fp;
            al_set_path_extension(path, ".dsc");
            cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            if ((fp = fopen(cpath, "rb+"))) {
                if (fread(sd->geom, 1, sizeof(sd->geom), fp) >= 22) {
                    unsigned heads = sd->geom[15];
                    unsigned cyls = (sd->geom[13] << 8) | sd->geom[14];
                    sd->blocks = heads * cyls * 33;
                    sd->dsc_fp = fp;
                }
                else {
                    log_warn("scsi lun %d: short geometry file %s", lun, cpath);
                    fclose(fp);
                }
            }
            if (sd->blocks == 0) {
                unsigned bytes, cyl;
                fseek(sd->dat_fp, 0, SEEK_END);
                bytes = ftell(sd->dat_fp);
                memset(sd->geom, 0, sizeof(sd->geom));
                cyl = 1 + ((bytes - 1) / (33 * 255));
                sd->geom[13] = cyl >> 8;
                sd->geom[14] = cyl & 0xff;
                sd->geom[15] = 255;
                if ((fp = fopen(cpath, "wb+"))) {
                    fwrite(sd->geom, sizeof(sd->geom), 1, fp);
                    fflush(fp);
                    sd->dsc_fp = fp;
                }
            }
        }
        else
            log_error("scsi lun %d: unable to open data file %s: %s", lun, cpath, strerror(errno));
    }
    else
        log_warn("scsi lun %d: no disc file %s found", lun, name);
}

void scsi_init(void)
{
    if (scsi_enabled)
        for (int lun = 0; lun < SCSI_DRIVES; lun++)
            scsi_init_lun(&SCSIDisc[lun], lun);
}

void scsi_close(void)
{
    for (int lun = 0; lun < SCSI_DRIVES; lun++) {
        scsidisc *sd = &SCSIDisc[lun];
        if (sd->dat_fp) {
            fclose(sd->dat_fp);
            sd->dat_fp = NULL;
        }
        if (sd->dsc_fp) {
            fclose(sd->dsc_fp);
            sd->dsc_fp = NULL;
        }
        if (sd->path) {
            al_destroy_path(sd->path);
            sd->path= NULL;
        }
    }
}
