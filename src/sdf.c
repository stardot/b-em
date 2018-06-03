/*
 * B-EM SDF - Simple Disk Formats
 *
 * This is a module to handle the various disk image formats where
 * the sectors that comprise the disk image are stored in the file
 * in a logical order and without ID headers.
 *
 * It understands enough of the filing systems on Acorn to be able
 * to work out the geometry including the sector size, the number
 * of sectors per track etc. and whether the sides are interleaved
 * as used in DSD images or sequential as in SSD.  It can handle
 * double-density images that are not ADFS, i.e. are one of the
 * non-Acorn DFS.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "b-em.h"
#include "disc.h"

typedef enum {
    SIDES_NA,
    SIDES_SINGLE,
    SIDES_SEQUENTIAL,
    SIDES_INTERLEAVED
} sides_t;

typedef enum {
    DENS_NA,
    DENS_SINGLE,
    DENS_DOUBLE,
    DENS_QUAD
} density_t;

typedef struct {
    const char *name;
    sides_t    sides;
    density_t  density;
    uint16_t   size_in_sectors;
    uint8_t    tracks;
    uint8_t    sectors_per_track;
    uint16_t   sector_size;
} geometry_t;

static const geometry_t adfs_new_formats[] =
{
    { "Acorn ADFS F", SIDES_INTERLEAVED, DENS_QUAD,  1600, 80, 10, 1024 },
    { "Acorn ADFS D", SIDES_INTERLEAVED, DENS_DOUBLE, 800, 80,  5, 1024 }
};

static const geometry_t adfs_old_formats[] =
{
    { "ADFS+DOS",     SIDES_INTERLEAVED, DENS_DOUBLE, 2720, 80, 16,  256 },
    { "Acorn ADFS L", SIDES_INTERLEAVED, DENS_DOUBLE, 2560, 80, 16,  256 },
    { "Acorn ADFS M", SIDES_SINGLE,      DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Acorn ADFS S", SIDES_SINGLE,      DENS_DOUBLE,  640, 40, 16,  256 }
};

static const geometry_t watford_dfs_formats[] =
{
    { "Watford/Opus DDFS", SIDES_INTERLEAVED, DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SEQUENTIAL,  DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SINGLE,      DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_INTERLEAVED, DENS_DOUBLE,  720, 40, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SEQUENTIAL,  DENS_DOUBLE,  720, 40, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SINGLE,      DENS_DOUBLE,  720, 40, 18,  256 }
};

static const geometry_t solidisk_dfs_formats[] =
{
    { "Solidisk DDFS",     SIDES_INTERLEAVED, DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_SEQUENTIAL,  DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_SINGLE,      DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_INTERLEAVED, DENS_DOUBLE,  640, 40, 16,  256 },
    { "Solidisk DDFS",     SIDES_SEQUENTIAL,  DENS_DOUBLE,  640, 40, 16,  256 },
    { "Solidisk DDFS",     SIDES_SINGLE,      DENS_DOUBLE,  640, 40, 16,  256 }
};

static const geometry_t acorn_dfs_formats[] =
{
    { "Acorn DFS",         SIDES_INTERLEAVED, DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_SEQUENTIAL,  DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_SINGLE,      DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_INTERLEAVED, DENS_SINGLE, 400, 40, 10, 256 },
    { "Acorn DFS",         SIDES_SEQUENTIAL,  DENS_SINGLE, 400, 40, 10, 256 },
    { "Acorn DFS",         SIDES_SINGLE,      DENS_SINGLE, 400, 40, 10, 256 }
};

static const geometry_t other_formats[] =
{
    { "ADFS+DOS 800K",     SIDES_INTERLEAVED, DENS_DOUBLE,  800, 80,  5, 1024 },
    { "ADFS+DOS 640K",     SIDES_INTERLEAVED, DENS_DOUBLE, 2560, 80, 16,  256 },
    { "DOS 720K",          SIDES_INTERLEAVED, DENS_DOUBLE, 1440, 80,  9,  512 },
    { "DOS 360K",          SIDES_INTERLEAVED, DENS_DOUBLE,  720, 40,  9,  512 },
    { "Acorn DFS",         SIDES_SINGLE,      DENS_SINGLE,  800, 80, 10,  256 },
    { "Acorn DFS",         SIDES_INTERLEAVED, DENS_SINGLE, 1600, 80, 10,  256 }
};

#define ARRAYEND(a) (a + sizeof(a) / sizeof(a[0]))

static int check_id(FILE *fp, long posn, const char *id)
{
    int ch;

    if (fseek(fp, posn, SEEK_SET) == -1)
        return 0;
    while ((ch = *id++))
        if (ch != getc(fp))
            return 0;
    return 1;
}

static const geometry_t *try_adfs_new(FILE *fp)
{
    long size;
    const geometry_t *ptr, *end;

    if (check_id(fp, 0x401, "Nick") || check_id(fp, 0x801, "Nick")) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        end = ARRAYEND(adfs_new_formats);
        for (ptr = adfs_new_formats; ptr < end; ptr++)
            if (size == (ptr->size_in_sectors * ptr->sector_size))
                return ptr;
    }
    return NULL;
}

static const geometry_t *try_adfs_old(FILE *fp)
{
    uint32_t sects;
    const geometry_t *ptr, *end;

    if (check_id(fp, 0x201, "Hugo") && check_id(fp, 0x6fb, "Hugo")) {
        fseek(fp, 0xfc, SEEK_SET);
        sects = getc(fp) | (getc(fp) << 8) | (getc(fp) << 16);
        log_debug("sdf: found old ADFS signature, sects=%u", sects);
        end = ARRAYEND(adfs_old_formats);
        for (ptr = adfs_old_formats; ptr < end; ptr++)
            if (sects == ptr->size_in_sectors)
                return ptr;
    }
    return NULL;
}

static uint32_t watford_start_sect(const uint8_t *entry)
{
    return ((entry[0x006] & 0x80) << 3) | ((entry[0x106] & 0x03) << 8) | entry[0x107];
}

static uint32_t solidisk_start_sect(const uint8_t *entry)
{
    return ((entry[0x106] & 0x07) << 8) | entry[0x107];
}

static uint32_t acorn_start_sect(const uint8_t *entry)
{
    return ((entry[0x106] & 0x03) << 8) | entry[0x107];
}

static bool check_sorted(const uint8_t *base, uint32_t dirsize, uint32_t (*get_start_sect)(const uint8_t *entry))
{
    uint32_t cur_start = UINT32_MAX;
    uint32_t new_start;

    while (dirsize > 0) {
        base += 8;
        dirsize -= 8;
        new_start = get_start_sect(base);
        if (new_start > cur_start)
            return false;
        cur_start = new_start;
    }
    return true;
}

static const char *desc_sides(const geometry_t *geo)
{
    switch(geo->sides) {
        case SIDES_SINGLE:
            return "single-sided";
        case SIDES_SEQUENTIAL:
            return "doubled-sided, sequential";
        case SIDES_INTERLEAVED:
            return "double-sided, interleaved";
        default:
            return "unknown";
    }
}

static const char *desc_dens(const geometry_t *geo)
{
    switch(geo->density) {
        case DENS_QUAD:
            return "quad";
        case DENS_DOUBLE:
            return "double";
        case DENS_SINGLE:
            return "single";
        default:
            return "unknown";
    }
}

static const geometry_t *dfs_search(FILE *fp, uint32_t offset, uint32_t dirsize0, uint32_t sects0, uint8_t *twosect0, uint32_t fsize, const geometry_t *formats, const geometry_t *end, uint32_t (*get_start_sect)(const uint8_t *entry))
{
    const geometry_t *ptr;
    uint32_t dirsize2, sects2, side2_off, track_bytes;
    uint8_t twosect2[512];

    if (check_sorted(twosect0, dirsize0, get_start_sect)) {
        log_debug("sdf: dfs_search: check_sorted true for side0");
        for (ptr = formats; ptr < end; ptr++) {
            log_debug("sdf: dfs_search: trying entry name=%s, sides=%s, dens=%s", ptr->name, desc_sides(ptr), desc_dens(ptr));
            if (sects0 == ptr->size_in_sectors && (ptr->size_in_sectors * ptr->sector_size) >= fsize) {
                if (ptr->sides == SIDES_SINGLE)
                    return ptr;
                side2_off = track_bytes = ptr->sectors_per_track * ptr->sector_size;
                if (ptr->sides == SIDES_SEQUENTIAL)
                    side2_off = ptr->tracks * track_bytes;
                if (fseek(fp, side2_off+offset, SEEK_SET) >= 0) {
                    if (fread(twosect2, sizeof twosect2, 1, fp) == 1) {
                        dirsize2 = twosect2[0x105];
                        log_debug("sdf: dfs_search: dirsize2=%d bytes, %d entries", dirsize2, dirsize2 / 8);
                        if (!(dirsize2 & 0x07) && dirsize2 < (31 * 8)) {
                            sects2 = ((twosect2[0x106] & 0x07) << 8) | twosect2[0x107];
                            log_debug("sdf: dfs_search: sects2=%d", sects2);
                            if (sects2 == sects0) {
                                if (check_sorted(twosect2, dirsize2, get_start_sect)) {
                                    log_debug("sdf: dfs_search: check_sorted true for side2");
                                    return ptr;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

static const geometry_t *try_dfs(FILE *fp, uint32_t offset)
{
    uint32_t dirsize0, sects0, fsize;
    const geometry_t *ptr;
    uint8_t twosect0[512];

    if (fseek(fp, offset, SEEK_SET) >= 0) {
        if (fread(twosect0, sizeof twosect0, 1, fp) == 1) {
            dirsize0 = twosect0[0x105];
            log_debug("sdf: try_dfs: dirsize0=%d bytes, %d entries", dirsize0, dirsize0 / 8);
            if (!(dirsize0 & 0x07) && dirsize0 < (31 * 8)) {
                sects0 = ((twosect0[0x106] & 0x07) << 8) | twosect0[0x107];
                fseek(fp, 0L, SEEK_END);
                fsize = ftell(fp);
                log_debug("sdf: try_dfs: sects0=%d, fsize=%d", sects0, fsize);
                if ((ptr = dfs_search(fp, offset, dirsize0, sects0, twosect0, fsize, watford_dfs_formats, ARRAYEND(watford_dfs_formats), watford_start_sect)))
                    return ptr;
                if ((ptr = dfs_search(fp, offset, dirsize0, sects0, twosect0, fsize, solidisk_dfs_formats, ARRAYEND(solidisk_dfs_formats), solidisk_start_sect)))
                    return ptr;
                if ((ptr = dfs_search(fp, offset, dirsize0, sects0, twosect0, fsize, acorn_dfs_formats, ARRAYEND(acorn_dfs_formats), acorn_start_sect)))
                    return ptr;
            }
        }
    }
    return NULL;
}

static const geometry_t *try_others(FILE *fp) {
    off_t size;
    const geometry_t *ptr, *end;

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    end = ARRAYEND(other_formats);
    for (ptr = other_formats; ptr < end; ptr++)
        if ((ptr->size_in_sectors * ptr->sector_size) == size)
            return ptr;
    return NULL;
}

static const geometry_t *geometry[NUM_DRIVES];
static FILE *sdf_fp[NUM_DRIVES];
static uint8_t current_track[NUM_DRIVES];

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
    const geometry_t *geo;

    if (drive < NUM_DRIVES)
        if ((geo = geometry[drive]))
            if (track >= 0 && track < geo->tracks)
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE))
                    return 1;
    return 0;
}

static void io_seek(const geometry_t *geo, uint8_t drive, uint8_t sector, uint8_t track, uint8_t side)
{
    uint32_t track_bytes, offset;

    track_bytes = geo->sectors_per_track * geo->sector_size;
    if (side == 0) {
        offset = track * track_bytes;
        if (geo->sides == SIDES_INTERLEAVED)
            offset *= 2;
    } else {
        if (geo->sides == SIDES_SEQUENTIAL)
            offset = (track + geo->tracks) * track_bytes;
        else
            offset = (track * 2 + 1) * track_bytes;
    }
    offset += sector * geo->sector_size;
    log_debug("sdf: seeking for drive=%u, side=%u, track=%u, sector=%u to %d bytes\n", drive, side, track, sector, offset);
    fseek(sdf_fp[drive], offset, SEEK_SET);
}

static void sdf_readsector(int drive, int sector, int track, int side, int density)
{
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (geo->sector_size > 256)
                            sector--;
                        if (sector >= 0 && sector <= geo->sectors_per_track ) {
                            if (side == 0 || geo->sides != SIDES_SINGLE) {
                                io_seek(geo, drive, sector, track, side);
                                count = geo->sector_size;
                                sdf_drive = drive;
                                state = ST_READSECTOR;
                                return;
                            } else log_debug("sdf: invalid side");
                        } else log_debug("sdf: invalid sector: %d not between 0 and %d", sector, geo->sectors_per_track);
                    }  else log_debug("sdf: invalid track: %d should be %d", track, current_track[drive]);
                } else log_debug("sdf: invalid density");
            }  else log_debug("sdf: geometry not found");
        } else log_debug("sdf: drive %d out of range", drive);
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_writesector(int drive, int sector, int track, int side, int density)
{
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (geo->sector_size > 256)
                            sector--;
                        if (sector >= 0 && sector < geo->sectors_per_track ) {
                            if (side == 0 || geo->sides != SIDES_SINGLE) {
                                io_seek(geo, drive, sector, track, side);
                                count = geo->sector_size;
                                sdf_drive = drive;
                                sdf_side = side;
                                sdf_track = track;
                                sdf_sector = sector;
                                sdf_time = -20;
                                state = ST_WRITESECTOR;
                                return;
                            }
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_readaddress(int drive, int track, int side, int density)
{
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (side == 0 || geo->sides != SIDES_SINGLE) {
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
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (side == 0 || geo->sides != SIDES_SINGLE) {
                            io_seek(geo, drive, 0, track, side);
                            sdf_drive = drive;
                            sdf_side = side;
                            sdf_track = track;
                            sdf_sector = 0;
                            count = 500;
                            state = ST_FORMAT;
                            return;
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
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

void sdf_load(int drive, const char *fn)
{
    FILE *fp;
    const geometry_t *geo;

    writeprot[drive] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[drive] = 1;
    }
    if ((geo = try_adfs_new(fp)) == NULL) {
        if ((geo = try_adfs_old(fp)) == NULL) {
            if ((geo = try_dfs(fp, 0)) == NULL) {
                if ((geo = try_dfs(fp, 0xefb)) == NULL) {
                    if ((geo = try_others(fp)) == NULL) {
                        log_error("Unable to determine geometry for %s", fn);
                        fclose(fp);
                        return;
                    }
                }
            }
        }
    }
    sdf_fp[drive] = fp;
    log_info("Loaded drive %d with %s, format %s, %s, %d tracks, %s-density, %d %d byte sectors/track",
             drive, fn, geo->name, desc_sides(geo), geo->tracks, desc_dens(geo), geo->sectors_per_track, geo->sector_size);
    geometry[drive] = geo;
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
