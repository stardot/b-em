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
#define MAX_INF_LINE     80

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
static vdfs_entry *cur_dir;
static vdfs_entry *lib_dir;
static vdfs_entry *prev_dir;
static vdfs_entry *cat_dir;
static unsigned   scan_seq;
static char dfs_dir;
static char dfs_lib;

typedef struct vdfs_findres {
    struct vdfs_entry *parent;
    const char *errmsg;
    char acorn_fn[MAX_FILE_NAME+1];
} vdfs_findres;

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
    VDFS_ROM_CLOSE_CMD,
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
    VDFS_ACT_MMBDIN
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
#define DFS_MODE    0x20

static uint8_t  reg_a;
static uint8_t  fs_flags = 0;
static uint8_t  fs_num   = 0;
static uint16_t cmd_tail;

/*
 * Switching between DFS and ADFS mode.  These function pointers
 * allow different functions to be used to make VDFS look like either
 * ADFS or DFS to the guest.
 */

static vdfs_entry *(*find_entry)(const char *filename, vdfs_findres *res, vdfs_entry *dir, int dfsdir);
static void (*osgbpb_get_dir)(uint32_t pb, vdfs_entry *dir, int dfsdir);
static bool (*cat_prep)(uint16_t addr, vdfs_entry *dir, int dfsdir, const char *dir_desc);
static void (*cat_get_dir)(vdfs_entry *dir, int dfsdir);
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
static const char err_badaddr[]  = "\x94" "Bad parms";
static const char err_nomem[]    = "\x92" "Out of memory";
static const char err_no_swr[]   = "\x93" "No SWRAM at that address";
static const char err_too_big[]  = "\x94" "Too big";
static const char err_wildcard[] = "\xfd" "Wild cards";

// Error messages unique to VDFS

static const char err_baddir[]  = "\xc7" "Not a directory";

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

static void scan_attr(vdfs_entry *ent)
{
    struct stat stb;

    if (stat(ent->host_path, &stb) == -1)
        log_warn("vdfs: unable to stat '%s': %s\n", ent->host_path, strerror(errno));
    else {
        ent->attribs |= ATTR_EXISTS;
        if (S_ISDIR(stb.st_mode)) {
            if (!(ent->attribs & ATTR_IS_DIR)) {
                ent->attribs |= ATTR_IS_DIR;
                ent->u.dir.children = NULL;
                ent->u.dir.scan_mtime = 0;
                ent->u.dir.scan_seq = 0;
                ent->u.dir.sorted = SORT_NONE;
                ent->u.dir.boot_opt = 0;
                ent->u.dir.title[0] = 0;
            }
        }
        else {
            if (ent->attribs & ATTR_IS_DIR) {
                log_debug("vdfs: dir %s has become a file", ent->acorn_fn);
                ent->attribs &= ~ATTR_IS_DIR;
                free_entry(ent->u.dir.children);
            }
            ent->u.file.load_addr = 0;
            ent->u.file.exec_addr = 0;
            ent->u.file.length = stb.st_size;
        }
#ifdef WIN32
        if (stb.st_mode & S_IRUSR)
            ent->attribs |= ATTR_USER_READ|ATTR_OTHR_READ;
        if (stb.st_mode & S_IWUSR)
            ent->attribs |= ATTR_USER_WRITE|ATTR_OTHR_WRITE;
        if (stb.st_mode & S_IXUSR)
            ent->attribs |= ATTR_USER_EXEC|ATTR_OTHR_EXEC;
#else
        if (stb.st_mode & S_IRUSR)
            ent->attribs |= ATTR_USER_READ;
        if (stb.st_mode & S_IWUSR)
            ent->attribs |= ATTR_USER_WRITE;
        if (stb.st_mode & S_IXUSR)
            ent->attribs |= ATTR_USER_EXEC;
        if (stb.st_mode & (S_IRGRP|S_IROTH))
            ent->attribs |= ATTR_OTHR_READ;
        if (stb.st_mode & (S_IWGRP|S_IWOTH))
            ent->attribs |= ATTR_OTHR_WRITE;
        if (stb.st_mode & (S_IXGRP|S_IXOTH))
            ent->attribs |= ATTR_OTHR_EXEC;
#endif
    }
    log_debug("vdfs: scan_attr: host=%s, attr=%04X\n", ent->host_fn, ent->attribs);
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
            if ((ch = *lptr++) == '.') {
                ent->dfs_dir = ic;
                ch = *lptr++;
            }
            else {
                ent->dfs_dir = '$';
                *ptr++ = ic;
            }
            while (ch && ch != ' ' && ch != '\t') {
                if (ptr < end)
                    *ptr++ = ch;
                ch = *lptr++;
            }
            if (ptr < end)
                *ptr = '\0';
            return lptr;
        }
    }
    return NULL;
}

static void scan_inf_file(vdfs_entry *ent)
{
    char inf_line[MAX_INF_LINE];
    int ch, nyb;
    uint32_t load_addr = 0, exec_addr = 0;
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
    }
    ent->u.file.load_addr = load_addr;
    ent->u.file.exec_addr = exec_addr;
}

