/*
 * VDFS for B-EM
 * Steve Fosdick 2016-2023
 *
 * This module implements the host part of a Virtual Disk Filing
 * System, one in which a part of filing system of the host is
 * exposed to the guest through normal OS calls on the guest.
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
#include "mmb.h"
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
const char *vdfs_boot_dir = NULL;

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
#define ATTR_BTIME_VALID 0x0800
#define ATTR_NL_TRANS    0x0100

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
    uint8_t    acorn_len;
    char       acorn_fn[MAX_FILE_NAME];
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
            uint8_t    title_len;
            char       title[MAX_TITLE];
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
    uint8_t acorn_len;
    char acorn_fn[MAX_FILE_NAME];
    char dfs_dir;
} vdfs_findres;

static vdfs_findres info_res;

typedef struct vdfs_path {
    uint8_t len;
    char path[MAX_ACORN_PATH];
} vdfs_path;

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
    VDFS_ROM_PRINT_SPLIT,
    VDFS_ROM_MMBDIN,
    VDFS_ROM_MMBDOP,
    VDFS_ROM_MMBONBT,
    VDFS_ROM_MMBDOUT,
    VDFS_ACT_MMBDBAS,
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
    VDFS_ACT_MMBDABT,
    VDFS_ACT_MMBDBOT,
    VDFS_ACT_MMBDCAT,
    VDFS_ACT_MMBDDRV,
    VDFS_ACT_MMBDFRE,
    VDFS_ACT_MMBRCAT,
    VDFS_ACT_DRIVE,
    VDFS_ACT_ACCESS,
    VDFS_ACT_COPY,
    VDFS_ACT_PWD
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
#define GSTRANS_FN  0x20
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

static vdfs_entry *(*find_entry)(const vdfs_path *path, vdfs_findres *res, vdfs_dirlib *dir);
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
    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        FILE *fp = vdfs_chan[channel].fp;
        if (fp)
            fflush(fp);
    }
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

static inline void hst2bbc(vdfs_entry *ent)
{
    const char *host_fn = ent->host_fn;
    char *acorn_fn = ent->acorn_fn;
    char *end = acorn_fn + MAX_FILE_NAME;
    int ch;

    while ((ch = *host_fn++) && acorn_fn < end) {
        if (ch >= ' ' && ch <= '~') {
            const char *ptr = strchr(hst_chars, ch);
            if (ptr)
                ch = bbc_chars[ptr-hst_chars];
            *acorn_fn++ = ch;
        }
    }
    ent->acorn_len = acorn_fn - ent->acorn_fn;
}

static inline char *bbc2hst(vdfs_findres *res, char *host_fn)
{
    const char *acorn_fn = res->acorn_fn;
    const char *acorn_end = acorn_fn + res->acorn_len;
    char *host_end = host_fn + MAX_FILE_NAME;

    while (acorn_fn < acorn_end && host_fn < host_end) {
        int ch = *acorn_fn++;
        if (ch >= ' ' && ch <= '~') {
            const char *ptr = strchr(bbc_chars, ch);
            if (ptr)
                ch = hst_chars[ptr-bbc_chars];
            *host_fn++ = ch;
        }
    }
    *host_fn = '\0';
    return host_fn;
}

static char *make_host_path(vdfs_entry *ent, const char *host_fn)
{
    char *host_dir_path, *host_file_path, *ptr;
    size_t len = strlen(host_fn) + 6;
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

void vdfs_error(const char *err)
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

static void vdfs_hosterr(int errnum)
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
    vdfs_error(msg);
}

// Populate a VDFS entry from host information.

static void init_dir(vdfs_entry *ent)
{
    ent->u.dir.children = NULL;
    ent->u.dir.scan_mtime = 0;
    ent->u.dir.scan_seq = 0;
    ent->u.dir.sorted = SORT_NONE;
    ent->u.dir.boot_opt = 0;
    ent->u.dir.title_len = 0;
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
                log_debug("vdfs: dir %.*s has become a file", ent->acorn_len, ent->acorn_fn);
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
                log_debug("vdfs: dir %.*s has become a file", ent->acorn_len, ent->acorn_fn);
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

static const char c_esc_bin[] = "\a\b\e\f\n\r\t\v\\\"";
static const char c_esc_asc[] = "abefnrtv\\\"";

static const char *scan_inf_start(vdfs_entry *ent, char inf_line[MAX_INF_LINE])
{
    *ent->host_inf = '.';
    FILE *fp = fopen(ent->host_path, "rt");
    *ent->host_inf = '\0';
    if (fp) {
        const char *lptr = fgets(inf_line, MAX_INF_LINE, fp);
        fclose(fp);
        if (lptr) {
            int ch;
            while ((ch = *lptr++) == ' ' || ch == '\t')
                ;
            if (ch == '"') {
                char tmp[MAX_FILE_NAME+2];
                char *name = tmp;
                char *ptr = tmp;
                char *end = ptr + MAX_FILE_NAME + 2;
                while ((ch = *lptr++) && ch != '"' && ch != '\n') {
                    log_debug("vdfs: scan_inf_start, encoded, ch=%02X", ch);
                    if (ch == '\\') {
                        int nc = *lptr++;
                        log_debug("vdfs: scan_inf_start, escape, nc=%02X", nc);
                        if (nc >= '0' && nc <= '9') {
                            ch = nc & 7;
                            nc = *lptr;
                            log_debug("vdfs: scan_inf_start, numeric1, ch=%02X, nc=%02X", ch, nc);
                            if (nc >= '0' && nc <= '9') {
                                ch = (ch << 3) | (nc & 7);
                                nc = *++lptr;
                                log_debug("vdfs: scan_inf_start, numeric2, ch=%02X, nc=%02X", ch, nc);
                                if (nc >= '0' && nc <= '9') {
                                    ch = (ch << 3) | (nc & 7);
                                    log_debug("vdfs: scan_inf_start, numeric3, ch=%02X, nc=%02X", ch, nc);
                                    ++lptr;
                                }
                            }
                        }
                        else {
                            const char *pos = strchr(c_esc_asc, nc);
                            if (pos)
                                ch = c_esc_bin[pos-c_esc_asc];
                            else
                                ch = nc;
                        }
                    }
                    if (ptr < end)
                        *ptr++ = ch;
                }
                int len = ptr - name;
                log_dump("vdfs: scan_inf_start, encoded name: ", (uint8_t *)name, len);
                if (name[1] == '.') {
                    ent->dfs_dir = name[0];
                    name += 2;
                    len -= 2;
                }
                else
                    ent->dfs_dir = '$';
                ent->acorn_len = len;
                memcpy(ent->acorn_fn, name, len);
            }
            else {
                log_debug("vdfs: scan_inf_start: ch=%02X, 0=%02X, 1=%02X", ch, lptr[0], lptr[1]);
                char *ptr = ent->acorn_fn;
                char *end = ptr + MAX_FILE_NAME;
                if (*lptr == '.') {
                    ent->dfs_dir = ch;
                    ++lptr;
                    ch = *lptr++;
                }
                else
                    ent->dfs_dir = '$';
                while (ch && ch != ' ' && ch != '\t' && ch != '\n') {
                    if (ptr < end)
                        *ptr++ = ch;
                    ch = *lptr++;
                }
                ent->acorn_len = ptr - ent->acorn_fn;
                log_debug("vdfs: scan_inf_start, simple_name %c.%.*s", ent->dfs_dir, ent->acorn_len, ent->acorn_fn);
            }
            if (ch != '\n')
                return lptr;
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
        ent->attribs |= attribs & (ATTR_USER_LOCKD|ATTR_OTHR_LOCKD|ATTR_NL_TRANS);
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
                    dir->u.dir.title_len = ptr - dir->u.dir.title;
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

static unsigned scan_inf_dir_old(const char *lptr, vdfs_entry *dir)
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
    char *ptr = dir->u.dir.title;
    char *end = ptr + MAX_TITLE;
    while (ptr < end && ch && ch != '\n') {
        *ptr++ = ch & 0x7f;
        ch = *lptr++;
    }
    dir->u.dir.title_len = ptr - dir->u.dir.title;
    return opt;
}

static void scan_inf_dir(vdfs_entry *dir)
{
    char inf_line[MAX_INF_LINE];
    dir->u.dir.title_len = 0;
    const char *lptr = scan_inf_start(dir, inf_line);
    if (lptr) {
        const char *eptr = strchr(lptr, '=');
        if (eptr)
            scan_inf_dir_new(dir, lptr, eptr);
        else
            dir->u.dir.boot_opt = scan_inf_dir_old(lptr, dir);
    }
}

static void scan_entry(vdfs_entry *ent)
{
    scan_attr(ent);
    if (ent->attribs & ATTR_IS_DIR)
        scan_inf_dir(ent);
    else
        scan_inf_file(ent);
    if (ent->acorn_len == 0)
        hst2bbc(ent);
}

static void init_entry(vdfs_entry *ent)
{
    ent->next = NULL;
    ent->host_path = NULL;
    ent->host_fn = ".";
    ent->acorn_len = 0;
    ent->dfs_dir = '$';
    ent->attribs = 0;
}

static int vdfs_cmpch(int ca, int cb)
{
    if (ca == cb)
        return 0;
    if (ca >= 'a' && ca <= 'z')
        ca = ca - 'a' + 'A';
    if (cb >= 'a' && cb <= 'z')
        cb = cb - 'a' + 'A';
    return ca - cb;
}

static int vdfs_cmp(vdfs_entry *ent_a, vdfs_entry *ent_b)
{
    const unsigned char *namea = (unsigned char *)ent_a->acorn_fn;
    const unsigned char *nameb = (unsigned char *)ent_b->acorn_fn;
    const unsigned char *enda = namea + ent_a->acorn_len;
    const unsigned char *endb = nameb + ent_b->acorn_len;

    while (namea < enda && nameb < endb) {
        int d = vdfs_cmpch(*namea++, *nameb++);
        if (d)
            return d;
    }
    if (namea < enda)
        return 1;
    else if (nameb < endb)
        return -1;
    else
        return 0;
}

static vdfs_entry *acorn_search(vdfs_entry *dir, vdfs_entry *obj)
{
    for (vdfs_entry *ent = dir->u.dir.children; ent; ent = ent->next)
        if (!vdfs_cmp(ent, obj))
            return ent;
    return NULL;
}

bool vdfs_wildmat(const char *pattern, unsigned pat_len, const char *candidate, unsigned can_len)
{
    log_debug("vdfs: vdfs_wildmat, pattern=%.*s, candidate=%.*s", pat_len, pattern, can_len, candidate);
    while (pat_len) {
        int pat_ch = *pattern++;
        --pat_len;
        if (pat_ch == '*') {
            if (!pat_len) {
                log_debug("vdfs: vdfs_wildmat return#1, true, * matches nothing");
                return true;
            }
            do {
                if (vdfs_wildmat(pattern, pat_len, candidate++, can_len--)) {
                    log_debug("vdfs: vdfs_wildmat return#2, true, * recursive");
                    return true;
                }
            } while (can_len);
            log_debug("vdfs: vdfs_wildmat return#3, false, * mismatch");
            return false;
        }
        if (can_len) {
            int can_ch = *candidate++;
            --can_len;
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
        else {
            log_debug("vdfs: vdfs_wildmat return#5, false, candidate too short");
            return false;
        }
    }
    if (can_len) {
        log_debug("vdfs: vdfs_wildmat return#6, false, pattern too short");
        return false;
    }
    else {
        log_debug("vdfs: vdfs_wildmat return#7, true, reached end");
        return true;
    }
}

static vdfs_entry *wild_search(vdfs_entry *dir, vdfs_findres *res)
{
    for (vdfs_entry *ent = dir->u.dir.children; ent; ent = ent->next)
        if (vdfs_wildmat(res->acorn_fn, res->acorn_len, ent->acorn_fn, ent->acorn_len))
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
    vdfs_entry *ent = malloc(sizeof(vdfs_entry));
    if (ent) {
        init_entry(ent);
        ent->parent = dir;
        char *host_path = make_host_path(ent, host_fn);
        if (host_path) {
            scan_entry(ent);
            if (acorn_search(dir, ent)) {
                // name was already in use - generate a unique one.
                int seq_ch = '0';
                size_t ix = ent->acorn_len;
                if (ix > (MAX_FILE_NAME-2)) {
                    ix = MAX_FILE_NAME-2;
                    ent->acorn_len = MAX_FILE_NAME;
                }
                else
                    ent->acorn_len = ix + 2;
                ent->acorn_fn[ix] = '~';
                ent->acorn_fn[ix+1] = seq_ch;
                while (acorn_search(dir, ent)) {
                    if ((seq_ch = next_seq_ch(seq_ch)) < 0) {
                        log_warn("vdfs: unable to create unique acorn name for %s", host_fn);
                        free(ent);
                        return NULL;
                    }
                    ent->acorn_fn[ix+1] = seq_ch;
                }
                log_debug("vdfs: new_entry: unique name %.*s used", ent->acorn_len, ent->acorn_fn);
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
    for (vdfs_entry *ent = dir->u.dir.children; ent; ent = ent->next)
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
    struct stat stb;

    // Has this been scanned sufficiently recently already?

    if (stat(dir->host_path, &stb) == -1)
        log_warn("vdfs: unable to stat directory '%s': %s", dir->host_path, strerror(errno));
    else if (scan_seq <= dir->u.dir.scan_seq && stb.st_mtime <= dir->u.dir.scan_mtime) {
        log_debug("vdfs: using cached dir info for %s", dir->host_path);
        return 0;
    }
    show_activity();

    DIR *dp = opendir(dir->host_path);
    if (dp) {
        scan_dir_host(dir, dp);
        closedir(dp);
        scan_inf_dir(dir);
        dir->u.dir.scan_seq = scan_seq;
        dir->u.dir.scan_mtime = stb.st_mtime;
        return 0;
    }
    else {
        log_warn("vdfs: unable to opendir '%s': %s", dir->host_path, strerror(errno));
        return 1;
    }
}

/*
 * Parse a name, probably of a file, from 'addr' on the guest into 'str'
 * on the host with a maximum length of 'size'.  Return the address on
 * the guest immediately after the name, or zero if too long.
 */

