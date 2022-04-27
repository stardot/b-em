/*
 * VDFS for B-EM
 * Steve Fosdick 2016-2020
 *
 * This module implements the host part of a Virtual Disk Filing
 * System, one in which a part of filing system of the host is
 * exposed to the guest through normal OS cals on the guest.
 *
 * This particular implementation comes in two parts:
 *
 * 1. A ROM which runs on the guest, the emulated BBC, and takes over
 *    the filing system vectors to become the current filing system.
 *
 *    In the first implementation of VDFS for B-Em this ROM would
 *    process some operations locally and only forward operations it
 *    could not do to this module by writing to a set of ports in the
 *    expansion area FRED.  This original implementation was based
 *    on the VDFS ROM by J.G Harston which was in turn based on work
 *    by Sprow.
 *
 *    See http://mdfs.net/Apps/Filing/
 *
 *    The current implementation takes a different approach where the
 *    ROM forwards all service calls and filing system operations to
 *    the host module via the ports in FRED and then carries out
 *    operations with the MOS on behalf of the host module via a
 *    dispatch table.
 *
 * 2. This module which runs in the emulator and is called when
 *    the ports concerned are written to.  This module then carries
 *    out the filing system opertation concerned from the host side
 *    and uses the dispatch table mentioned above if it needs the ROM
 *    to carry out operations with the guest machine's MOS.
 */

#include "b-em.h"
#include "6502.h"
#include "disc.h"
#include "keyboard.h"
#include "led.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "sdf.h"
#include "tube.h"
#include "savestate.h"

#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
//#include <unistd.h>

#include <sys/stat.h>

bool vdfs_enabled = 0;
const char *vdfs_cfg_root = NULL;

/*
 * The definition of the VDFS entry that follows is the key data
 * structure for this module and models the association between
 * a file/directory as seen by the BBC and as it exists on the host.
 *
 * The pointers host_path, host_fn and host_inf all point into a
 * single chunk of memory obtained from malloc with host_path
 * containing the start address as well as pointing to the full
 * path name.  The host_fn pointer points to the last element within
 * that pathname, i.e. the filename.  The host_inf pointer points to
 * the .inf extension which can be made part of the path name by
 * writing a dot at this pointer and removed again by writing a NUL.
 *
 * In the event this is a directory rather than a file this entry
 * will contain a head pointer to a linked list of children, those
 * being contents of the directory.
 */

#define MAX_FILE_NAME    10
#define MAX_ACORN_PATH   256
#define MAX_TITLE        19
#define MAX_INF_LINE     160

// These are Acorn standard attributes

#define ATTR_USER_READ   0x0001
#define ATTR_USER_WRITE  0x0002
#define ATTR_USER_EXEC   0x0004
#define ATTR_USER_LOCKD  0x0008
#define ATTR_OTHR_READ   0x0010
#define ATTR_OTHR_WRITE  0x0020
#define ATTR_OTHR_EXEC   0x0040
#define ATTR_OTHR_LOCKD  0x0080

#define ATTR_ACORN_MASK  0x00ff

// These are VDFS internal attributes.

#define ATTR_EXISTS      0x8000
#define ATTR_OPEN_READ   0x1000
#define ATTR_OPEN_WRITE  0x2000
#define ATTR_IS_DIR      0x4000
#define ATTR_BTIME_VALID 0x0100

// Which file metadata to process.

#define META_LOAD        0x0001
#define META_EXEC        0x0002
#define META_ATTR        0x0004

typedef struct vdfs_entry vdfs_entry;

typedef enum {
    SORT_NONE,
    SORT_ADFS,
    SORT_DFS
} sort_type;

struct vdfs_entry {
    vdfs_entry *parent;
    vdfs_entry *next;
    char       *host_path;
    char       *host_fn;
    char       *host_inf;
    char       acorn_fn[MAX_FILE_NAME+1];
    char       dfs_dir;
    uint16_t   attribs;
    time_t     btime;
    time_t     mtime;
    union {
        struct {
            uint32_t   load_addr;
            uint32_t   exec_addr;
            uint32_t   length;
        } file;
        struct {
            vdfs_entry *children;
            time_t     scan_mtime;
            unsigned   scan_seq;
            sort_type  sorted;
            char       boot_opt;
            char       title[MAX_TITLE+1];
        } dir;
    } u;
};

static vdfs_entry root_dir;     // root as seen by BBC, not host.
static vdfs_entry *cat_dir;
static unsigned   scan_seq;
static uint8_t    vdfs_opt1;

typedef struct {
    vdfs_entry *dir;
    const char *desc;
    char       dfs_dir;
    uint8_t    drive;
} vdfs_dirlib;

static vdfs_dirlib cur_dir;
static vdfs_dirlib lib_dir;
static vdfs_dirlib prev_dir;

typedef struct vdfs_findres {
    struct vdfs_entry *parent;
    const char *errmsg;
    char dfs_dir;
    char acorn_fn[MAX_FILE_NAME+1];
} vdfs_findres;

static vdfs_findres info_res;

/*
 * Open files.  An open file is an association between a host OS file
 * pointer, i.e. the host file is kept open too, and a catalogue
 * entry.  Normally for an open file both pointers are set and for a
 * closed file both are NULL but to emulate the OSFIND call correctly
 * a directory can be opened and becomes "half-open", i.e. it can be
 * closed again but any attempt to read or write it will fail.  That
 * case is marked by the vdfs_entry pointer being set but the host
 * FILE pointer being NULL.
 */

#define NUM_CHANNELS  32
#define MIN_CHANNEL   96

typedef struct {
    FILE       *fp;
    vdfs_entry *ent;
} vdfs_open_file;

static vdfs_open_file vdfs_chan[NUM_CHANNELS];

/*
 * Actions.  These are used in two ways:
 *
 * 1. When this module requires the VDFS ROM to carry out some action
 *    from within the guest, i.e. some interaction with the guest OS
 *    it sets the 6502 PC to an address found in a dispatch table
 *    within the ROM itself.
 *
 *    The lower numbered actions, with ROM in the name, are indexes
 *    into this dispatch table.  These values can be passed to the
 *    rom_dispatch function or to vdfs_do function.  These values
 *    must match the entries in the dispatch table in vdfs.asm
 *
 * 2. Because some of the commands are dispatched as ROM actions as
 *    above, for consistency, and avoid a needing to use a union,
 *    other command table entries also translate to one of these
 *    action codes and the entire set of action codes can be passwd
 *    to the vdfs_do function to be carried out.
 */

enum vdfs_action {
    VDFS_ROM_RETURN,
    VDFS_ROM_FSSTART,
    VDFS_ROM_FSBOOT,
    VDFS_ROM_FSINFO,
    VDFS_ROM_FSCLAIM,
    VDFS_ROM_CAT,
    VDFS_ROM_EX,
    VDFS_ROM_INFO,
    VDFS_ROM_DUMP,
    VDFS_ROM_LIST,
    VDFS_ROM_PRINT,
    VDFS_ROM_TYPE,
    VDFS_ROM_ROMS,
    VDFS_ROM_HELP_SHORT,
    VDFS_ROM_HELP_ALL,
    VDFS_ROM_HELP_VDFS,
    VDFS_ROM_HELP_UTILS,
    VDFS_ROM_HELP_SRAM,
    VDFS_ROM_TUBE_EXEC,
    VDFS_ROM_TUBE_INIT,
    VDFS_ROM_TUBE_EXPL,
    VDFS_ROM_OSW7F,
    VDFS_ROM_BREAK,
    VDFS_ROM_FILES,
    VDFS_ROM_NOPEN,
    VDFS_ROM_OSW_TAIL,
    VDFS_ROM_CLOSEALL,
    VDFS_ROM_BUILD,
    VDFS_ROM_APPEND,
    VDFS_ROM_OPT1,
    VDFS_ACT_NOP,
    VDFS_ACT_QUIT,
    VDFS_ACT_SRLOAD,
    VDFS_ACT_SRSAVE,
    VDFS_ACT_SRREAD,
    VDFS_ACT_SRWRITE,
    VDFS_ACT_BACK,
    VDFS_ACT_CDIR,
    VDFS_ACT_DELETE,
    VDFS_ACT_DIR,
    VDFS_ACT_EX,
    VDFS_ACT_FILES,
    VDFS_ACT_INFO,
    VDFS_ACT_LCAT,
    VDFS_ACT_LEX,
    VDFS_ACT_LIB,
    VDFS_ACT_RENAME,
    VDFS_ACT_RESCAN,
    VDFS_ACT_TITLE,
    VDFS_ACT_VDFS,
    VDFS_ACT_ADFS,
    VDFS_ACT_DISC,
    VDFS_ACT_FSCLAIM,
    VDFS_ACT_OSW7F,
    VDFS_ACT_OSW7F_NONE,
    VDFS_ACT_OSW7F_ALL,
    VDFS_ACT_OSW7F_AC1,
    VDFS_ACT_OSW7F_AC2,
    VDFS_ACT_OSW7F_WATF,
    VDFS_ACT_OSW7F_WAT5,
    VDFS_ACT_MMBDIN,
    VDFS_ACT_DRIVE,
    VDFS_ACT_ACCESS,
    VDFS_ACT_COPY
};

/*
 * Command tables.  These map command names as used in a guest OS
 * command to one of the action codes above.  There can be multiple
 * command tables sharing the same lookup function.  The lookup
 * function supports abbreviated commands in the BBC OS fashion
 * with the additional requirement that that initial part of the
 * command name that is in capitals must be present.
 */

#define MAX_CMD_LEN 8

struct cmdent {
    char cmd[MAX_CMD_LEN];
    enum vdfs_action act;
};

/*
 * Filing system numbers.  By default, VDFS uses its own filing
 * system number but when it is masquerading as DFS or ADFS it will
 * use the filing system number for those filing systems.
 */

#define FSNO_VDFS   0x11
#define FSNO_ADFS   0x08
#define FSNO_DFS    0x04

/*
 * Claiming (masquerading as) other filing systems.  For compatibility
 * with programs that select a specific filing system, VDFS is able to
 * pretend to be either DFS or ADFS in that it responds to the
 * command to select those filings systems and also the ROM service
 * call to select a filing system by number.  The next two flags
 * define this behaviour and are set/reset with the *FSCLAIM command.
 */

#define CLAIM_ADFS  0x80
#define CLAIM_DFS   0x40
#define WRITE_DATES 0x10
#define DFS_MODE    0x02
#define VDFS_ACTIVE 0x01

static uint8_t  reg_a;
static uint8_t  fs_flags = 0;
static uint8_t  fs_num   = FSNO_VDFS;
static uint16_t cmd_tail;

/*
 * Switching between DFS and ADFS mode.  These function pointers
 * allow different functions to be used to make VDFS look like either
 * ADFS or DFS to the guest.
 */

static vdfs_entry *(*find_entry)(const char *filename, vdfs_findres *res, vdfs_dirlib *dir);
static vdfs_entry *(*find_next)(vdfs_entry *ent, vdfs_findres *res);
static void (*osgbpb_get_dir)(uint32_t pb, vdfs_dirlib *dir);
static bool (*cat_prep)(uint16_t addr, vdfs_dirlib *dir);
static void (*cat_get_dir)(vdfs_dirlib *dir);
static void (*cmd_dir)(uint16_t addr);
static void (*cmd_lib)(uint16_t addr);

/* Function to light an LED during VDFS activity.  This is only called
 * when VDFS has to go to the host OS.  When it answers meta data
 * queries from its internal data structures the LED is not lit.
 */

static void show_activity()
{
    led_update(LED_VDFS, true, 10);
}

static uint16_t readmem16(uint16_t addr)
{
    uint16_t value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    return value;
}

static uint32_t readmem24(uint16_t addr)
{
    uint32_t value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    value |= (readmem(addr+2) << 16);
    return value;
}

static uint32_t readmem32(uint16_t addr)
{
    uint32_t value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    value |= (readmem(addr+2) << 16);
    value |= (readmem(addr+3) << 24);
    return value;
}

static void writemem16(uint16_t addr, uint16_t value)
{
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
}

static void writemem24(uint16_t addr, uint32_t value)
{
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
    writemem(addr+2, (value >> 16) & 0xff);
}

static void writemem32(uint16_t addr, uint32_t value)
{
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
    writemem(addr+2, (value >> 16) & 0xff);
    writemem(addr+3, (value >> 24) & 0xff);
}

static void rom_dispatch(enum vdfs_action act)
{
    int max = readmem(0x8000);
    if (act < max)
        pc = readmem16(readmem16(0x8001) + (act << 1));
    else
        log_warn("vdfs: ROM does not support action %d, max is %d", act, max);
}

static void flush_all(void)
{
    int channel;
    FILE *fp;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        if ((fp = vdfs_chan[channel].fp))
            fflush(fp);
}

static void free_entry(vdfs_entry *ent)
{
    if (ent) {
        free_entry(ent->next);
        char *ptr = ent->host_path;
        if (ptr)
            free(ptr);
        if (ent->attribs & ATTR_IS_DIR)
            free_entry(ent->u.dir.children);
        free(ent);
    }
}

void vdfs_reset(void)
{
    flush_all();
}

static int hex2nyb(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    else if (ch >= 'a' && ch <='f')
        return ch - 'a' + 10;
    else
        return -1;
}

/*
 * Translate non-BBC filename characters to BBC ones according to
 * the table at http://beebwiki.mdfs.net/Filename_character_mapping
*/

static const char hst_chars[] = "#$%&.?@^";
static const char bbc_chars[] = "?<;+/#=>";

static inline void hst2bbc(const char *host_fn, char *acorn_fn)
{
    int ch;
    const char *ptr;
    char *end = acorn_fn + MAX_FILE_NAME;

    while ((ch = *host_fn++) && acorn_fn < end) {
        if ((ptr = strchr(hst_chars, ch)))
            ch = bbc_chars[ptr-hst_chars];
        *acorn_fn++ = ch;
    }
    *acorn_fn = '\0';
}

static inline void bbc2hst(const char *acorn_fn, char *host_fn)
{
    int ch;
    const char *ptr;
    char *end = host_fn + MAX_FILE_NAME;

    while ((ch = *acorn_fn++) && host_fn < end) {
        if ((ptr = strchr(bbc_chars, ch)))
            ch = hst_chars[ptr-bbc_chars];
        *host_fn++ = ch;
    }
    *host_fn = '\0';
}

static char *make_host_path(vdfs_entry *ent, const char *host_fn)
{
    char *host_dir_path, *host_file_path, *ptr;
    size_t len;

    len = strlen(host_fn) + 6;
    host_dir_path = ent->parent->host_path;
    if (host_dir_path[0] != '.' || host_dir_path[1])
        len += strlen(host_dir_path);
    if ((host_file_path = malloc(len))) {
        ent->host_path = ptr = host_file_path;
        if (host_dir_path[0] != '.' || host_dir_path[1]) {
            ptr = stpcpy(ptr, host_dir_path);
            *ptr++ = '/';
        }
        ent->host_fn = ptr;
        ptr = stpcpy(ptr, host_fn);
        ent->host_inf = ptr;
        *ptr++ = '\0';
        *ptr++ = 'i';
        *ptr++ = 'n';
        *ptr++ = 'f';
        *ptr = '\0';
        log_debug("vdfs: make_host_path returns '%s' for '%s'", host_file_path, host_fn);
        return host_file_path;
    }
    log_warn("vdfs: out of memory allocaing host path");
    return NULL;
}

// ADFS Error messages (used)

static const char err_wont[]     = "\x93" "Won't";
static const char err_badparms[] = "\x94" "Bad parms";
static const char err_badcopy[]  = "\x94" "Bad copy - two names needed";
static const char err_delcsd[]   = "\x97" "Can't delete CSD";
static const char err_dellib[]   = "\x98" "Can't delete library";
static const char err_badren[]   = "\xb0" "Bad rename";
static const char err_notempty[] = "\xb4" "Dir not empty";
static const char err_access[]   = "\xbd" "Access violation";
static const char err_nfile[]    = "\xc0" "Too many open files";
static const char err_nupdate[]  = "\xc1" "Not open for update";
static const char err_isopen[]   = "\xc2" "Already open";
static const char err_exists[]   = "\xc4" "Already exists";
static const char err_direxist[] = "\xc4" "Dir already exists";
static const char err_filexist[] = "\xc4" "File already exists";
static const char err_discerr[]  = "\xc7" "Disc error";
static const char err_badname[]  = "\xcc" "Bad name";
static const char err_notfound[] = "\xd6" "Not found";
static const char err_channel[]  = "\xde" "Channel";
static const char err_badcmd[]   = "\xfe" "Bad command";
static const char err_badaddr[]  = "\x94" "Bad address";
static const char err_nomem[]    = "\x92" "Out of memory";
static const char err_no_swr[]   = "\x93" "No SWRAM at that address";
static const char err_too_big[]  = "\x94" "Too big";
static const char err_wildcard[] = "\xfd" "Wild cards";

// DFS error messages (used)

static const char err_baddir[]   = "\xce" "Bad directory";

// Error messages unique to VDFS

static const char err_notdir[]   = "\xc7" "Not a directory";

/* Other ADFS messages not used.

static const char err_aborted[]  = "\x92" "Aborted";
static const char err_badcsum[]  = "\xaa" "Bad checksum";
static const char err_outside[]  = "\xb7" "Outside file";
static const char err_locked[]   = "\xc3" "Locked";
static const char err_full[]     = "\xc6" "Disc full";
static const char err_datalost[] = "\xca" "Data lost, channel";
static const char err_badopt[]   = "\xcb" "Bad opt";
static const char err_eof[]      = "\xdf" "EOF";

*/

static void adfs_error(const char *err)
{
    uint16_t addr = 0x100;
    int ch;

    writemem(addr++, 0);  // BRK instruction.
    do {
        ch = *err++;
        writemem(addr++, ch);
    } while (ch);
    pc = 0x100;          // jump to BRK sequence just created.
}

static void adfs_hosterr(int errnum)
{
    const char *msg;

    switch(errnum) {
        case EACCES:
        case EPERM:
        case EROFS:
            msg = err_access;
            break;
        case EEXIST:
            msg = err_exists;
            break;
        case EISDIR:
            msg = err_wont;
            break;
        case EMFILE:
        case ENFILE:
            msg = err_nfile;
            break;
        case ENOENT:
            msg = err_notfound;
            break;
        case ENOTEMPTY:
            msg = err_notempty;
            break;
        default:
            msg = err_discerr;
    }
    adfs_error(msg);
}

// Populate a VDFS entry from host information.

static void init_dir(vdfs_entry *ent)
{
    ent->u.dir.children = NULL;
    ent->u.dir.scan_mtime = 0;
    ent->u.dir.scan_seq = 0;
    ent->u.dir.sorted = SORT_NONE;
    ent->u.dir.boot_opt = 0;
    ent->u.dir.title[0] = 0;
}

#ifdef WIN32
#include <fileapi.h>

static time_t time_win2unix(LPFILETIME td)
{
    uint64_t lo = td->dwLowDateTime;
    uint64_t hi = td->dwHighDateTime;
    uint64_t ticks = lo | (hi << 32);
    /* Convert units, 100ns to second */
    uint64_t secs = ticks / 10000000;
    /* Convert epoch, 01-Jan-1601 to 01-Jan-1970 */
    return secs - (uint64_t)((1970-1601)*365+92-3)*24*60*60;
}

