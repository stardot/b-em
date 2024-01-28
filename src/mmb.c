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
#include "main.h"
#include "mem.h"
#include "vdfs.h"

#define MMB_ENTRY_SIZE     16
#define MMB_NAME_SIZE      12
#define MMB_DISC_SIZE      (200*1024)
#define MMB_ZONE_DISCS     511
#define MMB_ZONE_CAT_SIZE  ((MMB_ZONE_DISCS+1)*MMB_ENTRY_SIZE)
#define MMB_ZONE_FULL_SIZE (MMB_ZONE_CAT_SIZE+MMB_ZONE_DISCS*MMB_DISC_SIZE)

struct mmb_zone {
    unsigned num_discs;
    unsigned char header[MMB_ENTRY_SIZE];
    unsigned char index[MMB_ZONE_DISCS][MMB_ENTRY_SIZE];
};

/*
#define MMB_ENTRY_SIZE     16
#define MMB_ZONE_SKIP_SIZE (MMB_ZONE_DISCS*MMB_DISC_SIZE+MMB_ENTRY_SIZE)
#define MMB_NAME_SIZE      12
*/

static bool mmb_writeprot;
static unsigned mmb_num_zones;
static unsigned mmb_base_zone;
static struct mmb_zone *mmb_zones;
static unsigned mmb_boot_discs[4];

static unsigned mmb_cat_size;
static unsigned char *mmb_cat;
unsigned mmb_ndisc;
char *mmb_fn;
static unsigned mmb_dcat_posn;
static unsigned mmb_dcat_end;
static unsigned mmb_dcat_count;
static unsigned mmb_dcat_pat_len;
static char mmb_dcat_pattern[MMB_NAME_SIZE];
static int mmb_loaded_discs[4] = { -1, -1, -1, -1 };

static const char err_disc_not_fnd[] = "\xd6" "Disk not found in MMB file";
static const char err_bad_drive_id[] = "\x94" "Bad drive ID";

static void mmb_read_error(const char *fn, FILE *fp)
{
    if (ferror(fp))
        log_error("mmb: read error on MMB file %s: %s", fn, strerror(errno));
    else
        log_error("mmb: unexpected EOF on MMB file %s", fn);
    fclose(fp);
}

static long mmb_boot_offset(unsigned drive)
{
    unsigned disc = mmb_boot_discs[drive];
    unsigned zone = mmb_base_zone;
    if (disc < mmb_zones[zone].num_discs) {
        long offset = zone * MMB_ZONE_FULL_SIZE + MMB_ZONE_CAT_SIZE + disc * MMB_DISC_SIZE;
        log_debug("mmb: mmb_boot_offset, drive=%u, disc=%u -> offset=%08lx", drive, disc, offset);
        return offset;
    }
    else {
        log_debug("mmb: mmb_boot_offset, drive=%u, disc=%u -> invalid disc", drive, disc);
        return 0;
    }
}

static void mmb_boot_drive(unsigned drive)
{
    long sideA = mmb_boot_offset(drive);
    long sideB = mmb_boot_offset(drive+2);
    if (sideA || sideB) {
        if (sdf_fp[drive] != mmb_fp) {
            disc_close(drive);
            sdf_mount(drive, mmb_fn, mmb_fp, &sdf_geometries.dfs_10s_seq_80t);
        }
        writeprot[drive] = mmb_writeprot;
        if (sideA) {
            mmb_offset[drive][0] = sideA;
            mmb_loaded_discs[drive] = mmb_boot_discs[drive];
        }
        if (sideB) {
            mmb_offset[drive][1] = sideB - MMB_DISC_SIZE;
            mmb_loaded_discs[drive+2] = mmb_boot_discs[drive+2];
        }
    }
}

void mmb_reset(void)
{
    if (mmb_fp) {
        mmb_boot_drive(0);
        mmb_boot_drive(1);
        if (fdc_spindown)
            fdc_spindown();
    }
}