static uint16_t parse_name(vdfs_path *path, uint16_t addr)
{
    char *ptr = path->path;
    char *end = ptr + sizeof(path->path) - 1;
    int quote= 0;

    log_debug("vdfs: parse_name: addr=%04x", addr);
    int ch = readmem(addr);
    while (ch == ' ' || ch == '\t')
        ch = readmem(++addr);

    if (ch == '"') {
        quote = 1;
        ch = readmem(++addr);
    }
    while (ptr < end) {
        if (ch == '\r' || ((ch == ' ' || ch == '\t') && !quote) || (ch == '"' && quote)) {
            path->len = ptr - path->path;
            log_debug("vdfs: parse_name: name=%.*s", path->len, path->path);
            return addr;
        }
        if (ch == '|' && (fs_flags & GSTRANS_FN)) {
            ch = readmem(++addr);
            if (ch == '\r')
                break;
            if (ch =='?')
                ch = 0x7f;
            else if (ch == '!') {
                ch = readmem(++addr);
                if (ch == '|') {
                    ch = readmem(++addr);
                    if (ch == '\r')
                        break;
                    if (ch =='?')
                        ch = 0x7f;
                    else if (ch != '"')
                        ch &= 0x1f;
                }
                ch |= 0x80;
            }
            else if (ch != '"')
                ch &= 0x1f;
        }
        *ptr++ = ch;
        ch = readmem(++addr);
    }
    vdfs_error(err_badname);
    return 0;
}

static bool check_valid_dir(vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    if (ent && ent->attribs & ATTR_IS_DIR)
        return true;
    log_warn("vdfs: %s directory is not valid", dir->desc);
    vdfs_error(err_baddir);
    return false;
}

// Given an Acorn filename, find the VDFS entry.

