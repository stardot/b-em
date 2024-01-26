#define _DEBUG
/*
 * B-EM MMB - Support for MMB files.
 *
 * This B-Em module is to support MMB files which are containers
 * containing a number of SSD disc images with an index at the start.
 *
 * This module works with the SDF module to enable disk images to
 * be inserted and then access via the normal FDC and therefore
 * Acorn DFS without the need to load a MMB-specific filing system.
 */

#include "b-em.h"
#include "disc.h"
#include "mmb.h"
#include "sdf.h"
#include "6502.h"
#include "mem.h"
#include "vdfs.h"

#define MMB_DISC_SIZE      (200*1024)
#define MMB_NAME_SIZE      16
#define MMB_ZONE_DISCS     511
#define MMB_ZONE_CAT_SIZE  (MMB_ZONE_DISCS*MMB_NAME_SIZE)
#define MMB_ZONE_FULL_SIZE (MMB_ZONE_CAT_SIZE+MMB_NAME_SIZE+MMB_ZONE_DISCS*MMB_DISC_SIZE)
#define MMB_ZONE_SKIP_SIZE (MMB_ZONE_DISCS*MMB_DISC_SIZE+MMB_NAME_SIZE)

static unsigned mmb_boot_discs[4];
static unsigned mmb_cat_size;
static char *mmb_cat;
unsigned mmb_ndisc;
char *mmb_fn;

static void mmb_read_error(const char *fn, FILE *fp)
{
    if (ferror(fp))
        log_error("mmb: read error on MMB file %s: %s", fn, strerror(errno));
    else
        log_error("mmb: unexpected EOF on MMB file %s", fn);
    fclose(fp);
}

static unsigned mmb_calc_offset(unsigned disc)
{
    unsigned zone_start = disc / MMB_ZONE_DISCS;
    unsigned zone_index = disc % MMB_ZONE_DISCS;
    unsigned offset = zone_start * MMB_ZONE_FULL_SIZE + MMB_ZONE_CAT_SIZE + MMB_NAME_SIZE + zone_index * MMB_DISC_SIZE;
    log_debug("mmb: mmb_calc_offset(%u) -> zone_start=%u, zone_index=%u, offset=%u", disc, zone_start, zone_index, offset);
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
    log_debug("mmb: mmb extra zones=%u, mmb cat total size=%u", extra_zones, reqd_cat_size);
    if (reqd_cat_size != mmb_cat_size) {
        if (mmb_cat)
            free(mmb_cat);
        if (!(mmb_cat = malloc(reqd_cat_size))) {
            log_error("mmb: out of memory allocating MMB catalogue");
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
            log_error("mmb: seek error on MMB file %s: %s", fn, strerror(errno));
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
        log_debug("mmb: mmb#%04u=%-12.12s", i++, mmb_ptr);
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
    log_debug("mmb: picking MMB disc, drive=%d, side=%d, disc=%d", drive, side, disc);

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