static void scan_attr(vdfs_entry *ent)
{
    uint16_t attribs = ent->attribs;
    WIN32_FILE_ATTRIBUTE_DATA wattr;
    if (GetFileAttributesEx(ent->host_path, GetFileExInfoStandard, &wattr)) {
        ent->btime = time_win2unix(&wattr.ftCreationTime);
        ent->mtime = time_win2unix(&wattr.ftLastWriteTime);
        attribs &= ~ATTR_ACORN_MASK;
        attribs |= ATTR_EXISTS|ATTR_BTIME_VALID|ATTR_USER_READ|ATTR_OTHR_READ;
        if (wattr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(attribs & ATTR_IS_DIR)) {
                attribs |= ATTR_IS_DIR;
                init_dir(ent);
            }
        }
        else {
            if (attribs & ATTR_IS_DIR) {
                log_debug("vdfs: dir %s has become a file", ent->acorn_fn);
                attribs &= ~ATTR_IS_DIR;
                free_entry(ent->u.dir.children);
            }
            ent->u.file.load_addr = 0;
            ent->u.file.exec_addr = 0;
            if (wattr.nFileSizeHigh) {
                log_warn("vdfs: file %s is too big (>4Gb)", ent->host_path);
                ent->u.file.length = 0xffffffff;
            }
            else
                ent->u.file.length = wattr.nFileSizeLow;
        }
        if (wattr.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
            attribs |= ATTR_USER_LOCKD;
        else
            attribs |= ATTR_USER_WRITE|ATTR_OTHR_WRITE;
        ent->attribs = attribs;
    }
    else
        log_warn("vdfs: GetFileAttributesEx failed for %s: %s", ent->host_path, strerror(errno));
#else
static void scan_attr(vdfs_entry *ent)
{
    uint16_t attribs = ent->attribs;
#if defined(linux) && ((__GLIBC__ == 2 && __GLIBC_MINOR__ >= 28) || __GLIBC__ > 2)
#define size_field stx.stx_size
    struct statx stx;

    if (!statx(AT_FDCWD, ent->host_path, AT_NO_AUTOMOUNT, STATX_BASIC_STATS|STATX_BTIME, &stx)) {
        mode_t mode = stx.stx_mode;
        ent->btime = stx.stx_btime.tv_sec;
        ent->mtime = stx.stx_mtime.tv_sec;
        if (stx.stx_mask & STATX_BTIME)
            attribs |= ATTR_BTIME_VALID;
#else
#define size_field stb.st_size
    struct stat stb;

    if (!stat(ent->host_path, &stb)) {
        mode_t mode = stb.st_mode;
        ent->btime = 0;
        ent->mtime = stb.st_mtime;
#endif
        attribs |= ATTR_EXISTS;
        attribs &= ~ATTR_ACORN_MASK;
        if (S_ISDIR(mode)) {
            if (!(attribs & ATTR_IS_DIR)) {
                attribs |= ATTR_IS_DIR;
                init_dir(ent);
            }
        }
        else {
            if (attribs & ATTR_IS_DIR) {
                log_debug("vdfs: dir %s has become a file", ent->acorn_fn);
                attribs &= ~ATTR_IS_DIR;
                free_entry(ent->u.dir.children);
            }
            ent->u.file.load_addr = 0;
            ent->u.file.exec_addr = 0;
            ent->u.file.length = size_field;
        }
        if (mode & S_IRUSR)
            attribs |= ATTR_USER_READ;
        if (mode & S_IWUSR)
            attribs |= ATTR_USER_WRITE;
        if (mode & S_IXUSR)
            attribs |= ATTR_USER_EXEC;
        if (mode & (S_IRGRP|S_IROTH))
            attribs |= ATTR_OTHR_READ;
        if (mode & (S_IWGRP|S_IWOTH))
            attribs |= ATTR_OTHR_WRITE;
        if (mode & (S_IXGRP|S_IXOTH))
            attribs |= ATTR_OTHR_EXEC;
    }
    else
        log_warn("vdfs: unable to stat '%s': %s", ent->host_path, strerror(errno));
#endif
    log_debug("vdfs: scan_attr: host=%s, attr=%04X", ent->host_fn, attribs);
    ent->attribs = attribs;
}

static const char *scan_inf_start(vdfs_entry *ent, char inf_line[MAX_INF_LINE])
{
    FILE *fp;
    *ent->host_inf = '.';
    fp = fopen(ent->host_path, "rt");
    *ent->host_inf = '\0';
    if (fp) {
        const char *lptr = fgets(inf_line, MAX_INF_LINE, fp);
        fclose(fp);
        if (lptr) {
            int ic, ch;
            char *ptr = ent->acorn_fn;
            char *end = ptr + MAX_FILE_NAME;

            // Parse filename.
            while ((ic = *lptr++) == ' ' || ic == '\t')
                ;
            if (ic != '\n') {
                if ((ch = *lptr++) == '.') {
                    ent->dfs_dir = ic;
                    ch = *lptr++;
                }
                else {
                    ent->dfs_dir = '$';
                    *ptr++ = ic;
                }
                while (ch && ch != ' ' && ch != '\t' && ch != '\n') {
                    if (ptr < end && ch >= '!' && ch <= '~')
                        *ptr++ = ch;
                    ch = *lptr++;
                }
                if (ptr < end)
                    *ptr = '\0';
                if (ch != '\n')
                    return lptr;
            }
        }
    }
    return NULL;
}

static void acorn_date_unix(struct tm *tp, unsigned adate)
{
    tp->tm_year = 1981 + (((adate & 0x00e0) >> 1) | ((adate & 0xf000) >> 12)) - 1900;
    tp->tm_mon = ((adate & 0x0f00) >> 8) - 1;
    tp->tm_mday = (adate & 0x001f);
    log_debug("vdfs: acorn_date_unix: acorn_date=%04X, local=%02u/%02u/%02u", adate, tp->tm_mday, tp->tm_mon+1, tp->tm_year + 1900);
}

static void acorn_time_unix(struct tm *tp, unsigned atime)
{
    tp->tm_hour = atime & 0x0000ff;
    tp->tm_min = (atime & 0x00ff00) >> 8;
    tp->tm_sec = (atime & 0xff0000) >> 16;
    log_debug("vdfs: acorn_time_unix: acorn_time=%06X, local=%02u:%02u:%02u", atime, tp->tm_hour, tp->tm_min, tp->tm_sec);
}

static time_t acorn_timedate_unix(unsigned adate, unsigned atime)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    acorn_date_unix(&tm, adate);
    acorn_time_unix(&tm, atime);
    tm.tm_isdst = -1;
    time_t secs = mktime(&tm);
    return secs;
}

static void set_ent_date_time(vdfs_entry *ent, bool mdate_valid, unsigned mdate, unsigned mtime, bool cdate_valid, unsigned cdate, unsigned ctime)
{
    if (mdate_valid) {
        time_t msecs = acorn_timedate_unix(mdate, mtime);
        if (msecs > ent->mtime)
            ent->mtime = msecs;
    }
    if (cdate_valid) {
        time_t csecs = acorn_timedate_unix(cdate, ctime);
        if (!(ent->attribs & ATTR_BTIME_VALID) || csecs < ent->btime)
            ent->btime = csecs;
        ent->attribs |= ATTR_BTIME_VALID;
    }
}

static void scan_inf_file(vdfs_entry *ent)
{
    char inf_line[MAX_INF_LINE];
    int ch, nyb;
    uint32_t load_addr = 0, exec_addr = 0, attribs = 0;
    bool mdate_valid = false, cdate_valid = false;
    unsigned mdate = 0, mtime = 0, cdate = 0, ctime = 0;
    const char *lptr = scan_inf_start(ent, inf_line);
    if (lptr) {
        // Parse load address.
        while ((ch = *lptr++) == ' ' || ch == '\t')
            ;
        while ((nyb = hex2nyb(ch)) >= 0) {
            load_addr = (load_addr << 4) | nyb;
            ch = *lptr++;
        }

        // Parse exec address.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0) {
            exec_addr = (exec_addr << 4) | nyb;
            ch = *lptr++;
        }

        // Skip the length.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0)
            ch = *lptr++;

        // Parse the attributes.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        if (ch == 'L' || ch == 'l')
            attribs = ATTR_USER_READ|ATTR_USER_LOCKD|ATTR_OTHR_READ|ATTR_OTHR_LOCKD;
        else {
            while ((nyb = hex2nyb(ch)) >= 0) {
                attribs = (attribs << 4) | nyb;
                ch = *lptr++;
            }
        }

        // Parse modification date.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0) {
            mdate_valid = true;
            mdate = (mdate << 4) | nyb;
            ch = *lptr++;
        }

        // Parse modification time.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0) {
            mtime = (mtime << 4) | nyb;
            ch = *lptr++;
        }

        // Parse creation date.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0) {
            cdate_valid = true;
            cdate = (cdate << 4) | nyb;
            ch = *lptr++;
        }

        // Parse creation time.
        while (ch == ' ' || ch == '\t')
            ch = *lptr++;
        while ((nyb = hex2nyb(ch)) >= 0) {
            ctime = (ctime << 4) | nyb;
            ch = *lptr++;
        }
    }
    ent->u.file.load_addr = load_addr;
    ent->u.file.exec_addr = exec_addr;
    log_debug("vdfs: load=%08X, exec=%08X", load_addr, exec_addr);
    if (attribs) {
        /* merge .inf and host filing system attributes */
        ent->attribs &= attribs|~ATTR_ACORN_MASK;
        ent->attribs |= attribs & (ATTR_USER_LOCKD|ATTR_OTHR_LOCKD);
    }
    set_ent_date_time(ent, mdate_valid, mdate, mtime, cdate_valid, cdate, ctime);
}

static void scan_inf_dir_new(vdfs_entry *dir, const char *lptr, const char *eptr)
{
    unsigned opt = 0;
    bool mdate_valid = false, cdate_valid = false;
    unsigned mdate = 0, mtime = 0, cdate = 0, ctime = 0;
    do {
        // Skip spaces after the '='
        const char *aptr = eptr;
        int ch;
        while ((ch = *++aptr) == ' ' || ch == '\t')
            ;
        // Try three character keys.
        const char *sptr = eptr - 3;
        if (sptr >= lptr && !strncasecmp(sptr, "OPT=", 4)) {
            int nyb;
            // Parse options.
            while ((nyb = hex2nyb(ch)) >= 0) {
                opt = (opt << 4) | nyb;
                ch = *++aptr;
            }
        }
        else {
            // Try five character keys.
            sptr = eptr - 5;
            if (sptr >= lptr) {
                if (!strncasecmp(sptr, "TITLE=", 6)) {
                    // Parse title.
                    char *ptr = dir->u.dir.title;
                    char *end = dir->u.dir.title + MAX_TITLE;
                    int quote= 0;

                    if (ch == '"') {
                        quote = 1;
                        ch = *++aptr;
                    }
                    while (ptr < end && ch && ch != '\n' && (ch != '"' || !quote) && ((ch != ' ' && ch != '\t') || quote)) {
                        *ptr++ = ch & 0x7f;
                        ch = *++aptr;
                    }
                    *ptr = '\0';
                }
                else if (!strncasecmp(sptr, "MDATE=", 6)) {
                    int nyb;
                    while ((nyb = hex2nyb(ch)) >= 0) {
                        mdate_valid = true;
                        mdate = (mdate << 4) | nyb;
                        ch = *++aptr;
                    }
                }
                else if (!strncasecmp(sptr, "MTIME=", 6)) {
                    int nyb;
                    while ((nyb = hex2nyb(ch)) >= 0) {
                        mtime = (mtime << 4) | nyb;
                        ch = *++aptr;
                    }
                }
                else if (!strncasecmp(sptr, "CDATE=", 6)) {
                    int nyb;
                    while ((nyb = hex2nyb(ch)) >= 0) {
                        cdate_valid = true;
                        cdate = (cdate << 4) | nyb;
                        ch = *++aptr;
                    }
                }
                else if (!strncasecmp(sptr, "CTIME=", 6)) {
                    int nyb;
                    while ((nyb = hex2nyb(ch)) >= 0) {
                        ctime = (ctime << 4) | nyb;
                        ch = *++aptr;
                    }
                }
            }
            else
                ++aptr;
        }
        lptr = aptr;
        eptr = strchr(lptr, '=');
    }
    while (eptr);
    dir->u.dir.boot_opt = opt;
    set_ent_date_time(dir, mdate_valid, mdate, mtime, cdate_valid, cdate, ctime);
}

static unsigned scan_inf_dir_old(const char *lptr, char *title)
{
    unsigned opt = 0;
    // Old .inf format for directories.
    int ch, nyb;
    // Parse options.
    while ((ch = *lptr++) == ' ' || ch == '\t')
        ;
    while ((nyb = hex2nyb(ch)) >= 0) {
        opt = (opt << 4) | nyb;
        ch = *lptr++;
    }
    while (ch == ' ' || ch == '\t')
        ch = *lptr++;
    char *ptr = title;
    char *end = title + MAX_TITLE;
    while (ptr < end && ch && ch != '\n') {
        *ptr++ = ch & 0x7f;
        ch = *lptr++;
    }
    *ptr = '\0';
    return opt;
}

static void scan_inf_dir(vdfs_entry *dir)
{
    char inf_line[MAX_INF_LINE];
    dir->u.dir.title[0] = 0;
    const char *lptr = scan_inf_start(dir, inf_line);
    if (lptr) {
        const char *eptr = strchr(lptr, '=');
        if (eptr)
            scan_inf_dir_new(dir, lptr, eptr);
        else
            dir->u.dir.boot_opt = scan_inf_dir_old(lptr, dir->u.dir.title);
    }
}

static void scan_entry(vdfs_entry *ent)
{
    scan_attr(ent);
    if (ent->attribs & ATTR_IS_DIR)
        scan_inf_dir(ent);
    else
        scan_inf_file(ent);
    if (ent->acorn_fn[0] == '\0')
        hst2bbc(ent->host_fn, ent->acorn_fn);
}

static void init_entry(vdfs_entry *ent)
{
    ent->next = NULL;
    ent->host_path = NULL;
    ent->host_fn = ".";
    ent->acorn_fn[0] = '\0';
    ent->acorn_fn[MAX_FILE_NAME] = '\0';
    ent->dfs_dir = '$';
    ent->attribs = 0;
}

static int vdfs_cmp(const char *namea, const char *nameb, size_t len)
{
    int ca, cb, d;

    while (len-- > 0) {
        ca = *(const unsigned char *)namea++;
        cb = *(const unsigned char *)nameb++;
        if (!ca)
            return cb ? -1 : 0;
        if (ca != cb) {
            if (ca >= 'a' && ca <= 'z')
                ca = ca - 'a' + 'A';
            if (cb >= 'a' && cb <= 'z')
                cb = cb - 'a' + 'A';
            if ((d = ca - cb))
                return d;
        }
    }
    return 0;
}

static vdfs_entry *acorn_search(vdfs_entry *dir, const char *acorn_fn)
{
    vdfs_entry *ent;

    for (ent = dir->u.dir.children; ent; ent = ent->next)
        if (!vdfs_cmp(ent->acorn_fn, acorn_fn, MAX_FILE_NAME))
            return ent;
    return NULL;
}

static bool vdfs_wildmat(const char *pattern, const char *candidate)
{
    log_debug("vdfs: vdfs_wildmat, pattern=%s, candidate=%s", pattern, candidate);
    for (;;) {
        int pat_ch = *pattern++;
        if (pat_ch == '*') {
            if (!*pattern) {
                log_debug("vdfs: vdfs_wildmat return#1, true, * matches nothing");
                return true;
            }
            do {
                if (vdfs_wildmat(pattern, candidate++)) {
                    log_debug("vdfs: vdfs_wildmat return#2, true, * recursive");
                    return true;
                }
            } while (*candidate);
            log_debug("vdfs: vdfs_wildmat return#3, false, * mismatch");
            return false;
        }
        int can_ch = *candidate++;
        if (can_ch) {
            if (pat_ch != can_ch && pat_ch != '#') {
                if (pat_ch >= 'a' && pat_ch <= 'z')
                    pat_ch = pat_ch - 'a' + 'A';
                if (can_ch >= 'a' && can_ch <= 'z')
                    can_ch = can_ch - 'a' + 'A';
                if (can_ch != pat_ch) {
                    log_debug("vdfs: vdfs_wildmat return#4, character mismatch");
                    return false;
                }
            }
        }
        else if (pat_ch) {
            log_debug("vdfs: vdfs_wildmat return#5, false, candidate too short");
            return false;
        }
        else {
            log_debug("vdfs: vdfs_wildmat return#6, true, NUL on both");
            return true;
        }
    }
    log_debug("vdfs: vdfs_wildmat return#7, true, reached max length");
    return true;
}

static vdfs_entry *wild_search(vdfs_entry *dir, const char *pattern)
{
    vdfs_entry *ent;

    for (ent = dir->u.dir.children; ent; ent = ent->next)
        if (vdfs_wildmat(pattern, ent->acorn_fn))
            return ent;
    return NULL;
}

// Create VDFS entry for a new file.

static int next_seq_ch(int seq_ch)
{
    if (seq_ch == '9')
        return 'A';
    if (seq_ch == 'Z')
        return 'a';
    if (seq_ch == 'z')
        return -1;
    return seq_ch + 1;
}

static vdfs_entry *new_entry(vdfs_entry *dir, const char *host_fn)
{
    vdfs_entry *ent;
    char *host_path;

    if ((ent = malloc(sizeof(vdfs_entry)))) {
        init_entry(ent);
        ent->parent = dir;
        if ((host_path = make_host_path(ent, host_fn))) {
            scan_entry(ent);
            if (acorn_search(dir, ent->acorn_fn)) {
                // name was already in use - generate a unique one.
                int seq_ch = '0';
                size_t ix = strlen(ent->acorn_fn);
                if (ix > (MAX_FILE_NAME-2))
                    ix = MAX_FILE_NAME-2;
                ent->acorn_fn[ix] = '~';
                ent->acorn_fn[ix+1] = seq_ch;
                while (acorn_search(dir, ent->acorn_fn)) {
                    if ((seq_ch = next_seq_ch(seq_ch)) < 0) {
                        log_warn("vdfs: unable to create unique acorn name for %s", host_fn);
                        free(ent);
                        return NULL;
                    }
                    ent->acorn_fn[ix+1] = seq_ch;
                }
                log_debug("vdfs: new_entry: unique name %s used", ent->acorn_fn);
            }
            ent->next = dir->u.dir.children;
            dir->u.dir.children = ent;
            dir->u.dir.sorted = SORT_NONE;
            log_debug("vdfs: new_entry: returning new entry %p", ent);
            return ent;
        }
        free(ent);
    }
    return NULL;
}

// Given a VDFS entry representing a dir scan the corresponding host dir.

static vdfs_entry *host_search(vdfs_entry *dir, const char *host_fn)
{
    vdfs_entry *ent;

    for (ent = dir->u.dir.children; ent; ent = ent->next)
        if (!strcmp(ent->host_fn, host_fn))
            return ent;
    return NULL;
}

