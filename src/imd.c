/*
 * B-EM IMD Module.
 *
 * This module is part of B-Em, a BBC Micro emulator by Sarah Walker
 * and implements the ImageDisk floppy disc image format.  ImageDisk
 * is an open, high-level, self-describing format that can deal with
 * non-standard ID fields, deleted data and CRC errors.
 */

#include "b-em.h"
#include "disc.h"
#include "imd.h"

#define IMD_MAX_SECTS 36

struct imd_list;

struct imd_sect;

struct imd_sect {
    struct imd_sect *next;
    struct imd_sect *prev;
    uint16_t sectsize;
    uint8_t  mode;
    uint8_t  cylinder;
    uint8_t  head;
    uint8_t  sectid;
    unsigned char data[1];
};

struct imd_maps {
    uint8_t snum_map[IMD_MAX_SECTS];
    uint8_t cyl_map[IMD_MAX_SECTS];
    uint8_t head_map[IMD_MAX_SECTS];
    uint8_t ssize_map[IMD_MAX_SECTS];
};

struct imd_track;

struct imd_track {
    struct imd_track *next;
    struct imd_track *prev;
    struct imd_sect *sect_head;
    struct imd_sect *sect_tail;
    uint8_t mode;
    uint8_t cylinder;
    uint8_t head;
    uint8_t nsect;
    uint8_t sectsize;
    struct imd_sect *sects[IMD_MAX_SECTS];
};

struct imd_file {
    FILE *fp;
    struct imd_track *track_head;
    struct imd_track *track_tail;
    struct imd_track *track_cur;
    long track0;
    int trackno;
    int headno;
    bool dirty;
} imd_discs[NUM_DRIVES];

enum imd_state {
    ST_IDLE,
    ST_NOTFOUND,
    ST_READSECTOR,
    ST_READCOMPR,
    ST_WRITEPROT,
    ST_WRITESECTOR0,
    ST_WRITESECTOR1,
    ST_WRITESECTOR2,
    ST_READ_ADDR0,
    ST_READ_ADDR1,
    ST_READ_ADDR2,
    ST_READ_ADDR3,
    ST_READ_ADDR4,
    ST_READ_ADDR5,
    ST_READ_ADDR6
};

static enum imd_state state;
static unsigned count;
static int      imd_time;
static unsigned char *data, cdata;
struct imd_track *cur_trk;
struct imd_sect  *cur_sect;

static void imd_save(struct imd_file *imd)
{
    fseek(imd->fp, imd->track0, SEEK_SET);
    for (struct imd_track *trk = imd->track_head; trk; trk = trk->next) {
        uint8_t buf[5+36];
        buf[0] = trk->mode;
        buf[1] = trk->cylinder;
        buf[2] = trk->head;
        buf[3] = trk->nsect;
        buf[4] = trk->sectsize;
        uint8_t *ptr = buf+5;
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
            *ptr++ = sect->sectid;
        fwrite(buf, ptr-buf, 1, imd->fp);
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next) {
            putc(sect->mode, imd->fp);
            if (sect->mode & 1)
                fwrite(sect->data, sect->sectsize, 1, imd->fp);
            else if (sect->mode)
                putc(sect->data[0], imd->fp);
        }
    }
    ftruncate(fileno(imd->fp), ftell(imd->fp));
}

static void imd_free(struct imd_file *imd)
{
    struct imd_track *trk = imd->track_head;
    while (trk) {
        struct imd_track *trk_next = trk->next;
        struct imd_sect *sect = trk->sect_head;
        while (sect) {
            struct imd_sect *sect_next = sect->next;
            free(sect);
            sect = sect_next;
        }
        free(trk);
        trk = trk_next;
    }
}

static void imd_close(int drive)
{
    if (drive >= 0 && drive < NUM_DRIVES) {
        struct imd_file *imd = &imd_discs[drive];
        if (imd->dirty)
            imd_save(imd);
        imd_free(imd);
        fclose(imd->fp);
        imd->fp = NULL;
    }
}