void mmb_load(char *fn)
{
    log_info("mmb: load file '%s'", fn);
    /*
     * Start by reading the new MMB file into local variables and
     * freshly malloced memory so as not to disturb any file already
     * open.
     */
    bool new_writeprot = false;
    FILE *fp = fopen(fn, "rb+");
    if (fp == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        new_writeprot = true;
    }
    unsigned char header[16];
    if (fread(header, sizeof(header), 1, fp) != 1) {
        mmb_read_error(fn, fp);
        return;
    }
    log_dump("mmb header: ", header, 16);
    unsigned new_num_zones = 1;
    unsigned new_base_zone = 0;
    unsigned zone_byte = header[8];
    if ((zone_byte & 0xf0) == 0xa0) {
        new_num_zones = (zone_byte & 0x0f) + 1;
        new_base_zone = (header[9] & 0x0f);
        if (new_base_zone >= new_num_zones)
            new_base_zone = 0;
    }
    log_info("mmb: num_zones=%u, base_zone=%u", new_num_zones, new_base_zone);
    struct mmb_zone *new_zones = malloc(new_num_zones * sizeof(struct mmb_zone));
    if (!new_zones) {
        log_error("mmb: out of memory allocating MMB catalogue");
        fclose(fp);
        return;
    }
    for (unsigned zone = 0; zone < new_num_zones; ++zone) {
        if (fseek(fp, zone * MMB_ZONE_FULL_SIZE, SEEK_SET)) {
            log_error("mmb: seek error on MMB file %s: %s", fn, strerror(errno));
            free(new_zones);
            fclose(fp);
            return;
        }
        if (fread(new_zones[zone].header, MMB_ZONE_CAT_SIZE, 1, fp) != 1) {
            mmb_read_error(fn, fp);
            free(new_zones);
            return;
        }
        new_zones[zone].num_discs = MMB_ZONE_DISCS;
        for (unsigned discno = 0; discno < MMB_ZONE_DISCS; ++discno) {
            if (new_zones[zone].index[discno][15] == 0xff) {
                new_zones[zone].num_discs = discno;
                break;
            }
        }
    }
    /*
     * Now the file has been read successfully, replace the static
     * variables for the current file with the new one and free any
     * malloced memory.
     */
    if (mmb_zones)
        free(mmb_zones);
    mmb_zones = new_zones;
    if (mmb_fp)
        fclose(mmb_fp);
    mmb_fp = fp;
    mmb_fn = fn;
    mmb_writeprot = new_writeprot;
    mmb_num_zones = new_num_zones;
    mmb_base_zone = new_base_zone;

    const unsigned char *zone_hdr = new_zones[new_base_zone].header;
    mmb_boot_discs[0] = zone_hdr[0] | (zone_hdr[4] << 8);
    mmb_boot_discs[1] = zone_hdr[1] | (zone_hdr[5] << 8);
    mmb_boot_discs[2] = zone_hdr[2] | (zone_hdr[6] << 8);
    mmb_boot_discs[3] = zone_hdr[3] | (zone_hdr[7] << 8);
    mmb_reset();
}

static void mmb_eject_one(int drive)
{
    if (sdf_fp[drive] == mmb_fp) {
        disc_close(drive);
        ALLEGRO_PATH *path = discfns[drive];
        if (path)
            disc_load(drive, path);
    }
}

void mmb_eject(void)
{
    if (mmb_fp) {
        mmb_eject_one(0);
        mmb_eject_one(1);
        fclose(mmb_fp);
        mmb_fp = NULL;
    }
    if (mmb_zones) {
        free(mmb_zones);
        mmb_zones = NULL;
    }
    if (mmb_fn) {
        free(mmb_fn);
        mmb_fn = NULL;
    }
    mmb_loaded_discs[0] = -1;
    mmb_loaded_discs[1] = -1;
    mmb_loaded_discs[2] = -1;
    mmb_loaded_discs[3] = -1;
}