static unsigned scan_inf_dir_new(const char *lptr, const char *eptr, char *title)
{
    unsigned opt = 0;
    do {
        const char *sptr = eptr - 3;
        if (sptr >= lptr && !strncasecmp(sptr, "OPT=", 4)) {
            int ch, nyb;
            // Parse options.
            while ((ch = *++eptr) == ' ' || ch == '\t')
                ;
            while ((nyb = hex2nyb(ch)) >= 0) {
                opt = (opt << 4) | nyb;
                ch = *++eptr;
            }
        }
        else {
            sptr = eptr - 5;
            if (sptr >= lptr && !strncasecmp(sptr, "TITLE=", 6)) {
                // Parse title.
                char *ptr = title;
                char *end = title + MAX_TITLE;
                int ch, quote= 0;

                do
                    ch = *++eptr;
                while (ch == ' ' || ch == '\t');

                if (ch == '"') {
                    quote = 1;
                    ch = *++eptr;
                }
                while (ptr < end && ch && ch != '\n' && (ch != '"' || !quote) && ((ch != ' ' && ch != '\t') || quote)) {
                    *ptr++ = ch & 0x7f;
                    ch = *++eptr;
                }
                *ptr = '\0';
            }
            else
                ++eptr;
        }
        lptr = eptr;
        eptr = strchr(lptr, '=');
    }
    while (eptr);
    return opt;
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
            dir->u.dir.boot_opt = scan_inf_dir_new(lptr, eptr, dir->u.dir.title);
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

static int vdfs_wildmat(const char *pattern, const char *candidate, size_t len)
{
    int pat_ch, can_ch, d;

    while (len-- > 0) {
        pat_ch = *(const unsigned char *)pattern++;
        if (pat_ch == '*') {
            if (!*pattern)
                return 0;
            len++;
            do {
                if (vdfs_wildmat(pattern, candidate++, len--))
                    return 0;
            } while (len && *candidate);
            return 1;
        }
        can_ch = *(const unsigned char *)candidate++;
        if (!pat_ch)
            return can_ch ? -1 : 0;
        if (pat_ch != can_ch && pat_ch != '#') {
            if (pat_ch >= 'a' && pat_ch <= 'z')
                pat_ch = pat_ch - 'a' + 'A';
            if (can_ch >= 'a' && can_ch <= 'z')
                can_ch = can_ch - 'a' + 'A';
            if ((d = pat_ch - can_ch))
                return d;
        }
    }
    return 0;
}

static vdfs_entry *wild_search(vdfs_entry *dir, const char *pattern)
{
    vdfs_entry *ent;

    for (ent = dir->u.dir.children; ent; ent = ent->next)
        if (!vdfs_wildmat(pattern, ent->acorn_fn, MAX_FILE_NAME))
            return ent;
    return NULL;
}

// Create VDFS entry for a new file.

static vdfs_entry *new_entry(vdfs_entry *dir, const char *host_fn)
{
    vdfs_entry *ent;
    int name_len, seq_ch = '0';
    char *host_path;

    if ((ent = malloc(sizeof(vdfs_entry)))) {
        init_entry(ent);
        ent->parent = dir;
        if ((host_path = make_host_path(ent, host_fn))) {
            scan_entry(ent);
            if (acorn_search(dir, ent->acorn_fn)) {
                // name was already in tree - generate a unique one.
                name_len = strlen(ent->acorn_fn);
                if (name_len < (MAX_FILE_NAME-2)) {
                    ent->acorn_fn[name_len] = '~';
                    ent->acorn_fn[name_len+1] = seq_ch;
                } else {
                    ent->acorn_fn[MAX_FILE_NAME-2] = '~';
                    ent->acorn_fn[MAX_FILE_NAME-1] = seq_ch;
                }
                while (acorn_search(dir, ent->acorn_fn)) {
                    if (seq_ch == '9')
                        seq_ch = 'A';
                    else if (seq_ch == 'Z')
                        seq_ch = 'a';
                    else if (seq_ch == 'z')
                        break;
                    else
                        seq_ch++;
                    if (name_len < (MAX_FILE_NAME-2))
                        ent->acorn_fn[name_len+1] = seq_ch;
                    else
                        ent->acorn_fn[MAX_FILE_NAME-1] = seq_ch;
                }
                log_debug("vdfs: new_entry: unique name %s used\n", ent->acorn_fn);
            }
            ent->next = dir->u.dir.children;
            dir->u.dir.children = ent;
            dir->u.dir.sorted = SORT_NONE;
            log_debug("vdfs: new_entry: returing new entry %p\n", ent);
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
        log_warn("vdfs: unable to opendir '%s': %s\n", dir->host_path, strerror(errno));
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

    log_debug("vdfs: parse_name: addr=%04x\n", addr);
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

static bool check_valid_dir(vdfs_entry *ent, const char *which)
{
    if (ent && ent->attribs & ATTR_IS_DIR)
        return true;
    log_warn("vdfs: %s directory is not valid", which);
    adfs_error(err_baddir);
    return false;
}

// Given an Acorn filename, find the VDFS entry.

static vdfs_entry *find_entry_adfs(const char *filename, vdfs_findres *res, vdfs_entry *ent, int dfsdir)
{
    int ch, fn0, fn1;
    const char *fn_src;
    char *fn_ptr, *fn_end;
    vdfs_entry *ptr;

    res->parent = NULL;
    res->errmsg = err_notfound;

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
        else if (fn0 == '%' && fn1 == '\0' && check_valid_dir(lib_dir, "library"))
            ent = lib_dir;
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
            res->errmsg = err_baddir;
            return NULL;
        }
    }
    return NULL;
}

static vdfs_entry *find_entry_dfs(const char *filename, vdfs_findres *res, vdfs_entry *dir, int dfsdir)
{
    log_debug("vdfs: find_entry_dfs, filename=%s, dfsdir=%c", filename, dfsdir);
    if (!scan_dir(dir)) {
        int ic = filename[0];
        if (ic && filename[1] == '.') {
            dfsdir = ic;
            filename += 2;
        }
        for (vdfs_entry *ent = dir->u.dir.children; ent; ent = ent->next) {
            log_debug("vdfs: find_entry_dfs, considering entry %s", ent->acorn_fn);
            if (dfsdir == '*' || dfsdir == '#' || dfsdir == ent->dfs_dir) {
                log_debug("vdfs: find_entry_dfs, matched DFS dir");
                if (!vdfs_wildmat(filename, ent->acorn_fn, MAX_FILE_NAME))
                    return ent;
            }
        }
    }
    res->errmsg = err_notfound;
    return NULL;
}

static vdfs_entry *add_new_file(vdfs_findres *res)
{
    vdfs_entry *dir = res->parent;
    vdfs_entry *new_ent;
    char host_fn[MAX_FILE_NAME];

    if ((new_ent = malloc(sizeof(vdfs_entry)))) {
        init_entry(new_ent);
        memcpy(new_ent->acorn_fn, res->acorn_fn, MAX_FILE_NAME+1);
        new_ent->dfs_dir = dfs_dir;
        bbc2hst(res->acorn_fn, host_fn);
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

static void write_back(vdfs_entry *ent)
{
    FILE *fp;

    show_activity();
    *ent->host_inf = '.'; // select .inf file.
    if ((fp = fopen(ent->host_path, "wt"))) {
        if (ent->attribs & ATTR_IS_DIR) {
            const char *fmt = "%s OPT=%02X DIR=1 TITLE=%s\n";
            if (strpbrk(ent->u.dir.title, " \t"))
                fmt = "%s OPT=%02X DIR=1 TITLE=\"%s\"\n";
            fprintf(fp, fmt, ent->acorn_fn, ent->u.dir.boot_opt, ent->u.dir.title);
        }
        else
            fprintf(fp, "%c.%s %08X %08X %08X %02X\n", ent->dfs_dir, ent->acorn_fn, ent->u.file.load_addr, ent->u.file.exec_addr, ent->u.file.length, ent->attribs & ATTR_ACORN_MASK);
        fclose(fp);
    } else
        log_warn("vdfs: unable to create INF file '%s': %s\n", ent->host_path, strerror(errno));
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
            scan_attr(ent);
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

static int vdfs_new_root(const char *root, vdfs_entry *ent)
{
    size_t len;
    char   *path, *inf;
    int    ch;

    init_entry(ent);
    ent->parent = ent;
    len = strlen(root);
    while (len > 0 && ((ch = root[--len]) == '/' || ch == '\\'))
        ;
    if (++len > 0) {
        if ((path = malloc(len + 6))) {
            memcpy(path, root, len);
            inf = path + len;
            ent->host_path = path;
            ent->host_inf = inf;
            *inf++ = '\0';
            *inf++ = 'i';
            *inf++ = 'n';
            *inf++ = 'f';
            *inf = '\0';
            ent->acorn_fn[0] = '$';
            ent->acorn_fn[1] = '\0';
            scan_entry(ent);
            if (ent->attribs & ATTR_IS_DIR)
                return 1;
            log_error("vdfs: unable to set %s as root as it is not a valid directory", path);
        } else
            log_error("vdfs: unable to set root as unable to allocate path");
    } else
        log_warn("vdfs: unable to set root as path is empty");
    return 0;
}

void vdfs_set_root(const char *root)
{
    vdfs_entry new_root;
    if (vdfs_new_root(root, &new_root)) {
        vdfs_findres res;
        vdfs_close();
        root_dir = new_root;
        root_dir.parent = cur_dir = prev_dir = cat_dir = &root_dir;
        lib_dir = find_entry_adfs("Lib", &res, &root_dir, dfs_dir);
        scan_seq++;
    } else if (new_root.host_path)
        free(new_root.host_path);
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
    if ((ent = find_entry(path, &res, &root_dir, '$')))
        if (!(ent->attribs & ATTR_IS_DIR))
            ent = NULL;
    free(path);
    return ent;
}

static vdfs_entry *ss_load_dir(vdfs_entry *dir, FILE *f, const char *which)
{
    vdfs_entry *ent;
    int ch;

    if ((ch = getc(f)) != EOF) {
        if (ch == 'R') {
            dir = &root_dir;
            log_debug("vdfs: loadstate %s directory set to root", which);
        } else if (ch == 'C') {
            dir = cur_dir;
            log_debug("vdfs: loadstate %s directory set to current", which);
        } else if (ch == 'S' && ((ent = ss_spec_path(f, which))))
            dir = ent;
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
        cur_dir = ss_load_dir(cur_dir, f, "current");
        lib_dir = ss_load_dir(lib_dir, f, "library");
        prev_dir = ss_load_dir(prev_dir, f, "previous");
        cat_dir = ss_load_dir(cat_dir, f, "catalogue");
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

    if (ent == &root_dir)
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
    if (ent == cur_dir)
        putc('C', f);
    else
        ss_save_dir1(ent, f);
}

void vdfs_savestate(FILE *f)
{
    putc(vdfs_enabled ? 'V' : 'v', f);
    ss_save_dir1(cur_dir, f);
    ss_save_dir2(lib_dir, f);
    ss_save_dir2(prev_dir, f);
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
        log_debug("vdfs: swr_calc_addr: pseudo addr bank=%02d, start=%04x\n", romid, start);
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
        log_debug("vdfs: swr_calc_addr: abs addr bank=%02d, start=%04x\n", romid, start);
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

    log_debug("vdfs: exec_swr_fs: flags=%02x, fn=%04x, romid=%02d, start=%04x, len=%04x\n", flags, fname, romid, start, pblen);
    if (check_valid_dir(cur_dir, "current")) {
        if ((romid = swr_calc_addr(flags, &start, romid)) >= 0) {
            vdfs_findres res;
            if (parse_name(path, sizeof path, fname)) {
                ent = find_entry(path, &res, cur_dir, dfs_dir);
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
                                log_warn("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
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
    if (fs_num) {
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
    writemem32(pb+0x0e, ent->attribs);
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

static void osfile_write(uint32_t pb, const char *path, uint32_t (*callback)(FILE *fp, uint32_t addr, size_t bytes))
{
    vdfs_entry *ent;
    FILE *fp;
    uint32_t start_addr, end_addr;

    if (no_wildcards(path)) {
        vdfs_findres res;
        if ((ent = find_entry(path, &res, cur_dir, dfs_dir))) {
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
        } else
            log_warn("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
    }
}

static void osfile_set_meta(uint32_t pb, const char *path, uint16_t which)
{
    vdfs_entry *ent;
    vdfs_findres res;

    if ((ent = find_entry(path, &res, cur_dir, dfs_dir)) && ent->attribs & ATTR_EXISTS) {
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
#ifndef WIN32
                if (attr != (ent->attribs & ATTR_ACORN_MASK)) {
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
                        log_warn("unable to chmod %s: %s", ent->host_path, strerror(errno));
                }
#endif
                ent->attribs = (ent->attribs & ~ATTR_ACORN_MASK) | attr;
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

    if ((ent = find_entry(path, &res, cur_dir, dfs_dir)) && ent->attribs & ATTR_EXISTS) {
        scan_entry(ent);
        osfile_attribs(pb, ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
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
        if (ent == cur_dir)
            adfs_error(err_delcsd);
        else if (ent == lib_dir)
            adfs_error(err_dellib);
        else if (rmdir(ent->host_path) == 0) {
            if (ent == prev_dir)
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

        if ((ent = find_entry(path, &res, cur_dir, dfs_dir)) && ent->attribs & ATTR_EXISTS) {
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
        if ((ent = find_entry(path, &res, cur_dir, dfs_dir))) {
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

static void read_file_io(FILE *fp, uint32_t addr)
{
    char buffer[32768];
    size_t nbytes;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--)
            writemem(addr++, *ptr++);
    }
}

static void read_file_tube(FILE *fp, uint32_t addr)
{
    char buffer[32768];
    size_t nbytes;

    while ((nbytes = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        char *ptr = buffer;
        while (nbytes--)
            tube_writemem(addr++, *ptr++);
    }
}

static void osfile_load(uint32_t pb, const char *path)
{
    vdfs_findres res;
    vdfs_entry *ent = find_entry(path, &res, cur_dir, dfs_dir);
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
                    read_file_io(fp, addr);
                else
                    read_file_tube(fp, addr);
                fclose(fp);
                osfile_attribs(pb, ent);
                a = 1;
            } else {
                log_warn("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
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

    if (a <= 0x08 || a == 0xff) {
        log_debug("vdfs: osfile(A=%02X, X=%02X, Y=%02X)", a, x, y);
        if (check_valid_dir(cur_dir, "current")) {
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
        log_debug("vdfs: channel %d out of range\n", channel);
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
    else if (check_valid_dir(cur_dir, "current")) {        // open file.
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
            ent = find_entry(path, &res, cur_dir, dfs_dir);
            if (ent && (ent->attribs & (ATTR_EXISTS|ATTR_IS_DIR)) == (ATTR_EXISTS|ATTR_IS_DIR)) {
                vdfs_chan[channel].ent = ent;  // make "half-open"
                a = MIN_CHANNEL + channel;
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
                    scan_attr(ent);
                    vdfs_chan[channel].fp = fp;
                    vdfs_chan[channel].ent = ent;
                    a = MIN_CHANNEL + channel;
                } else
                    log_warn("vdfs: osfind: unable to open file '%s' in mode '%s': %s\n", ent->host_fn, mode, strerror(errno));
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

static uint32_t write_len_str(uint32_t mem_ptr, const char *str)
{
    size_t len = strlen(str);
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
    if (check_valid_dir(cur_dir, "current")) {
        uint32_t mem_ptr = readmem32(pb+1);
        const char *title = cur_dir->u.dir.title;
        if (!*title)
            title = cur_dir->acorn_fn;
        mem_ptr = write_len_str(mem_ptr, title);
        if (mem_ptr >= 0xffff0000 || curtube == -1) {
            writemem(mem_ptr++, cur_dir->u.dir.boot_opt);
            writemem(mem_ptr, 0);   // drive is always 0.
        }
        else {
            tube_writemem(mem_ptr++, cur_dir->u.dir.boot_opt);
            tube_writemem(mem_ptr, 0);   // drive is always 0.
        }
    }
}

static void osgbpb_get_dir_adfs(uint32_t pb, vdfs_entry *dir, int dfsdir)
{
    uint32_t mem_ptr = readmem32(pb+1);
    mem_ptr = write_len_str(mem_ptr, "");
    write_len_str(mem_ptr, dir ? dir->acorn_fn : "Unset");
}

static void osgbpb_get_dir_dfs(uint32_t pb, vdfs_entry *dir, int dfsdir)
{
    uint32_t mem_ptr = readmem32(pb+1);
    char tmp[2];
    tmp[0] = dfsdir;
    tmp[1] = 0;
    mem_ptr = write_len_str(mem_ptr, "0");
    write_len_str(mem_ptr, tmp);
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

    if (check_valid_dir(cur_dir, "current")) {
        seq_ptr = readmem32(pb+9);
        if (seq_ptr == 0)
            acorn_sort(cur_dir, (fs_flags & DFS_MODE) ? SORT_DFS : SORT_ADFS);
        n = seq_ptr;
        for (cat_ptr = cur_dir->u.dir.children; cat_ptr; cat_ptr = cat_ptr->next)
            if (cat_ptr->attribs & ATTR_EXISTS)
                if (n-- == 0)
                    break;
        if (cat_ptr) {
            status = 0;
            mem_ptr = readmem32(pb+1);
            n = readmem32(pb+5);
            log_debug("vdfs: seq_ptr=%d, writing max %d entries starting %04X, first=%s\n", seq_ptr, n, mem_ptr, cat_ptr->acorn_fn);
            for (;;) {
                mem_ptr = write_len_str(mem_ptr, cat_ptr->acorn_fn);
                seq_ptr++;
                if (--n == 0)
                    break;
                do
                    cat_ptr = cat_ptr->next;
                while (cat_ptr && !(cat_ptr->attribs & ATTR_EXISTS));
                if (!cat_ptr) {
                    status = 1;
                    break;
                }
                log_debug("vdfs: next=%s", cat_ptr->acorn_fn);
            }
            log_debug("vdfs: finish at %04X\n", mem_ptr);
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

    log_debug("vdfs: osgbpb(A=%02X, YX=%04X)\n", a, pb);

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
            osgbpb_get_dir(pb, cur_dir, dfs_dir);
            break;

        case 0x07: // get library dir.
            osgbpb_get_dir(pb, lib_dir, dfs_lib);
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

static void cmd_back(void)
{
    vdfs_entry *ent;

    ent = cur_dir;
    cur_dir = prev_dir;
    prev_dir = ent;
}

static void cmd_cdir(uint16_t addr)
{
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(cur_dir, "current"))
        if (parse_name(path, sizeof path, addr))
            osfile_cdir(path);
}

static void cmd_delete(uint16_t addr)
{
    if (check_valid_dir(cur_dir, "current")) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            if (!no_wildcards(path))
                return;
            if ((ent = find_entry(path, &res, cur_dir, dfs_dir))) {
                delete_file(ent);
                return;
            }
            adfs_error(res.errmsg);
        }
    }
}

static vdfs_entry *lookup_dir(uint16_t addr)
{
    if (check_valid_dir(cur_dir, "current")) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            ent = find_entry(path, &res, cur_dir, dfs_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    return ent;
                else
                    adfs_error(err_baddir);
            } else
                adfs_error(res.errmsg);
        }
    }
    return NULL;
}

static void cmd_dir_adfs(uint16_t addr)
{
    vdfs_entry *ent;

    if ((ent = lookup_dir(addr))) {
        prev_dir = cur_dir;
        cur_dir = ent;
    }
}

static void cmd_lib_adfs(uint16_t addr)
{
    vdfs_entry *ent;

    if ((ent = lookup_dir(addr)))
        lib_dir = ent;
}

static int parse_dfs_dir(uint16_t addr)
{
    int ch, dir;
    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');
    dir = ch;
    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');
    if (ch == '\r')
        return dir;
    adfs_error(err_baddir);
    return 0;
}

static void cmd_dir_dfs(uint16_t addr)
{
    int dir = parse_dfs_dir(addr);
    if (dir > 0)
        dfs_dir = dir;
}

static void cmd_lib_dfs(uint16_t addr)
{
    int dir = parse_dfs_dir(addr);
    if (dir > 0)
        dfs_lib = dir;
}

static void cmd_title(uint16_t addr)
{
    if (check_valid_dir(cur_dir, "current")) {
        char *ptr = cur_dir->u.dir.title;
        char *end = cur_dir->u.dir.title + sizeof(cur_dir->u.dir.title) - 1;
        int ch, quote= 0;

        log_debug("vdfs: cmd_title: addr=%04x\n", addr);
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
        log_debug("vdfs: cmd_title: name=%s\n", cur_dir->u.dir.title);
        write_back(cur_dir);
    }
}

static void run_file(const char *err)
{
    if (check_valid_dir(cur_dir, "current")) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if ((cmd_tail = parse_name(path, sizeof path, (y << 8) | x))) {
            ent = find_entry(path, &res, cur_dir, dfs_dir);
            if (!(ent && ent->attribs & ATTR_EXISTS) && lib_dir)
                ent = find_entry(path, &res, lib_dir, dfs_lib);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR)
                    adfs_error(err_wont);
                else {
                    FILE *fp = fopen(ent->host_path, "rb");
                    if (fp) {
                        uint16_t addr = ent->u.file.load_addr;
                        show_activity();
                        if (addr >= 0xffff0000 || curtube == -1) {
                            log_debug("vdfs: run_file: writing to I/O proc memory at %08X", addr);
                            read_file_io(fp, addr);
                            pc = ent->u.file.exec_addr;
                        } else {
                            log_debug("vdfs: run_file: writing to tube proc memory at %08X", addr);
                            writemem32(0xc0, ent->u.file.exec_addr); // set up for tube execution.
                            read_file_tube(fp, addr);
                            rom_dispatch(VDFS_ROM_TUBE_EXEC);
                        }
                        fclose(fp);
                    } else {
                        log_warn("vdfs: unable to run file '%s': %s\n", ent->host_fn, strerror(errno));
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
                vdfs_entry *old_ent = find_entry(old_path, &old_res, cur_dir, dfs_dir);
                if (old_ent && old_ent->attribs & ATTR_EXISTS) {
                    vdfs_findres new_res;
                    vdfs_entry *new_ent = find_entry(new_path, &new_res, cur_dir, dfs_dir);
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
    { "ACcess",  VDFS_ACT_NOP     },
    { "APpend",  VDFS_ROM_APPEND  },
    { "BAck",    VDFS_ACT_BACK    },
    { "BACKUp",  VDFS_ACT_NOP     },
    { "BUild",   VDFS_ROM_BUILD   },
    { "CDir",    VDFS_ACT_CDIR    },
    { "COMpact", VDFS_ACT_NOP     },
    { "COpy",    VDFS_ACT_NOP     },
    { "DELete",  VDFS_ACT_DELETE  },
    { "DEStroy", VDFS_ACT_NOP     },
    { "DIR",     VDFS_ACT_DIR     },
    { "DRive",   VDFS_ACT_NOP     },
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

static uint16_t parse_cmd(uint16_t addr, char *dest)
{
    int   ch, i = 0;

    do {
        if (i == MAX_CMD_LEN)
            return 0;
        ch = readmem(addr++);
        dest[i++] = ch;
    } while (ch != ' ' && ch != '\r' && ch != '.' && ch != '"');

    log_debug("vdfs: parse_cmd: cmd=%.*s, finish with %02X at %04X", i, dest, ch, addr);
    if (ch != '\r')
        while ((ch = readmem(addr++)) == ' ')
            ;
    return --addr;
}

static const struct cmdent *lookup_cmd(const struct cmdent *tab, size_t nentry, char *cmd)
{
    const struct cmdent *tab_ptr = tab;
    const struct cmdent *tab_end = tab+nentry;
    int tab_ch, cmd_ch;

    while (tab_ptr < tab_end) {
        const char *tab_cmd = tab_ptr->cmd;
        char *cmd_ptr = cmd;
        do {
            tab_ch = *tab_cmd++;
            cmd_ch = *cmd_ptr++;
        } while (tab_ch && !((tab_ch ^ cmd_ch) & 0x5f)); // case insensitive comparison.
        if ((!tab_ch && (cmd_ch == ' ' || cmd_ch == '\r' || cmd_ch == '"')) || (cmd_ch == '.' && (tab_ch < 'A' || tab_ch > 'Z')))
            return tab_ptr;
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

static vdfs_entry *cat_ent;
static int cat_dfs;

static bool cat_prep_adfs(uint16_t addr, vdfs_entry *dir, int dfsdir, const char *dir_desc)
{
    if (check_valid_dir(dir, dir_desc)) {
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            if (*path) {
                vdfs_findres res;
                vdfs_entry *ent = find_entry(path, &res, dir, dfsdir);
                if (ent && ent->attribs & ATTR_EXISTS) {
                    if (ent->attribs & ATTR_IS_DIR) {
                        dir = ent;
                    } else {
                        adfs_error(err_baddir);
                        return false;
                    }
                } else {
                    adfs_error(res.errmsg);
                    return false;
                }
            }
            if (!scan_dir(dir)) {
                acorn_sort(dir, SORT_ADFS);
                cat_dir = dir;
                cat_ent = dir->u.dir.children;
                cat_dfs = dfsdir;
                return true;
            }
        }
    }
    return false;
}

static bool cat_prep_dfs(uint16_t addr, vdfs_entry *dir, int dfsdir, const char *dir_desc)
{
    int ch;
    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');
    if (ch != '\r') {
        dfsdir = ch;
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
        if (ch != '\r') {
            adfs_error(err_baddir);
            return false;
        }
    }
    if (!dir)
        dir = cur_dir;
    if (!scan_dir(dir)) {
        acorn_sort(dir, SORT_DFS);
        cat_dir = dir;
        cat_ent = dir->u.dir.children;
        cat_dfs = dfsdir;
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

static void cat_get_dir_adfs(vdfs_entry *ent, int dfsdir)
{
    uint32_t mem_ptr = CAT_TMP;
    const char *ptr = ent ? ent->acorn_fn : "Unset";
    int ch;
    while ((ch = *ptr++))
        writemem(mem_ptr++, ch);
    writemem(mem_ptr, 0);
}

static void cat_get_dir_dfs(vdfs_entry *ent, int dfsdir)
{
    writemem(CAT_TMP, ':');
    writemem(CAT_TMP+1, '0');
    writemem(CAT_TMP+2, '.');
    writemem(CAT_TMP+3, dfsdir);
    writemem(CAT_TMP+4, 0);
}

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

static void gcopy_attr(vdfs_entry *ent)
{
    uint16_t mem_ptr = gcopy_fn(ent, CAT_TMP);
    writemem16(mem_ptr, ent->attribs);
    write_file_attr(mem_ptr, ent);
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
    if (check_valid_dir(cur_dir, "current")) {
        vdfs_entry *ent;
        vdfs_findres res;
        char path[MAX_ACORN_PATH];
        if (parse_name(path, sizeof path, addr)) {
            if ((ent = find_entry(path, &res, cur_dir, dfs_dir)) && ent->attribs & ATTR_EXISTS) {
                gcopy_attr(ent);
                rom_dispatch(VDFS_ROM_INFO);
                return;
            }
            adfs_error(res.errmsg);
        }
    }
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
    const struct cmdent *ent;
    char  cmd[MAX_CMD_LEN];

    if (readmem(addr) == '\r') {
        x = osw7fmc_act - VDFS_ACT_OSW7F_NONE;
        rom_dispatch(VDFS_ROM_OSW7F);
    }
    else if ((addr = parse_cmd(addr, cmd)) && ((ent = lookup_cmd(ctab_osw7f, ARRAY_SIZE(ctab_osw7f), cmd)))) {
        osw7fmc_act = ent->act;
        if (ent->act == VDFS_ACT_OSW7F_NONE)
            osw7fmc_tab = NULL;
        else
            osw7fmc_tab = osw7fmc_tabs[ent->act - VDFS_ACT_OSW7F_NONE - 1];
    }
    else
        adfs_error(err_badcmd);
}

static void vdfs_dfs_mode(void)
{
    fs_flags |= DFS_MODE;
    find_entry = find_entry_dfs;
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
    if (!fs_num) {
        y = fsno;
        rom_dispatch(VDFS_ROM_FSSTART);
    }
}

static int mmb_parse_find(uint16_t addr, int ch)
{
    char name[17];
    int i;

    if (ch == '"')
        ch = readmem(addr++);
    i = 0;
    while (ch != '"' && ch != '\r' && i < sizeof(name)) {
        name[i++] = ch;
        ch = readmem(addr++);
    }
    name[i] = 0;
    if ((i = mmb_find(name)) < 0)
        adfs_error(err_discerr);
    return i;
}

static void cmd_mmb_din(uint16_t addr)
{
    int num1, num2, ch;

    ch = readmem(addr++);
    if (ch >= '0' && ch <= '9') {
        num1 = ch - '0';
        while ((ch = readmem(addr++)) >= '0' && ch <= '9')
            num1 = num1 * 10 + ch - '0';
        while (ch == ' ')
            ch = readmem(addr++);
        if (ch == '\r')
            mmb_pick(0, num1);
        else if (ch >= '0' && ch <= '9') {
            num2 = ch - '0';
            while ((ch = readmem(addr++)) >= '0' && ch <= '9')
                num2 = num2 * 10 + ch - '0';
            if (num1 >= 0 && num1 <= 3)
                mmb_pick(num1, num2);
            else
                adfs_error(err_badparms);
        }
        else if ((num2 = mmb_parse_find(addr, ch)) >= 0) {
            if (num1 >= 0 && num1 <= 3)
                mmb_pick(num1, num2);
            else
                adfs_error(err_badparms);
        }
    }
    else if ((num1 = mmb_parse_find(addr, ch)) >= 0)
        mmb_pick(0, num1);
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
            log_debug("vdfs: cmd_dump, start loop, nyb=%x, start=%x\n", ch, start);
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
                log_debug("vdfs: cmd_dump, start loop, nyb=%x, offset=%x\n", ch, offset);
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
        cat_prep(addr, cur_dir, dfs_dir, "current");
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
        cat_prep(addr, (fs_flags & DFS_MODE) ? cur_dir : lib_dir, dfs_lib, "library");
        cat_title();
        rom_dispatch(VDFS_ROM_CAT);
        break;
    case VDFS_ACT_LEX:
        cat_prep(addr, lib_dir, dfs_lib, "library");
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
        select_vdfs(FSNO_VDFS);
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
    default:
        rom_dispatch(act);
    }
    return true;
}

/*
 * OSFSC and supporting functions.
 */

static void osfsc_cmd(void)
{
    uint16_t addr;
    const struct cmdent *ent;
    char  cmd[MAX_CMD_LEN];

    if ((addr = parse_cmd((y << 8) | x, cmd))) {
        if ((ent = lookup_cmd(ctab_filing, ARRAY_SIZE(ctab_filing), cmd))) {
            if (vdfs_do(ent->act, addr))
            return;
        }
    }
    run_file(err_badcmd);
}

static void osfsc_opt(void)
{
    if (x == 4) {
        if (check_valid_dir(cur_dir, "current")) {
            cur_dir->u.dir.boot_opt = y;
            write_back(cur_dir);
        }
    }
    else
        log_debug("vdfs: osfsc unimplemented option %d,%d", x, y);
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
            if (cat_prep(x + (y << 8), cur_dir, dfs_dir, "current")) {
                cat_title();
                rom_dispatch(VDFS_ROM_CAT);
            }
            break;
        case 0x06: // new filesystem taking over.
            fs_num = 0;
            break;
        case 0x07:
            x = MIN_CHANNEL;
            y = MIN_CHANNEL + NUM_CHANNELS;
            break;
        case 0x09:
            if (cat_prep(x + (y << 8), cur_dir, dfs_dir, "current"))
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
    log_debug("vdfs: osword 7F: cmd=%02X, track=%d, sect=%d, sects=%d, ssize=%d", cmd, track, sect, sects, ssize);

    FILE *fp = sdf_owseek(drive & 1, sect, track, drive >> 1, ssize);
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
    uint16_t addr;
    const struct cmdent *ent;
    char  cmd[MAX_CMD_LEN];

    if ((addr = parse_cmd(readmem16(0xf2) + y, cmd))) {
        ent = lookup_cmd(ctab_always, ARRAY_SIZE(ctab_always), cmd);
        if (!ent && vdfs_enabled)
            ent = lookup_cmd(ctab_enabled, ARRAY_SIZE(ctab_enabled), cmd);
        if (!ent && mmb_fn)
            ent = lookup_cmd(ctab_mmb, ARRAY_SIZE(ctab_mmb), cmd);
        if (ent)
            if (vdfs_do(ent->act, addr))
                a = 0;
    }
}

const struct cmdent ctab_help[] = {
    { "VDFS",  VDFS_ROM_HELP_VDFS  },
    { "SRAM",  VDFS_ROM_HELP_SRAM  },
    { "UTILS", VDFS_ROM_HELP_UTILS }
};

static void serv_help(void)
{
    const struct cmdent *ent;
    char  cmd[MAX_CMD_LEN];
    uint16_t addr = readmem16(0xf2) + y;

    int ch = readmem(addr);
    if (ch == '\r')
        rom_dispatch(VDFS_ROM_HELP_SHORT);
    else if (ch == '.')
        rom_dispatch(VDFS_ROM_HELP_ALL);
    else if ((addr = parse_cmd(addr, cmd)))
        if ((ent = lookup_cmd(ctab_help, ARRAY_SIZE(ctab_help), cmd)))
            rom_dispatch(ent->act);
}

static uint8_t save_y;

static void serv_boot(void)
{
    if (vdfs_enabled && (!key_any_down() || key_code_down(ALLEGRO_KEY_S))) {
        vdfs_entry *dir = cur_dir;
        if (check_valid_dir(dir, "current")) {
            scan_dir(dir);
            a = dir->u.dir.boot_opt;
            rom_dispatch(VDFS_ROM_FSBOOT);
        }
    }
    else
        fs_num = 0; // some other filing system.
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
        case 0x10: cat_get_dir(cur_dir, dfs_dir); break;
        case 0x11: cat_get_dir(lib_dir, dfs_lib); break;
        case 0x12: set_ram();   break;
        case 0x13: rest_ram();  break;
        default: log_warn("vdfs: function code %d not recognised\n", value);
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

void vdfs_init(const char *root)
{
    vdfs_findres res;
    scan_seq = 0;
    const char *env = getenv("BEM_VDFS_ROOT");
    if (env)
        root = env; //environment variable wins
    if (!root)
        root = ".";
    vdfs_new_root(root, &root_dir);
    root_dir.parent = cur_dir = prev_dir = cat_dir = &root_dir;
    lib_dir = find_entry_adfs("Lib", &res, &root_dir, dfs_dir);
    dfs_dir = dfs_lib = '$';
    scan_seq++;
    vdfs_adfs_mode();
}