static void imd_seek(int drive, int track)
{
    if (state == ST_IDLE && drive >= 0 && drive < NUM_DRIVES) {
        log_debug("imd: drive %d: seek to track %d", drive, track);
        struct imd_file *imd = &imd_discs[drive];
        if (track != imd->trackno) {
            imd->track_cur = NULL;
            imd->trackno = track;
            imd->headno = -1;
        }
    }
}

static int imd_verify(int drive, int track, int density)
{
    if (state == ST_IDLE && drive >= 0 && drive < NUM_DRIVES) {
        log_debug("sdf: drive %d: verify track=%d, density=%d", drive, track, density);
        struct imd_file *imd = &imd_discs[drive];
        struct imd_track *trk = imd->track_cur;
        if (!trk) {
            log_debug("imd: drive %d: searching for track", drive);
            for (trk = imd->track_head; trk; trk = trk->next) {
                if (trk->cylinder == imd->trackno) {
                    log_debug("imd: drive %d: found track", drive);
                    imd->track_cur = trk;
                    imd->headno = trk->head;
                    break;
                }
            }
        }
        return trk && trk->sect_head->cylinder == track && ((density && trk->mode >= 3) || (!density && trk->mode <= 2));
    }
    return 0;
}

static struct imd_track *imd_find_track(int drive, int track, int side, int density)
{
    if (drive >= 0 && drive < NUM_DRIVES) {
        struct imd_file *imd = &imd_discs[drive];
        struct imd_track *trk = imd->track_cur;
        if (!trk) {
            log_debug("imd: drive %d: searching for track", drive);
            for (trk = imd->track_head; trk; trk = trk->next) {
                log_debug("imd: drive %d: cyl %u<>%u, head %u<>%u", drive, trk->cylinder, imd->trackno, trk->head, side);
                if (trk->cylinder == imd->trackno && trk->head == side && ((density && trk->mode >= 3) || (!density && trk->mode <= 2))) {
                    log_debug("imd: drive %d: found track", drive);
                    imd->track_cur = trk;
                    imd->headno = side;
                    break;
                }
            }
        }
        return trk;
    }
    return NULL;
}

static struct imd_sect *imd_find_sector(int drive, int track, int side, int sector, struct imd_track *trk)
{
    log_debug("imd: drive %d: searching for sector", drive);
    for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next) {
        log_debug("imd: drive %d: cyl %u<>%u, head %u<>%u, sectid %u<>%u", drive, sect->cylinder, track, sect->head, side, sect->sectid, sector);
        if (sect->cylinder == track && sect->head == side && sect->sectid == sector)
            return sect;
    }
    return NULL;
}