static bool is_inf(const char *path)
{
    const char *ptr = strrchr(path, '.');
    // we know if ptr is not NULL, ptr[0] is a dot, no need to test.
    return ptr && (ptr[1] == 'I' || ptr[1] == 'i') && (ptr[2] == 'N' || ptr[2] == 'n') && (ptr[3] == 'F' || ptr[3] == 'f') && !ptr[4];
}

static void scan_dir_host(vdfs_entry *dir, DIR *dp)
{
    struct dirent *dep;

    // Mark all previos entries deleted but leave them in the list.
    for (vdfs_entry *ent = dir->u.dir.children; ent; ent = ent->next)
        ent->attribs &= (ATTR_IS_DIR|ATTR_OPEN_READ|ATTR_OPEN_WRITE);

    // Go through the entries in the host dir, find each in the
    // list and mark it as extant, if found, or create a new
    // entry if not.
    while ((dep = readdir(dp))) {
        if (*(dep->d_name) != '.') {
            if (!is_inf(dep->d_name)) {
                vdfs_entry *ent = host_search(dir, dep->d_name);
                if (ent)
                    scan_entry(ent);
                else if (!(ent = new_entry(dir, dep->d_name)))
                    break;
            }
        }
    }
}

static int scan_dir(vdfs_entry *dir)
{
    DIR  *dp;
    struct stat stb;

    // Has this been scanned sufficiently recently already?

    if (stat(dir->host_path, &stb) == -1)
        log_warn("vdfs: unable to stat directory '%s': %s", dir->host_path, strerror(errno));
    else if (scan_seq <= dir->u.dir.scan_seq && stb.st_mtime <= dir->u.dir.scan_mtime) {
        log_debug("vdfs: using cached dir info for %s", dir->host_path);
        return 0;
    }
    show_activity();

    if ((dp = opendir(dir->host_path))) {
        scan_dir_host(dir, dp);
        closedir(dp);
        scan_inf_dir(dir);
        dir->u.dir.scan_seq = scan_seq;
        dir->u.dir.scan_mtime = stb.st_mtime;
        return 0;
    } else {
        log_warn("vdfs: unable to opendir '%s': %s", dir->host_path, strerror(errno));
        return 1;
    }
}

/*
 * Parse a name, probably of a file, from 'addr' on the guest into 'str'
 * on the host with a maximum length of 'size'.  Return the address on
 * the guest immediately after the name, or zero if too long.
 */

static uint16_t parse_name(char *str, size_t size, uint16_t addr)
{
    char *ptr = str;
    char *end = str + size - 1;
    int ch, quote= 0;

    log_debug("vdfs: parse_name: addr=%04x", addr);
    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (ch == '"') {
        quote = 1;
        ch = readmem(addr++);
    }
    while (!(ch == '\r' || ch == ' ' || ch == '\t' || (ch == '"' && quote))) {
        if (ptr >= end || ch < ' ' || ch == 0x7f) {
            adfs_error(err_badname);
            return 0;
        }
        *ptr++ = ch & 0x7f;
        ch = readmem(addr++);
    }
    *ptr = '\0';
    log_debug("vdfs: parse_name: name=%s", str);
    return addr;
}

static bool check_valid_dir(vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    if (ent && ent->attribs & ATTR_IS_DIR)
        return true;
    log_warn("vdfs: %s directory is not valid", dir->desc);
    adfs_error(err_baddir);
    return false;
}

// Given an Acorn filename, find the VDFS entry.

static vdfs_entry *find_entry_adfs(const char *filename, vdfs_findres *res, vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    int ch, fn0, fn1;
    const char *fn_src;
    char *fn_ptr, *fn_end;
    vdfs_entry *ptr;

    res->parent = NULL;
    res->errmsg = err_notfound;
    res->dfs_dir = dir->dfs_dir;

    for (fn_src = filename;;) {
        fn_ptr = res->acorn_fn;
        fn_end = fn_ptr + MAX_FILE_NAME;
        while ((ch = *fn_src++) && ch != '.') {
            if (fn_ptr >= fn_end) {
                res->errmsg = err_badname;
                return NULL;
            }
            *fn_ptr++ = ch;
        }
        *fn_ptr = '\0';
        fn0 = res->acorn_fn[0];
        fn1 = res->acorn_fn[1];
        if (((fn0 == '$' || fn0 == '&') && fn1 == '\0') || (fn0 == ':' && fn1 >= '0' && fn1 <= '9'))
            ent = &root_dir;
        else if (fn0 == '%' && fn1 == '\0' && check_valid_dir(&lib_dir))
            ent = lib_dir.dir;
        else if (fn0 == '^' && fn1 == '\0')
            ent = ent->parent;
        else if (!scan_dir(ent) && (ptr = wild_search(ent, res->acorn_fn)))
            ent = ptr;
        else {
            if (ch != '.')
                res->parent = ent;
            log_debug("vdfs: find_entry: acorn path %s not found", filename);
            return NULL; // not found
        }
        if (ch != '.') {
            log_debug("vdfs: find_entry: acorn path %s found as %s", filename, ent->host_path);
            return ent;
        }
        if (!(ent->attribs & ATTR_IS_DIR)) {
            log_debug("vdfs: find_entry: acorn path %s has file %s where directory expected", filename, ent->host_path);
            res->errmsg = err_notdir;
            return NULL;
        }
    }
    return NULL;
}

static vdfs_entry *find_next_adfs(vdfs_entry *ent, vdfs_findres *res)
{
    do {
        ent = ent->next;
        if (!ent)
            return NULL;
    } while (!(ent->attribs & ATTR_EXISTS) || !vdfs_wildmat(res->acorn_fn, ent->acorn_fn));
    return ent;
}

static vdfs_entry *find_entry_dfs(const char *filename, vdfs_findres *res, vdfs_dirlib *dir)
{
    int srchdir = dir->dfs_dir;
    log_debug("vdfs: find_entry_dfs, filename=%s, dfsdir=%c", filename, srchdir);
    int ic = filename[0];
    if (ic == ':') {
        int drive = filename[1];
        if (drive < '0' || drive > '3' || filename[2] != '.') {
            adfs_error(err_badname);
            res->parent = NULL;
            return NULL;
        }
        filename += 3;
        ic = filename[0];
    }
    if (ic && filename[1] == '.') {
        srchdir = ic;
        filename += 2;
        log_debug("vdfs: find_entry_dfs, parsed DFS dir %c, filename=%s", srchdir, filename);
    }
    res->dfs_dir = srchdir;
    size_t len = strlen(filename);
    if (len > MAX_FILE_NAME) {
        adfs_error(err_badname);
        res->parent = NULL;
    }
    memcpy(res->acorn_fn, filename, len+1);
    if (!scan_dir(dir->dir)) {
        for (vdfs_entry *ent = dir->dir->u.dir.children; ent; ent = ent->next) {
            log_debug("vdfs: find_entry_dfs, considering entry %c.%s", ent->dfs_dir, ent->acorn_fn);
            if (srchdir == '*' || srchdir == '#' || srchdir == ent->dfs_dir) {
                log_debug("vdfs: find_entry_dfs, matched DFS dir");
                if (vdfs_wildmat(filename, ent->acorn_fn))
                    return ent;
            }
        }
    }
    res->parent = dir->dir;
    res->errmsg = err_notfound;
    return NULL;
}

static vdfs_entry *find_next_dfs(vdfs_entry *ent, vdfs_findres *res)
{
    int srchdir = res->dfs_dir;
    log_debug("vdfs: find_next_dfs, pattern=%s, start=%c.%s, srchdir=%c", res->acorn_fn, ent->dfs_dir, ent->acorn_fn, srchdir);
    do {
        ent = ent->next;
        if (!ent)
            return NULL;
        log_debug("vdfs: find_next_dfs, checking %c.%s", ent->dfs_dir, ent->acorn_fn);
    } while (!(ent->attribs & ATTR_EXISTS) || !(srchdir == '*' || srchdir == '#' || srchdir == ent->dfs_dir) || !vdfs_wildmat(res->acorn_fn, ent->acorn_fn));
    return ent;
}

static vdfs_entry *add_new_file(vdfs_findres *res)
{
    vdfs_entry *dir = res->parent;
    vdfs_entry *new_ent;
    char host_fn[MAX_FILE_NAME+3];

    if ((new_ent = malloc(sizeof(vdfs_entry)))) {
        init_entry(new_ent);
        memcpy(new_ent->acorn_fn, res->acorn_fn, MAX_FILE_NAME+1);
        new_ent->dfs_dir = res->dfs_dir;
        bbc2hst(res->acorn_fn, host_fn);
        if (host_search(dir, host_fn)) {
            /* host name already exists, generate a unique name */
            size_t name_len = strlen(host_fn);
            int seq_ch = '0';
            host_fn[name_len] = '~';
            host_fn[name_len+1] = seq_ch;
            host_fn[name_len+2] = 0;
            while (host_search(dir, host_fn)) {
                if ((seq_ch = next_seq_ch(seq_ch)) < 0) {
                    log_warn("vdfs: unable to create unique host name for %c.%s", res->dfs_dir, res->acorn_fn);
                    res->errmsg = err_badname;
                    free(new_ent);
                    return NULL;
                }
                host_fn[name_len+1] = seq_ch;
            }
            log_debug("vdfs: add_new_file: unique name %s used", host_fn);
        }
        new_ent->parent = dir;
        if (make_host_path(new_ent, host_fn)) {
            new_ent->next = dir->u.dir.children;
            dir->u.dir.children = new_ent;
            dir->u.dir.sorted = SORT_NONE;
            return new_ent;
        }
        free(new_ent);
    }
    log_warn("vdfs: out of memory adding new file");
    return NULL;
}

// Write changed attributes back to the .inf file.

static unsigned unix_date_acorn(const struct tm *tp)
{
    unsigned year = tp->tm_year + 1900 - 1981;
    unsigned adate = tp->tm_mday | ((tp->tm_mon + 1) << 8) | ((year & 0x70) << 1) | ((year & 0x0f) << 12);
    log_debug("vdfs: unix_date_acorn: local=%02u/%02u/%04u, ayear=%u, acorn_date=%04X", tp->tm_mday, tp->tm_mon+1, tp->tm_year + 1900, year, adate);
    return adate;
}

static unsigned unix_time_acorn(const struct tm *tp)
{
    unsigned atime = tp->tm_hour | (tp->tm_min << 8) | (tp->tm_sec << 16);
    log_debug("vdfs: unix_time_acorn: local=%02u:%02u:%02u, acorn_time=%06X", tp->tm_hour, tp->tm_min, tp->tm_sec, atime);
    return atime;
}

static void write_back(vdfs_entry *ent)
{
    FILE *fp;

    show_activity();
    *ent->host_inf = '.'; // select .inf file.
    if ((fp = fopen(ent->host_path, "wt"))) {
        const struct tm *tp = localtime(&ent->mtime);
        if (ent->attribs & ATTR_IS_DIR) {
            fprintf(fp, "%s OPT=%02X DIR=1 MDATE=%04X MTIME=%06X", ent->acorn_fn, ent->u.dir.boot_opt, unix_date_acorn(tp), unix_time_acorn(tp));
            if (ent->attribs & ATTR_BTIME_VALID) {
                tp = localtime(&ent->btime);
                fprintf(fp, " CDATE=%04X CTIME=%06X", unix_date_acorn(tp), unix_time_acorn(tp));
            }
            if (ent->u.dir.title[0]) {
                const char *fmt = " TITLE=%s\n";
                if (strpbrk(ent->u.dir.title, " \t"))
                    fmt = " TITLE=\"%s\"\n";
                fprintf(fp, fmt, ent->u.dir.title);
            }
            else
                putc('\n', fp);
        }
        else {
            fprintf(fp, "%c.%s %08X %08X %08X %02X %04X %06X", ent->dfs_dir, ent->acorn_fn, ent->u.file.load_addr, ent->u.file.exec_addr, ent->u.file.length, ent->attribs & ATTR_ACORN_MASK, unix_date_acorn(tp), unix_time_acorn(tp));
            if (ent->attribs & ATTR_BTIME_VALID) {
                tp = localtime(&ent->btime);
                fprintf(fp, " %04X %06X\n", unix_date_acorn(tp), unix_time_acorn(tp));
            }
            else
                putc('\n', fp);
        }
        fclose(fp);
    } else
        log_warn("vdfs: unable to create INF file '%s': %s", ent->host_path, strerror(errno));
    *ent->host_inf = '\0'; // select real file.
}

static void close_file(int channel)
{
    vdfs_entry *ent;
    FILE *fp;

    if ((ent = vdfs_chan[channel].ent)) {
        if ((fp = vdfs_chan[channel].fp)) {
            fclose(fp);
            vdfs_chan[channel].fp = NULL;
            scan_entry(ent);
        }
        ent->attribs &= ~(ATTR_OPEN_READ|ATTR_OPEN_WRITE);
        write_back(ent);
        vdfs_chan[channel].ent = NULL;
    }
}

static void close_all(void)
{
    int channel;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        close_file(channel);
}

void vdfs_close(void)
{
    void *ptr;

    close_all();
    if ((ptr = root_dir.host_path)) {
        free(ptr);
        root_dir.host_path = NULL;
    }
    if ((ptr = root_dir.u.dir.children)) {
        free_entry(ptr);
        root_dir.u.dir.children = NULL;
        root_dir.u.dir.sorted = SORT_NONE;
    }
}

void vdfs_set_root(const char *root)
{
    vdfs_entry new_root;
    init_entry(&new_root);
    new_root.parent = &new_root;
    size_t len = strlen(root);
    int ch;
    while (len > 0 && ((ch = root[--len]) == '/' || ch == '\\'))
        ;
    if (++len > 0) {
        char *path = malloc(len + 6);
        if (path) {
            memcpy(path, root, len);
            char *inf = path + len;
            new_root.host_path = path;
            new_root.host_inf = inf;
            *inf++ = '\0';
            *inf++ = 'i';
            *inf++ = 'n';
            *inf++ = 'f';
            *inf = '\0';
            new_root.acorn_fn[0] = '$';
            new_root.acorn_fn[1] = '\0';
            scan_entry(&new_root);
            if (new_root.attribs & ATTR_IS_DIR) {
                vdfs_close();
                root_dir = new_root;
                root_dir.parent = cur_dir.dir = prev_dir.dir = cat_dir = &root_dir;
                vdfs_findres res;
                lib_dir.dir = find_entry_adfs("Lib", &res, &cur_dir);
                scan_seq++;
                return;
            }
            log_error("vdfs: unable to set %s as root as it is not a valid directory", path);
            free(path);
        } else
            log_error("vdfs: unable to set %s as root as unable to allocate path", root);
    }
    else
        log_warn("vdfs: unable to set root as path is empty");
}

const char *vdfs_get_root(void)
{
    return root_dir.host_path;
}

/*
 * Functions concerned with saving and re-loading the state of VDFS
 * to the save state file.
 */

static vdfs_entry *ss_spec_path(FILE *f, const char *which)
{
    char *path;
    vdfs_entry *ent;
    vdfs_findres res;

    path = savestate_load_str(f);
    log_debug("vdfs: loadstate setting %s directory to $.%s", which, path);
    if ((ent = find_entry_adfs(path, &res, &cur_dir)))
        if (!(ent->attribs & ATTR_IS_DIR))
            ent = NULL;
    free(path);
    return ent;
}

static vdfs_entry *ss_load_dir(vdfs_entry *dir, FILE *f, const char *which)
{
    int ch = ch = getc(f);
    if (ch != EOF) {
        if (ch == 'N') {
            dir = NULL;
            log_debug("vdfs: loadstate %s directory set to undefined", which);
        }
        else if (ch == 'R') {
            dir = &root_dir;
            log_debug("vdfs: loadstate %s directory set to root", which);
        }
        else if (ch == 'C') {
            dir = cur_dir.dir;
            log_debug("vdfs: loadstate %s directory set to current", which);
        }
        else if (ch == 'S') {
            vdfs_entry *ent = ss_spec_path(f, which);
            if (ent)
                dir = ent;
        }
    }
    return dir;
}

void vdfs_loadstate(FILE *f)
{
    int ch;

    if ((ch = getc(f)) != EOF) {
        if (ch == 'V')
            vdfs_enabled = true;
        else if (ch == 'v')
            vdfs_enabled = false;
        cur_dir.dir = &root_dir;
        vdfs_entry *new_cdir = ss_load_dir(cur_dir.dir, f, "current");
        lib_dir.dir = ss_load_dir(lib_dir.dir, f, "library");
        prev_dir.dir = ss_load_dir(prev_dir.dir, f, "previous");
        cat_dir = ss_load_dir(cat_dir, f, "catalogue");
        cur_dir.dir = new_cdir;
    }
}

static size_t ss_calc_len(vdfs_entry *ent)
{
    vdfs_entry *parent = ent->parent;
    size_t len = strlen(ent->acorn_fn);

    if (parent->parent != parent)
        len += ss_calc_len(ent->parent) + 1;
    return len;
}

static void ss_save_ent(vdfs_entry *ent, FILE *f)
{
    vdfs_entry *parent = ent->parent;

    if (parent->parent == parent)
        fputs(ent->acorn_fn, f);
    else {
        ss_save_ent(ent->parent, f);
        putc('.', f);
        fputs(ent->acorn_fn, f);
    }
}

static void ss_save_dir1(vdfs_entry *ent, FILE *f)
{
    size_t len;

    if (!ent)
        putc('N', f);
    else if (ent == &root_dir)
        putc('R', f);
    else {
        putc('S', f);
        len = ss_calc_len(ent);
        savestate_save_var(len, f);
        ss_save_ent(ent, f);
    }
}

static void ss_save_dir2(vdfs_entry *ent, FILE *f)
{
    if (ent == cur_dir.dir)
        putc('C', f);
    else
        ss_save_dir1(ent, f);
}

void vdfs_savestate(FILE *f)
{
    putc(vdfs_enabled ? 'V' : 'v', f);
    ss_save_dir1(cur_dir.dir, f);
    ss_save_dir2(lib_dir.dir, f);
    ss_save_dir2(prev_dir.dir, f);
    ss_save_dir2(cat_dir, f);
}

/*
 * This is the execution part of the sideways RAM commands.
 */

static int16_t swr_calc_addr(uint8_t flags, uint32_t *st_ptr, int16_t romid)
{
    uint16_t start = *st_ptr;
    int banks;

    if (flags & 0x40) {
        // Pseudo addressing.  How many banks into the ram does the
        // address call for?

        banks = start / 16384;
        start %= 16384;

        // Find the nth RAM bank.

        if ((romid = mem_findswram(banks)) < 0)  {
            adfs_error(err_no_swr);
            return -1;
        }
        log_debug("vdfs: swr_calc_addr: pseudo addr bank=%02d, start=%04x", romid, start);
    } else {
        // Absolutre addressing.

        if (start < 0x8000 || start >= 0xc000) {
            adfs_error(err_badaddr);
            return -1;
        }

        if ((romid > 16) | !rom_slots[romid].swram) {
            adfs_error(err_no_swr);
            return -1;
        }
        log_debug("vdfs: swr_calc_addr: abs addr bank=%02d, start=%04x", romid, start);
        start -= 0x8000;
    }
    *st_ptr = start;
    return romid;
}

