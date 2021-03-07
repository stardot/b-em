/*
 * B-EM IMD Module.
 *
 * This module is part of B-Em, a BBC Micro emulator by Sarah Walker
 * and implements the ImageDisk floppy disc image format.  ImageDisk
 * is an open, high-level, self-describing format that can deal with
 * non-standard ID fields, deleted data and CRC errors.
 *
 * Because this file format was designed more for archive use than
 * for use in an emulator and uses compressed sectors the whole image
 * is read when a disc is loaded and written back when the disc is
 * closed/ejected.
 *
 * While in memory the data is stored in three levels.  Each drive,
 * i.e. image file has an imd_file structure which contains the head
 * and tail pointers to a doubly linked list of tracks.  Each track
 * is stored in an imd_track structure which contains some track level
 * attributes and the head and tail pointers to a doubly linked list
 * of sectors which are each stored in a imd_sect structure.
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
    uint8_t  mode;
    uint8_t  cylinder;
    uint8_t  head;
    uint8_t  sectid;
    uint8_t  sectsize;
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
};

struct imd_file {
    FILE *fp;
    struct imd_track *track_head;
    struct imd_track *track_tail;
    struct imd_track *track_cur;
    long track0;
    int trackno;
    int headno;
    uint8_t maxcyl;
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
    ST_WRTRACK_DATA0,
    ST_WRTRACK_DATA1,
    ST_WRTRACK_DATA2,
    ST_WRTRACK_DATACRC,
    ST_RDTRACK_GAP0F,
    ST_RDTRACK_GAP00,
    ST_RDTRACK_IDFE,
    ST_RDTRACK_CYLID,
    ST_RDTRACK_HEADID,
    ST_RDTRACK_SECTID,
    ST_RDTRACK_SECTSZ,
    ST_RDTRACK_HDR_CRC,
    ST_RDTRACK_GAP1F,
    ST_RDTRACK_GAP10,
    ST_RDTRACK_DATA,
    ST_RDTRACK_CDATA,
    ST_RDTRACK_DATA_CRC
};

static enum imd_state state;
static unsigned count;
static int      imd_time;
static unsigned char *data, cdata;
struct imd_track *cur_trk;
struct imd_sect  *cur_sect;
static uint8_t wt_cylid;
static uint8_t wt_headid;
static uint8_t wt_sectid;
static uint8_t wt_sectsz;

#ifdef WIN32
#ifdef __GNUC__
extern int ftruncate(int fd, off_t length);
#else
// https://stackoverflow.com/a/19932364/433626
int ftruncate(int fd, off_t length) {
    HANDLE handle = (HANDLE) _get_osfhandle(_fileno(fd));
    SetFilePointer(handle, length, 0, FILE_BEGIN);
    SetEndOfFile(handle);
}
#endif
#endif

/*
 * This function writes the IMD file in memory back to the disc file.
 * it does not re-write the comment at the start of the file but
 * rewrites everything else and truncates any remaining junk from
 * the end of the file.
 */

static void imd_save(struct imd_file *imd)
{
    fseek(imd->fp, imd->track0, SEEK_SET);
    for (struct imd_track *trk = imd->track_head; trk; trk = trk->next) {
        uint8_t buf[5+IMD_MAX_SECTS];
        buf[0] = trk->mode;
        buf[1] = trk->cylinder;
        buf[2] = trk->head;
        buf[3] = trk->nsect;
        buf[4] = trk->sectsize;
        uint8_t *ptr = buf+5;
        bool cylmap = false;
        bool headmap = false;
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next) {
            *ptr++ = sect->sectid;
            if (sect->cylinder != trk->cylinder)
                cylmap = true;
            if (sect->head != trk->head)
                headmap = true;
        }
        fwrite(buf, ptr-buf, 1, imd->fp);
        if (cylmap) {
            ptr = buf;
            for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
                *ptr++ = sect->cylinder;
            fwrite(buf, ptr-buf, 1, imd->fp);
        }
        if (headmap) {
            ptr = buf;
            for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
                *ptr++ = sect->head;
            fwrite(buf, ptr-buf, 1, imd->fp);
        }
        if (trk->sectsize == 0xff) {
            ptr = buf;
            for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
                *ptr++ = sect->sectsize;
            fwrite(buf, ptr-buf, 1, imd->fp);
        }
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next) {
            putc(sect->mode, imd->fp);
            if (sect->mode & 1)
                fwrite(sect->data, 128 << sect->sectsize, 1, imd->fp);
            else if (sect->mode)
                putc(sect->data[0], imd->fp);
        }
    }
    fflush(imd->fp);
    ftruncate(fileno(imd->fp), ftell(imd->fp));
}

