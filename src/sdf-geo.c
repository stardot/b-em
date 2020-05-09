/*
 * B-EM SDF - Simple Disk Formats - Geometry
 *
 * This B-Em module is part of the handling of simple disc formats,
 * i.e. those where the sectors that comprise the disk image are
 * stored in the file in a logical order and without ID headers.
 *
 * This module contains a table of disc geometries and functions
 * to detect one of these from a filename and open image file, to
 * create a disk image of the corresponding type and to report on
 * the geometry.
 */

#include <allegro5/allegro.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "logging.h"
#include "sdf.h"

#define SSD_SIDE_SIZE (80 * 10 * 256)

/* Functions to create various disc image types. */

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
    if (geo->sides != SDF_SIDES_SINGLE)
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

    if (geo->sides != SDF_SIDES_SINGLE)
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

/*
 * Table of disc geometries.
 *
 * The order of the entries in this table is contained to match the
 * order of the enum sdf_disc_type and all DFS types must occur as
 * a set of threee geometries in the order single-sided, interleaved
 * sides and sequential sides.
 */

struct sdf_geometry sdf_geo_tab[] =
{
    { "ADFS-S",       SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 40, 16,  256, new_adfs                },
    { "ADFS-M",       SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "ADFS-L",       SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "ADFS-D",       SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80,  5, 1024, NULL                    },
    { "Acorn DFS",    SDF_SIDES_SINGLE,      SDF_DENS_SINGLE, 40, 10,  256, new_dfs_single          },
    { "Acorn DFS",    SDF_SIDES_INTERLEAVED, SDF_DENS_SINGLE, 40, 10,  256, new_dfs_interleaved     },
    { "Acorn DFS",    SDF_SIDES_SEQUENTIAL,  SDF_DENS_SINGLE, 40, 10,  256, NULL                    },
    { "Acorn DFS",    SDF_SIDES_SINGLE,      SDF_DENS_SINGLE, 80, 10,  256, new_dfs_single          },
    { "Acorn DFS",    SDF_SIDES_INTERLEAVED, SDF_DENS_SINGLE, 80, 10,  256, new_dfs_interleaved     },
    { "Acorn DFS",    SDF_SIDES_SEQUENTIAL,  SDF_DENS_SINGLE, 80, 10,  256, NULL                    },
    { "Solidisk",     SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 80, 16,  256, new_stl_single          },
    { "Solidisk",     SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80, 16,  256, new_stl_interleaved     },
    { "Solidisk",     SDF_SIDES_SEQUENTIAL,  SDF_DENS_DOUBLE, 80, 16,  256, NULL                    },
    { "Watford/Opus", SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 80, 18,  256, new_watford_single      },
    { "Watford/Opus", SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80, 18,  256, new_watford_interleaved },
    { "Watford/Opus", SDF_SIDES_SEQUENTIAL,  SDF_DENS_DOUBLE, 80, 18,  256, NULL                    },
    { "DOS 720k",     SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80,  9,  512, NULL                    },
    { "DOS 360k",     SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 40,  9,  512, NULL                    }
};

/* Functions to describe sides and density */

const char *sdf_desc_sides(const struct sdf_geometry *geo)
{
    switch(geo->sides) {
        case SDF_SIDES_SINGLE:
            return "single-sided";
        case SDF_SIDES_SEQUENTIAL:
            return "double-sided, sequential";
        case SDF_SIDES_INTERLEAVED:
            return "double-sided, interleaved";
        default:
            return "unknown";
    }
}

const char *sdf_desc_dens(const struct sdf_geometry *geo)
{
    switch(geo->density) {
        case SDF_DENS_QUAD:
            return "quad-density";
        case SDF_DENS_DOUBLE:
            return "double-density";
        case SDF_DENS_SINGLE:
            return "single-density";
        default:
            return "unknown-density";
    }
}

/* Geometry identification */