static void exec_swr_fs(uint8_t flags, uint16_t fname, int8_t romid, uint32_t start, uint16_t pblen)
{
    uint32_t load_add;
    int len;
    vdfs_entry *ent;
    FILE *fp;
    char path[MAX_ACORN_PATH];

    log_debug("vdfs: exec_swr_fs: flags=%02x, fn=%04x, romid=%02d, start=%04x, len=%04x", flags, fname, romid, start, pblen);
    if (check_valid_dir(&cur_dir)) {
        if ((romid = swr_calc_addr(flags, &start, romid)) >= 0) {
            vdfs_findres res;
            if (parse_name(path, sizeof path, fname)) {
                ent = find_entry(path, &res, &cur_dir);
                if (flags & 0x80) {
                    // read file into sideways RAM.
                    len = 0x4000 - start;
                    if (len > 0) {
                        if (ent && ent->attribs & ATTR_EXISTS) {
                            if (ent->attribs & ATTR_IS_DIR)
                                adfs_error(err_wont);
                            else if ((fp = fopen(ent->host_path, "rb"))) {
                                if (fread(rom + romid * 0x4000 + start, len, 1, fp) != 1 && ferror(fp))
                                    log_warn("vdfs: error reading file '%s': %s", ent->host_fn, strerror(errno));
                                fclose(fp);
                            } else {
                                log_warn("vdfs: unable to load file '%s': %s", ent->host_fn, strerror(errno));
                                adfs_hosterr(errno);
                            }
                        } else
                            adfs_error(res.errmsg);
                    } else
                        adfs_error(err_too_big);
                }
                else {
                    // write sideways RAM to file.
                    len = pblen;
                    if (len <= 16384) {
                        if (!ent && res.parent)
                            ent = add_new_file(&res);
                        if (ent) {
                            if ((fp = fopen(ent->host_path, "wb"))) {
                                fwrite(rom + romid * 0x4000 + start, len, 1, fp);
                                fclose(fp);
                                load_add = 0xff008000 | (romid << 16) | start;
                                ent->attribs |= ATTR_EXISTS;
                                scan_attr(ent);
                                ent->u.file.load_addr = load_add;
                                ent->u.file.exec_addr = load_add;
                                write_back(ent);
                            } else
                                log_warn("vdfs: unable to create file '%s': %s", ent->host_fn, strerror(errno));
                        } else
                            adfs_error(res.errmsg);
                    } else
                        adfs_error(err_too_big);
                }
            }
        }
    }
}

static void osword_swr_fs(void)
{
    uint16_t pb = (y << 8) | x;
    uint8_t flags  = readmem(pb);
    uint16_t fname = readmem16(pb+1);
    int8_t   romid = readmem(pb+3);
    uint32_t start = readmem16(pb+4);
    uint16_t pblen = readmem16(pb+6);

    exec_swr_fs(flags, fname, romid, start, pblen);
}

static void exec_swr_ram(uint8_t flags, uint32_t ram_start, uint16_t len, uint32_t sw_start, uint8_t romid)
{
    uint8_t *rom_ptr;
    int16_t nromid;

    log_debug("vdfs: exec_swr_ram: flags=%02x, ram_start=%04x, len=%04x, sw_start=%04x, romid=%02d\n", flags, ram_start, len, sw_start, romid);
    if ((nromid = swr_calc_addr(flags, &sw_start, romid)) >= 0) {
        rom_ptr = rom + romid * 0x4000 + sw_start;
        if (ram_start >= 0xffff0000 || curtube == -1) {
            if (flags & 0x80)
                while (len--)
                    *rom_ptr++ = readmem(ram_start++);
            else
                while (len--)
                    writemem(ram_start++, *rom_ptr++);
        }
        else {
            if (flags & 0x80)
                while (len--)
                    *rom_ptr++ = tube_readmem(ram_start++);
            else
                while (len--)
                    tube_writemem(ram_start++, *rom_ptr++);
        }
    }
}

static void osword_swr_ram(void)
{
    uint16_t pb = (y << 8) | x;
    uint8_t  flags     = readmem(pb);
    uint32_t ram_start = readmem32(pb+1);
    uint16_t len       = readmem16(pb+5);
    int8_t   romid     = readmem(pb+7);
    uint32_t sw_start  = readmem16(pb+4);

    exec_swr_ram(flags, ram_start, len, sw_start, romid);
}

/*
 * Command parsing part of the sideways RAM commands.
 */

static uint16_t srp_fn(uint16_t addr, uint16_t *vptr)
{
    int ch;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (ch != '\r') {
        *vptr = addr-1;
        do
            ch = readmem(addr++);
        while (ch != ' ' && ch != '\t' && ch != '\r');
        if (ch != '\r')
            writemem(addr-1, '\r');
        return addr;
    }
    return 0;
}

static uint16_t srp_hex(int ch, uint16_t addr, uint16_t *vptr)
{
    uint16_t value = 0;
    int nyb;

    if ((nyb = hex2nyb(ch)) >= 0) {
        value = nyb;
        while ((nyb = hex2nyb(readmem(addr++))) >= 0)
            value = value << 4 | nyb;
        *vptr = value;
        return --addr;
    }
    *vptr = 0;
    return 0;
}

static uint16_t srp_start(uint16_t addr, uint16_t *start)
{
    int ch;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');
    return srp_hex(ch, addr, start);
}

static uint16_t srp_length(uint16_t addr, uint16_t start, uint16_t *len)
{
    int ch;
    uint16_t end;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (ch == '+') {
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
        return srp_hex(ch, addr, len);
    } else {
        if ((addr = srp_hex(ch, addr, &end)) == 0)
            return 0;
        *len = end - start;
        return addr;
    }
}

static uint16_t srp_romid(uint16_t addr, int16_t *romid)
{
    int ch;
    uint16_t naddr;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if ((naddr = srp_hex(ch, addr, (uint16_t*)romid)))
        return naddr;
    if (ch >= 'W' && ch <= 'Z')
        *romid = mem_findswram(ch - 'W');
    else if (ch >= 'w' && ch <= 'z')
        *romid = mem_findswram(ch - 'w');
    else {
        *romid = -1;
        addr--;
    }
    return addr;
}

static bool srp_tail(uint16_t addr, uint8_t flag, uint16_t fnaddr, uint16_t start, uint16_t len)
{
    int ch;
    int16_t romid;

    addr = srp_romid(addr, &romid);
    if (romid >= 0)
        flag &= ~0x40;
    if (fs_flags & VDFS_ACTIVE) {
        exec_swr_fs(flag, fnaddr, romid, start, len);
        p.c = 0;
        return true;
    } else {
        uint16_t msw;
        p.c = 1;
        writemem(0x100, flag);
        writemem16(0x101, fnaddr);
        writemem(0x103, romid);
        writemem16(0x104, start);
        writemem16(0x106, len);
        writemem16(0x108, 0);
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
        if (ch == 'Q' || ch == 'q')
            msw = 0xffff;
        else
            msw = 0;
        writemem16(0x10a, msw);
        log_debug("vdfs: srp_tail executing via OSWORD, flag=%d, fnaddr=%04X, romid=%d, start=%04X, len=%d, msw=%02X", flag, fnaddr, romid, start, len, msw);
        a = 67;
        x = 0;
        y = 1;
        rom_dispatch(VDFS_ROM_OSW_TAIL);
        return false;
    }
}

static bool cmd_srload(uint16_t addr)
{
    uint16_t fnadd, start;

    if ((addr = srp_fn(addr, &fnadd))) {
        if ((addr = srp_start(addr, &start)))
            return srp_tail(addr, 0xC0, fnadd, start, 0);
    }
    adfs_error(err_badparms);
    return true;
}

static bool cmd_srsave(uint16_t addr)
{
    uint16_t fnadd, start, len;

    if ((addr = srp_fn(addr, &fnadd))) {
        if ((addr = srp_start(addr, &start))) {
            if ((addr = srp_length(addr, start, &len)))
                return srp_tail(addr, 0x40, fnadd, start, len);
        }
    }
    adfs_error(err_badparms);
    return true;
}

static void srcopy(uint16_t addr, uint8_t flags)
{
    uint16_t ram_start, len, sw_start;
    int16_t romid;

    if ((addr = srp_start(addr, &ram_start))) {
        if ((addr = srp_length(addr, ram_start, &len))) {
            if ((addr = srp_start(addr, &sw_start))) {
                addr = srp_romid(addr, &romid);
                if (romid >= 0)
                    flags &= ~0x40;
                else {
                    flags |= 0x40;
                    romid = 0;
                }
                exec_swr_ram(flags, ram_start, len, sw_start, romid);
                return;
            }
        }
    }
    adfs_error(err_badparms);
}

/*
 * OSFILE and supporting functions..
 */

static bool no_wildcards(const char *path)
{
    int ch;

    while ((ch = *path++)) {
        if (ch == '*' || ch == '#') {
            adfs_error(err_wildcard);
            return false;
        }
    }
    return true;
}

static void write_file_attr(uint32_t maddr, vdfs_entry *ent)
{
    uint32_t load_addr, exec_addr, length;

    if (ent->attribs & ATTR_IS_DIR)
        load_addr = exec_addr = length = 0;
    else {
        load_addr = ent->u.file.load_addr;
        exec_addr = ent->u.file.exec_addr;
        length = ent->u.file.length;
    }
    writemem32(maddr+0x02, load_addr);
    writemem32(maddr+0x06, exec_addr);
    writemem32(maddr+0x0a, length);
}

static void osfile_attribs(uint32_t pb, vdfs_entry *ent)
{
    write_file_attr(pb, ent);
    writemem(pb+0x0e, ent->attribs);
    writemem16(pb+0x0f, unix_date_acorn(localtime(&ent->mtime)));
    writemem(pb+0x11, 0);
}

static uint32_t write_bytes(FILE *fp, uint32_t addr, size_t bytes)
{
    char buffer[8192];

    if (addr >= 0xffff0000 || curtube == -1) {
        while (bytes >= sizeof buffer) {
            char *ptr = buffer;
            size_t chunk = sizeof buffer;
            while (chunk--)
                *ptr++ = readmem(addr++);
            fwrite(buffer, sizeof buffer, 1, fp);
            bytes -= sizeof buffer;
        }
        if (bytes > 0) {
            char *ptr = buffer;
            size_t chunk = bytes;
            while (chunk--)
                *ptr++ = readmem(addr++);
            fwrite(buffer, bytes, 1, fp);
        }
    }
    else {
        while (bytes >= sizeof buffer) {
            char *ptr = buffer;
            size_t chunk = sizeof buffer;
            while (chunk--)
                *ptr++ = tube_readmem(addr++);
            fwrite(buffer, sizeof buffer, 1, fp);
            bytes -= sizeof buffer;
        }
        if (bytes > 0) {
            char *ptr = buffer;
            size_t chunk = bytes;
            while (chunk--)
                *ptr++ = tube_readmem(addr++);
            fwrite(buffer, bytes, 1, fp);
        }
    }
    return addr;
}

static uint32_t cfile_callback(FILE *fp, uint32_t start_addr, size_t bytes)
{
    fseek(fp, bytes-1, SEEK_SET);
    putc(0, fp);
    return 0;
}

#define CAT_TMP 0x100
static vdfs_entry *cat_ent;
static int cat_dfs;

static uint16_t gcopy_fn(vdfs_entry *ent, uint16_t mem_ptr)
{
    uint16_t mem_end = mem_ptr + MAX_FILE_NAME+2;
    const char *ptr = ent->acorn_fn;
    int ch;

    if (ent->dfs_dir == cat_dfs) {
        log_debug("vdfs: gcopy_fn, same DFS directory file");
        writemem(mem_ptr++, ' ');
        writemem(mem_ptr++, ' ');
    }
    else {
        log_debug("vdfs: gcopy_fn, different DFS directory file");
        writemem(mem_ptr++, ent->dfs_dir);
        writemem(mem_ptr++, '.');
    }
    while (mem_ptr < mem_end && (ch = *ptr++))
        writemem(mem_ptr++, ch);
    while (mem_ptr < mem_end)
        writemem(mem_ptr++, ' ');
    return mem_ptr;
}

static void write_bcd_byte(uint32_t maddr, unsigned value)
{
    value = ((value / 10) << 4) | (value % 10);
    writemem(maddr, value);
}

static void write_bcd_word(uint32_t maddr, unsigned value)
{
    write_bcd_byte(maddr, value / 100);
    write_bcd_byte(maddr+1, value % 100);
}

static void gcopy_attr(vdfs_entry *ent)
{
    uint16_t mem_ptr = gcopy_fn(ent, CAT_TMP);
    writemem16(mem_ptr, ent->attribs);
    write_file_attr(mem_ptr, ent);
    const struct tm *tp = localtime(&ent->mtime);
    write_bcd_byte(mem_ptr+0x0e, tp->tm_mday);
    writemem(mem_ptr+0x0f, tp->tm_mon);
    write_bcd_word(mem_ptr+0x10, tp->tm_year + 1900);
    write_bcd_byte(mem_ptr+0x12, tp->tm_hour);
    write_bcd_byte(mem_ptr+0x13, tp->tm_min);
    write_bcd_byte(mem_ptr+0x14, tp->tm_sec);
}

static void osfile_write(uint32_t pb, const char *path, uint32_t (*callback)(FILE *fp, uint32_t addr, size_t bytes))
{
    vdfs_entry *ent;
    FILE *fp;
    uint32_t start_addr, end_addr;

    if (no_wildcards(path)) {
        vdfs_findres res;
        if ((ent = find_entry(path, &res, &cur_dir))) {
            if (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)) {
                log_debug("vdfs: attempt to save file %s which is already open via OSFIND", res.acorn_fn);
                adfs_error(err_isopen);
                return;
            }
            if (ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR) {
                    log_debug("vdfs: attempt to create file %s over an existing dir", res.acorn_fn);
                    adfs_error(err_direxist);
                    return;
                }
            }
        }
        else if (!res.parent) {
            log_debug("vdfs: osfile_write, no parent into which to create new file");
            adfs_error(res.errmsg);
            return;
        }
        else if (!(ent = add_new_file(&res))) {
            adfs_error(err_nomem);
            return;
        }
        if ((fp = fopen(ent->host_path, "wb"))) {
            ent->attribs = (ent->attribs & ~ATTR_IS_DIR) | ATTR_EXISTS;
            start_addr = readmem32(pb+0x0a);
            end_addr = readmem32(pb+0x0e);
            callback(fp, start_addr, end_addr - start_addr);
            fclose(fp);
            scan_attr(ent);
            ent->u.file.load_addr = readmem32(pb+0x02);
            ent->u.file.exec_addr = readmem32(pb+0x06);
            write_back(ent);
            writemem32(pb+0x0a, ent->u.file.length);
            writemem32(pb+0x0e, ent->attribs);
            a = 1;
            if (vdfs_opt1) {
                gcopy_attr(ent);
                rom_dispatch(VDFS_ROM_OPT1);
            }
        }
        else {
            int err = errno;
            log_warn("vdfs: unable to create file '%s': %s", ent->host_fn, strerror(err));
            adfs_hosterr(err);
        }
    }
}

#ifdef WIN32

static void time_unix2win(time_t secs, LPFILETIME td)
{
    /* Convert epoch, 01-Jan-1970 to 01-Jan-1601 */
    uint64_t wsec = secs + (uint64_t)((1970-1601)*365+92-3)*24*60*60;
    /* Convert units, seconds to 100ns */
    uint64_t ticks = wsec * 10000000;
    /* Split into Windows file time */
    td->dwLowDateTime = ticks & 0xffffffff;
    td->dwHighDateTime = ticks >> 32;
}

static void set_file_times(vdfs_entry *ent)
{
    HANDLE h = CreateFile(ent->host_path, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h) {
        FILETIME mtime, btime;
        time_unix2win(ent->mtime, &mtime);
        if (ent->attribs & ATTR_BTIME_VALID)
            time_unix2win(ent->btime, &btime);
        else {
            btime.dwLowDateTime = 0;
            btime.dwHighDateTime = 0;
        }
        if (!SetFileTime(h, &btime, NULL, &mtime))
            log_warn("unable to set mtime on %s: SetFileTime failed: %s", ent->host_path, strerror(errno));
    }
    else
        log_warn("unable to set mtime on %s: CreateFile failed: %s", ent->host_path, strerror(errno));
}

static void set_file_atribs(vdfs_entry *ent, uint16_t attr)
{
    DWORD wattr = GetFileAttributes(ent->host_fn);
    if (wattr != INVALID_FILE_ATTRIBUTES) {
        if (attr & ATTR_USER_LOCKD || !(attr & ATTR_USER_WRITE))
            wattr |= FILE_ATTRIBUTE_READONLY;
        else
            wattr &= ~FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributes(ent->host_fn, wattr))
            log_warn("vdfs: unable to set attributes, SetFileAttributes failed on %s: %s", ent->host_fn, strerror(errno));
    }
    else
        log_warn("vdfs: unable to set attributes, GetFileAttributes failed on %s: %s", ent->host_fn, strerror(errno));
}

#else

static void set_file_times(vdfs_entry *ent)
{
    struct timespec times[2];
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = ent->mtime;
    times[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, ent->host_path, times, 0) == -1)
        log_warn("vdfs: unable to set times on %s: %s", ent->host_path, strerror(errno));
}

static void set_file_atribs(vdfs_entry *ent, uint16_t attr)
{
    mode_t mode = 0;
    if (attr & ATTR_USER_READ)
        mode |= S_IRUSR;
    if (attr & ATTR_USER_WRITE)
        mode |= S_IWUSR;
    if (attr & ATTR_USER_EXEC)
        mode |= S_IXUSR;
    if (attr & ATTR_OTHR_READ)
        mode |= S_IRGRP|S_IROTH;
    if (attr & ATTR_OTHR_WRITE)
        mode |= S_IWGRP|S_IWOTH;
    if (attr & ATTR_OTHR_EXEC)
        mode |= S_IXGRP|S_IXOTH;
    log_debug("vdfs: chmod(%s, %o)", ent->host_path, mode);
    if (chmod(ent->host_path, mode) == -1)
        log_warn("vdfs: unable to chmod %s: %s", ent->host_path, strerror(errno));
}

#endif

static void osfile_set_meta(uint32_t pb, const char *path, uint16_t which)
{
    vdfs_entry *ent;
    vdfs_findres res;

    if ((ent = find_entry(path, &res, &cur_dir)) && ent->attribs & ATTR_EXISTS) {
        if (ent->attribs & ATTR_IS_DIR)
            a = 2;
        else {
            if (which & META_LOAD)
                ent->u.file.load_addr = readmem32(pb+0x02);
            if (which & META_EXEC)
                ent->u.file.exec_addr = readmem32(pb+0x06);
            if (which & META_ATTR) {
                uint16_t attr = readmem(pb+0x0e);
                log_debug("vdfs: setting attributes of %02X", attr);
                if (fs_flags & WRITE_DATES) {
                    uint16_t mdate = readmem16(pb+0x0f);
                    if (mdate) {
                        struct tm *tp = localtime(&ent->mtime);
                        acorn_date_unix(tp, mdate);
                        ent->mtime = mktime(tp);
                        set_file_times(ent);
                    }
                }
                if (attr != (ent->attribs & ATTR_ACORN_MASK)) {
                    set_file_atribs(ent, attr);
                    ent->attribs = (ent->attribs & ~ATTR_ACORN_MASK) | attr;
                }
            }
            write_back(ent);
            a = 1;
        }
    }
    else
        a = 0;
}