static void mmb_mount(unsigned drive, unsigned zone, unsigned posn)
{
    if (sdf_fp[drive] != mmb_fp) {
        disc_close(drive);
        sdf_mount(drive, mmb_fn, mmb_fp, &sdf_geometries.dfs_10s_seq_80t);
    }
    unsigned side = (drive & 0x02) >> 1;
    long offset = zone * MMB_ZONE_FULL_SIZE + MMB_ZONE_CAT_SIZE + posn * MMB_DISC_SIZE;;
    if (side)
        offset -= MMB_DISC_SIZE;
    mmb_offset[drive & 0x01][side] = offset;
    if (fdc_spindown)
        fdc_spindown();
}

static bool mmb_pick(unsigned drive, unsigned disc)
{
    log_debug("mmb: picking MMB disc, drive=%d, disc=%d", drive, disc);
    if (drive & ~0x03) {
        log_debug("vdfs: mmb_check_pick: invalid logical drive");
        vdfs_error(err_bad_drive_id);
        return false;
    }
    unsigned zone = disc / MMB_ZONE_DISCS + mmb_base_zone;
    unsigned posn = disc % MMB_ZONE_DISCS;
    if (posn < mmb_zones[zone].num_discs) {
        mmb_mount(drive, zone, posn);
        mmb_loaded_discs[drive] = disc;
        return true;
    }
    else {
        vdfs_error(err_disc_not_fnd);
        return false;
    }
}

static inline int mmb_cat_name_cmp(const char *nam_ptr, const unsigned char *cat_ptr, const unsigned char *cat_nxt)
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

static int mmb_find(const char *name)
{
    const unsigned char *cat_ptr = mmb_cat;
    const unsigned char *cat_end = mmb_cat + mmb_cat_size;

    do {
        const unsigned char *cat_nxt = cat_ptr + 16;
        int i = mmb_cat_name_cmp(name, cat_ptr, cat_nxt);
        if (i >= 0) {
            log_debug("mmb: found MMB SSD '%s' at %d", name, i);
            return i;
        }
        cat_ptr = cat_nxt;
    } while (cat_ptr < cat_end);
    return -1;
}

static int mmb_parse_find(uint16_t addr)
{
    char name[17];
    int ch = readmem(addr++);
    int i = 0;
    bool quote = false;

    if (ch == '"') {
        quote = true;
        ch = readmem(addr++);
    }
    while (ch != '\r' && i < sizeof(name) && ((quote && ch != '"') || (!quote && ch != ' '))) {
        name[i++] = ch;
        ch = readmem(addr++);
    }
    name[i] = 0;
    if ((i = mmb_find(name)) < 0)
        vdfs_error(err_disc_not_fnd);
    return i;
}

bool mmb_din_common(uint16_t addr, unsigned default_drive)
{
    bool worked = false;
    int num1 = 0, num2 = 0;
    uint16_t addr2 = addr;
    int ch = readmem(addr2);
    while (ch >= '0' && ch <= '9') {
        num1 = num1 * 10 + ch - '0';
        ch = readmem(++addr2);
    }
    if (ch == ' ' || ch == '\r') {
        while (ch == ' ')
            ch = readmem(++addr2);
        if (ch == '\r')
            worked = mmb_pick(default_drive, num1);
        else {
            addr = addr2;
            while (ch >= '0' && ch <= '9') {
                num2 = num2 * 10 + ch - '0';
                ch = readmem(++addr2);
            }
            if (ch == ' ' || ch == '\r') {
                while (ch == ' ')
                    ch = readmem(++addr2);
                if (ch == '\r' && num1 >= 0 && num1 <= 3)
                    worked = mmb_pick(num1, num2);
                else
                    vdfs_error(err_bad_drive_id);
            }
            else if ((num2 = mmb_parse_find(addr)) >= 0) {
                if (num1 >= 0 && num1 <= 3)
                    worked = mmb_pick(num1, num2);
                else
                    vdfs_error(err_bad_drive_id);
            }
        }
    }
    else if ((num1 = mmb_parse_find(addr)) >= 0)
        worked = mmb_pick(default_drive, num1);
    return worked;
}

void mmb_cmd_din(uint16_t addr)
{
    mmb_din_common(addr, x);
}