static bool adfs_hugo_at(FILE *fp, long offset)
{
    unsigned char hugo[4];

    if (fseek(fp, offset, SEEK_SET) == 0) {
        if (fread(hugo, sizeof hugo, 1, fp) == 1) {
            if (!memcmp(hugo, "Hugo", 4)) {
                log_debug("sdf: found ADFS hugo at %lx", offset);
                return true;
            }
        }
    }
    log_debug("sdf: ADFS hugo not found at %lx", offset);
    return false;
}

static int32_t adfs_get_size(FILE *fp)
{
    if (fseek(fp, 0, SEEK_SET) == 0) {
        unsigned char twosect[0x200];
        if (fread(twosect, sizeof twosect, 1, fp) == 1) {
            uint8_t csum1 = adfs_checksum(twosect);
            uint8_t csum2 = adfs_checksum(twosect+0x100);
            log_debug("sdf: ADFS sector 0 checksum, calc=%02X, stored=%02X", csum1, twosect[0xff]);
            log_debug("sdf: ADFS sector 1 checksum, calc=%02X, stored=%02X", csum2, twosect[0x1ff]);
            if (csum1 == twosect[0xff] && csum2 == twosect[0x1ff]) {
                int32_t size = (twosect[0xfe] << 16) | (twosect[0xfd] << 8) | twosect[0xfc];
                log_debug("sdf: found ADFS total sectors as %u", size);
                return size;
            }
        }
    }
    return -1;
}

static const struct sdf_geometry *find_geo_adfs(FILE *fp)
{
    if (adfs_hugo_at(fp, 0x201) && adfs_hugo_at(fp, 0x6fb)) {
        // ADFS format S, M or L
        int32_t size = adfs_get_size(fp);
        if (size >= 0) {
            if (size <= (40*16))
                return sdf_geo_tab + SDF_FMT_ADFS_S;
            else if (size <= (80*16))
                return sdf_geo_tab + SDF_FMT_ADFS_M;
            else
                return sdf_geo_tab + SDF_FMT_ADFS_L;
        }
    }
    else if (adfs_hugo_at(fp, 0x401) && adfs_hugo_at(fp, 0xbfb)) {
        if (adfs_get_size(fp) >= 0)
            return sdf_geo_tab + SDF_FMT_ADFS_D;
    }
    return NULL;
}

static int32_t dfs_size(FILE *fp, long offset, long fsize)
{
    if (offset < (fsize + 0x200)) {
        log_debug("sdf: looking for DFS catalogue at offset %lx", offset);
        if (fseek(fp, offset+0x100, SEEK_SET) >= 0) {
            uint8_t sect[0x100];
            if (fread(sect, sizeof sect, 1, fp) == 1) {
                uint32_t dirsize = sect[5];
                log_debug("sdf: DFS dirsize=%d bytes, %d entries", dirsize, dirsize / 8);
                if (!(dirsize & 0x07) && dirsize <= (31 * 8)) {
                    uint32_t sects = ((sect[6] & 0x07) << 8) | sect[7];
                    uint8_t *base = sect;
                    uint32_t cur_start = UINT32_MAX;
                    // Check the files are sorted by decreasing start sector.
                    while (dirsize > 0) {
                        base += 8;
                        dirsize -= 8;
                        uint32_t new_start = ((base[6] & 0x03) << 8) | base[7];
                        if (new_start == 0) {
                            log_debug("sdf: impossible start position");
                            return -1;
                        }
                        if (new_start > cur_start) {
                            log_debug("sdf: catalogue not sorted");
                            return -1;
                        }
                        cur_start = new_start;
                    }
                    log_debug("sdf: found DFS size as %d sectors", sects);
                    return sects;
                }
                else
                    log_debug("sdf: DFS dirsize not valid");
            }
            else
                log_debug("sdf: unable to read");
        }
        else
            log_debug("sdf: unable to seek: %s", strerror(errno));
    }
    else
        log_debug("sdf: image file too small to read DFS catalogue at offset %lx", offset);
    return -1;
}