static void osfile_get_attr(uint32_t pb, const char *path)
{
    vdfs_entry *ent;
    vdfs_findres res;

    if ((ent = find_entry(path, &res, &cur_dir)) && ent->attribs & ATTR_EXISTS) {
        scan_entry(ent);
        osfile_attribs(pb, ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_get_extattr(uint32_t pb, const char *path)
{
    vdfs_entry *ent;
    vdfs_findres res;

    if ((ent = find_entry(path, &res, &cur_dir)) && ent->attribs & ATTR_EXISTS) {
        scan_entry(ent);
        writemem32(pb+2, 0);
        const struct tm *tp= localtime(&ent->mtime);
        writemem24(pb+6, unix_time_acorn(tp));
        if (ent->attribs & ATTR_BTIME_VALID) {
            tp = localtime(&ent->btime);
            writemem16(pb+9, unix_date_acorn(tp));
            writemem24(pb+11, unix_time_acorn(tp));
        }
        else {
            writemem16(pb+9, 0);
            writemem24(pb+11, 0);
        }
        writemem32(pb+14, 0);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_set_exattr(uint32_t pb, const char *path)
{
    if (fs_flags & WRITE_DATES) {
        vdfs_findres res;
        vdfs_entry *ent = find_entry(path, &res, &cur_dir);
        if (ent && ent->attribs & ATTR_EXISTS) {
            struct tm *tp = localtime(&ent->mtime);
            acorn_time_unix(tp, readmem24(pb+6));
            ent->mtime = mktime(tp);
            ent->btime = acorn_timedate_unix(readmem16(pb+9), readmem24(pb+11));
            ent->attribs |= ATTR_BTIME_VALID;
            write_back(ent);
            set_file_times(ent);
            a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
        }
        else
            a = 0;
    }
}

static void delete_inf(vdfs_entry *ent)
{
    *ent->host_inf = '.';
    if (unlink(ent->host_path) != 0 && errno != ENOENT)
        log_warn("vdfs: failed to delete 'INF file %s': %s", ent->host_path, strerror(errno));
    *ent->host_inf = '\0';
}

static void delete_file(vdfs_entry *ent)
{
    show_activity();
    if (ent->attribs & ATTR_IS_DIR) {
        if (ent == cur_dir.dir)
            adfs_error(err_delcsd);
        else if (ent == lib_dir.dir)
            adfs_error(err_dellib);
        else if (rmdir(ent->host_path) == 0) {
            if (ent == prev_dir.dir)
                prev_dir = cur_dir;
            ent->attribs &= ~(ATTR_IS_DIR|ATTR_EXISTS);
            delete_inf(ent);
            a = 2;
        } else
            adfs_hosterr(errno);
    } else {
        if (unlink(ent->host_path) == 0) {
            ent->attribs &= ~ATTR_EXISTS;
            delete_inf(ent);
            a = 1;
        } else
            adfs_hosterr(errno);
    }
}

static void osfile_delete(uint32_t pb, const char *path)
{
    if (no_wildcards(path)) {
        vdfs_entry *ent;
        vdfs_findres res;

        if ((ent = find_entry(path, &res, &cur_dir)) && ent->attribs & ATTR_EXISTS) {
            osfile_attribs(pb, ent);
            delete_file(ent);
        }
        else
            a = 0;
    }
}

static void create_dir(vdfs_entry *ent)
{
    int res;
#ifdef WIN32
    res = mkdir(ent->host_path);
#else
    res = mkdir(ent->host_path, 0777);
#endif
    if (res == 0) {
        ent->attribs |= ATTR_EXISTS;
        scan_attr(ent);
        a = 2;
    }
    else {
        adfs_hosterr(errno);
        log_debug("vdfs: unable to mkdir '%s': %s", ent->host_path, strerror(errno));
    }
}

static void osfile_cdir(const char *path)
{
    if (no_wildcards(path)) {
        vdfs_entry *ent;
        vdfs_findres res;
        if ((ent = find_entry(path, &res, &cur_dir))) {
            if (ent->attribs & ATTR_EXISTS) {
                if (!(ent->attribs & ATTR_IS_DIR)) {
                    log_debug("vdfs: attempt to create dir %s on top of an existing file", res.acorn_fn);
                    adfs_error(err_filexist);  // file in the way.
                }
            } else
                create_dir(ent);
        } else {
            vdfs_entry *parent = res.parent;
            if (parent && parent->attribs & ATTR_EXISTS) {
                if ((ent = add_new_file(&res))) {
                    ent->u.dir.boot_opt = 0;
                    ent->u.dir.title[0] = 0;
                    create_dir(ent);
                }
            } else {
                log_debug("vdfs: attempt to create dir %s in non-existent directory", res.acorn_fn);
                adfs_error(res.errmsg);
            }
        }
    }
}

static void update_length(vdfs_entry *ent, uint32_t length)
{
    if (length != ent->u.file.length) {
        ent->u.file.length = length;
        write_back(ent);
    }
}

static void read_file_io(vdfs_entry *ent, FILE *fp, uint32_t addr)
{
    char buffer[32768];
    size_t nbytes;
    uint32_t dest = addr;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--)
            writemem(dest++, *ptr++);
    }
    update_length(ent, dest - addr);
}

static void read_file_tube(vdfs_entry *ent, FILE *fp, uint32_t addr)
{
    char buffer[32768];
    size_t nbytes;
    uint32_t dest = addr;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--)
            tube_writemem(dest++, *ptr++);
    }
    update_length(ent, dest - addr);
}

static void osfile_load(uint32_t pb, const char *path)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, &cur_dir);
    if (ent && ent->attribs & ATTR_EXISTS) {
        if (ent->attribs & ATTR_IS_DIR)
            adfs_error(err_wont);
        else {
            FILE *fp = fopen(ent->host_path, "rb");
            if (fp) {
                uint32_t addr;
                show_activity();
                if (readmem(pb+0x06) == 0)
                    addr = readmem32(pb+0x02);
                else
                    addr = ent->u.file.load_addr;
                if (addr >= 0xffff0000 || curtube == -1)
                    read_file_io(ent, fp, addr);
                else
                    read_file_tube(ent, fp, addr);
                fclose(fp);
                osfile_attribs(pb, ent);
                a = 1;
                if (vdfs_opt1) {
                    gcopy_attr(ent);
                    rom_dispatch(VDFS_ROM_OPT1);
                }
            } else {
                log_warn("vdfs: unable to load file '%s': %s", ent->host_fn, strerror(errno));
                adfs_hosterr(errno);
            }
        }
    } else
        adfs_error(res.errmsg);
}

static void osfile(void)
{
    uint32_t pb = (y << 8) | x;
    char path[MAX_ACORN_PATH];

    if (a <= 0x08 || a == 0xff || a == 0xfd) {
        log_debug("vdfs: osfile(A=%02X, X=%02X, Y=%02X)", a, x, y);
        if (check_valid_dir(&cur_dir)) {
            if (parse_name(path, sizeof path, readmem16(pb))) {
                switch (a) {
                    case 0x00:  // save file.
                        osfile_write(pb, path, write_bytes);
                        break;
                    case 0x01:  // set all attributes.
                        osfile_set_meta(pb, path, META_LOAD|META_EXEC|META_ATTR);
                        break;
                    case 0x02:  // set load address only.
                        osfile_set_meta(pb, path, META_LOAD);
                        break;
                    case 0x03:  // set exec address only.
                        osfile_set_meta(pb, path, META_EXEC);
                        break;
                    case 0x04:  // write attributes.
                        osfile_set_meta(pb, path, META_ATTR);
                        break;
                    case 0x05:  // get addresses and attributes.
                        osfile_get_attr(pb, path);
                        break;
                    case 0x06:
                        osfile_delete(pb, path);
                        break;
                    case 0x07:
                        osfile_write(pb, path, cfile_callback);
                        break;
                    case 0x08:
                        osfile_cdir(path);
                        break;
                    case 0xfc:
                        osfile_set_exattr(pb, path);
                        break;
                    case 0xfd:
                        osfile_get_extattr(pb, path);
                        break;
                    case 0xff:  // load file.
                        osfile_load(pb, path);
                        break;
                }
            }
        }
    } else
        log_debug("vdfs: osfile(A=%02X, X=%02X, Y=%02X): not implemented", a, x, y);
}

/*
 * Functions concerned with byte I/O to files.  This includes the
 * OSBGET, OSBPUT, OSGBPB and OSFIND calls.
 */

static FILE *getfp_read(int channel)
{
    int chan = channel - MIN_CHANNEL;
    FILE *fp;

    if (chan >= 0 && chan < NUM_CHANNELS) {
        if ((fp = vdfs_chan[chan].fp))
            return fp;
        log_debug("vdfs: attempt to use closed channel %d", channel);
    } else
        log_debug("vdfs: channel %d out of range", channel);
    adfs_error(err_channel);
    return NULL;
}

static void osbget(void)
{
    int ch;
    FILE *fp;

    log_debug("vdfs: osbget(A=%02X, X=%02X, Y=%02X)", a, x, y);
    show_activity();
    if ((fp = getfp_read(y))) {
        if ((ch = getc(fp)) != EOF) {
            a = ch;
            p.c = 0;
            return;
        }
    }
    a = 0xfe;
    p.c = 1;
}

static FILE *getfp_write(int channel)
{
    int chan = channel - MIN_CHANNEL;
    vdfs_open_file *p;
    vdfs_entry *ent;
    FILE *fp = NULL;

    if (chan >= 0 && chan < NUM_CHANNELS) {
        p = &vdfs_chan[chan];
        if ((ent = p->ent)) {
            if (ent->attribs & ATTR_OPEN_WRITE) {
                if (!(fp = p->fp)) {
                    log_debug("vdfs: attempt to use write to channel %d not open for write", channel);
                    adfs_error(err_channel);
                }
            } else {
                log_debug("vdfs: attempt to write to a read-only channel %d", channel);
                adfs_error(err_nupdate);
            }
        } else {
            log_debug("vdfs: attempt to use closed channel %d", channel);
            adfs_error(err_channel);
        }
    } else {
        log_debug("vdfs: channel %d out of range\n", channel);
        adfs_error(err_channel);
    }
    return fp;
}

static void osbput(void)
{
    FILE *fp;

    log_debug("vdfs: osbput(A=%02X, X=%02X, Y=%02X)", a, x, y);

    show_activity();
    if ((fp = getfp_write(y)))
        putc(a, fp);
}

static void osfind(void)
{
    int acorn_mode, channel;
    vdfs_entry *ent;
    vdfs_findres res;
    const char *mode;
    uint16_t attribs;
    FILE *fp;
    char path[MAX_ACORN_PATH];

    log_debug("vdfs: osfind(A=%02X, X=%02X, Y=%02X)", a, x, y);

    if (a == 0) {   // close file.
        channel = y;
        if (channel == 0)
            rom_dispatch(VDFS_ROM_CLOSEALL);
        else {
            channel -= MIN_CHANNEL;
            if (channel >= 0 && channel < NUM_CHANNELS)
                close_file(channel);
            else
                adfs_error(err_channel);
        }
    }
    else if (check_valid_dir(&cur_dir)) {        // open file.
        mode = NULL;
        acorn_mode = a;
        a = 0;
        channel = -1;
        do {
            if (++channel >= NUM_CHANNELS) {
                log_debug("vdfs: no free channel");
                adfs_error(err_nfile);
                return;
            }
        } while (vdfs_chan[channel].ent);
        if (parse_name(path, sizeof path, (y << 8) | x)) {
            ent = find_entry(path, &res, &cur_dir);
            if (ent) {
                if ((ent->attribs & (ATTR_EXISTS|ATTR_IS_DIR)) == (ATTR_EXISTS|ATTR_IS_DIR)) {
                    vdfs_chan[channel].ent = ent;  // make "half-open"
                    a = MIN_CHANNEL + channel;
                    return;
                }
            }
            else if (!res.parent) {
                adfs_error(res.errmsg);
                return;
            }
            if (acorn_mode == 0x40) {
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_OPEN_WRITE)
                        adfs_error(err_isopen);
                    else {
                        mode = "rb";
                        attribs = ATTR_OPEN_READ;
                    }
                }
            }
            else if (acorn_mode == 0x80) {
                if (!no_wildcards(path))
                    return;
                if (ent && (ent->attribs & ATTR_EXISTS) && (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)))
                    adfs_error(err_isopen);
                else {
                    mode = "wb";
                    attribs = ATTR_OPEN_WRITE;
                    if (!ent && res.parent)
                        ent = add_new_file(&res);
                }
            }
            else if (acorn_mode == 0xc0) {
                attribs = ATTR_OPEN_READ|ATTR_OPEN_WRITE;
                if (ent) {
                    if (ent->attribs & ATTR_EXISTS) {
                        if (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE))
                            adfs_error(err_isopen);
                        else
                            mode = "rb+";
                    }
                    else
                        mode = "wb+";
                } else {
                    if (!no_wildcards(path))
                        return;
                    if (res.parent)
                        ent = add_new_file(&res);
                    mode = "wb+";
                }
            }
            if (mode && ent) {
                log_debug("vdfs: osfind open host file %s in mode %s", ent->host_path, mode);
                if ((fp = fopen(ent->host_path, mode))) {
                    show_activity();
                    ent->attribs |= attribs | ATTR_EXISTS; // file now exists.
                    scan_entry(ent);
                    vdfs_chan[channel].fp = fp;
                    vdfs_chan[channel].ent = ent;
                    a = MIN_CHANNEL + channel;
                } else
                    log_warn("vdfs: osfind: unable to open file '%s' in mode '%s': %s", ent->host_fn, mode, strerror(errno));
            }
        }
    }
    log_debug("vdfs: osfind returns a=%d", a);
}

static void osgbpb_write(uint32_t pb)
{
    FILE *fp;
    uint32_t mem_ptr;
    size_t bytes;

    if ((fp = getfp_write(readmem(pb)))) {
        show_activity();
        if (a == 0x01)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        mem_ptr = readmem32(pb+1);
        bytes = readmem32(pb+5);
        mem_ptr = write_bytes(fp, mem_ptr, bytes);
        writemem32(pb+1, mem_ptr);
        writemem32(pb+5, 0);
        writemem32(pb+9, ftell(fp));
    }
}

static size_t read_bytes(FILE *fp, uint32_t addr, size_t bytes)
{
    char buffer[8192];
    size_t nbytes;

    while (bytes >= sizeof buffer) {
        char *ptr = buffer;
        if ((nbytes = fread(buffer, 1, sizeof buffer, fp)) <= 0)
            return bytes;
        bytes -= nbytes;
        if (addr >= 0xffff0000 || curtube == -1) {
            while (nbytes--)
                writemem(addr++, *ptr++);
        }
        else {
            while (nbytes--)
                tube_writemem(addr++, *ptr++);
        }
    }
    while (bytes > 0) {
        char *ptr = buffer;
        if ((nbytes = fread(buffer, 1, bytes, fp)) <= 0)
            return bytes;
        bytes -= nbytes;
        if (addr >= 0xffff0000 || curtube == -1) {
            while (nbytes--)
                writemem(addr++, *ptr++);
        }
        else {
            while (nbytes--)
                tube_writemem(addr++, *ptr++);
        }
    }
    return 0;
}

static int osgbpb_read(uint32_t pb)
{
    FILE *fp;
    uint32_t mem_ptr;
    size_t bytes;
    size_t undone = 0;

    if ((fp = getfp_read(readmem(pb)))) {
        show_activity();
        if (a == 0x03)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        mem_ptr = readmem32(pb+1);
        bytes = readmem32(pb+5);
        undone = read_bytes(fp, mem_ptr, bytes);
        writemem32(pb+1, mem_ptr + bytes - undone);
        writemem32(pb+5, undone);
        writemem32(pb+9, ftell(fp));
    }
    return undone;
}

static uint32_t write_len_str(uint32_t mem_ptr, const char *str, size_t len)
{
    if (mem_ptr >= 0xffff0000 || curtube == -1) {
        writemem(mem_ptr++, len);
        for (const char *ptr = str, *end = str + len; ptr < end; )
            writemem(mem_ptr++, *ptr++);
    }
    else {
        tube_writemem(mem_ptr++, len);
        for (const char *ptr = str, *end = str + len; ptr < end; )
            tube_writemem(mem_ptr++, *ptr++);
    }
    return mem_ptr;
}

static void osgbpb_get_title(uint32_t pb)
{
    if (check_valid_dir(&cur_dir)) {
        uint32_t mem_ptr = readmem32(pb+1);
        const char *title = cur_dir.dir->u.dir.title;
        if (!*title)
            title = cur_dir.dir->acorn_fn;
        mem_ptr = write_len_str(mem_ptr, title, strlen(title));
        if (mem_ptr >= 0xffff0000 || curtube == -1)
            writemem(mem_ptr++, cur_dir.dir->u.dir.boot_opt);
        else
            tube_writemem(mem_ptr++, cur_dir.dir->u.dir.boot_opt);
    }
}

static void osgbpb_get_dir_adfs(uint32_t pb, vdfs_dirlib *dir)
{
    uint32_t mem_ptr = readmem32(pb+1);
    char tmp[2];
    tmp[0] = '0' + dir->drive;
    tmp[1] = 0;
    mem_ptr = write_len_str(mem_ptr, tmp, 1);
    vdfs_entry *ent = dir->dir;
    if (ent)
        write_len_str(mem_ptr, ent->acorn_fn, strlen(ent->acorn_fn));
    else
        write_len_str(mem_ptr, "Unset", 5);
}

static void osgbpb_get_dir_dfs(uint32_t pb, vdfs_dirlib *dir)
{
    uint32_t mem_ptr = readmem32(pb+1);
    char tmp[2];
    tmp[0] = '0' + dir->drive;
    tmp[1] = 0;
    mem_ptr = write_len_str(mem_ptr, tmp, 1);
    tmp[0] = dir->dfs_dir;
    write_len_str(mem_ptr, tmp, 1);
}