void mmb_cmd_dboot(uint16_t addr)
{
    if (mmb_din_common(addr, 0)) {
        autoboot = 150;
        main_key_break();
    }
}

static const char mmb_about_str[] = "B-Em internal MMB\r\n";

void mmb_cmd_dabout(void)
{
    memcpy(vdfs_split_addr(), mmb_about_str, sizeof(mmb_about_str));
    vdfs_split_go(0);
}

static uint8_t *mmb_name_flag(uint8_t *dest, const unsigned char *cat_ptr)
{
    for (int i = 0; i < MMB_NAME_SIZE; ++i) {
        int ch = cat_ptr[i] & 0x7f;
        if (ch < ' ' || ch > 0x7e)
            ch = ' ';
        *dest++ = ch;
    }
    *dest++ = ' ';
    int flag = cat_ptr[15];
    if (flag == 0xf0)
        flag = 'U';
    else if (flag == 0)
        flag = 'P';
    else
        flag = ' ';
    *dest++ = flag;
    return dest;
}

void mmb_cmd_dcat_cont(void)
{
    if (readmem(0xff) & 0x80)
        vdfs_error("\x17" "Escape");
    else {
        uint8_t *dest = vdfs_split_addr();
        while (mmb_dcat_posn < mmb_dcat_end) {
            unsigned zone = mmb_dcat_posn / MMB_ZONE_DISCS;
            unsigned posn = mmb_dcat_posn % MMB_ZONE_DISCS;
            if (posn < mmb_zones[zone].num_discs) {
                const unsigned char *ptr = mmb_zones[zone].index[posn];
                if (vdfs_wildmat(mmb_dcat_pattern, mmb_dcat_pat_len, (const char *)ptr, MMB_NAME_SIZE)) {
                    ++mmb_dcat_count;
                    dest += snprintf((char *)dest, 80, "%5d ", mmb_dcat_posn++);
                    dest = mmb_name_flag(dest, ptr);
                    *dest = 0;
                    vdfs_split_go(0x17);
                    return;
                }
            }
            ++mmb_dcat_posn;
        }
        snprintf((char *)dest, 20, "\r\n%d disks found\r\n", mmb_dcat_count);
        vdfs_split_go(0);
    }
}

void mmb_cmd_dcat_start(uint16_t addr)
{
    /* Defaults for an unfiltered list */
    mmb_dcat_count = 0;
    mmb_dcat_posn = 0;
    mmb_dcat_end = mmb_num_zones * MMB_ZONE_DISCS;
    mmb_dcat_pattern[0] = '*';
    mmb_dcat_pat_len = 1;

    int ch = readmem(addr++);
    while (ch == ' ' || ch == '\t')
        ch = readmem(addr++);
    if (ch != '\r') {
        unsigned value1 = 0;
        uint16_t addr2 = addr;
        while (ch >= '0' && ch <= '9') {
            value1 = (value1 * 10) + (ch - '0');
            ch = readmem(addr2++);
        }
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            log_debug("mmb: mmb_cmd_dcat_start, found 1st number=%u", value1);
            unsigned max_value = mmb_dcat_end;
            if (value1 > max_value)
                value1 = max_value;
            mmb_dcat_end = value1;
            while (ch == ' ' || ch == '\t')
                ch = readmem(addr2++);
            addr = addr2;
            if (ch != '\r') {
                unsigned value2 = 0;
                while (ch >= '0' && ch <= '9') {
                    value2 = (value2 * 10) + (ch - '0');
                    ch = readmem(addr2++);
                }
                if (ch == ' ' || ch == '\t' || ch == '\r') {
                    log_debug("mmb: mmb_cmd_dcat_start, found 2nd number=%u", value2);
                    if (value2 > max_value)
                        value2 = max_value;
                    mmb_dcat_posn = value1;
                    mmb_dcat_end = value2;
                    while (ch == ' ' || ch == '\t')
                        ch = readmem(addr2++);
                    addr = addr2;
                }
            }
        }
        if (ch != '\r') {
            log_debug("mmb: mmb_cmd_dcat_start, pattern found");
            unsigned pat_ix = 0;
            do {
                mmb_dcat_pattern[pat_ix++] = ch;
                ch = readmem(addr++);
            } while (pat_ix < MMB_NAME_SIZE && ch != ' ' && ch != '\t' && ch != '\r');
            mmb_dcat_pat_len = pat_ix;
            log_debug("mmb: mmb_cmd_dcat_start, pattern=%.*s", pat_ix, mmb_dcat_pattern);
        }
    }
    mmb_dcat_posn += mmb_base_zone * MMB_ZONE_DISCS;
    mmb_cmd_dcat_cont();
}

