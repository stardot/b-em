/*
 * B-EM SDF - Simple Disk Formats
 *
 * This is a module to handle the various disk image formats where
 * the sectors that comprise the disk image are stored in the file
 * in a logical order and without ID headers.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "b-em.h"
#include "disc.h"
#include "sdf.h"

#define SSD_SIDE_SIZE (80 * 10 * 256)

typedef enum {
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

struct sdf_geometry {
    const char *name;
    sides_t    sides;
    density_t  density;
    uint8_t    tracks;
    uint8_t    sectors_per_track;
    uint16_t   sector_size;
    void (*new_disc)(FILE *f, const struct sdf_geometry *geo);
};

static void prep_dfs(unsigned char *twosect, const struct sdf_geometry *geo)
{
    uint32_t nsect = geo->tracks * geo->sectors_per_track;
    memset(twosect, ' ', 8);
    memset(twosect+8, 0, 512-8);
    twosect[0x100] = ' ';
    twosect[0x101] = ' ';
    twosect[0x102] = ' ';
    twosect[0x103] = ' ';
    twosect[0x104] = 1;
    twosect[0x106] = (nsect >> 8);
    twosect[0x107] = (nsect & 0xff);
}

static void new_dfs_single(FILE *f, const struct sdf_geometry *geo)
{
    unsigned char twosect[512];

    prep_dfs(twosect, geo);
    fwrite(twosect, sizeof twosect, 1, f);
}

static void new_dfs_interleaved(FILE *f, const struct sdf_geometry *geo)
{
    unsigned char twosect[512];

    prep_dfs(twosect, geo);
    fwrite(twosect, sizeof twosect, 1, f);
    fseek(f, geo->sectors_per_track * geo->sector_size, SEEK_SET);
    fwrite(twosect, sizeof twosect, 1, f);
}

static void pad_out(FILE *f, const struct sdf_geometry *geo)
{
    long size = geo->tracks * geo->sectors_per_track * geo->sector_size;
    if (geo->sides != SIDES_SINGLE)
        size <<= 1;
    fseek(f, size-1, SEEK_SET);
    putc(0xe5, f);
    fflush(f);
}

static void new_stl_single(FILE *f, const struct sdf_geometry *geo)
{
    new_dfs_single(f, geo);
    pad_out(f, geo);
}

static void new_stl_interleaved(FILE *f, const struct sdf_geometry *geo)
{
    new_dfs_interleaved(f, geo);
    pad_out(f, geo);
}

static void prep_watford(unsigned char *foursect, const struct sdf_geometry *geo)
{
    // normal DFS catalogue.
    prep_dfs(foursect, geo);
    // duplicate in sectors 2 and 3.
    memcpy(foursect+512+8, foursect+8, 512-8);
    // set marker for 2nd catalogue.
    memset(foursect+512, 0xaa, 8);
}

static void new_watford_single(FILE *f, const struct sdf_geometry *geo)
{
    unsigned char foursect[1024];

    prep_watford(foursect, geo);
    fwrite(foursect, sizeof foursect, 1, f);
    pad_out(f, geo);
}

static void new_watford_interleaved(FILE *f, const struct sdf_geometry *geo)
{
    unsigned char foursect[1024];

    prep_watford(foursect, geo);
    fwrite(foursect, sizeof foursect, 1, f);
    fseek(f, geo->sectors_per_track * geo->sector_size, SEEK_SET);
    fwrite(foursect, sizeof foursect, 1, f);
    pad_out(f, geo);
}

static uint8_t adfs_checksum(uint8_t *base) {
    int i = 255, c = 0;
    unsigned sum = 255;
    while (--i >= 0) {
        sum += base[i] + c;
        c = 0;
        if (sum >= 256) {
            sum &= 0xff;
            c = 1;
        }
    }
    return sum;
}

static void new_adfs(FILE *f, const struct sdf_geometry *geo)
{
    uint32_t nsect = geo->tracks * geo->sectors_per_track;
    unsigned char sects[7*256];

    if (geo->sides != SIDES_SINGLE)
        nsect *= 2;
    log_debug("sdf: sdf_prep_adfs, nsect=%d", nsect);
    memset(sects, 0, sizeof sects);
    sects[0x000] = 7;
    sects[0x0fc] = nsect;
    sects[0x0fd] = nsect >> 8;
    sects[0x0fe] = nsect >> 16;
    sects[0x0ff] = adfs_checksum(sects);
    nsect -= 7;
    sects[0x100] = nsect;
    sects[0x101] = nsect >> 8;
    sects[0x102] = nsect >> 16;
    sects[0x1fe] = 3;
    sects[0x1ff] = adfs_checksum(sects+256);
    sects[0x201] = 'H';
    sects[0x202] = 'u';
    sects[0x203] = 'g';
    sects[0x204] = 'o';
    sects[0x6cc] = 0x24;
    sects[0x6d6] = 0x02;
    sects[0x6d9] = 0x24;
    sects[0x6fb] = 'H';
    sects[0x6fc] = 'u';
    sects[0x6fd] = 'g';
    sects[0x6fe] = 'o';
    fwrite(sects, sizeof sects, 1, f);
}

static const struct sdf_geometry geo_tab[] =
{
    { "DFS",          SIDES_SINGLE,      DENS_SINGLE, 80, 10,  256, new_dfs_single          },
    { "DFS",          SIDES_SEQUENTIAL,  DENS_SINGLE, 80, 10,  256, NULL                    },
    { "DFS",          SIDES_INTERLEAVED, DENS_SINGLE, 80, 10,  256, new_dfs_interleaved     },
    { "ADFS",         SIDES_SEQUENTIAL,  DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "ADFS",         SIDES_SEQUENTIAL,  DENS_DOUBLE, 80,  5, 1024, new_adfs                },
    { "ADFS",         SIDES_INTERLEAVED, DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "Solidisk",     SIDES_SINGLE,      DENS_DOUBLE, 80, 16,  256, new_stl_single          },
    { "Watford/Opus", SIDES_SINGLE,      DENS_DOUBLE, 80, 16,  256, new_watford_single      },
    { "Solidisk",     SIDES_INTERLEAVED, DENS_DOUBLE, 80, 16,  256, new_stl_interleaved     },
    { "Watford/Opus", SIDES_INTERLEAVED, DENS_DOUBLE, 80, 18,  256, new_watford_interleaved },
    { "DOS 720k",     SIDES_INTERLEAVED, DENS_DOUBLE, 80,  9,  512, NULL                    },
    { "DOS 360k",     SIDES_INTERLEAVED, DENS_DOUBLE, 40,  9,  512, NULL                    }
};

static const char *desc_sides(const struct sdf_geometry *geo)
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

static const char *desc_dens(const struct sdf_geometry *geo)
{
    switch(geo->density) {
        case DENS_QUAD:
            return "quad-density";
        case DENS_DOUBLE:
            return "double-density";
        case DENS_SINGLE:
            return "single-density";
        default:
            return "unknown-density";
    }
}

static const struct sdf_geometry *geometry[NUM_DRIVES];
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
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES)
        if ((geo = geometry[drive]))
            if (track >= 0 && track < geo->tracks)
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE))
                    return 1;
    return 0;
}

static void io_seek(const struct sdf_geometry *geo, uint8_t drive, uint8_t sector, uint8_t track, uint8_t side)
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

FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize)
{
    const struct sdf_geometry *geo;

    if (drive < NUM_DRIVES) {
        if ((geo = geometry[drive])) {
            if (ssize == geo->sector_size) {
                io_seek(geo, drive, sector, track, side);
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

static void sdf_readsector(int drive, int sector, int track, int side, int density)
{
    const struct sdf_geometry *geo;

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
    const struct sdf_geometry *geo;

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
    const struct sdf_geometry *geo;

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
    const struct sdf_geometry *geo;

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

static bool has_dfs_cat(FILE *fp, long offset)
{
    uint32_t dirsize, sects, cur_start, new_start;
    uint8_t *base, twosect[512];

    if (fseek(fp, offset, SEEK_SET) >= 0) {
        if (fread(twosect, sizeof twosect, 1, fp) == 1) {
            dirsize = twosect[0x105];
            log_debug("sdf: has_dfs_cat: dirsize=%d bytes, %d entries", dirsize, dirsize / 8);
            if (!(dirsize & 0x07) && dirsize <= (31 * 8)) {
                sects = ((twosect[0x106] & 0x07) << 8) | twosect[0x107];
                if (sects <= SSD_SIDE_SIZE) {
                    base = twosect + 0x100;
                    cur_start = UINT32_MAX;
                    // Check the files are sorted by decreasing start sector.
                    while (dirsize > 0) {
                        base += 8;
                        dirsize -= 8;
                        new_start = ((base[0x006] & 0x80) << 3) | ((base[0x106] & 0x03) << 8) | base[0x107];
                        if (new_start > cur_start)
                            return false;
                        cur_start = new_start;
                    }
                    return true;
                    log_debug("sdf: has_dfs_cat: returning true for %lx", offset);
                }
                else
                    log_debug("sdf: has_dfs_cat: too many sectors for %lx", offset);
            }
            else
                log_debug("sdf: has_dfs_cat: dirsize not valid for %lx", offset);
        }
        else
            log_debug("sdf: has_dfs_cat: unable to read for %lx", offset);
    }
    else
        log_debug("sdf: has_dfs_cat: unable to seek to %lx: %s", offset, strerror(errno));
    return false;
}

static void sdf_mount(int drive, const char *fn, FILE *fp, const struct sdf_geometry *geo)
{
    sdf_fp[drive] = fp;
    log_info("Loaded drive %d with %s, format %s, %s, %d tracks, %s, %d %d byte sectors/track",
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

void sdf_load(int drive, const char *fn, const char *ext)
{
    FILE *fp;
    const struct sdf_geometry *geo;
    long fsize;

    writeprot[drive] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[drive] = 1;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);

    geo = NULL;
    if (ext) {
        if (!strcasecmp(ext, "ssd")) {
            // check for sequential sides.
            if (fsize > SSD_SIDE_SIZE && has_dfs_cat(fp, 0) && has_dfs_cat(fp, SSD_SIDE_SIZE))
                geo = geo_tab + SDF_FMT_DFS_SEQUENTIAL;
            else
                geo = geo_tab + SDF_FMT_DFS_SINGLE;
        }
        else if (!strcasecmp(ext, "dsd")) {
            // check for sequential sides.
            if (has_dfs_cat(fp, 0) && has_dfs_cat(fp, SSD_SIDE_SIZE))
                geo = geo_tab + SDF_FMT_DFS_SEQUENTIAL;
            else
                geo = geo_tab + SDF_FMT_DFS_INTERLEAVED;
        }
        else if (!strcasecmp(ext, "adf")) {
            if (fsize > (700 * 1024))
                geo = geo_tab + SDF_FMT_ADFS_SEQ_LARGE;
            else
                geo = geo_tab + SDF_FMT_ADFS_SEQ_SMALL;
        }
        else if (!strcasecmp(ext, "adl"))
            geo = geo_tab + SDF_FMT_ADFS_INTERLEAVED;
        else if (!strcasecmp(ext, "sdd")) {
            if (fsize == (80 * 16 * 256))
                geo = geo_tab + SDF_FMT_DDFS_SINGLE_16S;
            else if (fsize == (80 * 18 * 256))
                geo = geo_tab + SDF_FMT_DDFS_SINGLE_18S;
        }
        else if (!strcasecmp(ext, "ddd")) {
            if (fsize == (2 * 80 * 16 * 256))
                geo = geo_tab + SDF_FMT_DDFS_INTERLEAVED_16S;
            else if (fsize == (2 * 80 * 18 * 256))
                geo = geo_tab + SDF_FMT_DDFS_INTERLEAVED_18S;
        }
    }
    if (!geo) {
        switch(fsize)
        {
        case 800*1024: // 800k ADFS/DOS - 80*2*5*1024
            geo = geo_tab + SDF_FMT_ADFS_SEQ_LARGE;
            break;
        case 640*1024: // 640k ADFS/DOS - 80*2*16*256
            geo = geo_tab + SDF_FMT_ADFS_INTERLEAVED;
            break;
        case 720*1024: // 720k DOS - 80*2*9*512
            geo = geo_tab + SDF_FMT_DOS720K;
            break;
        case 360*1024: // 360k DOS - 40*2*9*512
            geo = geo_tab + SDF_FMT_DOS360K;
            break;
        case 200*1024: // 200k DFS - 80*1*10*256
            geo = geo_tab + SDF_FMT_DFS_SINGLE;
            break;
        case 400*1024: // 400k DFS - 80*2*10*256
            geo = geo_tab + SDF_FMT_DFS_INTERLEAVED;
            break;
        default:
            log_error("Unable to determine geometry for %s", fn);
            fclose(fp);
            return;
        }
    }
    sdf_mount(drive, fn, fp, geo);
}

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, sdf_disc_type dtype)
{
    const struct sdf_geometry *geo;
    const char *cpath;
    FILE *f;

    if (dtype > SDF_FMT_MAX)
        log_error("sdf: inavlid disc type %d for new disc", dtype);
    else {
        geo = geo_tab + dtype;
        if (!geo->new_disc)
            log_error("sdf: creation of file disc type %s (%d) not supported", geo->name, dtype);
        else {
            cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "wb+"))) {
                geo->new_disc(f, geo);
                sdf_mount(drive, cpath, f, geo);
            }
            else
                log_error("Unable to open disk image %s for writing: %s", cpath, strerror(errno));
        }
    }
}