static void acorn_sort(vdfs_entry *dir, sort_type sort_reqd)
{
    vdfs_entry *list = dir->u.dir.children;
    if (list) {
        if (dir->u.dir.sorted != sort_reqd) {
            /* Linked List sort from Simon Tatum */
            int insize = 1;
            while (1) {
                int nmerges = 0;
                vdfs_entry *p = list;
                vdfs_entry *tail = list = NULL;

                while (p) {
                    vdfs_entry *q = p;
                    int psize= 0., qsize;
                    nmerges++; /* there exists a merge to be done */
                    /* step `insize' places along from p */
                    do {
                        if (psize >= insize)
                            break;
                        psize++;
                        q = q->next;
                    } while (q);
                    /* if q hasn't fallen off end, we have two lists to merge */
                    qsize = insize;

                    /* now we have two lists; merge them */
                    while (psize > 0 || (qsize > 0 && q)) {
                        vdfs_entry *e;
                        /* decide whether next element of merge comes from p or q */
                        if (psize == 0) {
                            /* p is empty; e must come from q. */
                            e = q;
                            q = q->next;
                            qsize--;
                        }
                        else if (qsize == 0 || !q) {
                            /* q is empty; e must come from p. */
                            e = p;
                            p = p->next;
                            psize--;
                        }
                        else {
                            int res = (sort_reqd == SORT_DFS) ? p->dfs_dir - q->dfs_dir : 0;
                            if (!res)
                                res = vdfs_cmp(p->acorn_fn, q->acorn_fn, MAX_FILE_NAME);
                            if (res <= 0) {
                                /* First element of p is lower (or same);
                                * e must come from p. */
                                e = p;
                                p = p->next;
                                psize--;
                            }
                            else {
                                /* First element of q is lower; e must come from q. */
                                e = q;
                                q = q->next;
                                qsize--;
                            }
                        }
                        /* add the next element to the merged list */
                        if (tail)
                            tail->next = e;
                        else
                            list = e;
                        tail = e;
                    }
                    /* now p has stepped `insize' places along, and q has too */
                    p = q;
                }
                tail->next = NULL;

                /* If we have done only one merge, we're finished. */
                if (nmerges <= 1) {  /* allow for nmerges==0, the empty list case */
                    dir->u.dir.children = list;
                    dir->u.dir.sorted = sort_reqd;
                    return;
                }
                /* Otherwise repeat, merging lists twice the size */
                insize *= 2;
            }
        }
    }
}

static int osgbpb_list(uint32_t pb)
{
    uint32_t seq_ptr, mem_ptr, n;
    vdfs_entry *cat_ptr;
    int status;

    if (check_valid_dir(&cur_dir)) {
        seq_ptr = readmem32(pb+9);
        if (seq_ptr == 0)
            acorn_sort(cur_dir.dir, (fs_flags & DFS_MODE) ? SORT_DFS : SORT_ADFS);
        n = seq_ptr;
        for (cat_ptr = cur_dir.dir->u.dir.children; cat_ptr; cat_ptr = cat_ptr->next)
            if (cat_ptr->attribs & ATTR_EXISTS)
                if (!(fs_flags & DFS_MODE) || cat_ptr->dfs_dir == cur_dir.dfs_dir)
                    if (n-- == 0)
                        break;
        if (cat_ptr) {
            status = 0;
            mem_ptr = readmem32(pb+1);
            n = readmem32(pb+5);
            log_debug("vdfs: seq_ptr=%d, writing max %d entries starting %04X, first=%s", seq_ptr, n, mem_ptr, cat_ptr->acorn_fn);
            for (;;) {
                mem_ptr = write_len_str(mem_ptr, cat_ptr->acorn_fn, strlen(cat_ptr->acorn_fn));
                seq_ptr++;
                if (--n == 0)
                    break;
                do
                    cat_ptr = cat_ptr->next;
                while (cat_ptr && !(cat_ptr->attribs & ATTR_EXISTS) && (fs_flags & DFS_MODE) && cat_ptr->dfs_dir != cur_dir.dfs_dir);
                if (!cat_ptr) {
                    status = 1;
                    break;
                }
                log_debug("vdfs: next=%s", cat_ptr->acorn_fn);
            }
            log_debug("vdfs: finish at %04X", mem_ptr);
            writemem32(pb+5, n);
            writemem32(pb+9, seq_ptr);
            return status;
        }
    }
    return 1;
}

static void osgbpb(void)
{
    int status = 0;
    uint32_t pb = (y << 8) | x;

    log_debug("vdfs: osgbpb(A=%02X, YX=%04X)", a, pb);

    switch (a)
    {
        case 0x01: // write multiple bytes to file.
        case 0x02:
            osgbpb_write(pb);
            break;

        case 0x03: // read multiple bytes from file.
        case 0x04:
            status = osgbpb_read(pb);
            break;

        case 0x05: // get current dir title etc.
            osgbpb_get_title(pb);
            break;

        case 0x06: // get current dir
            osgbpb_get_dir(pb, &cur_dir);
            break;

        case 0x07: // get library dir.
            osgbpb_get_dir(pb, &lib_dir);
            break;

        case 0x08: // list files in current directory.
            status = osgbpb_list(pb);
            break;

        default:
            log_debug("vdfs: osgbpb unimplemented for a=%d", a);
    }
    p.c = status;
}

/*
 * OSARGS
 */

static void osargs(void)
{
    FILE *fp;
    long temp;

    log_debug("vdfs: osargs(A=%02X, X=%02X, Y=%02X)", a, x, y);

    if (y == 0) {
        switch (a)
        {
            case 0:
                a = fs_num; // say this filing selected.
                break;
            case 1:
                writemem32(x, cmd_tail);
                break;
            case 0xff:
                flush_all();
                break;
            default:
                log_debug("vdfs: osargs not implemented for y=0, a=%d", a);
        }
    } else if ((fp = getfp_read(y))) {
        switch (a)
        {
            case 0:     // read sequential pointer
                writemem32(x, ftell(fp));
                break;
            case 1:     // write sequential pointer
                fseek(fp, readmem32(x), SEEK_SET);
                break;
            case 2:     // read file size (extent)
                temp = ftell(fp);
                fseek(fp, 0, SEEK_END);
                writemem32(x, ftell(fp));
                fseek(fp, temp, SEEK_SET);
                break;
            case 0xff:  // write any cache to media.
                fflush(fp);
                break;
            default:
                log_debug("vdfs: osargs: unrecognised function code a=%d for channel y=%d", a, y);
        }
    }
}

/*
 * Commands.  The next logical call to tackle is OSFSC but as this
 * includes the call by which internal filing system commands are
 * dispatched the commands need to come first.
 */

static void cmd_access(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        char path[MAX_ACORN_PATH];
        if ((addr = parse_name(path, sizeof path, addr))) {
            uint_least32_t attribs = 0;
            uint_least32_t attr_mask = ATTR_USER_READ|ATTR_USER_WRITE|ATTR_USER_LOCKD|ATTR_USER_EXEC;
            int ch = readmem(addr++);
            while (ch == ' ' || ch == '\t')
                ch = readmem(addr++);
            while (ch != ' ' && ch != '\t' && ch != '\r' && ch != '/') {
                if (ch == 'R' || ch == 'r')
                    attribs |= ATTR_USER_READ;
                else if (ch == 'W' || ch == 'w')
                    attribs |= ATTR_USER_WRITE;
                else if (ch == 'L' || ch == 'l')
                    attribs |= ATTR_USER_LOCKD;
                else if (ch == 'E' || ch == 'e')
                    attribs |= ATTR_USER_EXEC;
                else {
                    adfs_error(err_badparms);
                    return;
                }
                ch = readmem(addr++);
            }
            if (ch == '/') {
                attr_mask |= ATTR_OTHR_READ|ATTR_OTHR_WRITE|ATTR_OTHR_LOCKD|ATTR_OTHR_EXEC;
                ch = readmem(addr++);
                while (ch != ' ' && ch != '\t' && ch != '\r') {
                    if (ch == 'R' || ch == 'r')
                        attribs |= ATTR_OTHR_READ;
                    else if (ch == 'W' || ch == 'w')
                        attribs |= ATTR_OPEN_WRITE;
                    else if (ch == 'L' || ch == 'l')
                        attribs |= ATTR_OTHR_LOCKD;
                    else if (ch == 'E' || ch == 'e')
                        attribs |= ATTR_OTHR_EXEC;
                    else {
                        adfs_error(err_badparms);
                        return;
                    }
                    ch = readmem(addr++);
                }
            }
            vdfs_findres res;
            vdfs_entry *ent = find_entry(path, &res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                attr_mask = ~attr_mask;
                if (fs_flags & DFS_MODE) {
                    attribs |= ATTR_USER_READ;
                    if (attribs & ATTR_OTHR_LOCKD)
                        attribs &= ~ATTR_USER_WRITE;
                    do {
                        ent->attribs = (ent->attribs & attr_mask) | attribs;
                        write_back(ent);
                        ent = find_next_dfs(ent, &res);
                    } while (ent);
                }
                else {
                    do {
                        ent->attribs = (ent->attribs & attr_mask) | attribs;
                        write_back(ent);
                        ent = find_next_adfs(ent, &res);
                    } while (ent);
                }
            }
        }
    }
}

static void cmd_back(void)
{
    vdfs_dirlib tmp = cur_dir;
    cur_dir = prev_dir;
    prev_dir = tmp;
}

static void cmd_cdir(uint16_t addr)
{
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(&cur_dir))
        if (parse_name(path, sizeof path, addr))
            osfile_cdir(path);
}

#ifdef WIN32
#include <winbase.h>

static bool copy_file(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    BOOL res = CopyFile(old_ent->host_path, new_ent->host_path, FALSE);
    if (!res)
        adfs_hosterr(errno);
    return res;
}

#else
#ifdef __unix__

static bool copy_loop(const char *old_fn, int old_fd, const char *new_fn, int new_fd)
{
    char buf[4096];
    ssize_t bytes = read(old_fd, buf, sizeof(buf));
    while (bytes > 0) {
        if (write(new_fd, buf, bytes) != bytes) {
            int err = errno;
            adfs_hosterr(err);
            log_warn("vdfs: error writing %s: %s", new_fn, strerror(err));
            return false;
        }
    }
    if (bytes < 0) {
        int err = errno;
        adfs_hosterr(err);
        log_warn("vdfs: error reading %s: %s", old_fn, strerror(err));
        return false;
    }
    return true;
}

static bool copy_file(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    bool res = false;
    int old_fd = open(old_ent->host_path, O_RDONLY);
    if (old_fd >= 0) {
        int new_fd = open(new_ent->host_path, O_WRONLY|O_CREAT, 0666);
        if (new_fd >= 0) {
#ifdef linux
            struct stat stb;
            if (!fstat(old_fd, &stb)) {
                ssize_t bytes = copy_file_range(old_fd, NULL, new_fd, NULL, stb.st_size, 0);
                if (bytes == stb.st_size)
                    res = true;
                else if (bytes == 0 && errno == EXDEV)
                    res = copy_loop(old_ent->host_path, old_fd, new_ent->host_path, new_fd);
                else {
                    int err = errno;
                    adfs_hosterr(err);
                    log_warn("vdfs: copy_file_range failed %s to %s: %s", old_ent->host_path, new_ent->host_path, strerror(err));
                }
            }
            else {
                int err = errno;
                adfs_hosterr(err);
                log_warn("vdfs: unable to fstat '%s': %s", old_ent->host_path, strerror(err));
            }
#else
            res = copy_loop(old_ent->host_path, old_fd, new_ent->host_path, new_fd);
#endif
            close(new_fd);
        }
        else {
            int err = errno;
            adfs_hosterr(err);
            log_warn("vdfs: unable to open %s for writing: %s", new_ent->host_path, strerror(err));
        }
        close(old_fd);
    }
    else {
        int err = errno;
        adfs_hosterr(err);
        log_warn("vdfs: unable to open %s for reading: %s", old_ent->host_path, strerror(err));
    }
    return res;
}

#else

static bool copy_file(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    bool res = false;
    int old_fp = fopen(old_ent->host_path, "rb");
    if (old_fp) {
        int new_fp = fopen(new_ent->host_path, "wb");
        if (new_fp) {
            res = true;
            int ch = getc(old_fp);
            while (ch != EOF && putc(ch, new_fp) != EOF)
                ch = getc(old_fp);
            if (ferror(old_fp)) {
                int err = errno;
                adfs_hosterr(err);
                log_warn("vdfs: read error on %s: %s", old_ent->host_path, strerror(err));
                res = false;
            }
            if (ferror(new_fp)) {
                int err = errno;
                adfs_hosterr(err);
                log_warn("vdfs: write error on %s: %s", new_ent->host_path, strerror(err));
                res = false;
            }
            if (fclose(new_fp)) {
                int err = errno;
                adfs_hosterr(err);
                log_warn("vdfs: write error on %s: %s", new_ent->host_path, strerror(err));
                res = false;
            }
        }
        else {
            int err = errno;
            adfs_hosterr(err);
            log_warn("vdfs: unable to open %s for writing: %s", new_ent->host_path, strerror(err));
        }
        fclose(old_fp);
    }
    else {
        int err = errno;
        adfs_hosterr(err);
        log_warn("vdfs: unable to open %s for reading: %s", old_ent->host_path, strerror(err));
    }
    return res;
}

#endif
#endif

static void cmd_copy(uint16_t addr)
{
    log_debug("vdfs: cmd_copy, addr=%04X", addr);
    if (check_valid_dir(&cur_dir)) {
        char old_path[MAX_ACORN_PATH], new_path[MAX_ACORN_PATH];
        if ((addr = parse_name(old_path, sizeof old_path, addr))) {
            if (parse_name(new_path, sizeof new_path, addr)) {
                if (*old_path && *new_path) {
                    vdfs_findres old_res;
                    vdfs_entry *old_ent = find_entry(old_path, &old_res, &cur_dir);
                    if (old_ent && (old_ent->attribs & ATTR_EXISTS)) {
                        vdfs_findres new_res;
                        vdfs_entry *new_ent = find_entry(new_path, &new_res, &cur_dir);
                        if (new_ent) {
                            if ((new_ent->attribs & (ATTR_EXISTS|ATTR_IS_DIR)) == (ATTR_EXISTS|ATTR_IS_DIR)) {
                                /* as destination is a dir, loop over wild-cards */
                                new_res.parent = new_ent;
                                do {
                                    memcpy(new_res.acorn_fn, old_ent->acorn_fn, MAX_FILE_NAME);
                                    if (!(new_ent = add_new_file(&new_res)))
                                        break;
                                    if (!copy_file(old_ent, new_ent))
                                        break;
                                } while ((old_ent = find_next(old_ent, &old_res)));
                            }
                            else {
                                /* destination is a file */
                                if (find_next(old_ent, &old_res))
                                    adfs_error(err_wildcard);
                                else
                                    copy_file(old_ent, new_ent);
                            }
                        }
                        else if (find_next(old_ent, &old_res))
                            adfs_error(err_wildcard);
                        else if ((new_ent = add_new_file(&new_res)) && copy_file(old_ent, new_ent))
                            new_ent->attribs |= ATTR_EXISTS;
                    }
                    else
                        adfs_error(old_res.errmsg);
                }
                else {
                    log_debug("vdfs: copy attempted with an empty filename");
                    adfs_error(err_badcopy);
                }
            }
        }
    }
}

static void cmd_delete(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            if (!no_wildcards(path))
                return;
            if ((ent = find_entry(path, &res, &cur_dir))) {
                delete_file(ent);
                return;
            }
            adfs_error(res.errmsg);
        }
    }
}

static vdfs_entry *lookup_dir(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            ent = find_entry(path, &res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    return ent;
                else
                    adfs_error(err_notdir);
            } else
                adfs_error(res.errmsg);
        }
    }
    return NULL;
}

static vdfs_entry *parse_adfs_dir(uint16_t addr, int *drive)
{
    int ch = readmem(addr);
    if (ch == ':') {
        ch = readmem(++addr);
        if (ch < '0' || ch > '7') {
            log_debug("vdfs: parse_adfs_dir, bad drive number");
            adfs_error(err_badparms);
            return NULL;
        }
        *drive = ch - '0';
        ch = readmem(++addr);
        if (ch != '.') {
            if (ch == '\r')
                return &root_dir;
            log_debug("vdfs: parse_adfs_dir, missing dot");
            adfs_error(err_badparms);
            return NULL;
        }
        ++addr;
    }
    return lookup_dir(addr);
}

static void cmd_dir_adfs(uint16_t addr)
{
    int drive = cur_dir.drive;
    vdfs_entry *ent = parse_adfs_dir(addr, &drive);
    if (ent) {
        prev_dir = cur_dir;
        cur_dir.drive = drive;
        cur_dir.dir = ent;
    }
}

static void cmd_lib_adfs(uint16_t addr)
{
    int drive = lib_dir.drive;
    vdfs_entry *ent = lookup_dir(addr);
    if (ent) {
        lib_dir.drive = drive;
        lib_dir.dir = ent;
    }
}

static void parse_dfs_dir(uint16_t addr, vdfs_dirlib *dir)
{
    int drive = dir->drive;
    int ch = readmem(addr++);
    if (ch == ':') {
        ch = readmem(addr++);
        if (ch < '0' || ch > '3') {
            adfs_error(err_badparms);
            return;
        }
        drive = ch - '0';
        ch = readmem(addr++);
        if (ch == '.')
            ch = readmem(addr++);
        else {
            while (ch == ' ' || ch == '\t')
                ch = readmem(addr++);
            if (ch == '\r')
                dir->drive = drive;
            else
                adfs_error(err_baddir);
            return;
        }
    }
    if (ch != ' ' && ch != '\t' && ch != '\r') {
        int dfsdir = ch;
        while ((ch = readmem(addr++)) == ' ' || ch == '\t')
            ;
        if (ch == '\r') {
            dir->drive = drive;
            dir->dfs_dir = dfsdir;
            return;
        }
    }
    adfs_error(err_baddir);
}

static void cmd_dir_dfs(uint16_t addr)
{
    parse_dfs_dir(addr, &cur_dir);
}

static void cmd_lib_dfs(uint16_t addr)
{
    parse_dfs_dir(addr, &lib_dir);
}

static void cmd_drive(uint16_t addr)
{
    int ch = readmem(addr++);
    if (ch >= '0' && (ch <= '3' || (ch <= '7' && !(fs_flags & DFS_MODE))))
        cur_dir.drive = ch - '0';
    else
        adfs_error(err_badparms);
}

static void cmd_title(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_entry *ent = cur_dir.dir;
        char *ptr = ent->u.dir.title;
        char *end = ent->u.dir.title + sizeof(ent->u.dir.title) - 1;
        int ch, quote= 0;

        log_debug("vdfs: cmd_title: addr=%04x", addr);
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');

        if (ch == '"') {
            quote = 1;
            ch = readmem(addr++);
        }
        while (ptr < end && ch != '\r' && (ch != '"' || !quote) && ((ch != ' ' && ch != '\t') || quote)) {
            *ptr++ = ch & 0x7f;
            ch = readmem(addr++);
        }
        *ptr = '\0';
        log_debug("vdfs: cmd_title: name=%s", ent->u.dir.title);
        write_back(ent);
    }
}