static const char mmb_no_discs[] = "No discs loaded\r\n";

void mmb_cmd_ddrive(uint16_t addr)
{
    uint8_t *dest = vdfs_split_addr();
    bool loaded = false;
    for (int drive = 0; drive < 4; ++drive) {
        int disc = mmb_loaded_discs[drive];
        if (disc >= 0) {
            unsigned zone = disc / MMB_ZONE_DISCS + mmb_base_zone;
            unsigned posn = disc % MMB_ZONE_DISCS;
            if (posn < mmb_zones[zone].num_discs) {
                dest += sprintf((char *)dest, ":%u %4u ", drive, disc);
                dest = mmb_name_flag(dest, mmb_zones[zone].index[posn]);
                *dest++ = '\r';
                *dest++ = '\n';
                loaded = true;
            }
        }
        *dest = 0;
    }
    if (!loaded)
        memcpy(dest, mmb_no_discs, sizeof(mmb_no_discs));
    vdfs_split_go(0);
}

void mmb_cmd_dfree(void)
{
    unsigned total = 0;
    unsigned unform = 0;
    for (unsigned zone = mmb_base_zone; zone < mmb_num_zones; ++zone) {
        for (unsigned disc = 0; disc < mmb_zones[zone].num_discs; ++disc)
            if (mmb_zones[zone].index[disc][15] == 0xf0)
                ++unform;
        total += mmb_zones[zone].num_discs;
    }
    sprintf((char *)vdfs_split_addr(), "%u of %u disks free (unformatted)\r\n", unform, total);
    vdfs_split_go(0);
}

static const char err_disc_not_loaded[] = "\xd6" "Disk not loaded in that drive";
static const char err_bad_dop_oper[]    = "\x94" "Bad DOP operation";
static const char err_no_unformatted[]  = "\xd6" "No unformatted discs";
static const char err_wprotect[]        = "\xc1" "MMB file not open for update";
static const char err_read_err[]        = "\xc7" "Error reading MMB file";
static const char err_write_err[]       = "\xc7" "Error writing to MMB file";

static void mmb_dop_find_unformatted(unsigned drive)
{
    for (unsigned zone = mmb_base_zone; zone < mmb_num_zones; ++zone) {
        for (unsigned disc = 0; disc < mmb_zones[zone].num_discs; ++disc) {
            if (mmb_zones[zone].index[disc][15] == 0xf0) {
                mmb_mount(drive, zone, disc);
                mmb_loaded_discs[drive] = disc + (zone - mmb_base_zone) * MMB_ZONE_DISCS;
                return;
            }
        }
    }
    vdfs_error(err_no_unformatted);
}

static void mmb_dop_flags(unsigned drive, int op)
{
    if (writeprot[0])
        vdfs_error(err_wprotect);
    else {
        int disc = mmb_loaded_discs[drive];
        if (disc < 0)
            vdfs_error(err_disc_not_loaded);
        else {
            unsigned zone = disc / MMB_ZONE_DISCS + mmb_base_zone;
            unsigned posn = disc % MMB_ZONE_DISCS;
            if (posn < mmb_zones[zone].num_discs) {
                unsigned char *ptr = mmb_zones[zone].index[posn];
                if (op == 'P' && ptr[15] != 0xf0)
                    ptr[15] = 0;
                else if (op == 'U' && ptr[15] != 0xf0)
                    ptr[15] = 0x0f;
                else if (op == 'K')
                    ptr[15] = 0xf0;
                else if (op == 'R')
                    ptr[15] &= 0x0f;
                else {
                    vdfs_error(err_bad_dop_oper);
                    return;
                }
                /* write changed entry back to disk */
                unsigned offset = zone * MMB_ZONE_FULL_SIZE + (posn + 1) * MMB_ENTRY_SIZE;
                if (fseek(mmb_fp, offset, SEEK_SET) == -1) {
                    log_error("unable to seek on MMB file: %s", strerror(errno));
                    vdfs_error(err_write_err);
                }
                else if (fwrite(ptr, MMB_ENTRY_SIZE, 1, mmb_fp) != 1 || fflush(mmb_fp)) {
                    log_error("unable to write back to MMB file: %s", strerror(errno));
                    vdfs_error(err_write_err);
                }
            }
        }
    }
}