static vdfs_entry *find_entry_adfs(const vdfs_path *path, vdfs_findres *res, vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    res->parent = NULL;
    res->errmsg = err_notfound;
    res->dfs_dir = dir->dfs_dir;

    int path_len = path->len;
    for (const char *fn_src = path->path;;) {
        char *fn_ptr = res->acorn_fn;
        char *fn_end = fn_ptr + MAX_FILE_NAME;
        int ch;
        while (path_len--) {
            ch = *fn_src++;
            if (ch == '.')
                break;
            if (fn_ptr >= fn_end) {
                res->errmsg = err_badname;
                return NULL;
            }
            *fn_ptr++ = ch;
        }
        int len = fn_ptr - res->acorn_fn;
        res->acorn_len = len;
        int fn0 = res->acorn_fn[0];
        int fn1 = res->acorn_fn[1];
        if ((len == 1 && (fn0 == '$' || fn0 == '&')) || (len == 2 && fn0 == ':' && fn1 >= '0' && fn1 <= '9'))
            ent = &root_dir;
        else if (len == 1 && fn0 == '%' && check_valid_dir(&lib_dir))
            ent = lib_dir.dir;
        else if (len == 1 && fn0 == '^')
            ent = ent->parent;
        else {
            vdfs_entry *ptr;
            if (!scan_dir(ent) && (ptr = wild_search(ent, res)))
                ent = ptr;
            else {
                if (ch != '.')
                    res->parent = ent;
                log_debug("vdfs: find_entry: acorn path %.*s not found", path->len, path->path);
                return NULL; // not found
            }
        }
        if (ch != '.') {
            log_debug("vdfs: find_entry: acorn path %.*s found as %s", path->len, path->path, ent->host_path);
            return ent;
        }
        if (!(ent->attribs & ATTR_IS_DIR)) {
            log_debug("vdfs: find_entry: acorn path %.*s has file %s where directory expected", path->len, path->path, ent->host_path);
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
    } while (!(ent->attribs & ATTR_EXISTS) || !vdfs_wildmat(res->acorn_fn, res->acorn_len, ent->acorn_fn, ent->acorn_len));
    return ent;
}

static vdfs_entry *find_entry_dfs(const vdfs_path *path, vdfs_findres *res, vdfs_dirlib *dir)
{
    int srchdir = dir->dfs_dir;
    int len = path->len;
    const char *filename = path->path;
    log_debug("vdfs: find_entry_dfs, filename=%.*s, dfsdir=%c", len, filename, srchdir);
    if (!len) {
        vdfs_error(err_badname);
        res->parent = NULL;
        return NULL;
    }
    int ic = filename[0];
    if (ic == ':') {
        int drive = filename[1];
        if (len < 4 || drive < '0' || drive > '3' || filename[2] != '.') {
            vdfs_error(err_badname);
            res->parent = NULL;
            return NULL;
        }
        filename += 3;
        len -= 3;
        ic = filename[0];
    }
    if (len >= 2 && filename[1] == '.') {
        srchdir = ic;
        filename += 2;
        len -= 2;
        log_debug("vdfs: find_entry_dfs, parsed DFS dir %c, filename=%.*s", srchdir, len, filename);
    }
    res->dfs_dir = srchdir;
    if (len > MAX_FILE_NAME) {
        vdfs_error(err_badname);
        res->parent = NULL;
        return NULL;
    }
    memcpy(res->acorn_fn, filename, len);
    res->acorn_len = len;
    if (!scan_dir(dir->dir)) {
        for (vdfs_entry *ent = dir->dir->u.dir.children; ent; ent = ent->next) {
            log_debug("vdfs: find_entry_dfs, considering entry %c.%.*s", ent->dfs_dir, ent->acorn_len, ent->acorn_fn);
            if (srchdir == '*' || srchdir == '#' || !vdfs_cmpch(srchdir, ent->dfs_dir)) {
                log_debug("vdfs: find_entry_dfs, matched DFS dir");
                if (vdfs_wildmat(res->acorn_fn, len, ent->acorn_fn, ent->acorn_len))
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
    log_debug("vdfs: find_next_dfs, pattern=%.*s, start=%c.%.*s, srchdir=%c", res->acorn_len, res->acorn_fn, ent->dfs_dir, ent->acorn_len, ent->acorn_fn, srchdir);
    do {
        ent = ent->next;
        if (!ent)
            return NULL;
        log_debug("vdfs: find_next_dfs, checking %c.%.*s", ent->dfs_dir, ent->acorn_len, ent->acorn_fn);
    } while (!(ent->attribs & ATTR_EXISTS) || !(srchdir == '*' || srchdir == '#' || srchdir == ent->dfs_dir) || !vdfs_wildmat(res->acorn_fn, res->acorn_len, ent->acorn_fn, ent->acorn_len));
    return ent;
}

static vdfs_entry *add_new_file(vdfs_findres *res)
{
    vdfs_entry *dir = res->parent;
    char host_fn[MAX_FILE_NAME+3];

    vdfs_entry *new_ent = malloc(sizeof(vdfs_entry));
    if (new_ent) {
        init_entry(new_ent);
        new_ent->acorn_len = res->acorn_len;
        memcpy(new_ent->acorn_fn, res->acorn_fn, res->acorn_len);
        log_debug("vdfs: new_entry, acorn_len=%u, acorn_fn=%.*s", res->acorn_len, res->acorn_len, res->acorn_fn);
        new_ent->dfs_dir = res->dfs_dir;
        char *host_end = bbc2hst(res, host_fn);
        if (host_end == host_fn || host_search(dir, host_fn)) {
            /* host name is empty or already exists, generate a unique name */
            int seq_ch = '0';
            host_end[0] = '~';
            host_end[1] = seq_ch;
            host_end[2] = 0;
            while (host_search(dir, host_fn)) {
                if ((seq_ch = next_seq_ch(seq_ch)) < 0) {
                    log_warn("vdfs: unable to create unique host name for %c.%.*s", res->dfs_dir, res->acorn_len, res->acorn_fn);
                    res->errmsg = err_badname;
                    free(new_ent);
                    return NULL;
                }
                host_end[1] = seq_ch;
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

static void write_back_name(vdfs_entry *ent, FILE *fp)
{
    unsigned char tmp[MAX_FILE_NAME+2];
    int len = ent->acorn_len;
    bool quote = false;
    tmp[0] = ent->dfs_dir;
    tmp[1] = '.';
    memcpy(tmp+2, ent->acorn_fn, len);
    len += 2;
    log_dump("vdfs: write_back_name, tmp=", (uint8_t *)tmp, len);
    for (unsigned char *ptr = tmp; len; --len) {
        int ch = *ptr++;
        if (ch <= ' ' || ch >= 0x7f || ch == '"')
            quote = true;
    }
    len = ent->acorn_len+2;
    if (quote) {
        char encoded[MAX_FILE_NAME*4+2];
        char *enc = encoded;
        *enc++ = '"';
        for (unsigned char *ptr = tmp; len; --len) {
            int ch = *ptr++;
            if (ch >= ' ' && ch <= 0x7e && ch != '\\' && ch != '\"')
                *enc++ = ch;
            else {
                *enc++ = '\\';
                const char *pos;
                if (ch && (pos = strchr(c_esc_bin, ch)))
                    *enc++ = c_esc_asc[pos-c_esc_bin];
                else {
                    *enc++ = '0' + ((ch >> 6) & 0x07);
                    *enc++ = '0' + ((ch >> 3) & 0x07);
                    *enc++ = '0' + (ch & 0x07);
                }
            }
        }
        *enc++ = '"';
        len = enc-encoded;
        log_debug("vdfs: write_back_name, enc_len=%d, encoded=%.*s", len, len, encoded);
        fwrite(encoded, len, 1, fp);
    }
    else {
        log_debug("vdfs: write_back_name, ascii=%.*s", len, tmp);
        fwrite(tmp, len, 1, fp);
    }
}

static void write_back(vdfs_entry *ent)
{
    show_activity();
    *ent->host_inf = '.'; // select .inf file.
    FILE *fp = fopen(ent->host_path, "wt");
    if (fp) {
        const struct tm *tp = localtime(&ent->mtime);
        write_back_name(ent, fp);
        if (ent->attribs & ATTR_IS_DIR) {
            fprintf(fp, " OPT=%02X DIR=1 MDATE=%04X MTIME=%06X", ent->u.dir.boot_opt, unix_date_acorn(tp), unix_time_acorn(tp));
            if (ent->attribs & ATTR_BTIME_VALID) {
                tp = localtime(&ent->btime);
                fprintf(fp, " CDATE=%04X CTIME=%06X", unix_date_acorn(tp), unix_time_acorn(tp));
            }
            if (ent->u.dir.title_len) {
                fputs(" TITLE=", fp);
                if (strpbrk(ent->u.dir.title, " \t")) {
                    putc('"', fp);
                    fwrite(ent->u.dir.title, ent->u.dir.title_len, 1, fp);
                    putc('"', fp);
                }
                else
                    fwrite(ent->u.dir.title, ent->u.dir.title_len, 1, fp);
            }
            putc('\n', fp);
        }
        else {
            fprintf(fp, " %08X %08X %08X %02X %04X %06X", ent->u.file.load_addr, ent->u.file.exec_addr, ent->u.file.length, ent->attribs & (ATTR_ACORN_MASK|ATTR_NL_TRANS), unix_date_acorn(tp), unix_time_acorn(tp));
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
    vdfs_entry *ent = vdfs_chan[channel].ent;
    if (ent) {
        FILE *fp = vdfs_chan[channel].fp;
        if (fp) {
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
    for (int channel = 0; channel < NUM_CHANNELS; channel++)
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
            new_root.acorn_len = 1;
            scan_entry(&new_root);
            if (new_root.attribs & ATTR_IS_DIR) {
                vdfs_close();
                root_dir = new_root;
                root_dir.parent = cur_dir.dir = prev_dir.dir = cat_dir = &root_dir;
                vdfs_findres res;
                vdfs_path lib_path = { 3, "Lib" };
                lib_dir.dir = find_entry_adfs(&lib_path, &res, &cur_dir);
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
    vdfs_path path;    
    vdfs_findres res;
    int len = savestate_load_var(f);
    if (len < MAX_ACORN_PATH) {
        path.len = len;
        fread(path.path, len, 1, f);
        log_debug("vdfs: loadstate setting %s directory to $.%.*s", which, len, path.path);
        vdfs_entry *ent = find_entry_adfs(&path, &res, &cur_dir);
        if (ent && !(ent->attribs & ATTR_IS_DIR))
            ent = NULL;
        return ent;
    }
    else {
        log_debug("vdfs: unable to restore %s, name too long", which);
        fseek(f, len, SEEK_CUR);
        return NULL;
    }
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
    int ch = getc(f);
    if (ch != EOF) {
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
    size_t len = ent->acorn_len;

    if (parent->parent != parent)
        len += ss_calc_len(ent->parent) + 1;
    return len;
}

static void ss_save_ent(vdfs_entry *ent, FILE *f)
{
    vdfs_entry *parent = ent->parent;

    if (parent->parent == parent)
        fwrite(ent->acorn_fn, ent->acorn_len, 1, f);
    else {
        ss_save_ent(ent->parent, f);
        putc('.', f);
        fwrite(ent->acorn_fn, ent->acorn_len, 1, f);
    }
}

static void ss_save_dir1(vdfs_entry *ent, FILE *f)
{
    if (!ent)
        putc('N', f);
    else if (ent == &root_dir)
        putc('R', f);
    else {
        putc('S', f);
        size_t len = ss_calc_len(ent);
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

    if (flags & 0x40) {
        // Pseudo addressing.  How many banks into the ram does the
        // address call for?

        int banks = start / 16384;
        start %= 16384;

        // Find the nth RAM bank.

        if ((romid = mem_findswram(banks)) < 0)  {
            vdfs_error(err_no_swr);
            return -1;
        }
        log_debug("vdfs: swr_calc_addr: pseudo addr bank=%02d, start=%04x", romid, start);
    } else {
        // Absolutre addressing.

        if (start < 0x8000 || start >= 0xc000) {
            vdfs_error(err_badaddr);
            return -1;
        }

        if ((romid > 16) | !rom_slots[romid].swram) {
            vdfs_error(err_no_swr);
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
    log_debug("vdfs: exec_swr_fs: flags=%02x, fn=%04x, romid=%02d, start=%04x, len=%04x", flags, fname, romid, start, pblen);
    if (check_valid_dir(&cur_dir)) {
        if ((romid = swr_calc_addr(flags, &start, romid)) >= 0) {
            vdfs_path path;
            vdfs_findres res;
            if (parse_name(&path, fname)) {
                vdfs_entry *ent = find_entry(&path, &res, &cur_dir);
                if (flags & 0x80) {
                    // read file into sideways RAM.
                    int len = 0x4000 - start;
                    if (len > 0) {
                        if (ent && ent->attribs & ATTR_EXISTS) {
                            FILE *fp;
                            if (ent->attribs & ATTR_IS_DIR)
                                vdfs_error(err_wont);
                            else if ((fp = fopen(ent->host_path, "rb"))) {
                                if (fread(rom + romid * 0x4000 + start, len, 1, fp) != 1 && ferror(fp))
                                    log_warn("vdfs: error reading file '%s': %s", ent->host_fn, strerror(errno));
                                fclose(fp);
                            } else {
                                log_warn("vdfs: unable to load file '%s': %s", ent->host_fn, strerror(errno));
                                vdfs_hosterr(errno);
                            }
                        } else
                            vdfs_error(res.errmsg);
                    } else
                        vdfs_error(err_too_big);
                }
                else {
                    // write sideways RAM to file.
                    int len = pblen;
                    if (len <= 16384) {
                        if (!ent && res.parent)
                            ent = add_new_file(&res);
                        if (ent) {
                            FILE *fp = fopen(ent->host_path, "wb");
                            if (fp) {
                                fwrite(rom + romid * 0x4000 + start, len, 1, fp);
                                fclose(fp);
                                uint32_t load_add = 0xff008000 | (romid << 16) | start;
                                ent->attribs |= ATTR_EXISTS;
                                scan_attr(ent);
                                ent->u.file.load_addr = load_add;
                                ent->u.file.exec_addr = load_add;
                                write_back(ent);
                            } else
                                log_warn("vdfs: unable to create file '%s': %s", ent->host_fn, strerror(errno));
                        } else
                            vdfs_error(res.errmsg);
                    } else
                        vdfs_error(err_too_big);
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
    log_debug("vdfs: exec_swr_ram: flags=%02x, ram_start=%04x, len=%04x, sw_start=%04x, romid=%02d\n", flags, ram_start, len, sw_start, romid);
    int16_t nromid = swr_calc_addr(flags, &sw_start, romid);
    if (nromid >= 0) {
        uint8_t *rom_ptr = rom + romid * 0x4000 + sw_start;
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
    int nyb = hex2nyb(ch);
    if (nyb >= 0) {
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

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (ch == '+') {
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
        return srp_hex(ch, addr, len);
    }
    else {
        uint16_t end;
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
    int16_t romid;

    addr = srp_romid(addr, &romid);
    if (romid >= 0)
        flag &= ~0x40;
    if (fs_flags & VDFS_ACTIVE) {
        exec_swr_fs(flag, fnaddr, romid, start, len);
        p.c = 0;
        return true;
    }
    else {
        int ch;
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
    vdfs_error(err_badparms);
    return true;
}

static bool cmd_srsave(uint16_t addr)
{
    uint16_t fnadd;
    if ((addr = srp_fn(addr, &fnadd))) {
        uint16_t start;
        if ((addr = srp_start(addr, &start))) {
            uint16_t len;
            if ((addr = srp_length(addr, start, &len)))
                return srp_tail(addr, 0x40, fnadd, start, len);
        }
    }
    vdfs_error(err_badparms);
    return true;
}

static void srcopy(uint16_t addr, uint8_t flags)
{
    uint16_t ram_start;
    if ((addr = srp_start(addr, &ram_start))) {
        uint16_t len;
        if ((addr = srp_length(addr, ram_start, &len))) {
            uint16_t sw_start;
            if ((addr = srp_start(addr, &sw_start))) {
                int16_t romid;
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
    vdfs_error(err_badparms);
}

/*
 * OSFILE and supporting functions..
 */

static bool no_wildcards(const vdfs_path *path)
{
    const char *ptr = path->path;
    for (int len = path->len; len; --len) {
        int ch = *ptr++;
        if (ch == '*' || ch == '#') {
            vdfs_error(err_wildcard);
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

static uint32_t write_bytes(FILE *fp, uint32_t addr, size_t bytes, unsigned nlflag)
{
    if (addr >= 0xffff0000 || curtube == -1) {
        char buffer[8192];
        while (bytes >= sizeof buffer) {
            char *ptr = buffer;
            size_t chunk = sizeof buffer;
            while (chunk--) {
                int ch = readmem(addr++);
                if (ch == '\r' && nlflag)
                    ch = '\n';
                *ptr++ = ch;
            }
            fwrite(buffer, sizeof buffer, 1, fp);
            bytes -= sizeof buffer;
        }
        if (bytes > 0) {
            char *ptr = buffer;
            size_t chunk = bytes;
            while (chunk--) {
                int ch = readmem(addr++);
                if (ch == '\r' && nlflag)
                    ch = '\n';
                *ptr++ = ch;
            }
            fwrite(buffer, bytes, 1, fp);
        }
    }
    else {
        char buffer[8192];
        while (bytes >= sizeof buffer) {
            char *ptr = buffer;
            size_t chunk = sizeof buffer;
            while (chunk--) {
                int ch = tube_readmem(addr++);
                if (ch == '\r' && nlflag)
                    ch = '\n';
                *ptr++ = ch;
            }
            fwrite(buffer, sizeof buffer, 1, fp);
            bytes -= sizeof buffer;
        }
        if (bytes > 0) {
            char *ptr = buffer;
            size_t chunk = bytes;
            while (chunk--) {
                int ch = tube_readmem(addr++);
                if (ch == '\r' && nlflag)
                    ch = '\n';
                *ptr++ = ch;
            }
            fwrite(buffer, bytes, 1, fp);
        }
    }
    return addr;
}

static uint32_t cfile_callback(FILE *fp, uint32_t start_addr, size_t bytes, unsigned nlflag)
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
    unsigned len = ent->acorn_len;

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
    while (len && mem_ptr < mem_end) {
        writemem(mem_ptr++, *ptr++);
        --len;
    }
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

static void osfile_write(uint32_t pb, const vdfs_path *path, uint32_t (*callback)(FILE *fp, uint32_t addr, size_t bytes, unsigned nlflag))
{
    if (no_wildcards(path)) {
        FILE *fp;
        vdfs_findres res;
        vdfs_entry *ent = find_entry(path, &res, &cur_dir);
        if (ent) {
            if (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)) {
                log_debug("vdfs: attempt to save file %.*s which is already open via OSFIND", res.acorn_len, res.acorn_fn);
                vdfs_error(err_isopen);
                return;
            }
            if (ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR) {
                    log_debug("vdfs: attempt to create file %.*s over an existing dir", res.acorn_len, res.acorn_fn);
                    vdfs_error(err_direxist);
                    return;
                }
            }
        }
        else if (!res.parent) {
            log_debug("vdfs: osfile_write, no parent into which to create new file");
            vdfs_error(res.errmsg);
            return;
        }
        else if (!(ent = add_new_file(&res))) {
            vdfs_error(err_nomem);
            return;
        }
        if ((fp = fopen(ent->host_path, "wb"))) {
            uint32_t start_addr = readmem32(pb+0x0a);
            uint32_t end_addr = readmem32(pb+0x0e);
            ent->attribs = (ent->attribs & ~ATTR_IS_DIR) | ATTR_EXISTS;
            callback(fp, start_addr, end_addr - start_addr, ent->attribs & ATTR_NL_TRANS);
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
            vdfs_hosterr(err);
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

static void osfile_set_meta(uint32_t pb, const vdfs_path *path, uint16_t which)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, &cur_dir);
    if (ent && ent->attribs & ATTR_EXISTS) {
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

static void osfile_get_attr(uint32_t pb, const vdfs_path *path)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, &cur_dir);
    if (ent && ent->attribs & ATTR_EXISTS) {
        scan_entry(ent);
        osfile_attribs(pb, ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_get_extattr(uint32_t pb, const vdfs_path *path)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, &cur_dir);
    if (ent && ent->attribs & ATTR_EXISTS) {
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

static void osfile_set_exattr(uint32_t pb, const vdfs_path *path)
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
            vdfs_error(err_delcsd);
        else if (ent == lib_dir.dir)
            vdfs_error(err_dellib);
        else if (rmdir(ent->host_path) == 0) {
            if (ent == prev_dir.dir)
                prev_dir = cur_dir;
            ent->attribs &= ~(ATTR_IS_DIR|ATTR_EXISTS);
            delete_inf(ent);
            a = 2;
        } else
            vdfs_hosterr(errno);
    } else {
        if (unlink(ent->host_path) == 0) {
            ent->attribs &= ~ATTR_EXISTS;
            delete_inf(ent);
            a = 1;
        } else
            vdfs_hosterr(errno);
    }
}

static void osfile_delete(uint32_t pb, const vdfs_path *path)
{
    if (no_wildcards(path)) {
        vdfs_findres res;
        vdfs_entry *ent = find_entry(path, &res, &cur_dir);
        if (ent && ent->attribs & ATTR_EXISTS) {
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
        vdfs_hosterr(errno);
        log_debug("vdfs: unable to mkdir '%s': %s", ent->host_path, strerror(errno));
    }
}

static void osfile_cdir(const vdfs_path *path)
{
    if (no_wildcards(path)) {
        vdfs_entry *ent;
        vdfs_findres res;
        if ((ent = find_entry(path, &res, &cur_dir))) {
            if (ent->attribs & ATTR_EXISTS) {
                if (!(ent->attribs & ATTR_IS_DIR)) {
                    log_debug("vdfs: attempt to create dir %.*s on top of an existing file", res.acorn_len, res.acorn_fn);
                    vdfs_error(err_filexist);  // file in the way.
                }
            } else
                create_dir(ent);
        } else {
            vdfs_entry *parent = res.parent;
            if (parent && parent->attribs & ATTR_EXISTS) {
                if ((ent = add_new_file(&res))) {
                    ent->u.dir.boot_opt = 0;
                    ent->u.dir.title_len = 0;
                    create_dir(ent);
                }
            } else {
                log_debug("vdfs: attempt to create dir %.*s in non-existent directory", res.acorn_len, res.acorn_fn);
                vdfs_error(res.errmsg);
            }
        }
    }
}

static void update_length(vdfs_entry *ent, uint32_t addr, uint32_t dest)
{
    uint32_t length = dest - addr;
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
    unsigned nlflag = ent->attribs & ATTR_NL_TRANS;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--) {
            int ch = *ptr++;
            if (ch == '\n' && nlflag)
                ch = '\r';
            writemem(dest++, ch);
        }
    }
    update_length(ent, addr, dest);
}

static void read_file_tube(vdfs_entry *ent, FILE *fp, uint32_t addr)
{
    char buffer[32768];
    size_t nbytes;
    uint32_t dest = addr;
    unsigned nlflag = ent->attribs & ATTR_NL_TRANS;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--) {
            int ch = *ptr++;
            if (ch == '\n' && nlflag)
                ch = '\r';
            tube_writemem(dest++, ch);
        }
    }
    update_length(ent, addr, dest);
}

static void osfile_load(uint32_t pb, const vdfs_path *path)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, &cur_dir);
    if (ent && ent->attribs & ATTR_EXISTS) {
        if (ent->attribs & ATTR_IS_DIR)
            vdfs_error(err_wont);
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
                vdfs_hosterr(errno);
            }
        }
    } else
        vdfs_error(res.errmsg);
}

static void osfile(void)
{
    uint32_t pb = (y << 8) | x;

    if (a <= 0x08 || a == 0xff || a == 0xfd) {
        log_debug("vdfs: osfile(A=%02X, X=%02X, Y=%02X)", a, x, y);
        if (check_valid_dir(&cur_dir)) {
            vdfs_path path;
            if (parse_name(&path, readmem16(pb))) {
                switch (a) {
                    case 0x00:  // save file.
                        osfile_write(pb, &path, write_bytes);
                        break;
                    case 0x01:  // set all attributes.
                        osfile_set_meta(pb, &path, META_LOAD|META_EXEC|META_ATTR);
                        break;
                    case 0x02:  // set load address only.
                        osfile_set_meta(pb, &path, META_LOAD);
                        break;
                    case 0x03:  // set exec address only.
                        osfile_set_meta(pb, &path, META_EXEC);
                        break;
                    case 0x04:  // write attributes.
                        osfile_set_meta(pb, &path, META_ATTR);
                        break;
                    case 0x05:  // get addresses and attributes.
                        osfile_get_attr(pb, &path);
                        break;
                    case 0x06:
                        osfile_delete(pb, &path);
                        break;
                    case 0x07:
                        osfile_write(pb, &path, cfile_callback);
                        break;
                    case 0x08:
                        osfile_cdir(&path);
                        break;
                    case 0xfc:
                        osfile_set_exattr(pb, &path);
                        break;
                    case 0xfd:
                        osfile_get_extattr(pb, &path);
                        break;
                    case 0xff:  // load file.
                        osfile_load(pb, &path);
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

static vdfs_open_file *get_open_read(int channel)
{
    int chan = channel - MIN_CHANNEL;

    if (chan >= 0 && chan < NUM_CHANNELS) {
        vdfs_open_file *cp = &vdfs_chan[chan];
        if (cp->fp)
            return cp;
        log_debug("vdfs: attempt to use closed channel %d", channel);
    } else
        log_debug("vdfs: channel %d out of range", channel);
    vdfs_error(err_channel);
    return NULL;
}

static void osbget(void)
{
    log_debug("vdfs: osbget(A=%02X, X=%02X, Y=%02X)", a, x, y);
    show_activity();
    vdfs_open_file *cp = get_open_read(y);
    if (cp) {
        int ch = getc(cp->fp);
        if (ch != EOF) {
            if (ch == '\n' && cp->ent->attribs & ATTR_NL_TRANS)
                ch = '\r';
            a = ch;
            p.c = 0;
            return;
        }
    }
    a = 0xfe;
    p.c = 1;
}

static vdfs_open_file *get_open_write(int channel)
{
    int chan = channel - MIN_CHANNEL;

    if (chan >= 0 && chan < NUM_CHANNELS) {
        vdfs_open_file *cp = &vdfs_chan[chan];
        vdfs_entry *ent = cp->ent;
        if (ent) {
            if (ent->attribs & ATTR_OPEN_WRITE) {
                if (cp->fp)
                    return cp;
                log_debug("vdfs: attempt to use write to channel %d not open for write", channel);
                vdfs_error(err_channel);
            }
            else {
                log_debug("vdfs: attempt to write to a read-only channel %d", channel);
                vdfs_error(err_nupdate);
            }
        }
        else {
            log_debug("vdfs: attempt to use closed channel %d", channel);
            vdfs_error(err_channel);
        }
    }
    else {
        log_debug("vdfs: channel %d out of range\n", channel);
        vdfs_error(err_channel);
    }
    return NULL;
}

static void osbput(void)
{
    log_debug("vdfs: osbput(A=%02X, X=%02X, Y=%02X)", a, x, y);
    show_activity();
    vdfs_open_file *cp = get_open_write(y);
    if (cp) {
        int ch = a;
        if (ch == '\r' && cp->ent->attribs & ATTR_NL_TRANS)
            ch = '\n';
        putc(ch, cp->fp);
    }
}

static void osfind(void)
{
    int acorn_mode, channel;
    vdfs_entry *ent;
    vdfs_findres res;
    const char *mode;
    uint16_t attribs;
    FILE *fp;
    vdfs_path path;

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
                vdfs_error(err_channel);
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
                vdfs_error(err_nfile);
                return;
            }
        } while (vdfs_chan[channel].ent);
        if (parse_name(&path, (y << 8) | x)) {
            ent = find_entry(&path, &res, &cur_dir);
            if (ent) {
                if ((ent->attribs & (ATTR_EXISTS|ATTR_IS_DIR)) == (ATTR_EXISTS|ATTR_IS_DIR)) {
                    vdfs_chan[channel].ent = ent;  // make "half-open"
                    a = MIN_CHANNEL + channel;
                    return;
                }
            }
            else if (!res.parent) {
                vdfs_error(res.errmsg);
                return;
            }
            if (acorn_mode == 0x40) {
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_OPEN_WRITE)
                        vdfs_error(err_isopen);
                    else {
                        mode = "rb";
                        attribs = ATTR_OPEN_READ;
                    }
                }
            }
            else if (acorn_mode == 0x80) {
                if (!no_wildcards(&path))
                    return;
                if (ent && (ent->attribs & ATTR_EXISTS) && (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)))
                    vdfs_error(err_isopen);
                else {
                    mode = "wb";
                    attribs = ATTR_OPEN_WRITE;
                    if (!ent && res.parent)
                        ent = add_new_file(&res);
                }
            }
            else if (acorn_mode == 0xc0) {
                attribs = ATTR_OPEN_READ|ATTR_OPEN_WRITE;
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE))
                        vdfs_error(err_isopen);
                    else
                        mode = "rb+";
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
    vdfs_open_file *cp = get_open_write(readmem(pb));
    if (cp) {
        show_activity();
        FILE *fp = cp->fp;
        if (a == 0x01)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        writemem32(pb+1, write_bytes(fp, readmem32(pb+1), readmem32(pb+5), cp->ent->attribs & ATTR_NL_TRANS));
        writemem32(pb+5, 0);
        writemem32(pb+9, ftell(fp));
    }
}

static size_t read_bytes(FILE *fp, uint32_t addr, size_t bytes, unsigned nlflag)
{
    char buffer[8192];
    size_t nbytes;

    while (bytes >= sizeof buffer) {
        char *ptr = buffer;
        if ((nbytes = fread(buffer, 1, sizeof buffer, fp)) <= 0)
            return bytes;
        bytes -= nbytes;
        if (addr >= 0xffff0000 || curtube == -1) {
            while (nbytes--) {
                int ch = *ptr++;
                if (ch == '\n' && nlflag)
                    ch = '\r';
                writemem(addr++, ch);
            }
        }
        else {
            while (nbytes--) {
                int ch = *ptr++;
                if (ch == '\n' && nlflag)
                    ch = '\r';
                tube_writemem(addr++, ch);
            }
        }
    }
    while (bytes > 0) {
        char *ptr = buffer;
        if ((nbytes = fread(buffer, 1, bytes, fp)) <= 0)
            return bytes;
        bytes -= nbytes;
        if (addr >= 0xffff0000 || curtube == -1) {
            while (nbytes--) {
                int ch = *ptr++;
                if (ch == '\n' && nlflag)
                    ch = '\r';
                writemem(addr++, ch);
            }
        }
        else {
            while (nbytes--) {
                int ch = *ptr++;
                if (ch == '\n' && nlflag)
                    ch = '\r';
                tube_writemem(addr++, ch);
            }
        }
    }
    return 0;
}

static int osgbpb_read(uint32_t pb)
{
    size_t undone = 0;
    vdfs_open_file *cp = get_open_read(readmem(pb));
    if (cp) {
        show_activity();
        FILE *fp = cp->fp;
        if (a == 0x03)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        uint32_t mem_ptr = readmem32(pb+1);
        size_t bytes = readmem32(pb+5);
        undone = read_bytes(fp, mem_ptr, bytes, cp->ent->attribs & ATTR_NL_TRANS);
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
        unsigned title_len = cur_dir.dir->u.dir.title_len;
        if (!title_len) {
            title = cur_dir.dir->acorn_fn;
            title_len = cur_dir.dir->acorn_len;
        }
        mem_ptr = write_len_str(mem_ptr, title, title_len);
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
        write_len_str(mem_ptr, ent->acorn_fn, ent->acorn_len);
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
                                res = vdfs_cmp(p, q);
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
            log_debug("vdfs: seq_ptr=%d, writing max %d entries starting %04X, first=%.*s", seq_ptr, n, mem_ptr, cat_ptr->acorn_len, cat_ptr->acorn_fn);
            for (;;) {
                mem_ptr = write_len_str(mem_ptr, cat_ptr->acorn_fn, cat_ptr->acorn_len);
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
                log_debug("vdfs: next=%.*s", cat_ptr->acorn_len, cat_ptr->acorn_fn);
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

#ifdef WIN32
extern int ftruncate(int fd, off_t length);
#endif

static void osargs_set_ptr(vdfs_open_file *cp)
{
    FILE *fp = cp->fp;
    fseek(fp, 0, SEEK_END);
    long cur_len = ftell(fp);
    long new_seq = readmem32(x);
    if (new_seq > cur_len) {
        if (cp->ent->attribs & ATTR_OPEN_WRITE) {
            fseek(fp, new_seq-1, SEEK_SET);
            putc(0, fp);
        }
        else
            vdfs_error(err_nupdate);
    }
    else
        fseek(fp, new_seq, SEEK_SET);
}

static void osargs_get_ext(vdfs_open_file *cp)
{
    FILE *fp = cp->fp;
    long seq_ptr = ftell(fp);
    fseek(fp, 0, SEEK_END);
    writemem32(x, ftell(fp));
    fseek(fp, seq_ptr, SEEK_SET);
}

static void osargs_set_ext(vdfs_open_file *cp)
{
    if (cp->ent->attribs & ATTR_OPEN_WRITE) {
        FILE *fp = cp->fp;
        long seq_ptr = ftell(fp);
        fseek(fp, 0, SEEK_END);
        long cur_len = ftell(fp);
        long new_len = readmem32(x);
        if (new_len > cur_len) {
            fseek(fp, new_len-1, SEEK_SET);
            putc(0, fp);
        }
        else if (new_len < cur_len) {
            ftruncate(fileno(fp), new_len);
            if (new_len < seq_ptr)
                seq_ptr = new_len;
        }
        fseek(fp, seq_ptr, SEEK_SET);
    }
    else
        vdfs_error(err_nupdate);
}

static void osargs(void)
{
    log_debug("vdfs: osargs(A=%02X, X=%02X, Y=%02X)", a, x, y);

    if (y == 0) {
        switch (a)
        {
            case 0:
                a = fs_num; // say this filing selected.
                break;
            case 1:
                for (int ch = readmem(cmd_tail); ch == ' ' || ch == '\t'; ch = readmem(++cmd_tail))
                    ;
                writemem32(x, cmd_tail);
                break;
            case 0xff:
                flush_all();
                break;
            default:
                log_debug("vdfs: osargs not implemented for y=0, a=%d", a);
        }
    }
    else {
        vdfs_open_file *cp = get_open_read(y);
        if (cp) {
            switch (a) {
            case 0:     // read sequential pointer
                writemem32(x, ftell(cp->fp));
                break;
            case 1:     // write sequential pointer
                osargs_set_ptr(cp);
                break;
            case 2:     // read file size (extent)
                osargs_get_ext(cp);
                break;
            case 3:     // set file size (extent).
                osargs_set_ext(cp);
                break;
            case 0xff:  // write any cache to media.
                fflush(cp->fp);
                break;
            default:
                log_debug("vdfs: osargs: unrecognised function code a=%d for channel y=%d", a, y);
            }
        }
    }
}

/*
 * Commands.  The next logical call to tackle is OSFSC but as this
 * includes the call by which internal filing system commands are
 * dispatched the commands need to come first.
 */

// Attributes specific to *ACCESS internals which overlap file open.

#define ATTR_BAD_CHAR    ATTR_OPEN_READ
#define ATTR_GOT_OTHER   ATTR_OPEN_WRITE

static uint_least16_t cmd_access_parse(uint16_t addr, int ch)
{
    uint_least16_t attribs = 0;
    while (ch != ' ' && ch != '\t' && ch != '\r' && ch != '/') {
        if (ch == 'R' || ch == 'r')
            attribs |= ATTR_USER_READ;
        else if (ch == 'W' || ch == 'w')
            attribs |= ATTR_USER_WRITE;
        else if (ch == 'L' || ch == 'l')
            attribs |= ATTR_USER_LOCKD;
        else if (ch == 'E' || ch == 'e')
            attribs |= ATTR_USER_EXEC;
        else if (ch == 'T' || ch == 't')
            attribs |= ATTR_NL_TRANS;
        else {
            vdfs_error(err_badparms);
            return ATTR_BAD_CHAR;
        }
        ch = readmem(addr++);
    }
    if (ch == '/') {
        attribs |= ATTR_GOT_OTHER;
        ch = readmem(addr++);
        while (ch != ' ' && ch != '\t' && ch != '\r') {
            if (ch == 'R' || ch == 'r')
                attribs |= ATTR_OTHR_READ;
            else if (ch == 'W' || ch == 'w')
                attribs |= ATTR_OTHR_WRITE;
            else if (ch == 'L' || ch == 'l')
                attribs |= ATTR_OTHR_LOCKD;
            else if (ch == 'E' || ch == 'e')
                attribs |= ATTR_OTHR_EXEC;
            else {
                vdfs_error(err_badparms);
                return ATTR_BAD_CHAR;
            }
            ch = readmem(addr++);
        }
    }
    return attribs;
}

static void cmd_access(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_path path;
        if ((addr = parse_name(&path, addr))) {
            uint_least16_t attribs = 0;
            uint_least16_t attr_mask = 0;
            int ch = readmem(addr++);
            while (ch == ' ' || ch == '\t')
                ch = readmem(addr++);
            if (ch == '+') {
                ch = readmem(addr++);
                attribs = cmd_access_parse(addr, ch);
                if (attribs & ATTR_BAD_CHAR)
                    return;
                attribs &= ~ATTR_GOT_OTHER;
            }
            else if (ch == '-') {
                ch = readmem(addr++);
                attr_mask = cmd_access_parse(addr, ch);
                if (attr_mask & ATTR_BAD_CHAR)
                    return;
                attr_mask &= ~ATTR_GOT_OTHER;
            }
            else {
                attr_mask = ATTR_USER_READ|ATTR_USER_WRITE|ATTR_USER_LOCKD|ATTR_USER_EXEC;
                attribs = cmd_access_parse(addr, ch);
                if (attribs & ATTR_BAD_CHAR)
                    return;
                if (attribs & ATTR_GOT_OTHER) {
                    attr_mask &= ~ATTR_GOT_OTHER;
                    attr_mask |= ATTR_OTHR_READ|ATTR_OTHR_WRITE|ATTR_OTHR_LOCKD|ATTR_OTHR_EXEC;
                }
            }
            vdfs_findres res;
            vdfs_entry *ent = find_entry(&path, &res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                attr_mask = ~attr_mask;
                if (fs_flags & DFS_MODE) {
                    attribs |= ATTR_USER_READ;
                    if (attribs & ATTR_OTHR_LOCKD)
                        attribs &= ~ATTR_USER_WRITE;
                    do {
                        ent->attribs = (ent->attribs & attr_mask) | attribs;
                        write_back(ent);
                        set_file_atribs(ent, ent->attribs);
                        ent = find_next_dfs(ent, &res);
                    } while (ent);
                }
                else {
                    do {
                        ent->attribs = (ent->attribs & attr_mask) | attribs;
                        write_back(ent);
                        set_file_atribs(ent, ent->attribs);
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
    if (check_valid_dir(&cur_dir)) {
        vdfs_path path;
        if (parse_name(&path, addr))
            osfile_cdir(&path);
    }
}

#ifdef WIN32
#include <winbase.h>

static bool copy_file(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    BOOL res = CopyFile(old_ent->host_path, new_ent->host_path, FALSE);
    if (!res)
        vdfs_hosterr(errno);
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
            vdfs_hosterr(err);
            log_warn("vdfs: error writing %s: %s", new_fn, strerror(err));
            return false;
        }
    }
    if (bytes < 0) {
        int err = errno;
        vdfs_hosterr(err);
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
                    vdfs_hosterr(err);
                    log_warn("vdfs: copy_file_range failed %s to %s: %s", old_ent->host_path, new_ent->host_path, strerror(err));
                }
            }
            else {
                int err = errno;
                vdfs_hosterr(err);
                log_warn("vdfs: unable to fstat '%s': %s", old_ent->host_path, strerror(err));
            }
#else
            res = copy_loop(old_ent->host_path, old_fd, new_ent->host_path, new_fd);
#endif
            close(new_fd);
        }
        else {
            int err = errno;
            vdfs_hosterr(err);
            log_warn("vdfs: unable to open %s for writing: %s", new_ent->host_path, strerror(err));
        }
        close(old_fd);
    }
    else {
        int err = errno;
        vdfs_hosterr(err);
        log_warn("vdfs: unable to open %s for reading: %s", old_ent->host_path, strerror(err));
    }
    return res;
}

#else

static bool copy_file(vdfs_entry *old_ent, vdfs_entry *new_ent)
{
    bool res = false;
    FILE *old_fp = fopen(old_ent->host_path, "rb");
    if (old_fp) {
        FILE *new_fp = fopen(new_ent->host_path, "wb");
        if (new_fp) {
            res = true;
            int ch = getc(old_fp);
            while (ch != EOF && putc(ch, new_fp) != EOF)
                ch = getc(old_fp);
            if (ferror(old_fp)) {
                int err = errno;
                vdfs_hosterr(err);
                log_warn("vdfs: read error on %s: %s", old_ent->host_path, strerror(err));
                res = false;
            }
            if (ferror(new_fp)) {
                int err = errno;
                vdfs_hosterr(err);
                log_warn("vdfs: write error on %s: %s", new_ent->host_path, strerror(err));
                res = false;
            }
            if (fclose(new_fp)) {
                int err = errno;
                vdfs_hosterr(err);
                log_warn("vdfs: write error on %s: %s", new_ent->host_path, strerror(err));
                res = false;
            }
        }
        else {
            int err = errno;
            vdfs_hosterr(err);
            log_warn("vdfs: unable to open %s for writing: %s", new_ent->host_path, strerror(err));
        }
        fclose(old_fp);
    }
    else {
        int err = errno;
        vdfs_hosterr(err);
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
        vdfs_path old_path, new_path;
        if ((addr = parse_name(&old_path, addr))) {
            if (parse_name(&new_path, addr)) {
                if (old_path.len > 0 && new_path.len > 0) {
                    vdfs_findres old_res;
                    vdfs_entry *old_ent = find_entry(&old_path, &old_res, &cur_dir);
                    if (old_ent && (old_ent->attribs & ATTR_EXISTS)) {
                        vdfs_findres new_res;
                        vdfs_entry *new_ent = find_entry(&new_path, &new_res, &cur_dir);
                        if (new_ent) {
                            if ((new_ent->attribs & (ATTR_EXISTS|ATTR_IS_DIR)) == (ATTR_EXISTS|ATTR_IS_DIR)) {
                                /* as destination is a dir, loop over wild-cards */
                                new_res.parent = new_ent;
                                do {
                                    memcpy(new_res.acorn_fn, old_ent->acorn_fn, old_ent->acorn_len);
                                    if (!(new_ent = add_new_file(&new_res)))
                                        break;
                                    if (!copy_file(old_ent, new_ent))
                                        break;
                                } while ((old_ent = find_next(old_ent, &old_res)));
                            }
                            else {
                                /* destination is a file */
                                if (find_next(old_ent, &old_res))
                                    vdfs_error(err_wildcard);
                                else
                                    copy_file(old_ent, new_ent);
                            }
                        }
                        else if (find_next(old_ent, &old_res))
                            vdfs_error(err_wildcard);
                        else if ((new_ent = add_new_file(&new_res)) && copy_file(old_ent, new_ent))
                            new_ent->attribs |= ATTR_EXISTS;
                    }
                    else
                        vdfs_error(old_res.errmsg);
                }
                else {
                    log_debug("vdfs: copy attempted with an empty filename");
                    vdfs_error(err_badcopy);
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
        vdfs_path path;
        if (parse_name(&path, addr)) {
            if (!no_wildcards(&path))
                return;
            if ((ent = find_entry(&path, &res, &cur_dir))) {
                delete_file(ent);
                return;
            }
            vdfs_error(res.errmsg);
        }
    }
}

static vdfs_entry *lookup_dir(uint16_t addr)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_entry *ent;
        vdfs_findres res;
        vdfs_path path;
        if (parse_name(&path, addr)) {
            ent = find_entry(&path, &res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    return ent;
                else
                    vdfs_error(err_notdir);
            } else
                vdfs_error(res.errmsg);
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
            vdfs_error(err_badparms);
            return NULL;
        }
        *drive = ch - '0';
        ch = readmem(++addr);
        if (ch != '.') {
            if (ch == '\r')
                return &root_dir;
            log_debug("vdfs: parse_adfs_dir, missing dot");
            vdfs_error(err_badparms);
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
            vdfs_error(err_badparms);
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
                vdfs_error(err_baddir);
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
    vdfs_error(err_baddir);
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
        vdfs_error(err_badparms);
}

uint8_t *vdfs_split_addr(void)
{
    unsigned romno = readmem(0xf4);
    unsigned page = rom_slots[romno & 0x0f].split;
    writemem(0xa8, 0);
    writemem(0xa9, page);
    return rom + (romno * ROM_SIZE) + ((page - 0x80) << 8);
}

void vdfs_split_go(unsigned after)
{
    x = after;
    rom_dispatch(VDFS_ROM_PRINT_SPLIT);
}

static uint8_t *cmd_pwd_recurse(uint8_t *dptr, vdfs_entry *ent)
{
    vdfs_entry *parent = ent->parent;
    if (parent && parent != ent)
        dptr = cmd_pwd_recurse(dptr, parent);
    unsigned len = ent->acorn_len;
    memcpy(dptr, ent->acorn_fn, len);
    dptr+= len;
    *dptr++ = '.';
    return dptr;
}

static void cmd_pwd(void)
{
    if (check_valid_dir(&cur_dir)) {
        uint8_t *ptr = cmd_pwd_recurse(vdfs_split_addr(), cur_dir.dir);
        ptr[-1] = 0x0d;
        ptr[0]  = 0x0a;
        ptr[1]  = 0x00;
        vdfs_split_go(0);
    }
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
        ent->u.dir.title_len = ptr - ent->u.dir.title;
        log_debug("vdfs: cmd_title: name=%.*s", ent->u.dir.title_len, ent->u.dir.title);
        write_back(ent);
    }
}

static void run_file(const char *err)
{
    if (check_valid_dir(&cur_dir)) {
        vdfs_path path;
        vdfs_findres res;
        vdfs_entry *ent;
        if ((cmd_tail = parse_name(&path, (y << 8) | x))) {
            ent = find_entry(&path, &res, &cur_dir);
            if (!(ent && ent->attribs & ATTR_EXISTS) && lib_dir.dir)
                ent = find_entry(&path, &res, &lib_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    vdfs_error(err_wont);
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
                        vdfs_hosterr(errno);
                    }
                }
            }
            else
                vdfs_error(err);
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
        vdfs_hosterr(errno);
        log_debug("vdfs: failed to rename '%s' to '%s': %s", old_ent->host_path, new_ent->host_path, strerror(errno));
    }
}

static void rename_file(uint16_t addr)
{
    vdfs_path old_path;
    if ((addr = parse_name(&old_path, addr))) {
        vdfs_path new_path;
        if (parse_name(&new_path, addr)) {
            if (old_path.len > 0 && new_path.len > 0) {
                vdfs_findres old_res;
                vdfs_entry *old_ent = find_entry(&old_path, &old_res, &cur_dir);
                if (old_ent && old_ent->attribs & ATTR_EXISTS) {
                    vdfs_findres new_res;
                    vdfs_entry *new_ent = find_entry(&new_path, &new_res, &cur_dir);
                    if (new_ent) {
                        if (new_ent->attribs & ATTR_EXISTS) {
                            if (new_ent->attribs & ATTR_IS_DIR) {
                                old_res.parent = new_ent;
                                if ((new_ent = add_new_file(&old_res)))
                                    rename_tail(old_ent, new_ent);
                            } else {
                                log_debug("vdfs: new file '%.*s' for rename already exists", new_res.acorn_len, new_res.acorn_fn);
                                vdfs_error(err_exists);
                            }
                        } else
                            rename_tail(old_ent, new_ent);
                    }
                    else if (new_res.parent && (new_ent = add_new_file(&new_res)))
                        rename_tail(old_ent, new_ent);
                } else {
                    log_debug("vdfs: old file '%.*s' for rename not found", old_res.acorn_len, old_res.acorn_fn);
                    vdfs_error(old_res.errmsg);
                }
            } else {
                log_debug("vdfs: rename attempted with an empty filename");
                vdfs_error(err_badren);
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
    { "PWD",     VDFS_ACT_PWD     },
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
    vdfs_error(err_badparms);
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
        vdfs_path path;
        if (parse_name(&path, addr)) {
            if (path.len > 0) {
                vdfs_findres res;
                vdfs_entry *ent = find_entry(&path, &res, dir);
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_IS_DIR) {
                        cat_dir = ent;
                    } else {
                        vdfs_error(err_notdir);
                        return false;
                    }
                } else {
                    vdfs_error(res.errmsg);
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
        const char *ptr = cat_dir->u.dir.title;
        unsigned len = cat_dir->u.dir.title_len;
        if (!len) {
            ptr = cat_dir->acorn_fn;
            len = cat_dir->acorn_len;
        }
        writemem(mem_ptr++, cat_dir->u.dir.boot_opt);
        while (len) {
            writemem(mem_ptr++, *ptr++);
            --len;
        }
    }
    else
        writemem(mem_ptr++, 0);
    writemem(mem_ptr++, 0);
}

static void cat_get_dir_adfs(vdfs_dirlib *dir)
{
    vdfs_entry *ent = dir->dir;
    uint32_t mem_ptr = CAT_TMP;
    const char *ptr;
    unsigned len;
    if (ent) {
        ptr = ent->acorn_fn;
        len = ent->acorn_len;
    }
    else {
        ptr = "Unset";
        len = 5;
    }
    while (len) {
        writemem(mem_ptr++, *ptr++);
        --len;
    }
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
        log_debug("vdfs: cat_next_dfsdir skipping %c.%.*s", cat_ent->dfs_dir, cat_ent->acorn_len, cat_ent->acorn_fn);
        cat_ent = cat_ent->next;
    }
    cat_next_tail();
}

static void cat_next_dfsnot(void)
{
    while (cat_ent && (!(cat_ent->attribs & ATTR_EXISTS) || cat_ent->dfs_dir == cat_dfs)) {
        log_debug("vdfs: cat_next_dfsnot skipping %c.%.*s", cat_ent->dfs_dir, cat_ent->acorn_len, cat_ent->acorn_fn);
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
        vdfs_path path;
        if (parse_name(&path, addr)) {
            vdfs_entry *ent = find_entry(&path, &info_res, &cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                gcopy_attr(ent);
                cat_ent = ent;
                rom_dispatch(VDFS_ROM_INFO);
                return;
            }
            vdfs_error(info_res.errmsg);
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
            vdfs_error(err_badcmd);
    }
}

static void vdfs_dfs_mode(void)
{
    fs_flags |= (DFS_MODE|GSTRANS_FN);
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
    fs_flags &= ~(DFS_MODE|GSTRANS_FN);
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
        vdfs_error(err_badparms);
        return;
    }
    if (!(fs_flags & VDFS_ACTIVE)) {
        fs_num = FSNO_VDFS;
        rom_dispatch(VDFS_ROM_FSSTART);
    }
}

static void needs_filename(enum vdfs_action act, uint16_t addr)
{
    int ch = readmem(addr);
    if (ch == '\r') {
        vdfs_error(err_badparms);
        return;
    }
    x = addr & 0xff;
    y = addr >> 8;
    rom_dispatch(act);
}

static void cmd_dump(uint16_t addr)
{
    int ch = readmem(addr);
    if (ch == '\r') {
        vdfs_error(err_badparms);
        return;
    }
    x = addr & 0xff;
    y = addr >> 8;

    // Skip over the filename and any spaces after.

    while (ch != ' ' && ch != '\t' && ch != '\r')
        ch = readmem(++addr);
    while (ch == ' ' || ch == '\t')
        ch = readmem(++addr);
    uint32_t start = 0, offset = 0;
    if (ch != '\r') {
        log_debug("vdfs: cmd_dump, start present");
        do {
            ch = hex2nyb(ch);
            if (ch == -1) {
                vdfs_error(err_badparms);
                return;
            }
            start = start << 4 | ch;
            log_debug("vdfs: cmd_dump, start loop, nyb=%x, start=%x", ch, start);
            ch = readmem(++addr);
        }
        while (ch != ' ' && ch != '\t' && ch != '\r');
        while (ch == ' ' || ch == '\t')
            ch = readmem(++addr);
        if (ch == '\r')
            offset = start;
        else {
            log_debug("vdfs: cmd_dump, offset present");
            do {
                ch = hex2nyb(ch);
                if (ch == -1) {
                    vdfs_error(err_badparms);
                    return;
                }
                offset = offset << 4 | ch;
                log_debug("vdfs: cmd_dump, start loop, nyb=%x, offset=%x", ch, offset);
                ch = readmem(++addr);
            }
            while (ch != ' ' && ch != '\t' && ch != '\r');
        }
    }
    writemem32(0xa8, offset);
    writemem32(0xac, start);
    rom_dispatch(VDFS_ROM_DUMP);
}

static void cmd_quit(uint16_t addr)
{
    int exit_status = 0;
    int ch = readmem(addr);
    while (ch >= '0' && ch <= '9') {
        exit_status = exit_status * 10 + (ch & 0x0f);
        ch = readmem(++addr);
    }
    set_shutdown_exit_code(exit_status);
    set_quit();
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
        needs_filename(act, addr);
        break;
    case VDFS_ACT_NOP:
        break;
    case VDFS_ACT_QUIT:
        cmd_quit(addr);
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
            vdfs_error(err_badcmd);
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
    case VDFS_ACT_PWD:
        cmd_pwd();
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
    case VDFS_ACT_MMBDABT:
        mmb_cmd_dabout();
        break;
    case VDFS_ACT_MMBDBOT:
        mmb_cmd_dboot(addr);
        break;
    case VDFS_ACT_MMBDCAT:
        mmb_cmd_dcat_start(addr);
        break;
    case VDFS_ACT_MMBDDRV:
        mmb_cmd_ddrive(addr);
        break;
    case VDFS_ACT_MMBDFRE:
        mmb_cmd_dfree();
        break;
    case VDFS_ACT_MMBRCAT:
        mmb_cmd_drecat();
        break;
    case VDFS_ACT_MMBDBAS:
        mmb_cmd_dbase(addr);
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
        cmd_tail = addr;
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

static void osfsc_eof(void)
{
    vdfs_open_file *cp = get_open_read(x);
    if (cp) {
        FILE *fp = cp->fp;
        long ptr = ftell(fp);
        fseek(fp, 0L, SEEK_END);
        long ext = ftell(fp);
        x = ptr >= ext ? 0xff : 0;
        log_debug("vdfs: eof check, ptr=%08lX, ext=%08lX, x=%02X", ptr, ext, x);
        if (ptr != ext)
            fseek(fp, ptr, SEEK_SET);
    }
}

static void osfsc(void)
{
    log_debug("vdfs: osfsc(A=%02X, X=%02X, Y=%02X)", a, x, y);

    p.c = 0;
    switch(a) {
        case 0x00:
            osfsc_opt();
            break;
        case 0x01: // check EOF
            osfsc_eof();
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
            if (!MASTER) {
                // OSBYTE to close SPOO/EXEC files.
                a = 0x77;
                pc = 0xfff4;
            }
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
            read_bytes(fp, addr, bytes, 0);
        else if (cmd == 0x4b) {
            write_bytes(fp, addr, bytes, 0);
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
    if (fs_flags & VDFS_ACTIVE) {
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
    { "DAbout",  VDFS_ACT_MMBDABT },
    { "Din",     VDFS_ROM_MMBDIN  },
    { "DBAse",   VDFS_ACT_MMBDBAS },
    { "DBoot",   VDFS_ACT_MMBDBOT },
    { "DCat",    VDFS_ACT_MMBDCAT },
    { "DDrive",  VDFS_ACT_MMBDDRV },
    { "DFree",   VDFS_ACT_MMBDFRE },
    { "DOP",     VDFS_ROM_MMBDOP  },
    { "DONboot", VDFS_ROM_MMBONBT },
    { "DOUt",    VDFS_ROM_MMBDOUT },
    { "DREcat",  VDFS_ACT_MMBRCAT }
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
            cur_dir.dir = prev_dir.dir = lib_dir.dir = cat_dir = &root_dir;
            cur_dir.drive = lib_dir.drive = 0;
            cur_dir.dfs_dir = lib_dir.dfs_dir = '$';
            vdfs_findres res;
            vdfs_path lib_path = { 3, "Lib" };
            vdfs_entry *ent = find_entry_adfs(&lib_path, &res, &cur_dir);
            if (ent)
                lib_dir.dir = ent;
            if (vdfs_boot_dir) {
                vdfs_path boot_path;
                boot_path.len = strlen(vdfs_boot_dir);
                memcpy(boot_path.path, vdfs_boot_dir, boot_path.len);
                ent = find_entry(&boot_path, &res, &cur_dir);
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_IS_DIR)
                        cur_dir.dir = ent;
                    else
                        log_warn("vdfs: boot directory '%s' %s", vdfs_boot_dir, "is a file");
                }
                else
                    log_warn("vdfs: boot directory '%s' %s", vdfs_boot_dir, "not found");
            }
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
    unsigned romver = readmem(0x8008);
    if (romver < 7)
        log_warn("vdfs: old ROM, expected version 7, got version %u", romver);
    log_debug("vdfs: startup, break_type=%d, romno=%x, rom/ram split=%02x00", a, x, y);
    if (a > 0)
        mmb_reset();
    rom_slots[x & 0x0f].split = y;
    a = 0x02;
    y = save_y;
}

static double last_time = 0.0;

static void log_time(void)
{
    double this_time = al_get_time();
    if (last_time > 0)
        log_info("timestamp, %g seconds since last timestamp", this_time - last_time);
    else
        log_info("timestamp, %g seconds since start", this_time);
    last_time = this_time;
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
        case 0x15: log_time();  break;
        case 0x16: mmb_cmd_din(cmd_tail); break;
        case 0x17: mmb_cmd_dcat_cont();   break;
        case 0x18: mmb_cmd_dop(cmd_tail); break;
        case 0x19: mmb_cmd_donboot(cmd_tail); break;
        case 0x1a: mmb_cmd_dout(cmd_tail);    break;
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

void vdfs_init(const char *root, const char *dir)
{
    scan_seq = 0;
    init_dirlib(&cur_dir, "current");
    init_dirlib(&lib_dir, "library");
    init_dirlib(&prev_dir, "previous");
    if (!root) {
        const char *env = getenv("BEM_VDFS_ROOT");
        if (env)
            root = env; //environment variable wins
        if (!root)
            root = vdfs_cfg_root;
        if (!root)
            root = ".";
    }
    vdfs_set_root(root);
    vdfs_adfs_mode();
    vdfs_boot_dir = dir;
}