static void run_file(const char *err)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if ((cmd_tail = parse_name(path, sizeof path, (y << 8) | x))) {
            ent = find_entry(path, &res, &cur_dir);
            if (!(ent && ent->attribs & ATTR_EXISTS) && lib_dir.dir)
                ent = find_entry(path, &res, &lib_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    adfs_error(err_wont);
                else {
                    FILE *fp = fopen(ent->host_path, "rb");
                    if (fp) {
                        uint32_t addr = ent->u.file.load_addr;
                        show_activity();
                        if (addr >= 0xffff0000 || curtube == -1) {
                            log_debug("vdfs: run_file: writing to I/O proc memory at %08X", addr);
                            read_file_io(ent, fp, addr);
                            pc = ent->u.file.exec_addr;
                        } else {
                            log_debug("vdfs: run_file: writing to tube proc memory at %08X", addr);
                            writemem32(0xc0, ent->u.file.exec_addr); // set up for tube execution.
                            read_file_tube(ent, fp, addr);
                            rom_dispatch(VDFS_ROM_TUBE_EXEC);
                        }
                        fclose(fp);
                    } else {
                        log_warn("vdfs: unable to run file '%s': %s", ent->host_path, strerror(errno));
                        adfs_hosterr(errno);
                    }
                }
            }
            else
                adfs_error(err);
        }
    }
}

static void rename_tail(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    if (rename(old_ent->host_path, new_ent->host_path) == 0) {
        log_debug("vdfs: '%s' renamed to '%s'", old_ent->host_path, new_ent->host_path);
        if (old_ent->attribs & ATTR_IS_DIR) {
            new_ent->attribs |= ATTR_EXISTS|ATTR_IS_DIR;
            new_ent->u.dir.children   = old_ent->u.dir.children;
            new_ent->u.dir.scan_seq   = old_ent->u.dir.scan_seq;
            new_ent->u.dir.scan_mtime = old_ent->u.dir.scan_mtime;
            new_ent->u.dir.sorted     = old_ent->u.dir.sorted;
            old_ent->u.dir.children   = NULL;
            old_ent->u.dir.sorted     = SORT_NONE;
        }
        else {
            new_ent->attribs |= ATTR_EXISTS;
            new_ent->u.file.load_addr  = old_ent->u.file.load_addr;
            new_ent->u.file.exec_addr  = old_ent->u.file.exec_addr;
            new_ent->u.file.length     = old_ent->u.file.length;
        }
        old_ent->attribs &= ~ATTR_EXISTS;
        delete_inf(old_ent);
        write_back(new_ent);
    } else {
        adfs_hosterr(errno);
        log_debug("vdfs: failed to rename '%s' to '%s': %s", old_ent->host_path, new_ent->host_path, strerror(errno));
    }
}

static void rename_file(uint16_t addr)
{
    char old_path[MAX_ACORN_PATH], new_path[MAX_ACORN_PATH];

    if ((addr = parse_name(old_path, sizeof old_path, addr))) {
        if (parse_name(new_path, sizeof new_path, addr)) {
            if (*old_path && *new_path) {
                vdfs_findres old_res;
                vdfs_entry *old_ent = find_entry(old_path, &old_res, &cur_dir);
                if (old_ent && old_ent->attribs & ATTR_EXISTS) {
                    vdfs_findres new_res;
                    vdfs_entry *new_ent = find_entry(new_path, &new_res, &cur_dir);
                    if (new_ent) {
                        if (new_ent->attribs & ATTR_EXISTS) {
                            if (new_ent->attribs & ATTR_IS_DIR) {
                                old_res.parent = new_ent;
                                if ((new_ent = add_new_file(&old_res)))
                                    rename_tail(old_ent, new_ent);
                            } else {
                                log_debug("vdfs: new file '%s' for rename already exists", new_res.acorn_fn);
                                adfs_error(err_exists);
                            }
                        } else
                            rename_tail(old_ent, new_ent);
                    }
                    else if (new_res.parent && (new_ent = add_new_file(&new_res)))
                        rename_tail(old_ent, new_ent);
                } else {
                    log_debug("vdfs: old file '%s' for rename not found", old_res.acorn_fn);
                    adfs_error(old_res.errmsg);
                }
            } else {
                log_debug("vdfs: rename attempted with an empty filename");
                adfs_error(err_badren);
            }
        }
    }
}

const struct cmdent ctab_filing[] = {
    { "ACcess",  VDFS_ACT_ACCESS  },
    { "APpend",  VDFS_ROM_APPEND  },
    { "BAck",    VDFS_ACT_BACK    },
    { "BACKUp",  VDFS_ACT_NOP     },
    { "BUild",   VDFS_ROM_BUILD   },
    { "CDir",    VDFS_ACT_CDIR    },
    { "COMpact", VDFS_ACT_NOP     },
    { "COpy",    VDFS_ACT_COPY    },
    { "DELete",  VDFS_ACT_DELETE  },
    { "DEStroy", VDFS_ACT_NOP     },
    { "DIR",     VDFS_ACT_DIR     },
    { "DRive",   VDFS_ACT_DRIVE   },
    { "ENable",  VDFS_ACT_NOP     },
    { "EX",      VDFS_ACT_EX      },
    { "FIles",   VDFS_ACT_FILES   },
    { "FORM",    VDFS_ACT_NOP     },
    { "FRee",    VDFS_ACT_NOP     },
    { "Info",    VDFS_ACT_INFO    },
    { "LCat",    VDFS_ACT_LCAT    },
    { "LEX",     VDFS_ACT_LEX     },
    { "LIB",     VDFS_ACT_LIB     },
    { "MAP",     VDFS_ACT_NOP     },
    { "MOunt",   VDFS_ACT_NOP     },
    { "OSW7f",   VDFS_ACT_OSW7F   },
    { "REname",  VDFS_ACT_RENAME  },
    { "REScan",  VDFS_ACT_RESCAN  },
    { "TItle",   VDFS_ACT_TITLE   },
    { "VErify",  VDFS_ACT_NOP     },
    { "WIpe",    VDFS_ACT_NOP     }
};

static void parse_cmd(uint16_t addr, char *dest)
{
    for (int i = 0; i < MAX_CMD_LEN; ++i) {
        int ch = readmem(addr++);
        dest[i] = ch;
        if (ch == '\r' || ch == '.' || ch == ' ' || ch == '\t') {
            log_debug("vdfs: parse_cmd: cmd=%.*s, finish with %02X at %04X", i, dest, ch, addr);
            break;
        }
    }
}

static const struct cmdent *lookup_cmd(const struct cmdent *tab, size_t nentry, char *cmd, char **end)
{
    const struct cmdent *tab_ptr = tab;
    const struct cmdent *tab_end = tab+nentry;

    while (tab_ptr < tab_end) {
        int tab_ch, cmd_ch;
        const char *tab_cmd = tab_ptr->cmd;
        char *cmd_ptr = cmd;
        do {
            tab_ch = *tab_cmd++;
            cmd_ch = *cmd_ptr++;
        } while (tab_ch && !((tab_ch ^ cmd_ch) & 0x5f)); // case insensitive comparison.
        if (!tab_ch && (cmd_ch < 'A' || (cmd_ch > 'Z' && cmd_ch < 'a') || cmd_ch > 'z')) {
            *end = cmd_ptr-1;
            return tab_ptr;
        }
        if (cmd_ch == '.' && tab_ch > 'Z') {
            *end = cmd_ptr;
            return tab_ptr;
        }
        tab_ptr++;
    }
    return NULL;
}

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*a))

static void fsclaim(uint16_t addr)
{
    int ch = readmem(addr);
    log_debug("vdfs: fsclaim, ch = %02X, addr=%04X", ch, addr);
    if (ch == 'o' || ch == 'O') {
        ch = readmem(++addr);
        if (ch == 'n' || ch == 'N') {
            fs_flags |= CLAIM_ADFS|CLAIM_DFS;
            return;
        }
        else if (ch == 'f' || ch == 'F') {
            fs_flags &= ~(CLAIM_ADFS|CLAIM_DFS);
            return;
        }
    }
    else if (ch == '+') {
        ch = readmem(++addr);
        if (ch == 'a' || ch == 'A') {
            fs_flags |= CLAIM_ADFS;
            return;
        }
        else if (ch == 'd' || ch == 'D') {
            fs_flags |= CLAIM_DFS;
            return;
        }
    }
    else if (ch == '-') {
        ch = readmem(++addr);
        if (ch == 'a' || ch == 'A') {
            fs_flags &= ~CLAIM_ADFS;
            return;
        }
        else if (ch == 'd' || ch == 'D') {
            fs_flags &= ~CLAIM_DFS;
            return;
        }
    }
    else if (ch == '\r') {
        rom_dispatch(VDFS_ROM_FSCLAIM);
        return;
    }
    adfs_error(err_badparms);
}

/*
 * Functions used in the implementation of the *CAT, *EX, *INFO and
 * *LCAT commands.  To avoid needing to allocate workspace on the guest
 * each of these transfers a small amount of information from the VDFS
 * internal structures to temporary space on the guest, the guest prints
 * that information and this is repeated until the command is complete.
 */

#define CAT_TMP 0x100


static bool cat_prep_adfs(uint16_t addr, vdfs_dirlib *dir)
{
    if (check_valid_dir(dir)) {
        cat_dir = dir->dir;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            if (*path) {
                vdfs_findres res;
                vdfs_entry *ent = find_entry(path, &res, dir);
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_IS_DIR) {
                        cat_dir = ent;
                    } else {
                        adfs_error(err_notdir);
                        return false;
                    }
                } else {
                    adfs_error(res.errmsg);
                    return false;
                }
            }
            if (!scan_dir(cat_dir)) {
                acorn_sort(cat_dir, SORT_ADFS);
                cat_ent = cat_dir->u.dir.children;
                cat_dfs = dir->dfs_dir;
                return true;
            }
        }
    }
    return false;
}

static bool cat_prep_dfs(uint16_t addr, vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    if (!scan_dir(ent)) {
        cat_dfs = dir->dfs_dir;
        acorn_sort(ent, SORT_DFS);
        cat_dir = ent;
        cat_ent = ent->u.dir.children;
        return true;
    }
    return false;
}

static void cat_title(void)
{
    uint32_t mem_ptr = CAT_TMP;
    if (cat_dir) {
        int ch;
        const char *ptr = cat_dir->u.dir.title;
        if (!*ptr)
            ptr = cat_dir->acorn_fn;
        writemem(mem_ptr++, cat_dir->u.dir.boot_opt);
        while ((ch = *ptr++))
            writemem(mem_ptr++, ch);
    }
    else
        writemem(mem_ptr++, 0);
    writemem(mem_ptr++, 0);
}

static void cat_get_dir_adfs(vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    uint32_t mem_ptr = CAT_TMP;
    const char *ptr = ent ? ent->acorn_fn : "Unset";
    int ch;
    while ((ch = *ptr++))
        writemem(mem_ptr++, ch);
    writemem(mem_ptr, 0);
}

static void cat_get_dir_dfs(vdfs_dirlib *dir)
{
    writemem(CAT_TMP, ':');
    writemem(CAT_TMP+1, '0' + dir->drive);
    writemem(CAT_TMP+2, '.');
    writemem(CAT_TMP+3, dir->dfs_dir);
    writemem(CAT_TMP+4, 0);
}

static void cat_next_tail(void)
{
    if (cat_ent) {
        gcopy_attr(cat_ent);
        cat_ent = cat_ent->next;
        p.c = 0;
    }
    else
        p.c = 1;
}

static void cat_next_adfs(void)
{
    while (cat_ent && !(cat_ent->attribs & ATTR_EXISTS))
        cat_ent = cat_ent->next;
    cat_next_tail();
}

static void cat_next_dfsdir(void)
{
    while (cat_ent && (!(cat_ent->attribs & ATTR_EXISTS) || cat_ent->dfs_dir != cat_dfs))
    {
        log_debug("vdfs: cat_next_dfsdir skipping %c.%s", cat_ent->dfs_dir, cat_ent->acorn_fn);
        cat_ent = cat_ent->next;
    }
    cat_next_tail();
}

static void cat_next_dfsnot(void)
{
    while (cat_ent && (!(cat_ent->attribs & ATTR_EXISTS) || cat_ent->dfs_dir == cat_dfs)) {
        log_debug("vdfs: cat_next_dfsnot skipping %c.%s", cat_ent->dfs_dir, cat_ent->acorn_fn);
        cat_ent = cat_ent->next;
    }
    cat_next_tail();
}

static void cat_dfs_rewind(void)
{
    cat_ent = cat_dir->u.dir.children;
    cat_next_dfsnot();
}

static void file_info(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            vdfs_entry *ent = find_entry(path, &info_res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                gcopy_attr(ent);
                cat_ent = ent;
                rom_dispatch(VDFS_ROM_INFO);
                return;
            }
            adfs_error(info_res.errmsg);
        }
    }
}

static void info_next(void)
{
    vdfs_entry *ent = find_next(cat_ent, &info_res);
    if (ent) {
        gcopy_attr(ent);
        cat_ent = ent;
        p.c = 0;
    }
    else
        p.c = 1;
}

static int chan_seq = 0;

static void gcopy_open(vdfs_entry *ent)
{
    writemem(CAT_TMP, MIN_CHANNEL + chan_seq);
    uint16_t mem_ptr = gcopy_fn(ent, 0x101);
    writemem(mem_ptr++, ent->attribs >> 8);
    FILE *fp = vdfs_chan[chan_seq].fp;
    if (fp) {
        long pos = ftell(fp);
        writemem32(mem_ptr, pos);
        fseek(fp, 0, SEEK_END);
        long ext = ftell(fp);
        writemem32(mem_ptr+4, ext);
        ent->u.file.length = ext;
        fseek(fp, pos, SEEK_SET);
    }
}

static void files_prep(void)
{
    for (int chan = 0; chan < NUM_CHANNELS; chan++) {
        log_debug("vdfs: files_prep: chan=%d", chan);
        vdfs_entry *ent = vdfs_chan[chan].ent;
        log_debug("vdfs: files_prep: ent=%p", ent);
        if (ent) {
            chan_seq = chan;
            gcopy_open(ent);
            rom_dispatch(VDFS_ROM_FILES);
            return;
        }
    }
    rom_dispatch(VDFS_ROM_NOPEN);
}

static void files_nxt(void)
{
    int chan = chan_seq;
    while (++chan < NUM_CHANNELS) {
        vdfs_entry *ent = vdfs_chan[chan].ent;
        if (ent) {
            chan_seq = chan;
            gcopy_open(ent);
            p.c = 0;
            return;
        }
    }
    p.c = 1;
}

static enum vdfs_action osw7fmc_act = VDFS_ACT_OSW7F_NONE;
static const uint8_t *osw7fmc_tab;

static const uint8_t osw7fmc_ac1[] =
{
    0x72, 0x73, 0x74, 0x75, 0x80, 0x82, 0x83,
    0x85, 0xC8, 0xC9, 0xD3, 0xD5, 0xD6, 0x00
};

static const uint8_t osw7fmc_ac2[] =
{
    0x87, 0x88, 0x89, 0x8B, 0x8C, 0x8D, 0x8E,
    0xD3, 0xD6, 0xDE, 0xDF, 0xE0, 0xE1, 0x00
};

static const uint8_t osw7fmc_watf[] =
{
 0x42, 0x43, 0x4A, 0x78, 0x88, 0x89, 0x8A, 0x00
};

static const uint8_t osw7fmc_wat5[] =
{
    0x30, 0x36, 0x38, 0x3F, 0x42, 0x43, 0x4A, 0x78,
    0x88, 0x89, 0x8A, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4,
    0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0x00
};

static const uint8_t osw7fmc_all[] =
{
    0x30, 0x33, 0x36, 0x38, 0x3F, 0x42, 0x43, 0x44,
    0x4A, 0x72, 0x73, 0x74, 0x75, 0x78, 0x79, 0x7A,
    0x7B, 0x80, 0x82, 0x83, 0x85, 0x87, 0x88, 0x89,
    0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0xA0, 0xA1, 0xA2,
    0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
    0xC8, 0xC9, 0xD3, 0xD5, 0xD6, 0xDE, 0xDF, 0xE0,
    0xE1, 0x00
};

static const uint8_t *osw7fmc_tabs[] =
{
    osw7fmc_all,
    osw7fmc_ac1,
    osw7fmc_ac2,
    osw7fmc_watf,
    osw7fmc_wat5,
};

const struct cmdent ctab_osw7f[] = {
    { "None", VDFS_ACT_OSW7F_NONE },
    { "All",  VDFS_ACT_OSW7F_ALL  },
    { "A090", VDFS_ACT_OSW7F_AC1  },
    { "A120", VDFS_ACT_OSW7F_AC1  },
    { "A210", VDFS_ACT_OSW7F_AC2  },
    { "W110", VDFS_ACT_OSW7F_WATF },
    { "W120", VDFS_ACT_OSW7F_WATF },
    { "W130", VDFS_ACT_OSW7F_WATF },
    { "W14x", VDFS_ACT_OSW7F_WATF },
    { "W15x", VDFS_ACT_OSW7F_WAT5 }
};

static void cmd_osw7f(uint16_t addr)
{
    if (readmem(addr) == '\r') {
        x = osw7fmc_act - VDFS_ACT_OSW7F_NONE;
        rom_dispatch(VDFS_ROM_OSW7F);
    }
    else {
        char cmd[MAX_CMD_LEN], *end;
        parse_cmd(addr, cmd);
        const struct cmdent *ent = lookup_cmd(ctab_osw7f, ARRAY_SIZE(ctab_osw7f), cmd, &end);
        if (ent) {
            osw7fmc_act = ent->act;
            if (ent->act == VDFS_ACT_OSW7F_NONE)
                osw7fmc_tab = NULL;
            else
                osw7fmc_tab = osw7fmc_tabs[ent->act - VDFS_ACT_OSW7F_NONE - 1];
        }
        else
            adfs_error(err_badcmd);
    }
}

static void vdfs_dfs_mode(void)
{
    fs_flags |= DFS_MODE;
    find_entry = find_entry_dfs;
    find_next = find_next_dfs;
    osgbpb_get_dir = osgbpb_get_dir_dfs;
    cat_prep = cat_prep_dfs;
    cat_get_dir = cat_get_dir_dfs;
    cmd_dir = cmd_dir_dfs;
    cmd_lib = cmd_lib_dfs;
}

static void vdfs_adfs_mode(void)
{
    fs_flags &= ~DFS_MODE;
    find_entry = find_entry_adfs;
    find_next = find_next_adfs;
    osgbpb_get_dir = osgbpb_get_dir_adfs;
    cat_prep = cat_prep_adfs;
    cat_get_dir = cat_get_dir_adfs;
    cmd_dir = cmd_dir_adfs;
    cmd_lib = cmd_lib_adfs;
}

static void select_vdfs(uint8_t fsno)
{
    log_debug("vdfs: select_vdfs, fsno=%d", fsno);
    if (fsno == FSNO_DFS)
        vdfs_dfs_mode();
    else
        vdfs_adfs_mode();
    if (!(fs_flags & VDFS_ACTIVE)) {
        fs_num = fsno;
        rom_dispatch(VDFS_ROM_FSSTART);
    }
}

static void cmd_vdfs(uint16_t addr)
{
    int ch = readmem(addr);
    if (ch == 'A' || ch == 'a')
        vdfs_adfs_mode();
    else if (ch == 'D' || ch == 'd')
        vdfs_dfs_mode();
    else if (ch != '\r') {
        adfs_error(err_badparms);
        return;
    }
    if (!(fs_flags & VDFS_ACTIVE)) {
        fs_num = FSNO_VDFS;
        rom_dispatch(VDFS_ROM_FSSTART);
    }
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
        adfs_error(err_discerr);
    return i;
}