/*
 * This function traverses the linked lists of sectors freeing the
 * elements.
 */

static void imd_free_sectors(struct imd_track *trk)
{
    struct imd_sect *sect = trk->sect_head;
    while (sect) {
        struct imd_sect *sect_next = sect->next;
        free(sect);
        sect = sect_next;
    }
}

/*
 * This function traverses the linked lists of tracks freeing the
 * elements.
 */

static void imd_free(struct imd_file *imd)
{
    struct imd_track *trk = imd->track_head;
    while (trk) {
        struct imd_track *trk_next = trk->next;
        imd_free_sectors(trk);
        free(trk);
        trk = trk_next;
    }
}

/*
 * This function is called when a disc image is being closed, either
 * when the emulator is being shut down or the disc is being ejected
 * from the virtual disc drive.
 */

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

/*
 * This function implements the seek command, i.e. it moves the
 * virtual head to the specified cylinder.  It does not check that
 * the ID fields match this track number.
 */

static void imd_seek(int drive, int track)
{
    if (state == ST_IDLE && drive >= 0 && drive < NUM_DRIVES) {
        log_debug("imd: drive %d: seek to track %d", drive, track);
        struct imd_file *imd = &imd_discs[drive];
        if (track > imd->maxcyl)
            track = imd->maxcyl;
        if (track != imd->trackno) {
            imd->track_cur = NULL;
            imd->trackno = track;
            imd->headno = -1;
        }
    }
}

/*
 * Function to test if the mode of the specified track (FM/MFM) matches
 * the density requested from the controller.
 */

static int imd_density_ok(struct imd_track *trk, int density)
{
    return (density && trk->mode >= 3) || (!density && trk->mode <= 2);
}

/*
 * This function implements the verify command, i.e. checks that the
 * ID fields do match the cylinder the head is positioned on.  This
 * check only examines the first sector it can find on either surface.
 */

static int imd_verify(int drive, int track, int density)
{
    if (state == ST_IDLE && drive >= 0 && drive < NUM_DRIVES) {
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
        int res = trk && trk->sect_head->cylinder == track && imd_density_ok(trk, density);
        log_debug("imd: drive %d: verify result=%d", drive, res);
        return res;
    }
    return 0;
}

/*
 * This is an internal function to find a track prior to reading from
 * it or writing to it. This has to search the list for IDs rather
 * than counting as tracks in the image file can be in any order.
 *
 * Note that this searches for the physical cylinder ID which is not
 * the same as the cylinder encoded within sector headers.
 */

