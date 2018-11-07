/*
 * B-EM SDF - Simple Disk Formats - Access
 *
 * This B-Em module is part of the handling of simple disc formats,
 * i.e. those where the sectors that comprise the disk image are
 * stored in the file in a logical order and without ID headers.
 *
 * This module contains the functions to open and access the disc
 * images who geometry is described by the companion module sdf-geo.c
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "b-em.h"
#include "disc.h"
#include "sdf.h"

#define MMB_CAT_SIZE 0x2000

static FILE *sdf_fp[NUM_DRIVES], *mmb_fp;
static const struct sdf_geometry *geometry[NUM_DRIVES];
static uint8_t current_track[NUM_DRIVES];
static off_t mmb_offset[NUM_DRIVES][2];
static char *mmb_cat;
char *mmb_fn;

typedef enum {
    ST_IDLE,
    ST_NOTFOUND,
    ST_READSECTOR,
    ST_WRITESECTOR,
    ST_READ_ADDR0,
    ST_READ_ADDR1,
    ST_READ_ADDR2,
    ST_READ_ADDR3,
    ST_READ_ADDR4,
    ST_READ_ADDR5,
    ST_READ_ADDR6,
    ST_FORMAT
} state_t;

state_t state = ST_IDLE;

static uint16_t count = 0;

static int     sdf_time;
static uint8_t sdf_drive;
static uint8_t sdf_side;
static uint8_t sdf_track;
static uint8_t sdf_sector;

static void sdf_close(int drive)
{
    if (drive < NUM_DRIVES) {
        geometry[drive] = NULL;
        if (sdf_fp[drive]) {
            if (sdf_fp[drive] != mmb_fp)
                fclose(sdf_fp[drive]);
            sdf_fp[drive] = NULL;
        }
    }
}

static void sdf_seek(int drive, int track)
{
    if (drive < NUM_DRIVES)
        current_track[drive] = track;
}

static int sdf_verify(int drive, int track, int density)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES)
        if ((geo = geometry[drive]))
            if (track >= 0 && track < geo->tracks)
                if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE))
                    return 1;
    return 0;
}

static bool io_seek(const struct sdf_geometry *geo, uint8_t drive, uint8_t sector, uint8_t track, uint8_t side)
{
    uint32_t track_bytes, offset;

    if (track >= 0 && track < geo->tracks) {
        if (geo->sector_size > 256)
            sector--;
        if (sector >= 0 && sector <= geo->sectors_per_track ) {
            track_bytes = geo->sectors_per_track * geo->sector_size;
            if (side == 0) {
                offset = track * track_bytes;
                if (geo->sides == SDF_SIDES_INTERLEAVED)
                    offset *= 2;
            } else {
                switch(geo->sides)
                {
                case SDF_SIDES_SEQUENTIAL:
                    offset = (track + geo->tracks) * track_bytes;
                    break;
                case SDF_SIDES_INTERLEAVED:
                    offset = (track * 2 + 1) * track_bytes;
                    break;
                default:
                    log_debug("sdf: drive %u: attempt to read second side of single-sided disc", drive);
                    return false;
                }
            }
            offset += sector * geo->sector_size + mmb_offset[drive][side];
            log_debug("sdf: drive %u: seeking for side=%u, track=%u, sector=%u to %d bytes\n", drive, side, track, sector, offset);
            fseek(sdf_fp[drive], offset, SEEK_SET);
            return true;
        }
        else
            log_debug("sdf: drive %u: invalid sector: %d not between 0 and %d", drive, sector, geo->sectors_per_track);
    }
    else
        log_debug("sdf: drive %u: invalid track: %d not between 0 and %d", drive, track, geo->tracks);
    return false;
}

FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES) {
        if ((geo = geometry[drive])) {
            if (ssize == geo->sector_size) {
                if (io_seek(geo, drive, sector, track, side))
                    return sdf_fp[drive];
            }
            else
                log_debug("sdf: osword seek, sector size %u does not match disk (%u)", ssize, geo->sector_size);
        }
        else
            log_debug("sdf: osword seek, no geometry for drive %u", drive);
    }
    else
        log_debug("sdf: osword seek, drive %u out of range", drive);
    return NULL;
}

static const struct sdf_geometry *check_seek(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES) {
        if ((geo = geometry[drive])) {
            if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE)) {
                if (track == current_track[drive]) {
                    if (io_seek(geo, drive, sector, track, side))
                        return geo;
                }
                else
                    log_debug("sdf: drive %u: invalid track: %d should be %d", drive, track, current_track[drive]);
            }
            else
                log_debug("sdf: drive %u: invalid density", drive);
        }
        else
            log_debug("sdf: drive %u: geometry not found", drive);
    }
    else
        log_debug("sdf: drive %d: drive number  out of range", drive);

    count = 500;
    state = ST_NOTFOUND;
    return NULL;
}

static void sdf_readsector(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, sector, track, side, density))) {
        count = geo->sector_size;
        sdf_drive = drive;
        state = ST_READSECTOR;
    }
}

static void sdf_writesector(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, sector, track, side, density))) {
        count = geo->sector_size;
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = sector;
        sdf_time = -20;
        state = ST_WRITESECTOR;
    }
}

static void sdf_readaddress(int drive, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == SDF_DENS_SINGLE) || (density && geo->density == SDF_DENS_DOUBLE)) {
                    if (side == 0 || geo->sides != SDF_SIDES_SINGLE) {
                        sdf_drive = drive;
                        sdf_side = side;
                        sdf_track = track;
                        state = ST_READ_ADDR0;
                        return;
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_format(int drive, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, 0, track, side, density))) {
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = 0;
        count = 500;
        state = ST_FORMAT;
    }
}

static void sdf_poll()
{
    int c;
    uint16_t sect_size;

    if (++sdf_time <= 16)
        return;
    sdf_time = 0;

    switch(state) {
        case ST_IDLE:
            break;

        case ST_NOTFOUND:
            if (--count == 0) {
                fdc_notfound();
                state = ST_IDLE;
            }
            break;

        case ST_READSECTOR:
            fdc_data(getc(sdf_fp[sdf_drive]));
            if (--count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
            break;

        case ST_WRITESECTOR:
            if (writeprot[sdf_drive]) {
                log_debug("sdf: poll, write protected during write sector");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            c = fdc_getdata(--count == 0);
            if (c == -1) {
                log_warn("sdf: data underrun on write");
                count++;
            } else {
                putc(c, sdf_fp[sdf_drive]);
                if (count == 0) {
                    fdc_finishread();
                    state = ST_IDLE;
                }
            }
            break;

        case ST_READ_ADDR0:
            fdc_data(sdf_track);
            state = ST_READ_ADDR1;
            break;

        case ST_READ_ADDR1:
            fdc_data(sdf_side);
            state = ST_READ_ADDR2;
            break;

        case ST_READ_ADDR2:
            if (geometry[sdf_drive]->sector_size > 256)
                fdc_data(sdf_sector+1);
            else
                fdc_data(sdf_sector);
            state = ST_READ_ADDR3;
            break;

        case ST_READ_ADDR3:
            sect_size = geometry[sdf_drive]->sector_size;
            fdc_data(sect_size == 256 ? 1 : sect_size == 512 ? 2 : 3);
            state = ST_READ_ADDR4;
            break;

        case ST_READ_ADDR4:
            fdc_data(0);
            state = ST_READ_ADDR5;
            break;

        case ST_READ_ADDR5:
            fdc_data(0);
            state = ST_READ_ADDR6;
            break;

        case ST_READ_ADDR6:
            state = ST_IDLE;
            fdc_finishread();
            sdf_sector++;
            if (sdf_sector == geometry[sdf_drive]->sectors_per_track)
                sdf_sector = 0;
            break;

        case ST_FORMAT:
            if (writeprot[sdf_drive]) {
                log_debug("sdf: poll, write protected during write track");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            if (--count == 0) {
                putc(0, sdf_fp[sdf_drive]);
                if (++sdf_sector >= geometry[sdf_drive]->sectors_per_track) {
                    state = ST_IDLE;
                    fdc_finishread();
                    break;
                }
                io_seek(geometry[sdf_drive], sdf_drive, sdf_sector, sdf_track, sdf_side);
                count = 500;
            }
            break;
    }
}

static void sdf_abort(int drive)
{
    state = ST_IDLE;
}

static void sdf_mount(int drive, const char *fn, FILE *fp, const struct sdf_geometry *geo)
{
    sdf_fp[drive] = fp;
    log_info("Loaded drive %d with %s, format %s, %s, %d tracks, %s, %d %d byte sectors/track",
             drive, fn, geo->name, sdf_desc_sides(geo), geo->tracks,
             sdf_desc_dens(geo), geo->sectors_per_track, geo->sector_size);
    geometry[drive] = geo;
    mmb_offset[drive][0] = mmb_offset[drive][1] = 0;
    drives[drive].close       = sdf_close;
    drives[drive].seek        = sdf_seek;
    drives[drive].verify      = sdf_verify;
    drives[drive].readsector  = sdf_readsector;
    drives[drive].writesector = sdf_writesector;
    drives[drive].readaddress = sdf_readaddress;
    drives[drive].poll        = sdf_poll;
    drives[drive].format      = sdf_format;
    drives[drive].abort       = sdf_abort;
}

void sdf_load(int drive, const char *fn, const char *ext)
{
    FILE *fp;
    const struct sdf_geometry *geo;

    writeprot[drive] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[drive] = 1;
    }
    if ((geo = sdf_find_geo(fn, ext, fp)))
        sdf_mount(drive, fn, fp, geo);
    else {
        log_error("sdf: drive %d: unable to determine geometry for %s", drive, fn);
        fclose(fp);
    }
}

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, enum sdf_disc_type dtype)
{
    const struct sdf_geometry *geo;
    const char *cpath;
    FILE *f;

    if (dtype > SDF_FMT_MAX)
        log_error("sdf: drive %d: inavlid disc type %d for new disc", drive, dtype);
    else {
        geo = sdf_geo_tab + dtype;
        if (!geo->new_disc)
            log_error("sdf: drive %d: creation of file disc type %s (%d) not supported", drive, geo->name, dtype);
        else {
            cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "wb+"))) {
                geo->new_disc(f, geo);
                sdf_mount(drive, cpath, f, geo);
            }
            else
                log_error("sdf: drive %d: unable to open disk image %s for writing: %s", drive, cpath, strerror(errno));
        }
    }
}

void mmb_load(char *fn)
{
    FILE *fp;

    if (!mmb_cat) {
        if (!(mmb_cat = malloc(MMB_CAT_SIZE))) {
            log_error("sdf: out of memory allocating MMB catalogue");
            return;
        }
    }
    writeprot[0] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[0] = 1;
    }
    if (fread(mmb_cat, MMB_CAT_SIZE, 1, fp) != 1) {
        log_error("mmb: %s is not a valid MMB file", fn);
        fclose(fp);
        return;
    }
    if (mmb_fp) {
        fclose(mmb_fp);
        if (sdf_fp[1] == mmb_fp) {
            sdf_mount(1, fn, fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
            writeprot[1] = writeprot[0];
            mmb_offset[1][0] = MMB_CAT_SIZE;
            mmb_offset[1][1] = MMB_CAT_SIZE + 10 * 256 * 80;
        }
    }
    sdf_mount(0, fn, fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
    mmb_offset[0][0] = MMB_CAT_SIZE;
    mmb_offset[0][1] = MMB_CAT_SIZE + 10 * 256 * 80;
    mmb_fp = fp;
    mmb_fn = fn;
    if (fdc_spindown)
        fdc_spindown();
}

static void mmb_eject_one(int drive)
{
    ALLEGRO_PATH *path;

    if (sdf_fp[drive] == mmb_fp) {
        disc_close(drive);
        if ((path = discfns[drive]))
            disc_load(drive, path);
    }
}

void mmb_eject(void)
{
    if (mmb_fp) {
        mmb_eject_one(0);
        mmb_eject_one(1);
    }
    if (mmb_fn) {
        free(mmb_fn);
        mmb_fn = NULL;
    }
}

static void reset_one(int drive)
{
    if (sdf_fp[drive] == mmb_fp) {
        mmb_offset[drive][0] = MMB_CAT_SIZE;
        mmb_offset[drive][1] = MMB_CAT_SIZE + 10 * 256 * 80;
    }
}

void mmb_reset(void)
{
    if (mmb_fp) {
        reset_one(0);
        reset_one(1);
    }
}

void mmb_pick(int drive, int disc)
{
    int side;

    switch(drive) {
        case 0:
        case 1:
            side = 0;
            break;
        case 2:
        case 4:
            drive &= 1;
            disc--;
            side = 1;
            break;
        default:
            log_debug("sdf: sdf_mmb_pick: invalid logical drive %d", drive);
            return;
    }
    log_debug("sdf: picking MMB disc, drive=%d, side=%d, disc=%d", drive, side, disc);

    if (sdf_fp[drive] != mmb_fp) {
        disc_close(drive);
        sdf_mount(drive, mmb_fn, mmb_fp, &sdf_geo_tab[SDF_FMT_DFS_10S_SEQ_80T]);
    }
    mmb_offset[drive][side] = MMB_CAT_SIZE + 10 * 256 * 80 * disc;
    if (fdc_spindown)
        fdc_spindown();
}

static inline int cat_name_cmp(const char *nam_ptr, const char *cat_ptr, const char *cat_nxt)
{
    do {
        char cat_ch = *cat_ptr++;
        char nam_ch = *nam_ptr++;
        if (!nam_ch) {
            if (!cat_ch)
                break;
            else
                return -1;
        }
        if ((cat_ch ^ nam_ch) & 0x5f)
            return -1;
    } while (cat_nxt != cat_ptr);
    return (cat_nxt - mmb_cat) / 16 - 2;
}

int mmb_find(const char *name)
{
    const char *cat_ptr = mmb_cat + 16;
    const char *cat_end = mmb_cat + MMB_CAT_SIZE;
    int i;

    do {
        const char *cat_nxt = cat_ptr + 16;
        if ((i = cat_name_cmp(name, cat_ptr, cat_nxt)) >= 0) {
            log_debug("mmb: found MMB SSD '%s' at %d", name, i);
            return i;
        }
        cat_ptr = cat_nxt;
    } while (cat_ptr < cat_end);
    return -1;
}