static bool is_tail(const char *ext)
{
    // Check file extension finishes 'sd' or 'dd' in either case.
    int c = ext[1];
    if (c == 's' || c == 'S' || c == 'd' || c == 'D') {
        c = ext[2];
        return (c == 'd' || c == 'D') && !ext[3];
    }
    return false;
}

static bool is_ssd(const char *ext)
{
    // Check file extension is 'ssd' or 'sdd' in either case.
    int c = ext[0];
    return (c == 's' || c == 'S') && is_tail(ext);
}

static bool is_dsd(const char *ext)
{
    // Check file extension is 'dsd' or 'ddd' in either case.
    int c = ext[0];
    return (c == 'd' || c == 'D') && is_tail(ext);
}

static const struct sdf_geometry *find_geo_dfs(const char *fn, const char *ext, FILE *fp, long fsize)
{
    /* Work out the number of tracks and the number of sectors per
     * track from the first side.
     */
    int32_t sects = dfs_size(fp, 0, fsize);
    if (sects >= 0) {
        const struct sdf_geometry *geo;
        if (sects <= (40 * 10))
            geo = sdf_geo_tab + SDF_FMT_DFS_10S_SIN_40T;
        else if (sects <= (80 * 10))
            geo = sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
        else if (sects <= (80 * 16))
            geo = sdf_geo_tab + SDF_FMT_DFS_16S_SIN_80T;
        else if (sects <= (80 * 18))
            geo = sdf_geo_tab + SDF_FMT_DFS_18S_SIN_80T;
        else {
            log_debug("sdf: DFS sector count too high (%u) for %s", sects, fn);
            return NULL;
        }

        int32_t track_bytes = geo->sectors_per_track * geo->sector_size;
        int32_t side_bytes = track_bytes * geo->tracks;

        if (is_ssd(ext)) {
            /* An SSD file will usually be single-sided but there are
             * some where the acronym has been interpreted as
             * sequential-sided disk.
             */
            if (dfs_size(fp, side_bytes, fsize) >= 3)
                geo += 2;   // sequential side version.
        }
        else if (is_dsd(ext)) {
            geo++;          // interleaved side version.
            int32_t size_int = dfs_size(fp, track_bytes, fsize);
            if (size_int != sects) {
                int32_t size_seq = dfs_size(fp, side_bytes, fsize);
                if ((size_int >= 3 && size_seq == sects) || size_seq >= 3)
                    geo++;  // sequential side version.
            }
        }
        return geo;
    }
    return NULL;
}

static const struct sdf_geometry *find_geo_size(FILE *fp, long fsize)
{
    switch(fsize)
    {
    case 800*1024: // 800k ADFS/DOS - 80*2*5*1024
        return sdf_geo_tab + SDF_FMT_ADFS_D;
    case 640*1024: // 640k ADFS/DOS - 80*2*16*256
        return sdf_geo_tab + SDF_FMT_ADFS_L;
    case 720*1024: // 720k DOS - 80*2*9*512
        return sdf_geo_tab + SDF_FMT_DOS720K;
    case 360*1024: // 360k DOS - 40*2*9*512
        return sdf_geo_tab + SDF_FMT_DOS360K;
    case 200*1024: // 200k DFS - 80*1*10*256
        return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
    case 400*1024: // 400k DFS - 80*2*10*256
        return sdf_geo_tab + SDF_FMT_DFS_10S_INT_80T;
    }
    return NULL;
}

const struct sdf_geometry *sdf_find_geo(const char *fn, const char *ext, FILE *fp)
{
    long fsize;
    const struct sdf_geometry *geo;

    if ((geo = find_geo_adfs(fp)) == NULL) {
        fseek(fp, 0, SEEK_END);
        fsize = ftell(fp);
        if ((geo = find_geo_dfs(fn, ext, fp, fsize)) == NULL)
            geo = find_geo_size(fp, fsize);
    }
    return geo;
}