void mmb_cmd_dop(uint16_t addr)
{
    unsigned drive = x;
    int op = readmem(addr++) & 0x5f;
    int ch = readmem(addr++);
    while (ch == ' ' || ch == '\t')
        ch = readmem(addr++);
    if (ch >= '0' && ch <= '9') {
        drive = 0;
        do {
            drive = (drive * 10) + (ch & 0x0f);
            ch = readmem(addr++);
        } while (ch >= '0' && ch <= '9');
    }
    if (drive >= 4)
        vdfs_error(err_bad_drive_id);
    else if (op == 'N')
        mmb_dop_find_unformatted(drive);
    else
        mmb_dop_flags(drive, op);
}

void mmb_cmd_drecat(void)
{
    log_debug("mmb: sdf_fp[0]=%p, sdf_fp[1]=%p, mmb_fp=%p", sdf_fp[0], sdf_fp[1], mmb_fp);
    if (writeprot[0])
        vdfs_error(err_wprotect);
    else {
        unsigned char *cat_zone = mmb_cat;
        unsigned char *cat_end = cat_zone + mmb_cat_size;
        off_t zone_start = 0;
        unsigned zone_num = 0;
        while (cat_zone < cat_end) {
            unsigned char *cat_ptr = cat_zone;
            off_t zone_ptr = zone_start + MMB_ZONE_CAT_SIZE + MMB_ENTRY_SIZE;
            off_t zone_end = zone_start + MMB_ZONE_FULL_SIZE;
            bool dirty = false;
            while (cat_ptr < cat_end && zone_ptr < zone_end) {
                unsigned char title[MMB_NAME_SIZE];
                if (fseek(mmb_fp, zone_ptr, SEEK_SET) == -1      ||
                    fread(title, 8, 1, mmb_fp) != 1              ||
                    fseek(mmb_fp, zone_ptr+0x100, SEEK_SET) ==-1 ||
                    fread(title+8, 4, 1, mmb_fp) != 1)
                {
                    log_error("mmb: read error on MMB file %s: %s", mmb_fn, strerror(errno));
                    vdfs_error(err_read_err);
                    return;
                }
                if (memcmp(cat_ptr, title, MMB_NAME_SIZE)) {
                    memcpy(cat_ptr, title, MMB_NAME_SIZE);
                    dirty = true;
                }
                cat_ptr += MMB_ENTRY_SIZE;
                zone_ptr += MMB_DISC_SIZE;
            }
            if (dirty) {
                off_t offset = zone_start + MMB_ENTRY_SIZE;
                log_debug("mmb: zone #%u dirty, writing to %08lx", zone_num, offset);
                if (fseek(mmb_fp, offset, SEEK_SET) == -1 ||
                    fwrite(cat_zone, MMB_ZONE_CAT_SIZE, 1, mmb_fp) != 1)
                {
                    log_error("mmb: write error on MMB file %s: %s", mmb_fn, strerror(errno));
                    vdfs_error(err_write_err);
                    return;
                }
            }
            log_debug("mmb: zone end, zone: %u, zone_start=%08lx, dirty=%d", zone_num, zone_start, dirty);
            ++zone_num;
            zone_start = zone_ptr;
            cat_zone = cat_ptr;
        }
    }
}