static struct imd_track *imd_find_track(int drive, int track, int side, int density)
{
    if (drive >= 0 && drive < NUM_DRIVES) {
        struct imd_file *imd = &imd_discs[drive];
        struct imd_track *trk = imd->track_cur;
        if (!trk || !imd_density_ok(trk, density)) {
            log_debug("imd: drive %d: searching for track", drive);
            if (track > imd->maxcyl)
                track = imd->maxcyl;
            for (trk = imd->track_head; trk; trk = trk->next) {
                log_debug("imd: drive %d: cyl %u<>%u, head %u<>%u", drive, trk->cylinder, imd->trackno, trk->head, side);
                if (trk->cylinder == track && trk->head == side && imd_density_ok(trk, density)) {
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

/*
 * This is an internal function to find a sector prior to reading from
 * it or writing to it. This has to search the list for IDs rather
 * than counting as sectors can be skewed or interleaved.
 */

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

/*
 * This function implements the start of a readsector command, i.e. it
 * sets things up so that data is transferred to the FDC via the
 * imd_poll function and associated state machine.
 */

static void imd_readsector(int drive, int sector, int track, int side, int density)
{
    log_debug("imd: drive %d: readsector sector=%d, track=%d, side=%d, density=%d", drive, sector, track, side, density);
    if (state == ST_IDLE) {
        struct imd_track *trk = imd_find_track(drive, track, side, density);
        if (trk) {
            struct imd_sect *sect = imd_find_sector(drive, track, side, sector, trk);
            if (sect) {
                count = 128 << sect->sectsize;
                cur_sect = sect;
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

/*
 * This function implements the start of a writesector command, i.e. it
 * sets things up so that data is transferred from the FDC via the
 * imd_poll function and associated state machine.
 */

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
                    count = 128 << sect->sectsize;
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

/*
 * This function implements the start of a readaddress command, i.e.
 * it sets things up so that the ID header fields are fed to the FDC
 * via the imd_poll function and associated state machine.
 */

static void imd_readaddress(int drive, int track, int side, int density)
{
    log_debug("imd: drive %d: readaddress track=%d, side=%d, density=%d", drive, track, side, density);
    if (state == ST_IDLE) {
        struct imd_track *trk = cur_trk;
        if (trk && trk->cylinder == track && trk->head == side && imd_density_ok(trk, density)) {
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

/*
 * This function does the common set up for the i8271 format command
 * and the WD1770 write track command.
 */

static bool imd_begin_format(int drive, int track, int side, int density)
{
    if (drive >= 0 && drive < NUM_DRIVES) {
        if (writeprot[drive]) {
            count = 1;
            state = ST_WRITEPROT;
            return false;
        }
        struct imd_file *imd = &imd_discs[drive];
        struct imd_track *trk = imd->track_head;
        while (trk) {
            if (trk->cylinder == track && trk->head == side) {
                imd_free_sectors(trk);
                break;
            }
            trk = trk->next;
        }
        if (!trk) {
            if (track >= 0 && track < 80) {
                trk = malloc(sizeof(struct imd_track));
                if (!trk) {
                    log_error("imd: out of memory allocating new track");
                    count = 1;
                    state = ST_WRITEPROT;
                    return false;
                }
                trk->next = NULL;
                trk->prev = imd->track_tail;
                if (imd->track_tail)
                    imd->track_tail->next = trk;
                else
                    imd->track_head = trk;
                imd->track_tail = trk;
            }
            else {
                count = 500;
                state = ST_NOTFOUND;
                return false;
            }
        }
        trk->sect_head = NULL;
        trk->sect_tail = NULL;
        trk->mode      = density ? 0x05 : 0x02;
        trk->cylinder  = track;
        trk->head      = side;
        cur_trk = trk;
        cur_sect = NULL;
        imd_time = -20;
        count = 120;
        return true;
    }
    count = 500;
    state = ST_NOTFOUND;
    return false;
}

/*
 * This begins a format command which used by the i8271 rather than
 * the WD1770.  The difference is that the i8271 sends only ID fields.
 */

static void imd_format(int drive, int track, int side, unsigned par2)
{
    if (imd_begin_format(drive, track, side, 0)) {
        unsigned nsect = par2 & 0x1f;
        cur_trk->nsect = nsect;
        cur_trk->sectsize  = par2 >> 5;
        count = nsect;
        state = ST_FORMAT_CYLID;
    }
}

/*
 * This begins a write track command which us used by the WD1770 rather
 * than the i2871.  Unlike the i8271, the WD1770 sends the whole track.
 */

static void imd_writetrack(int drive, int track, int side, int density)
{
    if (imd_begin_format(drive, track, side, density)) {
        cur_trk->nsect = 0;
        cur_trk->sectsize = 0xfe;
        state = ST_WRTRACK_INITIAL;
    }
}

/* This is the read track command as used by the WD1770 rather than
 * the i2871.  This sends the whole track including gaps and IDs.
 */

static void imd_readtrack(int drive, int track, int side, int density)
{
    struct imd_track *trk = imd_find_track(drive, track, side, density);
    if (trk) {
        cur_sect = trk->sect_head;
        count = 16;
        state = ST_RDTRACK_GAP0F;
    }
    else {
        count = 500;
        state = ST_NOTFOUND;
    }
}

/*
 * This function aborts an operation in progress.
 */

const char abort_write_msg[] = "imd: abort while writing in state %s, corruption likely";

static void imd_abort(int drive)
{
    switch(state) {
        case ST_WRITESECTOR1:
            log_warn(abort_write_msg, "ST_WRITESECTOR1");
            break;
        case ST_WRITESECTOR2:
            log_warn(abort_write_msg, "ST_WRITESECTOR2");
            break;
        default:
            log_debug("imd: abort in state %u", state);
    }
    state = ST_IDLE;
}

/*
 * This function is part of the state machine to implement the read
 * sector command and calls the correct FDC callback to signal the
 * end of the operation.
 */

static void imd_poll_finish_read(void)
{
    switch(cur_sect->mode) {
        case 3:
        case 4:
            fdc_finishread(true);
        case 5:
        case 6:
            fdc_datacrcerror(false);
        case 7:
        case 8:
            fdc_datacrcerror(true);
        default:
            fdc_finishread(false);
    }
}

/*
 * This function is part of the state machine to implement the write
 * sector command and handles the first byte written.
 *
 * At this stage we do not know if this should be a compressed sector
 * i.e. one with all the bytes having the same value, or a normal
 * sector where they may be different.
 */

static void imd_poll_writesect0(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_writesect0 byte=%02X", b);
    if (b != -1) {
        cdata = b;
        count--;
        state = ST_WRITESECTOR1;
    }
}

/*
 * The imd_poll_writesect1 function is part of the state machine to
 * implement the write sector command and handles the second and
 * subsequents bytes of as long as they match the first, i.e. the
 * sector is compressed.  If a different byte is recieved it switches
 * to an uncompressed sector format.
 */

static unsigned mode_compressed(unsigned mode)
{
    if (mode == 0)
        return 2;
    if (mode & 1)
        return mode+1;
    return mode;
}

static unsigned mode_full(unsigned mode)
{
    if (mode == 0)
        return 1;
    if (mode & 1)
        return mode;
    return mode-1;
}

static void imd_poll_writesect1(void)
{
    int b = fdc_getdata(--count == 0);
    log_debug("imd: imd_poll_writesect1 byte=%02X", b);
    if (b == -1)
        count++;
    else {
        if (b != cdata) {
            if (!(cur_sect->mode & 1)) {
                log_debug("imd: imd_poll_writesect1 converting compressed sector");
                struct imd_sect *new_sect = malloc(sizeof(struct imd_sect)+(128 << cur_sect->sectsize));
                if (!new_sect) {
                    log_error("imd: out of memory reallocating sector");
                    fdc_finishread(false);
                    return;
                }
                /* Link the new sector into the list in place of the old */
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
                new_sect->mode     = mode_full(cur_sect->mode);
                new_sect->cylinder = cur_sect->cylinder;
                new_sect->head     = cur_sect->head;
                new_sect->sectid   = cur_sect->sectid;
                free(cur_sect);
                cur_sect = new_sect;
            }
            unsigned used = (128 << cur_sect->sectsize) - count - 1;
            log_debug("imd: imd_poll_writesect1 used=%u", used);
            memset(cur_sect->data, cdata, used);
            data = cur_sect->data + used;
            *data++ = b;
            state = ST_WRITESECTOR2;
            if (count == 0) {
                fdc_finishread(false);
                state = ST_IDLE;
            }
        }
        else if (count == 0) {
            cur_sect->data[0] = cdata;
            fdc_finishread(false);
            state = ST_IDLE;
            cur_sect->mode = mode_compressed(cur_sect->mode);
        }
    }
}

/*
 * This function is part of the state machine to implement the write
 * sector command and handles the third and subsequent bytes once a
 * switch to an uncompressed sector has been made.
 */

static void imd_poll_writesect2(void)
{
    int c = fdc_getdata(--count == 0);
    if (c == -1) {
        log_warn("imd: data underrun on write");
        count++;
    }
    else {
        log_debug("imd: imd_poll_writesect2 byte=%02X", c);
        *data++ = c;
        if (count == 0) {
            fdc_finishread(false);
            state = ST_IDLE;
        }
    }
}

/*
 * This function allocates a new sector during disc formatting and
 * links it into the list for the current track, i.e. the one being
 * assembled.  As this list starts empty the new sector is always
 * linked to the tail.
 */

static struct imd_sect *imd_poll_new_sect(size_t size)
{
    struct imd_sect *new_sect = malloc(size);
    if (new_sect) {
        new_sect->next = NULL;
        if (cur_trk->sect_tail) {
            new_sect->prev = cur_trk->sect_tail;
            cur_trk->sect_tail->next = new_sect;
        }
        else {
            new_sect->prev = NULL;
            cur_trk->sect_head = new_sect;
        }
        cur_trk->sect_tail = new_sect;
        return new_sect;
    }
    else {
        log_error("imd: out of memory allocating sector during formatting");
        count = 1;
        state = ST_WRITEPROT;
        return NULL;
    }
}

/*
 * This function is part of the state machine to implement the format
 * command as used by the i8271.  It reveives the cylinder ID of the
 * next sector to be formatted, creates a new sector and records the
 * ID within it.
 */

static void imd_poll_format_cylid(void)
{
    struct imd_sect *new_sect = imd_poll_new_sect(sizeof(struct imd_sect));
    if (new_sect) {
        int cylid = fdc_getdata(0);
        log_debug("imd: imd_poll_format_cylid, cylid=%02X, count=%u", cylid, count);
        new_sect->cylinder = cylid;
        new_sect->mode = 0x02;
        new_sect->data[0]  = 0xe5;
        cur_sect = new_sect;
        state = ST_FORMAT_HEADID;
    }
}

/*
 * This function is part of the state machine to implement the format
 * command as used by the i8271.  It reveives and records the head
 * ID  of the sector being formatted.
 */

static void imd_poll_format_headid(void)
{
    int headid = fdc_getdata(0);
    log_debug("imd: imd_poll_format_headid, headid=%02X, count=%u", headid, count);
    cur_sect->head = headid;
    state = ST_FORMAT_SECTID;
}

/*
 * This function is part of the state machine to implement the format
 * command as used by the i8271.  It reveives and records the sector
 * ID  of the sector being formatted.
 */

static void imd_poll_format_sectid(void)
{
    int sectid = fdc_getdata(0);
    log_debug("imd: imd_poll_format_sectid, sectid=%02X, count=%u", sectid, count);
    cur_sect->sectid = sectid;
    state = ST_FORMAT_SECTSZ;
}

static void imd_dump(struct imd_file *imd);

/*
 * This function is part of the state machine to implement the format
 * command as used by the i8271.  It reveives and records the logical
 * sector size of the sector being formatted.
 */

static void imd_poll_format_sectsz(void)
{
    int sectsz = fdc_getdata(--count == 0);
    log_debug("imd: imd_poll_format_sectsz, sectsz=%02X, count=%u", sectsz, count);
    if (sectsz != cur_trk->sectsize)
        cur_trk->sectsize = 0xff;
    cur_sect->sectsize = sectsz;
    if (count)
        state = ST_FORMAT_CYLID;
    else {
        fdc_finishread(false);
        state = ST_IDLE;
        imd_dump(&imd_discs[0]);
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and allocates a new sector which is then linked into
 * the list for the current track, i.e. the one being assembled.
 */

static struct imd_sect *imd_poll_wrtrack_new_sect(size_t size, unsigned mode)
{
    struct imd_sect *new_sect = imd_poll_new_sect(size);
    if (new_sect) {
        cur_trk->nsect++;
        if (cur_trk->sectsize == 0xfe)
            cur_trk->sectsize = wt_sectsz;
        else if (wt_sectsz != cur_trk->sectsize)
            cur_trk->sectsize = 0xff;
        new_sect->sectsize = wt_sectsz;
        new_sect->mode     = mode;
        new_sect->cylinder = wt_cylid;
        new_sect->head     = wt_headid;
        new_sect->sectid   = wt_sectid;
    }
    return new_sect;
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements the initial state which is waiting
 * for the ID address mark.  If this does not arrive within a
 * reasonable number of bytes we conclude the last sector has been
 * received.
 */

static void imd_poll_wrtrack_initial(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_initial, byte=%02X, count=%u", b, count);
    if (b == 0xfe)
        state = ST_WRTRACK_CYLID;
    else if (--count == 0) {
        fdc_finishread(false);
        state = ST_IDLE;
        cur_trk = NULL;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the cylinder
 * ID from the ID header.
 */

static void imd_poll_wrtrack_cylid(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_cylid byte=%02X", b);
    if (b != -1) {
        wt_cylid = b;
        state = ST_WRTRACK_HEADID;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the head ID
 * from the ID header.
 */

static void imd_poll_wrtrack_headid(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_headid byte=%02X", b);
    if (b != -1) {
        wt_headid = b;
        state = ST_WRTRACK_SECTID;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the sector ID
 * from the ID header.
 */

static void imd_poll_wrtrack_sectid(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_sectid byte=%02X", b);
    if (b != -1) {
        wt_sectid = b;
        state = ST_WRTRACK_SECTSZ;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which captures the logical
 * sector size code from the ID header.
 */

static void imd_poll_wrtrack_sectsz(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_sectsz byte=%02X", b);
    if (b != -1) {
        wt_sectsz = b;
        state = ST_WRTRACK_HDRCRC;
        log_debug("imd: id header cyl=%u, head=%u, sect=%u, size=%u", wt_cylid, wt_headid, wt_sectid, wt_sectsz);
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which waits for the request
 * to generate the header CRC.
 */

static void imd_poll_wrtrack_hdrcrc(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_hdrcrc byte=%02X", b);
    if (b == 0xfb)
        state = ST_WRTRACK_DATA0;
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which receives the
 * first byte of the sector data.
 */

static void imd_poll_wrtrack_data0(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_data0 byte=%02X", b);
    if (b != -1) {
        cdata = b;
        count = (128 << wt_sectsz) - 1;
        state = ST_WRTRACK_DATA1;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which receives the second
 * and subsequent bytes of the sector data until either a byte is
 * received that is not the same as the first one or until a
 * full sector has been received.
 */

static void imd_poll_wrtrack_data1(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_data1 byte=%02X, count=%d", b, count);
    if (b != -1) {
        if (b == cdata) {
            if (--count == 0) { // complete, compressed sector.
                struct imd_sect *new_sect = imd_poll_wrtrack_new_sect(sizeof(struct imd_sect), 0x02);
                if (new_sect) {
                    new_sect->data[0] = cdata;
                    cur_sect = new_sect;
                    state = ST_WRTRACK_DATACRC;
                }
            }
        }
        else {
            log_debug("imd: imd_poll_wrtrack_data1 switching to non-compressed");
            struct imd_sect *new_sect = imd_poll_wrtrack_new_sect(sizeof(struct imd_sect)+ (128 << wt_sectsz), 0x01);
            if (new_sect) {
                unsigned used = (128 << wt_sectsz) - count - 1;
                log_debug("imd: imd_poll_wrtrack_data1 used=%u", used);
                memset(new_sect->data, cdata, used);
                data = new_sect->data + used;
                *data++ = b;
                cur_sect = new_sect;
                state = ST_WRTRACK_DATA2;
                if (--count == 0)
                    state = ST_WRTRACK_DATACRC;
            }
        }
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which receives further bytes
 * after a switch has already been made to an uncompressed sector.
 */

static void imd_poll_wrtrack_data2(void)
{
    int b = fdc_getdata(0);
    if (b != -1) {
        log_debug("imd: imd_poll_wrtrack_data2 byte=%02X", b);
        *data++ = b;
        if (--count == 0)
            state = ST_WRTRACK_DATACRC;
    }
}

/*
 * This function is part of the state machine to implement the write
 * track command and implements a state which waits for the byte
 * requesting the data CRC be generated.
 */

static void imd_poll_wrtrack_datacrc(void)
{
    int b = fdc_getdata(0);
    log_debug("imd: imd_poll_wrtrack_datacrc byte=%02X", b);
    if (b == 0xf7) {
        state = ST_WRTRACK_INITIAL;
        count = 120;
    }
}

/*
 * This function is called on a timer and uses a state machine to
 * carry out the transfer operations set up by other functions.
 */

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
                imd_poll_finish_read();
                state = ST_IDLE;
            }
            break;

        case ST_READCOMPR:
            fdc_data(cdata);
            if (--count == 0) {
                imd_poll_finish_read();
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
            fdc_finishread(false);
            break;

        case ST_FORMAT_CYLID:
            imd_poll_format_cylid();
            break;

        case ST_FORMAT_HEADID:
            imd_poll_format_headid();
            break;

        case ST_FORMAT_SECTID:
            imd_poll_format_sectid();
            break;

        case ST_FORMAT_SECTSZ:
            imd_poll_format_sectsz();
            break;

        case ST_WRTRACK_INITIAL:
            imd_poll_wrtrack_initial();
            break;

        case ST_WRTRACK_CYLID:
            imd_poll_wrtrack_cylid();
            break;

        case ST_WRTRACK_HEADID:
            imd_poll_wrtrack_headid();
            break;

        case ST_WRTRACK_SECTID:
            imd_poll_wrtrack_sectid();
            break;

        case ST_WRTRACK_SECTSZ:
            imd_poll_wrtrack_sectsz();
            break;

        case ST_WRTRACK_HDRCRC:
            imd_poll_wrtrack_hdrcrc();
            break;

        case ST_WRTRACK_DATA0:
            imd_poll_wrtrack_data0();
            break;

        case ST_WRTRACK_DATA1:
            imd_poll_wrtrack_data1();
            break;

        case ST_WRTRACK_DATA2:
            imd_poll_wrtrack_data2();
            break;

        case ST_WRTRACK_DATACRC:
            imd_poll_wrtrack_datacrc();
            break;

        case ST_RDTRACK_GAP0F:
            fdc_data(0xff);
            if (--count == 0) {
                count = 6;
                state = ST_RDTRACK_GAP00;
            }
            break;

        case ST_RDTRACK_GAP00:
            fdc_data(0x00);
            if (--count == 0)
                state = ST_RDTRACK_IDFE;
            break;

        case ST_RDTRACK_IDFE:
            fdc_data(0xFE);
            state = ST_RDTRACK_CYLID;
            break;

        case ST_RDTRACK_CYLID:
            fdc_data(cur_sect->cylinder);
            state = ST_RDTRACK_HEADID;
            break;

        case ST_RDTRACK_HEADID:
            fdc_data(cur_sect->head);
            state = ST_RDTRACK_SECTID;
            break;

        case ST_RDTRACK_SECTID:
            fdc_data(cur_sect->sectid);
            state = ST_RDTRACK_SECTID;
            break;

        case ST_RDTRACK_SECTSZ:
            fdc_data(cur_sect->sectsize);
            count = 2;
            state = ST_RDTRACK_HDR_CRC;
            break;

        case ST_RDTRACK_HDR_CRC:
            fdc_data(0);
            if (--count == 0) {
                count = 11;
                state = ST_RDTRACK_GAP1F;
            }
            break;

        case ST_RDTRACK_GAP1F:
            fdc_data(0xff);
            if (--count == 0) {
                count = 5;
                state = ST_RDTRACK_GAP10;
            }
            break;

        case ST_RDTRACK_GAP10:
            fdc_data(0x00);
            if (--count == 0) {
                count = 128 << cur_sect->sectsize;
                if (cur_sect->mode & 1) {
                    data = cur_sect->data;
                    state = ST_RDTRACK_DATA;
                }
                else {
                    cdata = cur_sect->data[0];
                    state = ST_RDTRACK_CDATA;
                }
            }
            break;

        case ST_RDTRACK_DATA:
            fdc_data(*data++);
            if (--count == 0) {
                count = 2;
                state = ST_RDTRACK_DATA_CRC;
            }
            break;

        case ST_RDTRACK_CDATA:
            fdc_data(cdata);
            if (--count == 0) {
                count = 2;
                state = ST_RDTRACK_DATA_CRC;
            }
            break;

        case ST_RDTRACK_DATA_CRC:
            fdc_data(0);
            if (--count == 0) {
                cur_sect = cur_sect->next;
                if (cur_sect) {
                    count = 18;
                    state = ST_RDTRACK_GAP0F;
                }
                else {
                    fdc_finishread(false);
                    state = ST_IDLE;
                }
            }
            break;
    }
}

/*
 * This function checks that the file is a valid IMD file, skips over
 * the ASCII comments and returns the offset to the first track, or
 * zero if the file is not valid.
 */

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

/*
 * This function loads the sectors of one track from the file and
 * assembles them into a doubly linked list.
 */

static bool imd_load_sectors(const char *fn, FILE *fp, struct imd_track *trk, int trackno, struct imd_maps *mp)
{
    struct imd_sect *head = NULL;
    struct imd_sect *tail = NULL;
    for (int sectno = 0; sectno < trk->nsect; sectno++) {
        unsigned ssize = (trk->sectsize == 0xff) ? mp->ssize_map[sectno] : trk->sectsize;
        size_t bytes = 128 << ssize;
        int mode = getc(fp);
        if (mode == EOF) {
            imd_sect_err(fn, fp, trackno, sectno);
            goto failed;
        }
        struct imd_sect *sect = malloc((mode & 1) ? sizeof(struct imd_sect) + bytes : sizeof(struct imd_sect));
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
            if (fread(sect->data, bytes, 1, fp) != 1) {
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

/*
 * This function loads one map, i.e. a set of bytes, one per sector,
 * containing some sector ID header field.
 */

static bool imd_load_map(const char *fn, FILE *fp, size_t nsect, uint8_t map[IMD_MAX_SECTS], int trackno, const char *which)
{
    if (fread(map, nsect, 1, fp) == 1)
        return true;
    const char *msg = ferror(fp) ? strerror(errno) : "unexpected EOF";
    log_error("Disc image '%s' track %d: error reading %s map: %s", fn, trackno, which, msg);
    return false;
}

/*
 * This function loads the tracks from the file and assembles them
 * into a doubly linked list.
 */

static bool imd_load_tracks(const char *fn, FILE *fp, struct imd_file *imd)
{
    struct imd_track *head = NULL;
    struct imd_track *tail = NULL;
    unsigned trackno = 0;
    uint8_t hdr[5];
    imd->maxcyl  = 0;
    while (fread(hdr, sizeof(hdr), 1, fp) == 1) {
        log_debug("imd: header for track %d: %02X %02X %02X %02X %02X", trackno, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);
        struct imd_track *trk = malloc(sizeof(struct imd_track));
        if (!trk) {
            log_error("Disc image '%s' track %d: out of memory", fn, trackno);
            goto failed;
        }
        trk->mode     = hdr[0];
        trk->cylinder = hdr[1];
        trk->head     = hdr[2] & ~0xc0;
        trk->nsect    = hdr[3];
        trk->sectsize = hdr[4];
        if (trk->nsect > IMD_MAX_SECTS) {
            log_error("Disc image '%s' track %d has too many sectors", fn, trackno);
            goto failed;
        }
        if (trk->cylinder > imd->maxcyl)
            imd->maxcyl = trk->cylinder;
        struct imd_maps maps;
        if (!imd_load_map(fn, fp, trk->nsect, maps.snum_map, trackno, "sector ID"))
            goto failed;
        if ((hdr[2] & 0x80) && !imd_load_map(fn, fp, trk->nsect, maps.cyl_map, trackno, "cyclinder"))
            goto failed;
        if ((hdr[2] & 0x40) && !imd_load_map(fn, fp, trk->nsect, maps.head_map, trackno, "head"))
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

/*
 * This function is for debugging and writes a readable version of the
 * ID fields for the linked lists in memory to the log file.
 */

static void imd_dump(struct imd_file *imd)
{
#ifdef _DEBUG
    log_debug("imd: disc track0=%ld", imd->track0);
    for (struct imd_track *trk = imd->track_head; trk; trk = trk->next) {
        log_debug("imd: track mode=%02X, cylinder=%u, head=%02X, nsect=%u, sectsize=%02X", trk->mode, trk->cylinder, trk->head, trk->nsect, trk->sectsize);
        for (struct imd_sect *sect = trk->sect_head; sect; sect = sect->next)
            log_debug("imd: sector mode=%02X, cylinder=%u, head=%u, sectid=%u, sectsize=%02X", sect->mode, sect->cylinder, sect->head, sect->sectid, sect->sectsize);
    }
    log_debug("imd: maximum cylinder=%u", imd->maxcyl);
#endif
}

/*
 * This function loads an IMD file and, if successful, sets up the
 * function pointers for the various functions in the FDC to file
 * interface.
 */

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
                drives[drive].format      = imd_format;
                drives[drive].poll        = imd_poll;
                drives[drive].abort       = imd_abort;
                drives[drive].writetrack  = imd_writetrack;
                drives[drive].readtrack   = imd_readtrack;
                return;
            }
        }
        else
            log_error("File '%s' does not have a valid IMD header", fn);
        fclose(fp);
    }
}
