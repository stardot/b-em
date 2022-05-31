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
 */

const struct sdf_geometry_set sdf_geometries = {
    { "ADFS-S",       SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 40, 16,  256, new_adfs                },
    { "ADFS-M",       SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "ADFS-L",       SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80, 16,  256, new_adfs                },
    { "ADFS-D",       SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80,  5, 1024, NULL                    },
    { "Acorn DFS",    SDF_SIDES_SINGLE,      SDF_DENS_SINGLE, 40, 10,  256, new_dfs_single          },
    { "Acorn DFS",    SDF_SIDES_INTERLEAVED, SDF_DENS_SINGLE, 40, 10,  256, new_dfs_interleaved     },
    { "Acorn DFS",    SDF_SIDES_SINGLE,      SDF_DENS_SINGLE, 80, 10,  256, new_dfs_single          },
    { "Acorn DFS",    SDF_SIDES_INTERLEAVED, SDF_DENS_SINGLE, 80, 10,  256, new_dfs_interleaved     },
    { "Acorn DFS",    SDF_SIDES_SEQUENTIAL,  SDF_DENS_SINGLE, 80, 10,  256, NULL                    },
    { "Solidisk",     SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 40, 16,  256, new_stl_single          },
    { "Solidisk",     SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 40, 16,  256, new_stl_interleaved     },
    { "Solidisk",     SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 80, 16,  256, new_stl_single          },
    { "Solidisk",     SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 80, 16,  256, new_stl_interleaved     },
    { "Solidisk",     SDF_SIDES_SEQUENTIAL,  SDF_DENS_DOUBLE, 80, 16,  256, NULL                    },
    { "Watford/Opus", SDF_SIDES_SINGLE,      SDF_DENS_DOUBLE, 40, 18,  256, new_watford_single      },
    { "Watford/Opus", SDF_SIDES_INTERLEAVED, SDF_DENS_DOUBLE, 40, 18,  256, new_watford_interleaved },
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
            return "doubled-sided, sequential";
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

static bool adfs_root_at(FILE *fp, long offset)
{
    unsigned char hugo[4];

    if (fseek(fp, offset, SEEK_SET) == 0) {
        if (fread(hugo, sizeof hugo, 1, fp) == 1) {
            if (!memcmp(hugo, "Hugo", 4)) {
                log_debug("sdf: found ADFS root at %lx", offset);
                return true;
            }
        }
    }
    return false;
}

static int32_t read_size24(FILE *fp, long offset)
{
    int32_t size;
    unsigned char bytes[3];

    if (fseek(fp, offset, SEEK_SET) == 0) {
        if (fread(bytes, sizeof bytes, 1, fp) == 1) {
            size = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
            log_debug("sdf: found ADFS total sectors as %u", size);
            return size;
        }
    }
    return -1;
}

static const struct sdf_geometry *find_geo_adfs(FILE *fp, const struct sdf_geometry *dflt)
{
    if (adfs_root_at(fp, 0x201)) {
        // ADFS format S, M or L
        int32_t size = read_size24(fp, 0xfc);
        if (size >= 0) {
            if (size <= (40*16))
                return &sdf_geometries.adfs_s;
            else if (size <= (80*16))
                return &sdf_geometries.adfs_m;
            else
                return &sdf_geometries.adfs_l;
        }
    }
    else if (adfs_root_at(fp, 0x401))
        return &sdf_geometries.adfs_d;
    return dflt;
}

static int32_t dfs_size(FILE *fp, long offset)
{
    uint32_t dirsize, sects, cur_start, new_start;
    uint8_t *base, sect[0x100];

    log_debug("sdf: looking for DFS catalogue at offset %lx", offset);
    if (fseek(fp, offset+0x100, SEEK_SET) >= 0) {
        if (fread(sect, sizeof sect, 1, fp) == 1) {
            dirsize = sect[5];
            log_debug("sdf: DFS dirsize=%d bytes, %d entries", dirsize, dirsize / 8);
            if (!(dirsize & 0x07) && dirsize <= (31 * 8)) {
                sects = ((sect[6] & 0x07) << 8) | sect[7];
                base = sect;
                cur_start = UINT32_MAX;
                // Check the files are sorted by decreasing start sector.
                while (dirsize > 0) {
                    base += 8;
                    dirsize -= 8;
                    new_start = ((base[6] & 0x03) << 8) | base[7];
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
    return -1;
}

static const struct sdf_geometry *find_geo_dfs_ss(const char *fn, FILE *fp, long fsize)
{
    const struct sdf_geometry *geo = NULL;
    int32_t sects = dfs_size(fp, 0);
    if (sects > 0) {
        if (sects <= (40 * 10))
            geo = &sdf_geometries.dfs_10s_sin_40t;
        else if (sects <= (80 * 10))
            geo = &sdf_geometries.dfs_10s_sin_80t;
        else {
            log_warn("sdf: sector count too high (%u) for %s", sects, fn);
            return NULL;
        }
        uint32_t side_bytes = geo->tracks * geo->sectors_per_track * geo->sector_size;
        if (fsize > side_bytes)
            geo += 2;
    }
    else if (fsize <= (40 * 10 * 256))
        geo = &sdf_geometries.dfs_10s_sin_40t;
    else if (fsize <= (80 * 10 * 256))
        geo = &sdf_geometries.dfs_10s_sin_80t;
    else if (fsize <= (2 * 80 * 10 * 256))
        geo = &sdf_geometries.dfs_10s_seq_80t;
    return geo;
}

static const struct sdf_geometry *find_geo_dfs_is(const char *fn, FILE *fp, long fsize)
{
    int32_t sects = dfs_size(fp, 0);
    if (sects > 0) {
        if (sects <= (40 * 10))
            return &sdf_geometries.dfs_10s_int_40t;
        else if (sects <= (80 * 10))
            return &sdf_geometries.dfs_10s_int_80t;
        else {
            log_warn("sdf: sector count too high (%u) for %s", sects, fn);
            return NULL;
        }
    }
    else if (fsize <= (2 * 40 * 10 * 256))
        return &sdf_geometries.dfs_10s_int_40t;
    else if (fsize <= (2 * 80 * 10 * 256))
        return &sdf_geometries.dfs_10s_int_80t;
    else
        return NULL;
}

static const struct sdf_geometry *find_geo_ddfs_ss(const char *fn, FILE *fp, long fsize)
{
    if (fsize <= (40 * 16 * 256))
        return &sdf_geometries.dfs_16s_sin_40t;
    else if (fsize <= (40 * 18 * 256))
        return &sdf_geometries.dfs_18s_sin_40t;
    else if (fsize <= (80 * 16 * 256))
        return &sdf_geometries.dfs_16s_sin_80t;
    else if (fsize <= (80 * 18 * 256))
        return &sdf_geometries.dfs_18s_sin_80t;
    else if (fsize <= (2 * 80 * 16 * 256))
        return &sdf_geometries.dfs_16s_seq_80t;
    else if (fsize <= (2 * 80 * 18 * 256))
        return &sdf_geometries.dfs_18s_seq_80t;
    else {
        log_warn("sdf: file '%s' is too big to be a DD disc image (size=%ld)", fn, fsize);
        return NULL;
    }
}

static const struct sdf_geometry *find_geo_ddfs_is(const char *fn, FILE *fp, long fsize)
{
    if (fsize <= (2 * 40 * 16 * 256))
        return &sdf_geometries.dfs_16s_int_40t;
    else if (fsize <= (2 * 40 * 18 * 256))
        return &sdf_geometries.dfs_18s_int_40t;
    else if (fsize <= (2 * 80 * 16 * 256))
        return &sdf_geometries.dfs_16s_int_80t;
    else if (fsize <= (2 * 80 * 18 * 256))
        return &sdf_geometries.dfs_18s_int_80t;
    else {
        log_warn("sdf: file '%s' is too big to be a DD disc image (size=%ld)", fn, fsize);
        return NULL;
    }
}

static const struct sdf_geometry *find_geo_size(FILE *fp, long fsize)
{
    switch(fsize)
    {
    case 800*1024: // 800k ADFS/DOS - 80*2*5*1024
        return &sdf_geometries.adfs_d;
    case 640*1024: // 640k ADFS/DOS - 80*2*16*256
        return &sdf_geometries.adfs_l;
    case 720*1024: // 720k DOS - 80*2*9*512
        return &sdf_geometries.dos_720k;
    case 360*1024: // 360k DOS - 40*2*9*512
        return &sdf_geometries.dos_360k;
    case 200*1024: // 200k DFS - 80*1*10*256
        return &sdf_geometries.dfs_10s_sin_80t;
    case 400*1024: // 400k DFS - 80*2*10*256
        return &sdf_geometries.dfs_10s_int_80t;
    }
    return NULL;
}

const struct sdf_geometry *sdf_find_geo(const char *fn, const char *ext, FILE *fp)
{
    const struct sdf_geometry *geo = NULL;

    if (ext) {
        if (!strcasecmp(ext, "ads"))
            geo = find_geo_adfs(fp, &sdf_geometries.adfs_s);
        else if (!strcasecmp(ext, "adm"))
            geo = find_geo_adfs(fp, &sdf_geometries.adfs_m);
        else if (!strcasecmp(ext, "adl"))
            geo = find_geo_adfs(fp, &sdf_geometries.adfs_l);
        else if (!strcasecmp(ext, "adf"))
            geo = find_geo_adfs(fp, NULL);
        else {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            if (!strcasecmp(ext, "ssd"))
                geo = find_geo_dfs_ss(fn, fp, fsize);
            else if (!strcasecmp(ext, "dsd"))
                geo = find_geo_dfs_is(fn, fp, fsize);
            else if (!strcasecmp(ext, "sdd"))
                geo = find_geo_ddfs_ss(fn, fp, fsize);
            else if (!strcasecmp(ext, "ddd"))
                geo = find_geo_ddfs_is(fn, fp, fsize);
            else
                geo = find_geo_size(fp, fsize);
        }
    }
    else {
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        geo = find_geo_size(fp, fsize);
    }
    return geo;
}