static bool mmb_check_pick(unsigned drive, unsigned disc)
{
    if (disc >= mmb_ndisc) {
        adfs_error(err_notfound);
        return false;
    }
    unsigned side;
    switch(drive) {
        case 0:
        case 1:
            side = 0;
            break;
        case 2:
        case 3:
            drive &= 1;
            disc--;
            side = 1;
            break;
        default:
            log_debug("vdfs: mmb_check_pick: invalid logical drive %d", drive);
            adfs_error(err_badparms);
            return false;
    }
    mmb_pick(drive, side, disc);
    return true;
}

static void cmd_mmb_din(uint16_t addr)
{
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
            mmb_check_pick(0, num1);
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
                    mmb_check_pick(num1, num2);
                else
                    adfs_error(err_badparms);
            }
            else if ((num2 = mmb_parse_find(addr)) >= 0) {
                if (num1 >= 0 && num1 <= 3)
                    mmb_check_pick(num1, num2);
                else
                    adfs_error(err_badparms);
            }
        }
    }
    else if ((num1 = mmb_parse_find(addr)) >= 0)
        mmb_check_pick(0, num1);
}

static void cmd_dump(uint16_t addr)
{
    x = addr & 0xff;
    y = addr >> 8;

    // Skip over the filename and any spaces after.

    int ch;
    do
        ch = readmem(addr++);
    while (ch != ' ' && ch != '\t' && ch != '\r');
    while (ch == ' ' || ch == '\t')
        ch = readmem(addr++);
    uint32_t start = 0, offset = 0;
    if (ch != '\r') {
        log_debug("vdfs: cmd_dump, start present");
        do {
            ch = hex2nyb(ch);
            if (ch == -1) {
                adfs_error(err_badparms);
                return;
            }
            start = start << 4 | ch;
            log_debug("vdfs: cmd_dump, start loop, nyb=%x, start=%x", ch, start);
            ch = readmem(addr++);
        }
        while (ch != ' ' && ch != '\t' && ch != '\r');
        while (ch == ' ' || ch == '\t')
            ch = readmem(addr++);
        if (ch == '\r')
            offset = start;
        else {
            log_debug("vdfs: cmd_dump, offset present");
            do {
                ch = hex2nyb(ch);
                if (ch == -1) {
                    adfs_error(err_badparms);
                    return;
                }
                offset = offset << 4 | ch;
                log_debug("vdfs: cmd_dump, start loop, nyb=%x, offset=%x", ch, offset);
                ch = readmem(addr++);
            }
            while (ch != ' ' && ch != '\t' && ch != '\r');
        }
    }
    writemem32(0xa8, offset);
    writemem32(0xac, start);
    rom_dispatch(VDFS_ROM_DUMP);
}

static bool vdfs_do(enum vdfs_action act, uint16_t addr)
{
    log_debug("vdfs: vdfs_do, act=%d, addr=%04X", act, addr);
    switch(act)
    {
    case VDFS_ROM_DUMP:
        cmd_dump(addr);
        break;
    case VDFS_ROM_LIST:
    case VDFS_ROM_PRINT:
    case VDFS_ROM_TYPE:
    case VDFS_ROM_BUILD:
    case VDFS_ROM_APPEND:
        x = addr & 0xff;
        y = addr >> 8;
        rom_dispatch(act);
    case VDFS_ACT_NOP:
        break;
    case VDFS_ACT_QUIT:
        main_setquit();
        break;
    case VDFS_ACT_SRLOAD:
        return cmd_srload(addr);
    case VDFS_ACT_SRSAVE:
        return cmd_srsave(addr);
    case VDFS_ACT_SRREAD:
        srcopy(addr, 0);
        break;
    case VDFS_ACT_SRWRITE:
        srcopy(addr, 0x80);
        break;
    case VDFS_ACT_BACK:
        cmd_back();
    case VDFS_ACT_CDIR:
        cmd_cdir(addr);
        break;
    case VDFS_ACT_DELETE:
        cmd_delete(addr);
        break;
    case VDFS_ACT_DIR:
        cmd_dir(addr);
        break;
    case VDFS_ACT_EX:
        cat_prep(addr, &cur_dir);
        rom_dispatch(VDFS_ROM_EX);
        break;
    case VDFS_ACT_FILES:
        files_prep();
        break;
    case VDFS_ACT_INFO:
        if (readmem(addr) == '\r')
            adfs_error(err_badcmd);
        else
            file_info(addr);
        break;
    case VDFS_ACT_LCAT:
        cat_prep(addr, &lib_dir);
        cat_title();
        rom_dispatch(VDFS_ROM_CAT);
        break;
    case VDFS_ACT_LEX:
        cat_prep(addr, &lib_dir);
        rom_dispatch(VDFS_ROM_EX);
        break;
    case VDFS_ACT_LIB:
        cmd_lib(addr);
        break;
    case VDFS_ACT_RENAME:
        rename_file(addr);
        break;
    case VDFS_ACT_RESCAN:
        scan_seq++;
        break;
    case VDFS_ACT_TITLE:
        cmd_title(addr);
        break;
    case VDFS_ACT_VDFS:
        cmd_vdfs(addr);
        break;
    case VDFS_ACT_ADFS:
        if (!(fs_flags & CLAIM_ADFS))
            return false;
        select_vdfs(FSNO_ADFS);
        break;
    case VDFS_ACT_DISC:
        if (!(fs_flags & CLAIM_DFS))
            return false;
        select_vdfs(FSNO_DFS);
        break;
    case VDFS_ACT_FSCLAIM:
        fsclaim(addr);
        break;
    case VDFS_ACT_OSW7F:
        cmd_osw7f(addr);
        break;
    case VDFS_ACT_MMBDIN:
        cmd_mmb_din(addr);
        break;
    case VDFS_ACT_DRIVE:
        cmd_drive(addr);
        break;
    case VDFS_ACT_ACCESS:
        cmd_access(addr);
        break;
    case VDFS_ACT_COPY:
        cmd_copy(addr);
        break;
    default:
        rom_dispatch(act);
    }
    return true;
}

/*
 * OSFSC and supporting functions.
 */

static uint16_t skip_spaces(uint16_t addr)
{
    int ch = readmem(addr);
    while (ch == ' ' || ch == '\t')
        ch = readmem(++addr);
    return addr;
}

static void osfsc_cmd(void)
{
    uint16_t addr = (y << 8) | x;
    char cmd[MAX_CMD_LEN], *end;
    parse_cmd(addr, cmd);
    const struct cmdent *ent = lookup_cmd(ctab_filing, ARRAY_SIZE(ctab_filing), cmd, &end);
    if (ent) {
        vdfs_do(ent->act, skip_spaces(addr + (end - cmd)));
        return;
    }
    run_file(err_badcmd);
}

static void osfsc_opt(void)
{
    switch(x) {
        case 1:
            vdfs_opt1 = y;
            break;
        case 2:
            fs_num = y;
            break;
        case 4:
            if (check_valid_dir(&cur_dir)) {
                vdfs_entry *ent = cur_dir.dir;
                ent->u.dir.boot_opt = y;
                write_back(ent);
            }
            break;
        case 6:
            fs_flags |= (y & 0x0f) << 4;
            break;
        case 7:
            fs_flags &= ~((y & 0x0f) << 4);
            break;
        default:
            log_debug("vdfs: osfsc unimplemented option %d,%d", x, y);
    }
}

static void osfsc(void)
{
    FILE *fp;

    log_debug("vdfs: osfsc(A=%02X, X=%02X, Y=%02X)", a, x, y);

    p.c = 0;
    switch(a) {
        case 0x00:
            osfsc_opt();
            break;
        case 0x01: // check EOF
            if ((fp = getfp_read(x)))
                x = feof(fp) ? 0xff : 0x00;
            break;
        case 0x02: // */ command
        case 0x04: // *RUN command
            run_file(err_notfound);
            break;
        case 0x03: // unrecognised OS command
            osfsc_cmd();
            break;
        case 0x05:
            if (cat_prep(x + (y << 8), &cur_dir)) {
                cat_title();
                rom_dispatch(VDFS_ROM_CAT);
            }
            break;
        case 0x06: // new filesystem taking over.
            fs_flags &= ~VDFS_ACTIVE;
            break;
        case 0x07:
            x = MIN_CHANNEL;
            y = MIN_CHANNEL + NUM_CHANNELS;
            break;
        case 0x09:
            if (cat_prep(x + (y << 8), &cur_dir))
                rom_dispatch(VDFS_ROM_EX);
            break;
        case 0x0a:
            file_info((y << 8) | x);
            break;
        case 0x0c:
            rename_file((y << 8) | x);
            break;
        default:
            log_debug("vdfs: osfsc unimplemented for a=%d", a);
    }
}

/*
 * ROM Service.  These functions are concerned with implementing
 * actions as a result of a ROM servce call.
 */

static void osword_discio(void)
{
    uint16_t pb   = readmem(0xf0) | (readmem(0xf1) << 8);
    uint8_t drive = readmem(pb);
    uint8_t cmd   = readmem(pb+6);
    uint8_t track = readmem(pb+7);
    uint8_t sect  = readmem(pb+8);
    uint8_t byte9 = readmem(pb+9);
    uint8_t sects = byte9 & 0x0f;
    uint16_t ssize;

    byte9 &= 0xe0;
    if (byte9 == 0)
        ssize = 128;
    else if (byte9 == 0x40)
        ssize = 512;
    else
        ssize = 256;
    log_debug("vdfs: osword 7F: drive=%u, cmd=%02X, track=%u, sect=%u, sects=%u, ssize=%u", drive, cmd, track, sect, sects, ssize);

    FILE *fp = sdf_owseek(drive & 1, sect, track, (drive >> 1) & 1, ssize);
    if (fp) {
        uint32_t addr = readmem32(pb+1);
        size_t bytes = (sects & 0x0f) << 8;
        if (cmd == 0x53)
            read_bytes(fp, addr, bytes);
        else if (cmd == 0x4b) {
            write_bytes(fp, addr, bytes);
            p.z = 1;
        }
        writemem(pb+10, 0);
    }
    else {
        log_debug("vdfs: osword attempting to read invalid/empty drive %d", drive);
        writemem(pb+10, 0x14); // track 0 not found.
    }
    if (osw7fmc_tab) {
        const uint8_t *ptr;
        uint8_t tb, mb, nc, cf = 0;
        for (ptr = osw7fmc_tab; (tb = *ptr++); ) {
            mb = tb ^ readmem(0x1000+tb);
            // Simulate a ROL A
            nc = mb & 0x80;
            mb = mb << 1 | cf;
            cf = nc ? 1 : 0;
            mb ^= 0x23;
            writemem(0x1000+tb, mb);
        }
    }
}

static void osword(void)
{
    if (fs_num) {
        switch(readmem(0xef))
        {
            case 0x42:
                osword_swr_ram();
                a = 0;
                break;
            case 0x43:
                osword_swr_fs();
                a = 0;
                break;
            case 0x7f:
                osword_discio();
                a = 0;
                break;
        }
    }
}

static void check_ram(void)
{
    p.c = 0;
    if (y >= 0 && y <= 15)
        if (rom_slots[y].swram)
            p.c = 1;
}

static bool prev_ram = false;

static void set_ram(void)
{
    int rom_id = ram_fe30 & 0x0f;
    if (rom_slots[rom_id].swram)
        prev_ram = true;
    else {
        prev_ram = false;
        rom_slots[rom_id].swram = true;
        m6502_update_swram();
    }
}

static void rest_ram(void)
{
    if (!prev_ram) {
        rom_slots[ram_fe30 & 0x0f].swram = false;
        m6502_update_swram();
    }
}

static const struct cmdent ctab_always[] = {
    { "QUIT",    VDFS_ACT_QUIT    },
    { "Dump",    VDFS_ROM_DUMP    },
    { "List",    VDFS_ROM_LIST    },
    { "Print",   VDFS_ROM_PRINT   },
    { "Type",    VDFS_ROM_TYPE    },
    { "Roms",    VDFS_ROM_ROMS    }
};

static const struct cmdent ctab_mmb[] = {
    { "DAbout",  VDFS_ACT_NOP     },
    { "Din",     VDFS_ACT_MMBDIN  }
};

static const struct cmdent ctab_enabled[] = {
    { "VDFS",    VDFS_ACT_VDFS    },
    { "ADFS",    VDFS_ACT_ADFS    },
    { "FADFS",   VDFS_ACT_ADFS    },
    { "DISC",    VDFS_ACT_DISC    },
    { "DISK",    VDFS_ACT_DISC    },
    { "FSclaim", VDFS_ACT_FSCLAIM },
    { "SRLoad",  VDFS_ACT_SRLOAD  },
    { "SRSave",  VDFS_ACT_SRSAVE  },
    { "SRRead",  VDFS_ACT_SRREAD  },
    { "SRWrite", VDFS_ACT_SRWRITE }
};

static void serv_cmd(void)
{
    uint16_t addr = readmem16(0xf2) + y;
    char cmd[MAX_CMD_LEN], *end;

    parse_cmd(addr, cmd);
    const struct cmdent *ent = lookup_cmd(ctab_always, ARRAY_SIZE(ctab_always), cmd, &end);
    if (!ent && vdfs_enabled)
        ent = lookup_cmd(ctab_enabled, ARRAY_SIZE(ctab_enabled), cmd, &end);
    if (!ent && mmb_fn)
        ent = lookup_cmd(ctab_mmb, ARRAY_SIZE(ctab_mmb), cmd, &end);
    if (ent && vdfs_do(ent->act, skip_spaces(addr + (end - cmd))))
        a = 0;
}

const struct cmdent ctab_help[] = {
    { "VDFS",  VDFS_ROM_HELP_VDFS  },
    { "SRAM",  VDFS_ROM_HELP_SRAM  },
    { "UTILS", VDFS_ROM_HELP_UTILS }
};

static void serv_help(void)
{
    uint16_t addr = readmem16(0xf2) + y;
    int ch = readmem(addr);
    if (ch == '\r')
        rom_dispatch(VDFS_ROM_HELP_SHORT);
    else if (ch == '.')
        rom_dispatch(VDFS_ROM_HELP_ALL);
    else {
        char cmd[MAX_CMD_LEN], *end;
        parse_cmd(addr, cmd);
        const struct cmdent *ent = lookup_cmd(ctab_help, ARRAY_SIZE(ctab_help), cmd, &end);
        if (ent)
            rom_dispatch(ent->act);
    }
}

static uint8_t save_y;

static void serv_boot(void)
{
    if (vdfs_enabled && (!key_any_down() || key_code_down(ALLEGRO_KEY_S))) {
        if (readmem(0x028d)) { /* last break type */
            close_all();
            cur_dir.dir = prev_dir.dir = cat_dir = &root_dir;
            cur_dir.drive = lib_dir.drive = 0;
            cur_dir.dfs_dir = lib_dir.dfs_dir = '$';
            vdfs_findres res;
            lib_dir.dir = find_entry_adfs("Lib", &res, &cur_dir);
        }
        if (check_valid_dir(&cur_dir)) {
            scan_dir(cur_dir.dir);
            a = cur_dir.dir->u.dir.boot_opt;
            rom_dispatch(VDFS_ROM_FSBOOT);
        }
    }
    else
        fs_flags &= ~VDFS_ACTIVE; // some other filing system.
}

static void service(void)
{
    log_debug("vdfs: rom service a=%02X, x=%02X, y=%02X", a, x, y);
    switch(a)
    {
    case 0x02:
        save_y = y;
        rom_dispatch(VDFS_ROM_BREAK);
        break;
    case 0x03: // filing system boot.
        serv_boot();
        break;
    case 0x04: // unrecognised command.
        serv_cmd();
        break;
    case 0x06: // BRK instruction.
        rest_ram();
        break;
    case 0x08: // OSWORD
        osword();
        break;
    case 0x09: // *HELP
        serv_help();
        break;
    case 0x12: // Select filing system.
        if (y == FSNO_VDFS || ((fs_flags & CLAIM_ADFS) && y == FSNO_ADFS) || ((fs_flags & CLAIM_DFS) && y == FSNO_DFS))
            select_vdfs(y);
        break;
    case 0x25: // Filing system info.
        rom_dispatch(VDFS_ROM_FSINFO);
        break;
    case 0x26: // Close open files.
        close_all();
        break;
    case 0xfe: // Tube explode character set.
        x = models[curmodel].boot_logo;
        rom_dispatch(VDFS_ROM_TUBE_EXPL);
        break;
    case 0xff: // Tube initialisation.
        rom_dispatch(VDFS_ROM_TUBE_INIT);
        break;
    }
}

static void startup(void)
{
    if (a > 0)
        mmb_reset();
    a = 0x02;
    y = save_y;
}

static inline void dispatch(uint8_t value)
{
    switch(value) {
        case 0x00: service();   break;
        case 0x01: osfile();    break;
        case 0x02: osargs();    break;
        case 0x03: osbget();    break;
        case 0x04: osbput();    break;
        case 0x05: osgbpb();    break;
        case 0x06: osfind();    break;
        case 0x07: osfsc();     break;
        case 0x08: check_ram(); break;
        case 0x09: startup();   break;
        case 0x0a: files_nxt(); break;
        case 0x0b: close_all(); break;
        case 0x0c: cat_next_adfs();   break;
        case 0x0d: cat_next_dfsdir(); break;
        case 0x0e: cat_next_dfsnot(); break;
        case 0x0f: cat_dfs_rewind();  break;
        case 0x10: cat_get_dir(&cur_dir); break;
        case 0x11: cat_get_dir(&lib_dir); break;
        case 0x12: set_ram();   break;
        case 0x13: rest_ram();  break;
        case 0x14: info_next(); break;
        default: log_warn("vdfs: function code %d not recognised", value);
    }
}

uint8_t vdfs_read(uint16_t addr)
{
    switch (addr & 3) {
        case 0:
            log_debug("vdfs: get fs_flags=%02x", fs_flags);
            return fs_flags;
            break;
        case 1:
            log_debug("vdfs: get fs_num=%02x", fs_num);
            return fs_num;
            break;
        default:
            return 0xff;
    }
}

void vdfs_write(uint16_t addr, uint8_t value)
{
    switch (addr & 3) {
        case 0:
            fs_flags = value;
            log_debug("vdfs: set fs_flags=%02x", value);
            break;
        case 1:
            fs_num = value;
            autoboot = 0;
            log_debug("vdfs: set fs_num=%02x", value);
            break;
        case 2:
            a = reg_a;
            dispatch(value);
            break;
        case 3:
            reg_a = value;
    }
}

// Initialise the VDFS module.

static void init_dirlib(vdfs_dirlib *dir, const char *desc)
{
    dir->desc = desc;
    dir->dfs_dir = '$';
    dir->drive = 0;
}

void vdfs_init(const char *root)
{
    scan_seq = 0;
    init_dirlib(&cur_dir, "current");
    init_dirlib(&lib_dir, "library");
    init_dirlib(&prev_dir, "previous");
    const char *env = getenv("BEM_VDFS_ROOT");
    if (env)
        root = env; //environment variable wins
    if (!root)
        root = ".";
    vdfs_set_root(root);
    vdfs_adfs_mode();
}
