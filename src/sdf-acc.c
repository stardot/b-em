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

#define MMB_DISC_SIZE      (200*1024)
#define MMB_NAME_SIZE      16
#define MMB_ZONE_DISCS     511
#define MMB_ZONE_CAT_SIZE  (MMB_ZONE_DISCS*MMB_NAME_SIZE)
#define MMB_ZONE_FULL_SIZE (MMB_ZONE_CAT_SIZE+MMB_NAME_SIZE+MMB_ZONE_DISCS*MMB_DISC_SIZE)
#define MMB_ZONE_SKIP_SIZE (MMB_ZONE_DISCS*MMB_DISC_SIZE+MMB_NAME_SIZE)

static FILE *sdf_fp[NUM_DRIVES], *mmb_fp;
static const struct sdf_geometry *geometry[NUM_DRIVES];
static uint8_t current_track[NUM_DRIVES];
static off_t mmb_offset[NUM_DRIVES][2];
static unsigned mmb_boot_discs[4];
static unsigned mmb_cat_size;
static char *mmb_cat;
unsigned mmb_ndisc;
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
    ST_FORMAT_CYLID,
    ST_FORMAT_HEADID,
    ST_FORMAT_SECTID,
    ST_FORMAT_SECTSZ,
    ST_WRTRACK_INITIAL,
    ST_WRTRACK_CYLID,
    ST_WRTRACK_HEADID,
    ST_WRTRACK_SECTID,
    ST_WRTRACK_SECTSZ,
    ST_WRTRACK_HDRCRC,
    ST_WRTRACK_DATA,
    ST_WRTRACK_DATACRC
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

static void sdf_format(int drive, int track, int side, unsigned par2)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, 0, track, side, 0))) {
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = 0;
        count = par2 & 0x1f; // sectors per track.
        state = ST_FORMAT_CYLID;
    }
}