static void imd_readsector(int drive, int sector, int track, int side, int density)
{
    log_debug("imd: drive %d: readsector sector=%d, track=%d, side=%d, density=%d", drive, sector, track, side, density);
    if (state == ST_IDLE) {
        struct imd_track *trk = imd_find_track(drive, track, side, density);
        if (trk) {
            struct imd_sect *sect = imd_find_sector(drive, track, side, sector, trk);
            if (sect) {
                count = sect->sectsize;
                if (sect->mode & 1) {
                    log_debug("imd: drive %d: found full sector", drive);
                    data = sect->data;
                    state = ST_READSECTOR;
                }
                else {
                    log_debug("imd: drive %d: found compressed sector", drive);
                    cdata = sect->data[0];
                    state = ST_READCOMPR;
                }
                return;
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void imd_writesector(int drive, int sector, int track, int side, int density)
{
    log_debug("imd: drive %d: writesector sector=%d, track=%d, side=%d, density=%d", drive, sector, track, side, density);
    if (state == ST_IDLE) {
        struct imd_track *trk = imd_find_track(drive, track, side, density);
        if (trk) {
            struct imd_sect *sect = imd_find_sector(drive, track, side, sector, trk);
            if (sect) {
                if (writeprot[drive]) {
                    count = 1;
                    state = ST_WRITEPROT;
                }
                else {
                    cur_trk = trk;
                    cur_sect = sect;
                    count = sect->sectsize;
                    imd_discs[drive].dirty = true;
                    imd_time = -20;
                    state = ST_WRITESECTOR0;
                }
                return;
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void imd_readaddress(int drive, int track, int side, int density)
{
    log_debug("imd: drive %d: readaddress track=%d, side=%d, density=%d", drive, track, side, density);
    if (state == ST_IDLE) {
        struct imd_track *trk = cur_trk;
        if (trk && trk->cylinder == track && trk->head == side && ((density && trk->mode >= 3) || (!density && trk->mode <= 2))) {
            if (!(cur_sect = cur_sect->next))
                cur_sect = trk->sect_head;
            state = ST_READ_ADDR0;
        }
        else if ((trk = imd_find_track(drive, track, side, density))) {
            cur_trk = trk;
            cur_sect = trk->sect_head;
            state = ST_READ_ADDR0;
        }
        else {
            count = 500;
            state = ST_NOTFOUND;
        }
    }
}

static void imd_abort(int drive)
{
    state = ST_IDLE;
}

static void imd_poll_writesect0(void)
{
    int c = fdc_getdata(0);
    if (c == -1)
        log_warn("imd: data underrun on write");
    else {
        log_debug("imd: imd_poll_writesect0 c=%02X", c);
        cdata = c;
        count--;
        state = ST_WRITESECTOR1;
    }
}

static void imd_poll_writesect1(void)
{
    int c = fdc_getdata(--count == 0);
    if (c == -1) {
        log_warn("imd: data underrun on write");
        count++;
    }
    else {
        log_debug("imd: imd_poll_writesect1 c=%02X", c);
        if (c != cdata) {
            if (!(cur_sect->mode & 1)) {
                log_debug("imd: imd_poll_writesect1 converting compressed sector");
                struct imd_sect *new_sect = malloc(sizeof(struct imd_sect)+cur_sect->sectsize);
                if (!new_sect) {
                    log_error("imd: out of memory reallocating sector");
                    fdc_datacrcerror();
                    return;
                }
                if (cur_trk->sect_head == cur_sect)
                    cur_trk->sect_head = new_sect;
                if (cur_trk->sect_tail == cur_sect)
                    cur_trk->sect_tail = new_sect;
                struct imd_sect *next = cur_sect->next;
                new_sect->next = next;
                if (next)
                    next->prev = new_sect;
                struct imd_sect *prev = cur_sect->prev;
                new_sect->prev = prev;
                if (prev)
                    prev->next = new_sect;
                new_sect->sectsize = cur_sect->sectsize;
                new_sect->mode     = cur_sect->mode | 1;
                new_sect->cylinder = cur_sect->cylinder;
                new_sect->head     = cur_sect->head;
                new_sect->sectid   = cur_sect->sectid;
                free(cur_sect);
                cur_sect = new_sect;
            }
            unsigned used = cur_sect->sectsize - count - 1;
            log_debug("imd: imd_poll_writesect1 used=%u", used);
            memset(cur_sect->data, cdata, used);
            data = cur_sect->data + used;
            *data++ = c;
            state = ST_WRITESECTOR2;
            if (count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
        }
        else if (count == 0) {
            fdc_finishread();
            state = ST_IDLE;
            cur_sect-> mode &= ~1;
        }
    }
}

static void imd_poll_writesect2(void)
{
    int c = fdc_getdata(--count == 0);
    if (c == -1) {
        log_warn("imd: data underrun on write");
        count++;
    }
    else {
        log_debug("imd: imd_poll_writesect2 c=%02X", c);
        *data++ = c;
        if (count == 0) {
            fdc_finishread();
            state = ST_IDLE;
        }
    }
}

static void imd_poll(void)
{
    if (++imd_time <= 16)
        return;
    imd_time = 0;

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
            fdc_data(*data++);
            if (--count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
            break;

        case ST_READCOMPR:
            fdc_data(cdata);
            if (--count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
            break;

        case ST_WRITEPROT:
            log_debug("imd: poll, write protected during write sector");
            fdc_writeprotect();
            state = ST_IDLE;
            break;

        case ST_WRITESECTOR0:
            imd_poll_writesect0();
            break;

        case ST_WRITESECTOR1:
            imd_poll_writesect1();
            break;

        case ST_WRITESECTOR2:
            imd_poll_writesect2();
            break;

        case ST_READ_ADDR0:
            fdc_data(cur_trk->cylinder);
            state = ST_READ_ADDR1;
            break;

        case ST_READ_ADDR1:
            fdc_data(cur_trk->head);
            state = ST_READ_ADDR2;
            break;

        case ST_READ_ADDR2:
            fdc_data(cur_sect->sectid);
            state = ST_READ_ADDR3;
            break;

        case ST_READ_ADDR3:
            fdc_data(cur_trk->sectsize);
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
            break;
    }
}

static long imd_check_hdr(FILE *fp)
{
    char hdr[30];
    if (fread(hdr, sizeof(hdr), 1, fp) == 1) {
        if (!memcmp(hdr, "IMD ", 4)) {
            hdr[29] = 0;
            if (strspn(hdr+4, "0123456789:/. ") >= 25) {
                int ch;
                while ((ch = getc(fp)) != EOF) {
                    if (ch == 0x1a)
                        return ftell(fp);
                }
            }
        }
    }
    return 0;
}

static void imd_sect_err(const char *fn, FILE *fp, int trackno, int sectno)
{
    const char *msg = ferror(fp) ? strerror(errno) : "unexpected EOF";
    log_error("Disc image '%s' track %d, sector %d: %s", fn, trackno, sectno, msg);
}

static bool imd_load_sectors(const char *fn, FILE *fp, struct imd_track *trk, int trackno, struct imd_maps *mp)
{
    struct imd_sect *head = NULL;
    struct imd_sect *tail = NULL;
    for (int sectno = 0; sectno < trk->nsect; sectno++) {
        size_t ssize = 128 << ((trk->sectsize == 0xff) ? mp->ssize_map[sectno] : trk->sectsize);
        int mode = getc(fp);
        if (mode == EOF) {
            imd_sect_err(fn, fp, trackno, sectno);
            goto failed;
        }
        struct imd_sect *sect = malloc((mode & 1) ? sizeof(struct imd_sect) + ssize : sizeof(struct imd_sect));
        if (!sect) {
            log_error("Disc image '%s' track %d, sector %d: %s", fn, trackno, sectno, "out of memory");
            goto failed;
        }
        sect->mode = mode;
        sect->cylinder = (trk->head & 0x80) ? mp->cyl_map[sectno]  : trk->cylinder;
        sect->head     = (trk->head & 0x40) ? mp->head_map[sectno] : trk->head & ~0xc0;
        sect->sectid   = mp->snum_map[sectno];
        sect->sectsize = ssize;
        if (mode == 0)
            sect->data[0] = 0;
        else if (mode & 1) {
            if (fread(sect->data, ssize, 1, fp) != 1) {
                imd_sect_err(fn, fp, trackno, sectno);
                goto failed;
            }
        }
        else {
            int byte = getc(fp);
            if (byte == EOF) {
                imd_sect_err(fn, fp, trackno, sectno);
                goto failed;
            }
            sect->data[0] = byte;
        }
        sect->prev = tail;
        if (tail)
            tail->next = sect;
        else
            head = sect;
        sect->next = NULL;
        tail = sect;
    }
    trk->sect_head = head;
    trk->sect_tail = tail;
    return true;
failed:
    while (head) {
        struct imd_sect *next = head->next;
        free(head);
        head = next;
    }
    return false;
}

static bool imd_load_map(const char *fn, FILE *fp, size_t nsect, uint8_t map[IMD_MAX_SECTS], int trackno, const char *which)
{
    if (fread(map, nsect, 1, fp) == 1)
        return true;
    const char *msg = ferror(fp) ? strerror(errno) : "unexpected EOF";
    log_error("Disc image '%s' track %d: error reading %s map: %s", fn, trackno, which, msg);
    return false;
}

static bool imd_load_tracks(const char *fn, FILE *fp, struct imd_file *imd)
{
    struct imd_track *head = NULL;
    struct imd_track *tail = NULL;
    unsigned trackno = 0;
    uint8_t hdr[5];
    while (fread(hdr, sizeof(hdr), 1, fp) == 1) {
        log_debug("imd: header for track %d: %02X %02X %02X %02X %02X", trackno, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);
        struct imd_track *trk = malloc(sizeof(struct imd_track));
        if (!trk) {
            log_error("Disc image '%s' track %d: out of memory", fn, trackno);
            goto failed;
        }
        trk->mode     = hdr[0];
        trk->cylinder = hdr[1];
        trk->head     = hdr[2];
        trk->nsect    = hdr[3];
        trk->sectsize = hdr[4];
        if (trk->nsect > IMD_MAX_SECTS) {
            log_error("Disc image '%s' track %d has too many sectors", fn, trackno);
            goto failed;
        }
        struct imd_maps maps;
        if (!imd_load_map(fn, fp, trk->nsect, maps.snum_map, trackno, "sector ID"))
            goto failed;
        if ((trk->head & 0x80) && !imd_load_map(fn, fp, trk->nsect, maps.cyl_map, trackno, "cyclinder"))
            goto failed;
        if ((trk->head & 0x40) && !imd_load_map(fn, fp, trk->nsect, maps.head_map, trackno, "head"))
            goto failed;
        if ((trk->sectsize == 0xff) && !imd_load_map(fn, fp, trk->nsect, maps.ssize_map, trackno, "sector size"))
            goto failed;
        if (!imd_load_sectors(fn, fp, trk, trackno, &maps))
            goto failed;
        trk->prev = tail;
        if (tail)
            tail->next = trk;
        else
            head = trk;
        trk->next = NULL;
        tail = trk;
        trackno++;
    }
    imd->track_head = head;
    imd->track_tail = tail;
    return true;
failed:
    while (head) {
        struct imd_track *next = head->next;
        free(head);
        head = next;
    }
    return false;
}

static void imd_dump(struct imd_file *imd)
{
#ifdef _DEBUG
    log_debug("imd: disc track0=%ld", imd->track0);
    for (struct imd_track *trk = imd->track_head; trk; trk = trk->next) {
        log_debug("imd: track mode=%02X, cylinder=%u, head=%02X, nsect=%u, sectsize=%02X", trk->mode, trk->cylinder, trk->head, trk->nsect, trk->sectsize);
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
            log_debug("imd: sector mode=%02X, cylinder=%u, head=%u, sectid=%u, sectsize=%u", sect->mode, sect->cylinder, sect->head, sect->sectid, sect->sectsize);
    }
#endif
}

void imd_load(int drive, const char *fn)
{
    log_debug("imd: loading IMD image file '%s' into drive %d", fn, drive);
    if (drive >= 0 && drive < NUM_DRIVES) {
        int wprot = 0;
        FILE *fp = fopen(fn, "rb+");
        if (!fp) {
            if (!(fp = fopen(fn, "rb"))) {
                log_error("Unable to open file '%s' for reading - %s", fn, strerror(errno));
                return;
            }
            wprot = 1;
        }
        long track0 = imd_check_hdr(fp);
        if (track0) {
            struct imd_file *imd = &imd_discs[drive];
            if (imd_load_tracks(fn, fp, imd)) {
                imd->track_cur = NULL;
                imd->fp = fp;
                imd->track0 = track0;
                imd->trackno = 0;
                imd_dump(imd);
                writeprot[drive] = wprot;
                drives[drive].close       = imd_close;
                drives[drive].seek        = imd_seek;
                drives[drive].verify      = imd_verify;
                drives[drive].readsector  = imd_readsector;
                drives[drive].writesector = imd_writesector;
                drives[drive].readaddress = imd_readaddress;
                drives[drive].poll        = imd_poll;
                drives[drive].abort       = imd_abort;
                return;
            }
        }
        else
            log_error("File '%s' does not have a valid IMD header", fn);
        fclose(fp);
    }
}