static void sdf_writetrack(int drive, int track, int side, int density)
{
    const struct sdf_geometry *geo;

    if (state == ST_IDLE && (geo = check_seek(drive, 0, track, side, density))) {
        sdf_drive = drive;
        sdf_side = side;
        sdf_track = track;
        sdf_sector = 0;
        count = 120;
        state = ST_WRTRACK_INITIAL;
    }
    else {
        fdc_writeprotect();
        state = ST_IDLE;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements the initial state which is waiting
 * for the ID address mark.  If this does not arrive within a
 * reasonable number of bytes we conclude the last sector has been
 * received.
 */

static void sdf_poll_wrtrack_initial(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_initial, byte=%02X, count=%u", b, count);
    if (b == 0xfe)
        state = ST_WRTRACK_CYLID;
    else if (--count == 0) {
        fdc_finishread(false);
        state = ST_IDLE;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which skips the cylinder
 * and head IDs.
 */

static void sdf_poll_wrtrack_next(state_t nstate)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_next byte=%02X", b);
    if (b != -1)
        state = nstate;
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the sector ID
 * from the ID header.
 */

static void sdf_poll_wrtrack_sectid(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_sectid byte=%02X", b);
    if (b != -1) {
        if (b > geometry[sdf_drive]->sectors_per_track)
            log_warn("sdf: formatting sector %d beyond that supported by disc image", b);
        sdf_sector = b;
        state = ST_WRTRACK_SECTSZ;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the logical
 * sector size code from the ID header.
 */

static void sdf_poll_wrtrack_sectsz(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_sectsz byte=%02X", b);
    if (b != -1) {
        unsigned bytes = 128 << b;
        if (bytes != geometry[sdf_drive]->sector_size)
            log_warn("sdf: formatting, sector size mismatch: id %d, image file %d", b, geometry[sdf_drive]->sector_size);
        count = bytes;
        state = ST_WRTRACK_HDRCRC;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which waits for the request
 * to generate the header CRC.
 */

static void sdf_poll_wrtrack_hdrcrc(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_hdrcrc byte=%02X", b);
    if (b == 0xfb) {
        io_seek(geometry[sdf_drive], sdf_drive, sdf_sector, current_track[sdf_drive], sdf_side);
        state = ST_WRTRACK_DATA;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which receives the
 * first byte of the sector data.
 */

static void sdf_poll_wrtrack_data(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_data0 byte=%02X, count=%d", b, count);
    if (b != -1) {
        putc(b, sdf_fp[sdf_drive]);
        if (!--count)
            state = ST_WRTRACK_DATACRC;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which waits for the byte
 * requesting the data CRC be generated.
 */

static void sdf_poll_wrtrack_datacrc(void)
{
    int b = fdc_getdata(0);
    log_debug("sdf: sdf_poll_wrtrack_datacrc byte=%02X", b);
    if (b == 0xf7) {
        state = ST_WRTRACK_INITIAL;
        count = 120;
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
            if ((c = getc(sdf_fp[sdf_drive])) == EOF)
                c = 0xe5;
            fdc_data(c);
            if (--count == 0) {
                fdc_finishread(false);
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
                    fdc_finishread(false);
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
            fdc_finishread(false);
            sdf_sector++;
            if (sdf_sector == geometry[sdf_drive]->sectors_per_track)
                sdf_sector = 0;
            break;

        case ST_FORMAT_CYLID:
            if (writeprot[sdf_drive]) {
                log_debug("sdf: poll, write protected during format");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            fdc_getdata(0); // discard cylinder ID.
            state = ST_FORMAT_HEADID;
            break;

        case ST_FORMAT_HEADID:
            fdc_getdata(0); // discard head ID.
            state = ST_FORMAT_SECTID;
            break;

        case ST_FORMAT_SECTID:
            fdc_getdata(0); // discard sector ID
            state = ST_FORMAT_SECTSZ;
            break;

        case ST_FORMAT_SECTSZ:
            fdc_getdata(--count == 0);  // discard sector size.
            log_debug("sdf: poll format secsz, count=%d, sector=%d", count, sdf_sector);
            if (sdf_sector < geometry[sdf_drive]->sectors_per_track) {
                log_debug("sdf: poll format secsz, filling at offset %lu", ftell(sdf_fp[sdf_drive]));
                for (unsigned i = 0; i < geometry[sdf_drive]->sector_size; i++)
                    putc(0xe5, sdf_fp[sdf_drive]);
                sdf_sector++;
            }
            if (count == 0) {
                state = ST_IDLE;
                fdc_finishread(false);
            }
            else
                state = ST_FORMAT_CYLID;
            break;
        case ST_WRTRACK_INITIAL:
            sdf_poll_wrtrack_initial();
            break;
        case ST_WRTRACK_CYLID:
            sdf_poll_wrtrack_next(ST_WRTRACK_HEADID);
            break;
        case ST_WRTRACK_HEADID:
            sdf_poll_wrtrack_next(ST_WRTRACK_SECTID);
            break;
        case ST_WRTRACK_SECTID:
            sdf_poll_wrtrack_sectid();
            break;
        case ST_WRTRACK_SECTSZ:
            sdf_poll_wrtrack_sectsz();
            break;
        case ST_WRTRACK_HDRCRC:
            sdf_poll_wrtrack_hdrcrc();
            break;
        case ST_WRTRACK_DATA:
            sdf_poll_wrtrack_data();
            break;
        case ST_WRTRACK_DATACRC:
            sdf_poll_wrtrack_datacrc();
            break;
    }
}

static void sdf_abort(int drive)
{
    state = ST_IDLE;
}

#ifndef WIN32
static void sdf_lock(int drive, FILE *fp, int ltype)
{
#ifdef linux
#define LOCK_WAIT F_OFD_SETLKW
#else
#define LOCK_WAIT F_SETLKW
#endif
    int res;
    struct flock fl;
    int fd = fileno(fp);

    fl.l_type = ltype;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = 0;

    do {
        if (!(res = fcntl(fd, LOCK_WAIT, &fl))) {
            log_debug("sdf: lock operation %d successful for drive %d", ltype, drive);
            return;
        }
    } while (errno == EINTR);
    log_warn("sdf: lock failure on drive %d", drive);
}
#endif

static void sdf_spinup(int drive)
{
    log_debug("sdf: spinup drive %d", drive);
#ifndef WIN32
    FILE *fp = sdf_fp[drive];
    if (fp)
        sdf_lock(drive, fp, F_WRLCK);
#endif
}

static void sdf_spindown(int drive)
{
    FILE *fp = sdf_fp[drive];
    log_debug("sdf: spindown drive %d", drive);
    if (fp) {
        fflush(fp);
#ifndef WIN32
        sdf_lock(drive, fp, F_UNLCK);
#endif
    }
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
    drives[drive].writetrack  = sdf_writetrack;
    drives[drive].abort       = sdf_abort;
    drives[drive].spinup      = sdf_spinup;
    drives[drive].spindown    = sdf_spindown;

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

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, const struct sdf_geometry *geo)
{
    if (!geo->new_disc)
        log_error("sdf: drive %d: creation of file disc type %s not supported", drive, geo->name);
    else {
        const char *cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
        FILE *f = fopen(cpath, "wb+");
        if (f) {
            writeprot[drive] = 0;
            geo->new_disc(f, geo);
            sdf_mount(drive, cpath, f, geo);
        }
        else
            log_error("sdf: drive %d: unable to open disk image %s for writing: %s", drive, cpath, strerror(errno));
    }
}

static void mmb_read_error(const char *fn, FILE *fp)
{
    if (ferror(fp))
        log_error("sdf: read error on MMB file %s: %s", fn, strerror(errno));
    else
        log_error("sdf: unexpected EOF on MMB file %s", fn);
    fclose(fp);
}

static unsigned mmb_calc_offset(unsigned disc)
{
    unsigned zone_start = disc / MMB_ZONE_DISCS;
    unsigned zone_index = disc % MMB_ZONE_DISCS;
    unsigned offset = zone_start * MMB_ZONE_FULL_SIZE + MMB_ZONE_CAT_SIZE + MMB_NAME_SIZE + zone_index * MMB_DISC_SIZE;
    log_debug("sdf: mmb_calc_offset(%u) -> zone_start=%u, zone_index=%u, offset=%u", disc, zone_start, zone_index, offset);
    return offset;
}

void mmb_load(char *fn)
{
    writeprot[0] = 0;
    FILE *fp = fopen(fn, "rb+");
    if (fp == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[0] = 1;
    }
    unsigned char header[16];
    if (fread(header, sizeof(header), 1, fp) != 1) {
        mmb_read_error(fn, fp);
        return;
    }
    mmb_boot_discs[0] = header[0] | (header[4] << 8);
    mmb_boot_discs[1] = header[1] | (header[5] << 8);
    mmb_boot_discs[2] = header[2] | (header[6] << 8);
    mmb_boot_discs[3] = header[3] | (header[7] << 8);
    unsigned extra_zones = header[8];
    extra_zones = ((extra_zones & 0xf0) == 0xa0) ? extra_zones & 0x0f : 0;
    unsigned reqd_cat_size = (extra_zones + 1) * MMB_ZONE_CAT_SIZE;
    log_debug("sdf-acc: mmb extra zones=%u, mmb cat total size=%u", extra_zones, reqd_cat_size);
    if (reqd_cat_size != mmb_cat_size) {
        if (mmb_cat)
            free(mmb_cat);
        if (!(mmb_cat = malloc(reqd_cat_size))) {
            log_error("sdf: out of memory allocating MMB catalogue");
            return;
        }
        mmb_cat_size = reqd_cat_size;
    }
    if (fread(mmb_cat, MMB_ZONE_CAT_SIZE, 1, fp) != 1) {
        mmb_read_error(fn, fp);
        return;
    }
    char *mmb_ptr = mmb_cat + MMB_ZONE_CAT_SIZE;
    char *mmb_end = mmb_cat + reqd_cat_size;
    while (mmb_ptr < mmb_end) {
        if (fseek(fp, MMB_ZONE_SKIP_SIZE, SEEK_CUR)) {
            log_error("sdf: seek error on MMB file %s: %s", fn, strerror(errno));
            fclose(fp);
            return;
        }
        if (fread(mmb_ptr, MMB_ZONE_CAT_SIZE, 1, fp) != 1) {
            mmb_read_error(fn, fp);
            return;
        }
        mmb_ptr += MMB_ZONE_CAT_SIZE;
    }
    unsigned i = 0;
    for (mmb_ptr = mmb_cat; mmb_ptr < mmb_end; mmb_ptr += 16)
        log_debug("sdf-acc: mmb#%04u=%-12.12s", i++, mmb_ptr);
    if (mmb_fp) {
        fclose(mmb_fp);
        if (sdf_fp[1] == mmb_fp) {
            sdf_mount(1, fn, fp, &sdf_geometries.dfs_10s_seq_80t);
            writeprot[1] = writeprot[0];
            mmb_offset[1][0] = mmb_calc_offset(mmb_boot_discs[2]);
            mmb_offset[1][1] = mmb_calc_offset(mmb_boot_discs[3]);
        }
    }
    sdf_mount(0, fn, fp, &sdf_geometries.dfs_10s_seq_80t);
    mmb_offset[0][0] = mmb_calc_offset(mmb_boot_discs[0]);
    mmb_offset[0][1] = mmb_calc_offset(mmb_boot_discs[1]);
    mmb_fp = fp;
    mmb_fn = fn;
    mmb_ndisc = (extra_zones + 1) * MMB_ZONE_DISCS;
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
    mmb_ndisc = 0;
}

static void reset_one(int drive)
{
    if (sdf_fp[drive] == mmb_fp) {
        mmb_offset[drive][0] = mmb_calc_offset(0);
        mmb_offset[drive][1] = mmb_calc_offset(1);
    }
}

void mmb_reset(void)
{
    if (mmb_fp) {
        reset_one(0);
        reset_one(1);
    }
}

void mmb_pick(unsigned drive, unsigned side, unsigned disc)
{
    log_debug("sdf: picking MMB disc, drive=%d, side=%d, disc=%d", drive, side, disc);

    if (sdf_fp[drive] != mmb_fp) {
        disc_close(drive);
        sdf_mount(drive, mmb_fn, mmb_fp, &sdf_geometries.dfs_10s_seq_80t);
    }
    mmb_offset[drive][side] = mmb_calc_offset(disc);
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
    return (cat_nxt - mmb_cat) / 16 - 1;
}

int mmb_find(const char *name)
{
    const char *cat_ptr = mmb_cat;
    const char *cat_end = mmb_cat + mmb_cat_size;

    do {
        const char *cat_nxt = cat_ptr + 16;
        int i = cat_name_cmp(name, cat_ptr, cat_nxt);
        if (i >= 0) {
            log_debug("mmb: found MMB SSD '%s' at %d", name, i);
            return i;
        }
        cat_ptr = cat_nxt;
    } while (cat_ptr < cat_end);
    return -1;
}
