/*
 * VDFS for B-EM
 * Steve Fosdick 2016,2017
 *
 * This module implements the host part of a Virtual Disk Filing
 * System, one in which a part of filing system of the host is
 * exposed to the guest through normal OS cals on the guest.
 *
 * This particular implementation comes in two parts:
 *
 * 1. A ROM which runs on the guest, the emulated BBC, and takes over
 *    the filing system vectors to become the current filing system.
 *    This rom processes some operations locally on the guest but
 *    many options are passed on to the host by writing values to
 *    a pair of designated ports, in this case at the end of the
 *    address space allocated to the hard disk interface.  For this
 *    part this uses a modified version of the VDFS ROM by J.G Harston
 *    which was in turn based on work by sprow.
 *
 *    See http://mdfs.net/Apps/Filing/
 *
 * 2. This module which runs in the emulator and is called when
 *    the ports concerned are written to.  This module then carries
 *    out the filesystem opertation concerned from the host side.
 */

#include "b-em.h"
#include "6502.h"
#include "keyboard.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "sdf.h"
#include "tube.h"
#include "savestate.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

#include <search.h>
#include <sys/stat.h>

bool vdfs_enabled = 0;

/*
 * The definition of the VDFS entry that follows is the key data
 * structure for this module and models the association between
 * a file/directory as seen by the BBC and as it exists on the host.
 *
 * The pointers host_path, host_fn and host_ext all point into a
 * single chunk of memory obtained from malloc with host_path
 * containing the start address as well as pointing to the full
 * path name.  The host_fn pointer points to the last element within
 * that pathname, i.e. the filename.  The host_inf pointer points to
 * the .inf extension which can be made part of the path name by
 * writing a dot at this pointer and removed again by writing a NUL.
 *
 * In the event this is a directory rather than a file this entry
 * will contain two binary search trees allowing the contents of the
 * directory to be searched by either Acorn filename or host filename.
 */

#define MAX_FILE_NAME    10
#define MAX_ACORN_PATH   256

// These are Acorn standard attributes

#define ATTR_USER_READ   0x0001
#define ATTR_USER_WRITE  0x0002
#define ATTR_USER_EXEC   0x0004
#define ATTR_USER_LOCKD  0x0008
#define ATTR_OTHR_READ   0x0010
#define ATTR_OTHR_WRITE  0x0020
#define ATTR_OTHR_EXEC   0x0040
#define ATTR_OTHR_LOCKD  0x0080

// These are VDFS internal attributes.

#define ATTR_OPEN_READ   0x1000
#define ATTR_OPEN_WRITE  0x2000
#define ATTR_IS_DIR      0x4000
#define ATTR_EXISTS      0x8000

typedef struct _vdfs_entry vdfs_ent_t;

struct _vdfs_entry {
    vdfs_ent_t *parent;
    char       *host_path;
    char       *host_fn;
    char       *host_inf;
    char       acorn_fn[MAX_FILE_NAME+1];
    uint16_t   attribs;
    uint32_t   load_addr;
    uint32_t   exec_addr;
    uint32_t   length;
    time_t     inf_read;
    // the following only apply to a directory.
    void       *acorn_tree;
    void       *host_tree;
    vdfs_ent_t **cat_tab;
    size_t     cat_size;
    unsigned   scan_seq;
    time_t     scan_mtime;
};

static vdfs_ent_t **cat_ptr;    // used by the tree walk callback.

static vdfs_ent_t root_dir;     // root as seen by BBC, not host.
static vdfs_ent_t *cur_dir;
static vdfs_ent_t *lib_dir;
static vdfs_ent_t *prev_dir;
static vdfs_ent_t *cat_dir;
static unsigned   scan_seq;

/*
 * Open files.  An open file is an association between a host OS file
 * pointer, i.e. the host file is kept open too, and a catalogue
 * entry.  Normally for an open file both pointers are set and for a
 * closed file both are NULL but to emulate the OSFIND call correctly
 * a directory can be opened and becomes "half-open", i.e. it can be
 * closed again but any attempt to read or write it will fail.  That
 * case is marked by the vdfs_ent_t pointer being set but the host
 * FILE pointer being NULL.
 */

#define MIN_CHANNEL      128
#define MAX_CHANNEL      255
#define NUM_CHANNELS     (MAX_CHANNEL-MIN_CHANNEL)

typedef struct {
    FILE       *fp;
    vdfs_ent_t *ent;
} vdfs_file_t;

static vdfs_file_t vdfs_chan[NUM_CHANNELS];

enum vdfs_action {
    VDFS_ROM_RETURN,
    VDFS_ROM_FSSTART,
    VDFS_ROM_FSBOOT,
    VDFS_ROM_FSINFO,
    VDFS_ROM_CAT,
    VDFS_ROM_EX,
    VDFS_ROM_INFO,
    VDFS_ROM_DUMP,
    VDFS_ROM_LIST,
    VDFS_ROM_PRINT,
    VDFS_ROM_TYPE,
    VDFS_ROM_ROMS,
    VDFS_ROM_TUBE_EXEC,
    VDFS_ROM_TUBE_INIT,
    VDFS_ROM_TUBE_EXPL,
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
    VDFS_ACT_LIB,
    VDFS_ACT_RENAME,
    VDFS_ACT_RESCAN
};

#define MAX_CMD_LEN 8

struct cmdent {
    char cmd[MAX_CMD_LEN];
    enum vdfs_action act;
};

#define FSNO_VDFS   0x11
#define FSNO_ADFS   0x08
#define FSNO_DFS    0x04

#define CLAIM_ADFS  0x80
#define CLAIM_DFS   0x40

static uint8_t  reg_a;
static uint8_t  fs_flags = 0xff;
static uint8_t  fs_num   = 0x11;
static uint16_t cmd_tail;

static uint16_t readmem16(uint16_t addr)
{
    uint32_t value;

    value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    return value;
}

static uint32_t readmem32(uint16_t addr)
{
    uint32_t value;

    value = readmem(addr);
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

static void free_noop(void *ptr) { }

static void free_tree_node(void *ptr)
{
    vdfs_ent_t *ent = (vdfs_ent_t *)ptr;

    if ((ptr = ent->acorn_tree))
        tdestroy(ptr, free_noop);
    if ((ptr = ent->host_tree))
        tdestroy(ptr, free_tree_node);
    if ((ptr = ent->cat_tab))
        free(ptr);
    if ((ptr = ent->host_path))
        free(ptr);
    free(ent);
}

void vdfs_reset(void)
{
    flush_all();
}

static int host_comp(const void *a, const void *b)
{
    vdfs_ent_t *ca = (vdfs_ent_t *)a;
    vdfs_ent_t *cb = (vdfs_ent_t *)b;
    return strcmp(ca->host_fn, cb->host_fn);
}

static int acorn_comp(const void *a, const void *b)
{
    vdfs_ent_t *ca = (vdfs_ent_t *)a;
    vdfs_ent_t *cb = (vdfs_ent_t *)b;
    return strcasecmp(ca->acorn_fn, cb->acorn_fn);
}

/*
 * Functions used to parse values from the INF file on the host
 */

static void get_filename(FILE *fp, char *dest)
{
    int ch;
    char *ptr, *end;

    do
        ch = getc(fp);
    while (isspace(ch));

    ptr = dest;
    end = dest + MAX_FILE_NAME;
    while (ch != EOF && !isspace(ch)) {
        if (ptr < end)
            *ptr++ = ch;
        ch = getc(fp);
    }
    if (ptr < end)
        *ptr = '\0';
}

static unsigned get_hex(FILE *fp)
{
    int ch;
    unsigned value = 0;

    if (!feof(fp)) {
        do
            ch = getc(fp);
        while (isspace(ch));

        while (isxdigit(ch)) {
            value = value << 4;
            if (ch >= '0' && ch <= '9')
                value += (ch - '0');
            else if (ch >= 'A' && ch <= 'F')
                value += 10 + (ch - 'A');
            else
                value += 10 + (ch - 'a');
            ch = getc(fp);
        }
    }
    return value;
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

static char *make_host_path(vdfs_ent_t *ent, const char *host_fn)
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
static const char err_notfound[] = "\xd6" "Not found";
static const char err_channel[]  = "\xde" "Channel";
static const char err_badcmd[]   = "\xfe" "Bad command";
static const char err_badaddr[]  = "\x94" "Bad parms";
static const char err_nomem[]    = "\x92" "Out of memory";
static const char err_no_swr[]   = "\x93" "No SWRAM at that address";
static const char err_too_big[]  = "\x94" "Too big";

// Error messages unique to VDFS

static const char err_baddir[]  = "\xc7" "Bad directory";

/* Other ADFS messages not used.

static const char err_aborted[]  = "\x92" "Aborted";
static const char err_badcsum[]  = "\xaa" "Bad checksum";
static const char err_outside[]  = "\xb7" "Outside file";
static const char err_locked[]   = "\xc3" "Locked";
static const char err_full[]     = "\xc6" "Disc full";
static const char err_datalost[] = "\xca" "Data lost, channel";
static const char err_badopt[]   = "\xcb" "Bad opt";
static const char err_badname[]  = "\xcc" "Bad name";
static const char err_eof[]      = "\xdf" "EOF";
static const char err_wildcard[] = "\xfd" "Wild cards";

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

static inline void mark_extant(vdfs_ent_t *ent)
{
    if (!(ent->attribs & ATTR_EXISTS)) {
        ent->attribs |= ATTR_EXISTS;
        ent->parent->cat_size++;
    }
}

static inline void mark_deleted(vdfs_ent_t *ent)
{
    if (ent->attribs & ATTR_EXISTS) {
        ent->attribs &= ~ATTR_EXISTS;
        ent->parent->cat_size--;
    }
}

static void tree_destroy(vdfs_ent_t *ent)
{
    void *ptr;

    if ((ptr = ent->acorn_tree)) {
        tdestroy(ptr, free_noop);
        ent->acorn_tree = NULL;
    }
    if ((ptr = ent->host_tree)) {
        tdestroy(ptr, free_tree_node);
        ent->host_tree = NULL;
    }
    if ((ptr = ent->cat_tab)) {
        free(ptr);
        ent->cat_tab = NULL;
        ent->cat_size = 0;
    }
}

// Populate a VDFS entry from host information.

static void scan_entry(vdfs_ent_t *ent)
{
    FILE *fp;
    struct stat stb;

    // open and parse .inf file
    *ent->host_inf = '.';
    fp = fopen(ent->host_path, "rt");
    *ent->host_inf = '\0';
    if (fp) {
        get_filename(fp, ent->acorn_fn);
        ent->load_addr = get_hex(fp);
        ent->exec_addr = get_hex(fp);
        fclose(fp);
    } else if (ent->acorn_fn[0] == '\0')
        hst2bbc(ent->host_fn, ent->acorn_fn);

    // stat the real file.
    if (stat(ent->host_path, &stb) == -1)
        log_warn("vdfs: unable to stat '%s': %s\n", ent->host_path, strerror(errno));
    else {
        ent->length = stb.st_size;
        ent->attribs |= ATTR_EXISTS;
        if (S_ISDIR(stb.st_mode))
            ent->attribs |= ATTR_IS_DIR;
        else if (ent->attribs & ATTR_IS_DIR) {
            log_debug("vdfs: dir %s has become a file", ent->acorn_fn);
            tree_destroy(ent);
            ent->attribs &= ~ATTR_IS_DIR;
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
    log_debug("vdfs: scan_entry: acorn=%s, host=%s, attr=%04X, load=%08X, exec=%08X\n", ent->acorn_fn, ent->host_fn, ent->attribs, ent->load_addr, ent->exec_addr);
}

static void init_entry(vdfs_ent_t *ent)
{
    ent->host_fn = ".";
    ent->acorn_fn[0] = '\0';
    ent->acorn_fn[MAX_FILE_NAME] = '\0';
    ent->attribs = 0;
    ent->load_addr = 0;;
    ent->exec_addr = 0;
    ent->length = 0;
    ent->inf_read = 0;
    ent->acorn_tree = NULL;
    ent->host_tree = NULL;
    ent->cat_tab = NULL;
    ent->cat_size = 0;
    ent->host_path = NULL;
    ent->scan_seq = 0;
    ent->scan_mtime = 0;
}

// Create VDFS entry for a new file.

static vdfs_ent_t *new_entry(vdfs_ent_t *dir, const char *host_fn)
{
    vdfs_ent_t *ent, **ptr;
    int name_len, seq_ch = '0';
    char *host_path;

    if ((ent = malloc(sizeof(vdfs_ent_t)))) {
        init_entry(ent);
        ent->parent = dir;
        if ((host_path = make_host_path(ent, host_fn))) {
            scan_entry(ent);
            ptr = tsearch(ent, &dir->acorn_tree, acorn_comp);
            if (*ptr != ent) {
                // name was already in tree - generate a unique one.
                name_len = strlen(ent->acorn_fn);
                if (name_len < (MAX_FILE_NAME-2)) {
                    ent->acorn_fn[name_len] = '~';
                    ent->acorn_fn[name_len+1] = seq_ch;
                } else {
                    ent->acorn_fn[MAX_FILE_NAME-2] = '~';
                    ent->acorn_fn[MAX_FILE_NAME-1] = seq_ch;
                }
                ptr = tsearch(ent, &dir->acorn_tree, acorn_comp);
                while (*ptr != ent) {
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
                    ptr = tsearch(ent, &dir->acorn_tree, acorn_comp);
                }
                log_debug("vdfs: new_entry: unique name %s used\n", ent->acorn_fn);
            }
            tsearch(ent, &dir->host_tree, host_comp);
            log_debug("vdfs: new_entry: returing new entry %p\n", ent);
            return ent;
        }
        free(ent);
    }
    return NULL;
}

// Given a VDFS entry representing a dir scan the corresponding host dir.

static void tree_visit_del(const void *nodep, const VISIT which, const int depth)
{
    vdfs_ent_t *ent;

    if (which == postorder || which == leaf) {
        ent = *(vdfs_ent_t **)nodep;
        ent->attribs &= (ATTR_IS_DIR|ATTR_OPEN_READ|ATTR_OPEN_WRITE);
    }
}

static int scan_dir(vdfs_ent_t *dir)
{
    int  count = 0;
    DIR  *dp;
    struct stat stb;
    struct dirent *dep;
    vdfs_ent_t **ptr, *ent, key;
    const char *ext;

    // Has this been scanned sufficiently recently already?

    if (stat(dir->host_path, &stb) == -1)
        log_warn("vdfs: unable to stat directory '%s': %s", dir->host_path, strerror(errno));
    else if (scan_seq <= dir->scan_seq && stb.st_mtime <= dir->scan_mtime) {
        log_debug("vdfs: using cached dir info for %s", dir->host_path);
        return 0;
    }

    if ((dp = opendir(dir->host_path))) {
        // Mark all previosly seen entries deleted but leave them
        // in the tree.
        twalk(dir->acorn_tree, tree_visit_del);

        // Go through the entries in the host dir which are not
        // in sorted order, find each in the tree and remove the
        // deleted atribute, if found or create a new entry if not.
        while ((dep = readdir(dp))) {
            if (*(dep->d_name) != '.') {
                if (!(ext = strrchr(dep->d_name, '.')) || strcasecmp(ext, ".inf")) {
                    key.host_fn = dep->d_name;
                    if ((ptr = tfind(&key, &dir->host_tree, host_comp))) {
                        ent = *ptr;
                        scan_entry(ent);
                        count++;
                    } else if ((ent = new_entry(dir, dep->d_name)))
                        count++;
                    else {
                        count = -1;
                        break;
                    }
                }
            }
        }
        closedir(dp);
        dir->scan_seq = scan_seq;
        dir->scan_mtime = stb.st_mtime;
        dir->cat_size = count;
        log_debug("vdfs: scan_dir count=%d\n", count);
        return 0;
    } else {
        log_warn("vdfs: unable to opendir '%s': %s\n", dir->host_path, strerror(errno));
        return 1;
    }
}

// Generate the sorted table of entries used by the *CAT command
// and associated OSGBPB call.

static void tree_visit_cat(const void *nodep, const VISIT which, const int depth)
{
    vdfs_ent_t *ent;

    if (which == postorder || which == leaf) {
        ent = *(vdfs_ent_t **)nodep;
        if (ent->attribs & ATTR_EXISTS)
            *cat_ptr++ = ent;
    }
}

static int gen_cat_tab(vdfs_ent_t *dir)
{
    int result;
    vdfs_ent_t **new_tab;
    size_t new_size;

    if ((result = scan_dir(dir)) == 0 && dir->cat_size > 0) {
        if ((new_tab = malloc((dir->cat_size) * (sizeof(vdfs_ent_t *))))) {
            cat_ptr = new_tab;
            twalk(dir->acorn_tree, tree_visit_cat);
            if ((new_size = cat_ptr - new_tab) != dir->cat_size) {
                log_warn("vdfs: catalogue size mismatch for %s (%s), calculated %u, counted %u", dir->acorn_fn, dir->host_path, (unsigned)dir->cat_size, (unsigned)new_size);
                dir->cat_size = new_size;
            }
            if (dir->cat_tab)
                free(dir->cat_tab);
            dir->cat_tab = new_tab;
        } else {
            log_warn("vdfs: out of memory scanning directory");
            result = 1;
        }
    }
    return result;
}

static uint16_t simple_name(char *str, size_t size, uint16_t addr)
{
    char *ptr = str;
    char *end = str + size - 1;
    int ch;

    log_debug("vdfs: simple_name: addr=%04x\n", addr);
    do {
        ch = readmem(addr++);
        if (ch == '\r' || ch == ' ')
            break;
        *ptr++ = ch;
    } while (ptr < end);

    *ptr = '\0';
    return addr;
}

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
    while (ptr < end && ch != '\r' && (ch != '"' || !quote) && (ch != ' ' || quote)) {
        *ptr++ = ch & 0x7f;
        ch = readmem(addr++);
    };

    *ptr = '\0';
    return addr;
}

static int check_valid_dir(vdfs_ent_t *ent, const char *which)
{
    if (ent->attribs & ATTR_IS_DIR)
        return 1;
    log_warn("vdfs: %s directory is not valid", which);
    adfs_error(err_baddir);
    return 0;
}

// Given the address in BBC RAM of a filename find the VDFS entry.

static vdfs_ent_t *find_entry(const char *filename, vdfs_ent_t *key, vdfs_ent_t *ent)
{
    int i, ch;
    const char *fn_src;
    char *fn_ptr;
    vdfs_ent_t **ptr;

    init_entry(key);
    key->parent = NULL;
    for (fn_src = filename;;) {
        fn_ptr = key->acorn_fn;
        for (i = 0; i < MAX_FILE_NAME; i++) {
            ch = *fn_src++;
            if (ch == '\0' || ch == '.')
                break;
            *fn_ptr++ = ch;
        }
        *fn_ptr = '\0';
        if (((key->acorn_fn[0] == '$' || key->acorn_fn[0] == '&') && key->acorn_fn[1] == '\0') || (key->acorn_fn[0] == ':' && isdigit(key->acorn_fn[1])))
            ent = &root_dir;
        else if (key->acorn_fn[0] == '%' && key->acorn_fn[1] == '\0' && check_valid_dir(lib_dir, "library"))
            ent = lib_dir;
        else if (key->acorn_fn[0] == '^' && key->acorn_fn[1] == '\0')
            ent = ent->parent;
        else if (!scan_dir(ent) && (ptr = tfind(key, &ent->acorn_tree, acorn_comp)))
            ent = *ptr;
        else {
            if (ch != '.')
                key->parent = ent;
            log_debug("vdfs: find_entry: acorn path %s not found", filename);
            return NULL; // not found
        }
        if (ch != '.') {
            log_debug("vdfs: find_entry: acorn path %s found as %s", filename, ent->host_path);
            return ent;
        }
        if (!(ent->attribs & ATTR_IS_DIR)) {
            log_debug("vdfs: find_entry: acorn path %s has file %s where directory expected", filename, ent->host_path);
            return NULL;
        }
    }
    return NULL;
}

static vdfs_ent_t *add_new_file(vdfs_ent_t *dir, const char *name)
{
    vdfs_ent_t *new_ent;
    char host_fn[MAX_FILE_NAME];

    if ((new_ent = malloc(sizeof(vdfs_ent_t)))) {
        init_entry(new_ent);
        strncpy(new_ent->acorn_fn, name, MAX_FILE_NAME);
        bbc2hst(name, host_fn);
        new_ent->parent = dir;
        if (make_host_path(new_ent, host_fn)) {
            tsearch(new_ent, &dir->acorn_tree, acorn_comp);
            tsearch(new_ent, &dir->host_tree, host_comp);
            return new_ent;
        }
        free(new_ent);
    }
    log_warn("vdfs: out of memory adding new file");
    return NULL;
}

// Write changed attributes back to the .inf file.

static void write_back(vdfs_ent_t *ent)
{
    FILE *fp;

    *ent->host_inf = '.'; // select .inf file.
    if ((fp = fopen(ent->host_path, "wt"))) {
        fprintf(fp, "%s %08X %08X %08X\n", ent->acorn_fn, ent->load_addr, ent->exec_addr, ent->length);
        fclose(fp);
    } else
        log_warn("vdfs: unable to create INF file '%s': %s\n", ent->host_path, strerror(errno));
    *ent->host_inf = '\0'; // select real file.
}

static void close_file(int channel)
{
    vdfs_ent_t *ent;
    FILE *fp;

    if ((ent = vdfs_chan[channel].ent)) {
        if ((fp = vdfs_chan[channel].fp)) {
            fclose(fp);
            vdfs_chan[channel].fp = NULL;
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
    tree_destroy(&root_dir);
    if ((ptr = root_dir.host_path)) {
        free(ptr);
        root_dir.host_path = NULL;
    }
}

static int vdfs_new_root(const char *root, vdfs_ent_t *ent)
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
    vdfs_ent_t new_root;
    if (vdfs_new_root(root, &new_root)) {
        vdfs_close();
        root_dir = new_root;
        root_dir.parent = cur_dir = lib_dir = prev_dir = cat_dir = &root_dir;
        scan_seq++;
    } else if (new_root.host_path)
        free(new_root.host_path);
}

const char *vdfs_get_root(void)
{
    return root_dir.host_path;
}

// Initialise the VDFS module.

void vdfs_init(void)
{
    char *root;

    scan_seq = 0;
    if ((root = getenv("BEM_VDFS_ROOT")) == NULL)
        root = ".";
    vdfs_new_root(root, &root_dir);
    root_dir.parent = cur_dir = lib_dir = cat_dir = prev_dir = &root_dir;
    scan_seq++;
}

/*
 * Functions concerned with saving and re-loading the state of VDFS
 * to the save state file.
 */

static vdfs_ent_t *ss_spec_path(FILE *f, const char *which)
{
    char *path;
    vdfs_ent_t *ent, key;

    path = savestate_load_str(f);
    log_debug("vdfs: loadstate setting %s directory to $.%s", which, path);
    if ((ent = find_entry(path, &key, &root_dir)))
        if (!(ent->attribs & ATTR_IS_DIR))
            ent = NULL;
    free(path);
    return ent;
}

static vdfs_ent_t *ss_load_dir(vdfs_ent_t *dir, FILE *f, const char *which)
{
    vdfs_ent_t *ent;
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

static size_t ss_calc_len(vdfs_ent_t *ent)
{
    vdfs_ent_t *parent = ent->parent;
    size_t len = strlen(ent->acorn_fn);

    if (parent->parent != parent)
        len += ss_calc_len(ent->parent) + 1;
    return len;
}

static void ss_save_ent(vdfs_ent_t *ent, FILE *f)
{
    vdfs_ent_t *parent = ent->parent;

    if (parent->parent == parent)
        fputs(ent->acorn_fn, f);
    else {
        ss_save_ent(ent->parent, f);
        putc('.', f);
        fputs(ent->acorn_fn, f);
    }
}

static void ss_save_dir1(vdfs_ent_t *ent, FILE *f)
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

static void ss_save_dir2(vdfs_ent_t *ent, FILE *f)
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
    vdfs_ent_t *ent, key;
    FILE *fp;
    char path[MAX_ACORN_PATH];

    log_debug("vdfs: exec_swr_fs: flags=%02x, fn=%04x, romid=%02d, start=%04x, len=%04x\n", flags, fname, romid, start, pblen);
    if (check_valid_dir(cur_dir, "current")) {
        if ((romid = swr_calc_addr(flags, &start, romid)) >= 0) {
            simple_name(path, sizeof path, fname);
            ent = find_entry(path, &key, cur_dir);
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
                        adfs_error(err_notfound);
                } else
                    adfs_error(err_too_big);
            } else {
                // write sideways RAM to file.
                len = pblen;
                if (len <= 16384) {
                    if (!ent)
                        ent = add_new_file(cur_dir, key.acorn_fn);
                    if (ent) {
                        if ((fp = fopen(ent->host_path, "wb"))) {
                            fwrite(rom + romid * 0x4000 + start, len, 1, fp);
                            fclose(fp);
                            load_add = 0xff008000 | (romid << 16) | start;
                            ent->load_addr = load_add;
                            ent->exec_addr = load_add;
                            ent->length = len;
                            mark_extant(ent);
                            write_back(ent);
                        } else
                            log_warn("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
                    } else
                        adfs_error(err_wont);
                } else
                    adfs_error(err_too_big);
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
        if (ram_start > 0xffff0000 || curtube == -1) {
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

    if (isxdigit(ch)) {
        do {
            value = value << 4;
            if (ch >= '0' && ch <= '9')
                value += (ch - '0');
            else if (ch >= 'A' && ch <= 'F')
                value += 10 + (ch - 'A');
            else
                value += 10 + (ch - 'a');
            ch = readmem(addr++);
        } while (isxdigit(ch));
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

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (isxdigit(ch))
        return srp_hex(ch, addr, (uint16_t*)romid);
    if (ch >= 'W' && ch <= 'Z')
        *romid = mem_findswram(ch - 'W');
    else if (ch >= 'w' && ch <= 'z')
        *romid = mem_findswram(ch - 'w');
    else
        *romid = -1;
    return --addr;
}

static void srp_tail(uint16_t addr, uint8_t flag, uint16_t fnaddr, uint16_t start, uint16_t len)
{
    int ch;
    int16_t romid;

    addr = srp_romid(addr, &romid);
    if (romid >= 0)
        flag &= ~0x40;
    if (fs_num) {
        log_debug("vdfs: srp_tail - executing internally");
        exec_swr_fs(flag, fnaddr, romid, start, len);
        p.c = 0;
    } else {
        p.c = 1;
        writemem(0x70, flag);
        writemem16(0x71, fnaddr);
        writemem(0x73, romid);
        writemem16(0x74, start);
        writemem16(0x76, len);
        writemem16(0x78, 0);
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
        if (ch == 'Q' || ch == 'q')
            writemem16(0x7a, 0xffff);
        else
            writemem16(0x7a, 0);
    }
}

static void cmd_srload(uint16_t addr)
{
    uint16_t fnadd, start;

    if ((addr = srp_fn(addr, &fnadd))) {
        log_debug("vdfs: cmd_srload fnaddr=%04X", fnadd);
        if ((addr = srp_start(addr, &start))) {
            log_debug("vdfs: cmd_srload start=%04X", start);
            srp_tail(addr, 0xC0, fnadd, start, 0);
            return;
        }
    }
    adfs_error(err_badparms);
}

static void cmd_srsave(uint16_t addr)
{
    uint16_t fnadd, start, len;

    if ((addr = srp_fn(addr, &fnadd))) {
        if ((addr = srp_start(addr, &start))) {
            if ((addr = srp_length(addr, start, &len))) {
                srp_tail(addr, 0x40, fnadd, start, len);
                return;
            }
        }
    }
    adfs_error(err_badparms);
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

static void osfile_attribs(uint32_t pb, vdfs_ent_t *ent)
{
    writemem32(pb+0x02, ent->load_addr);
    writemem32(pb+0x06, ent->exec_addr);
    writemem32(pb+0x0a, ent->length);
    writemem32(pb+0x0e, ent->attribs);
}

static void save_callback(FILE *fp, uint32_t start_addr, uint32_t end_addr)
{
    uint32_t ptr;

    if (start_addr > 0xffff0000 || curtube == -1) {
        for (ptr = start_addr; ptr < end_addr; ptr++)
            putc(readmem(ptr), fp);
    } else {
        for (ptr = start_addr; ptr < end_addr; ptr++)
            putc(tube_readmem(ptr), fp);
    }
}

static void cfile_callback(FILE *fp, uint32_t start_addr, uint32_t end_addr)
{
    fseek(fp, end_addr - start_addr -1, SEEK_SET);
    putc(0, fp);
}

static void osfile_write(uint32_t pb, vdfs_ent_t *ent, vdfs_ent_t *key, void (*callback)(FILE *fp, uint32_t start_addr, uint32_t end_addr))
{
    FILE *fp;
    uint32_t start_addr, end_addr;

    if (ent) {
        if (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)) {
            log_debug("vdfs: attempt to save file %s which is already open via OSFIND", key->acorn_fn);
            adfs_error(err_isopen);
            return;
        }
        if (ent->attribs & ATTR_EXISTS) {
            if (ent->attribs & ATTR_IS_DIR) {
                log_debug("vdfs: attempt to create file %s over an existing dir", key->acorn_fn);
                adfs_error(err_direxist);
                return;
            }
        }
    }
    else if (!key->parent) {
        adfs_error(err_notfound);
        return;
    }
    else if (!(ent = add_new_file(key->parent, key->acorn_fn))) {
        adfs_error(err_nomem);
        return;
    }

    if ((fp = fopen(ent->host_path, "wb"))) {
        mark_extant(ent);
        ent->attribs = (ent->attribs & ~ATTR_IS_DIR);
        start_addr = readmem32(pb+0x0a);
        end_addr = readmem32(pb+0x0e);
        callback(fp, start_addr, end_addr);
        fclose(fp);
        ent->load_addr = readmem32(pb+0x02);
        ent->exec_addr = readmem32(pb+0x06);
        ent->length = end_addr-start_addr;
        write_back(ent);
        writemem32(pb+0x0a, ent->length);
        writemem32(pb+0x0e, ent->attribs);
        a = 1;
    } else
        log_warn("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
}

static void osfile_set_all(uint32_t pb, vdfs_ent_t *ent)
{
    if (ent && ent->attribs & ATTR_EXISTS) {
        ent->load_addr = readmem32(pb+0x02);
        ent->exec_addr = readmem32(pb+0x06);
        write_back(ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_set_load(uint32_t pb, vdfs_ent_t *ent)
{
    if (ent && ent->attribs & ATTR_EXISTS) {
        ent->load_addr = readmem32(pb+0x02);
        write_back(ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_set_exec(uint32_t pb, vdfs_ent_t *ent)
{
    if (ent && ent->attribs & ATTR_EXISTS) {
        ent->exec_addr = readmem32(pb+0x06);
        write_back(ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void osfile_set_attr(vdfs_ent_t *ent)
{
    log_debug("vdfs: write attributes not implemented");
    if (ent && ent->attribs & ATTR_EXISTS)
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    else
        a = 0;
}

static void osfile_get_attr(uint32_t pb, vdfs_ent_t *ent)
{
    if (ent && ent->attribs & ATTR_EXISTS) {
        scan_entry(ent);
        osfile_attribs(pb, ent);
        a = (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
    }
    else
        a = 0;
}

static void delete_inf(vdfs_ent_t *ent)
{
    *ent->host_inf = '.';
    if (unlink(ent->host_path) != 0 && errno != ENOENT)
        log_warn("vdfs: failed to delete 'INF file %s': %s", ent->host_path, strerror(errno));
    *ent->host_inf = '\0';
}

static void delete_file(vdfs_ent_t *ent)
{
    if (ent->attribs & ATTR_IS_DIR) {
        if (ent == cur_dir)
            adfs_error(err_delcsd);
        else if (ent == lib_dir)
            adfs_error(err_dellib);
        else if (rmdir(ent->host_path) == 0) {
            if (ent == prev_dir)
                prev_dir = cur_dir;
            tree_destroy(ent);
            ent->attribs &= ~ATTR_IS_DIR;
            mark_deleted(ent);
            a = 2;
        } else
            adfs_hosterr(errno);
    } else {
        if (unlink(ent->host_path) == 0) {
            mark_deleted(ent);
            delete_inf(ent);
            a = 1;
        } else
            adfs_hosterr(errno);
    }
}

static void osfile_delete(uint32_t pb, vdfs_ent_t *ent)
{
    if (ent && ent->attribs & ATTR_EXISTS) {
        osfile_attribs(pb, ent);
        delete_file(ent);
    }
    else
        a = 0;
}

static void create_dir(vdfs_ent_t *ent)
{
    int res;
#ifdef WIN32
    res = mkdir(ent->host_path);
#else
    res = mkdir(ent->host_path, 0777);
#endif
    if (res == 0) {
        mark_extant(ent);
        scan_entry(ent);
        a = 2;
    }
    else {
        adfs_hosterr(errno);
        log_debug("vdfs: unable to mkdir '%s': %s", ent->host_path, strerror(errno));
    }
}

static void osfile_cdir(vdfs_ent_t *ent, vdfs_ent_t *key)
{
    vdfs_ent_t *parent;

    if (ent) {
        if (ent->attribs & ATTR_EXISTS) {
            if (!(ent->attribs & ATTR_IS_DIR)) {
                log_debug("vdfs: attempt to create dir %s on top of an existing file", key->acorn_fn);
                adfs_error(err_filexist);  // file in the way.
            }
        } else
            create_dir(ent);
    } else {
        parent = key->parent;
        if (parent && parent->attribs & ATTR_EXISTS) {
            if ((ent = add_new_file(key->parent, key->acorn_fn)))
                create_dir(ent);
        } else {
            log_debug("vdfs: attempt to create dir %s in non-existent directory", key->acorn_fn);
            adfs_error(err_notfound);
        }
    }
}

static void osfile_load(uint32_t pb, vdfs_ent_t *ent)
{
    FILE *fp;
    uint32_t addr;
    uint32_t size;
    int ch;

    if (ent && ent->attribs & ATTR_EXISTS) {
        if (ent->attribs & ATTR_IS_DIR)
            adfs_error(err_wont);
        else if ((fp = fopen(ent->host_path, "rb"))) {
            if (readmem(pb+0x06) == 0)
                addr = readmem32(pb+0x02);
            else
                addr = ent->load_addr;
            size = 0;
            if (addr > 0xffff0000 || curtube == -1) {
                while ((ch = getc(fp)) != EOF) {
                    writemem(addr++, ch);
                    size++;
                }
            } else {
                while ((ch = getc(fp)) != EOF) {
                    tube_writemem(addr++, ch);
                    size++;
                }
            }
            fclose(fp);
            osfile_attribs(pb, ent);
            a = 1;
        } else {
            log_warn("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
            adfs_hosterr(errno);
        }
    } else
        adfs_error(err_notfound);
}

static void osfile(void)
{
    vdfs_ent_t *ent, key;
    uint32_t pb = (y << 8) | x;
    char path[MAX_ACORN_PATH];

    if (a <= 0x08 || a == 0xff) {
        log_debug("vdfs: osfile(A=%02X, X=%02X, Y=%02X)", a, x, y);
        if (check_valid_dir(cur_dir, "current")) {
            simple_name(path, sizeof path, readmem16(pb));
            ent = find_entry(path, &key, cur_dir);
            switch (a) {
                case 0x00:  // save file.
                    osfile_write(pb, ent, &key, save_callback);
                    break;
                case 0x01:  // set all attributes.
                    osfile_set_all(pb, ent);
                    break;
                case 0x02:  // set load address only.
                    osfile_set_load(pb, ent);
                    break;
                case 0x03:  // set exec address only.
                    osfile_set_exec(pb, ent);
                    break;
                case 0x04:  // write attributes.
                    osfile_set_attr(ent);
                    break;
                case 0x05:  // get addresses and attributes.
                    osfile_get_attr(pb, ent);
                    break;
                case 0x06:
                    osfile_delete(pb, ent);
                    break;
                case 0x07:
                    osfile_write(pb, ent, &key, cfile_callback);
                    break;
                case 0x08:
                    osfile_cdir(ent, &key);
                    break;
                case 0xff:  // load file.
                    osfile_load(pb, ent);
                    break;
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
    FILE *fp;

    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL) {
        if ((fp = vdfs_chan[channel-MIN_CHANNEL].fp))
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
    p.c = 1;
    if ((fp = getfp_read(y))) {
        if ((ch = getc(fp)) != EOF) {
            a = ch;
            p.c = 0;
        }
    }
}

static FILE *getfp_write(int channel)
{
    vdfs_file_t *p;
    vdfs_ent_t  *ent;
    FILE *fp = NULL;

    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL) {
        p = &vdfs_chan[channel-MIN_CHANNEL];
        if ((ent = p->ent)) {
            if (ent->attribs & ATTR_OPEN_WRITE) {
                if (!(fp = p->fp)) {
                    log_debug("vdfs: attempt to use closed channel %d", channel);
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

    if ((fp = getfp_write(y)))
        putc(a, fp);
}

static void osfind(void)
{
    int acorn_mode, channel;
    vdfs_ent_t *ent, key;
    const char *mode;
    uint16_t attribs;
    FILE *fp;
    char path[MAX_ACORN_PATH];

    log_debug("vdfs: osfind(A=%02X, X=%02X, Y=%02X)", a, x, y);

    if (a == 0) {   // close file.
        channel = y;
        if (channel == 0)
            close_all();
        else if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL)
            close_file(channel-MIN_CHANNEL);
        else
            adfs_error(err_channel);
    } else if (check_valid_dir(cur_dir, "current")) {        // open file.
        mode = NULL;
        acorn_mode = a;
        a = 0;
        for (channel = 0; vdfs_chan[channel].ent; channel++)
            if (channel >= NUM_CHANNELS)
                return;
        simple_name(path, sizeof path, (y << 8) | x);
        ent = find_entry(path, &key, cur_dir);
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
            if (ent && (ent->attribs & ATTR_EXISTS) && (ent->attribs & (ATTR_OPEN_READ|ATTR_OPEN_WRITE)))
                adfs_error(err_isopen);
            else {
                mode = "wb";
                attribs = ATTR_OPEN_WRITE;
                if (!ent)
                    ent = add_new_file(cur_dir, key.acorn_fn);
            }
        } else if (acorn_mode == 0xc0) {
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
                ent = add_new_file(cur_dir, key.acorn_fn);
                mode = "wb+";
            }
        }
        if (mode && ent) {
            log_debug("vdfs: osfind open host file %s in mode %s", ent->host_path, mode);
            if ((fp = fopen(ent->host_path, mode))) {
                mark_extant(ent);
                ent->attribs |= attribs; // file now exists.
                vdfs_chan[channel].fp = fp;
                vdfs_chan[channel].ent = ent;
                a = MIN_CHANNEL + channel;
            } else
                log_warn("vdfs: osfind: unable to open file '%s' in mode '%s': %s\n", ent->host_fn, mode, strerror(errno));
        }
    }
    log_debug("vdfs: osfind returns a=%d", a);
}

static void osgbpb_write(uint32_t pb)
{
    FILE *fp;
    uint32_t mem_ptr, n;

    if ((fp = getfp_write(readmem(pb)))) {
        if (a == 0x01)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        mem_ptr = readmem32(pb+1);
        n = readmem32(pb+5);
        if (mem_ptr > 0xffff0000 || curtube == -1) {
            // IO processor
            while (n--)
                putc(readmem(mem_ptr++), fp);
        } else {
            while (n--)
                putc(tube_readmem(mem_ptr++), fp);
        }
        writemem32(pb+1, mem_ptr);
        writemem32(pb+5, 0);
        writemem32(pb+9, ftell(fp));
    }
}

static int osgbpb_read(uint32_t pb)
{
    FILE *fp;
    uint32_t mem_ptr, n;
    int status = 0, ch;

    if ((fp = getfp_read(readmem(pb)))) {
        if (a == 0x03)
            fseek(fp, readmem32(pb+9), SEEK_SET);
        mem_ptr = readmem32(pb+1);
        n = readmem32(pb+5);
        if (mem_ptr > 0xffff0000 || curtube == -1) {
            // IO processor
            while (n--) {
                if ((ch = getc(fp)) == EOF) {
                    status = 1;
                    break;
                }
                writemem(mem_ptr++, ch);
            }
        } else {
            while (n--) {
                if ((ch = getc(fp)) == EOF) {
                    status = 1;
                    break;
                }
                tube_writemem(mem_ptr++, ch);
            }
        }
        writemem32(pb+1, mem_ptr);
        writemem32(pb+5, n+1);
        writemem32(pb+9, ftell(fp));
    }
    return status;
}

static void osgbpb_get_title(uint32_t pb)
{
    uint32_t mem_ptr;
    char *ptr;
    int ch;

    if (check_valid_dir(cur_dir, "current")) {
        mem_ptr = pb;
        writemem(mem_ptr++, strlen(cur_dir->acorn_fn));
        for (ptr = cur_dir->acorn_fn; (ch = *ptr++); )
            writemem(mem_ptr++, ch);
        writemem(mem_ptr++, 0); // no start-up option.
        writemem(mem_ptr, 0);   // drive is always 0.
    }
}

static void osgbpb_get_dir(uint32_t pb, vdfs_ent_t *dir, const char *which)
{
    uint32_t mem_ptr;
    char *ptr;
    int ch;

    if (check_valid_dir(dir, which)) {
        mem_ptr = pb;
        writemem(mem_ptr++, 1);   // length of drive number.
        writemem(mem_ptr++, '0'); // drive number.
        writemem(mem_ptr++, strlen(dir->acorn_fn));
        for (ptr = dir->acorn_fn; (ch = *ptr++); )
            writemem(mem_ptr++, ch);
    }
}

static int osgbpb_list(uint32_t pb)
{
    uint32_t seq_ptr, mem_ptr, n;
    vdfs_ent_t *cat_ptr;
    int status, ch;
    char *ptr;

    if (check_valid_dir(cur_dir, "current")) {
        seq_ptr = readmem32(pb+9);
        if (seq_ptr == 0) {
            if ((status = gen_cat_tab(cur_dir))) {
                adfs_error(err_notfound);
                return status;
            }
        }
        if (seq_ptr < cur_dir->cat_size) {
            status = 0;
            mem_ptr = readmem32(pb+1);
            n = readmem32(pb+5);
            log_debug("vdfs: seq_ptr=%d, writing max %d entries starting %04X\n", seq_ptr, n, mem_ptr);
            do {
                cat_ptr = cur_dir->cat_tab[seq_ptr++];
                log_debug("vdfs: writing acorn name %s\n", cat_ptr->acorn_fn);
                writemem(mem_ptr++, strlen(cat_ptr->acorn_fn));
                for (ptr = cat_ptr->acorn_fn; (ch = *ptr++); )
                    writemem(mem_ptr++, ch);
                if (seq_ptr >= cur_dir->cat_size) {
                    status = 1;
                    break;
                }
            } while (--n);
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

    log_debug("vdfs: osgbpb(A=%02X, X=%02X, Y=%02X)", a, x, y);

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
            osgbpb_get_dir(pb, cur_dir, "current");
            break;

        case 0x07: // get library dir.
            osgbpb_get_dir(pb, lib_dir, "library");
            break;

        case 0x08: // list files in current directory.
            status = osgbpb_list(pb);
            break;

        default:
            log_debug("vdfs: osgbpb unimplemented for a=%d", a);
            log_debug("vdfs: osgbpb pb.channel=%d, data=%04X num=%04X, ptr=%04X\n", readmem(pb), readmem32(pb+1), readmem32(pb+6), readmem32(pb+9));
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
    vdfs_ent_t *ent;

    ent = cur_dir;
    cur_dir = prev_dir;
    prev_dir = ent;
}

static void cmd_cdir(uint16_t addr)
{
    char path[MAX_ACORN_PATH];
    vdfs_ent_t key;

    if (check_valid_dir(cur_dir, "current")) {
        addr = parse_name(path, sizeof path, addr);
        osfile_cdir(find_entry(path, &key, cur_dir), &key);
    }
}

static void cmd_delete(uint16_t addr)
{
    char path[MAX_ACORN_PATH];
    vdfs_ent_t *ent, key;

    if (check_valid_dir(cur_dir, "current")) {
        addr = parse_name(path, sizeof path, addr);
        if ((ent = find_entry(path, &key, cur_dir))) {
            delete_file(ent);
            return;
        }
    }
    adfs_error(err_notfound);
}

static vdfs_ent_t *lookup_dir(uint16_t addr)
{
    vdfs_ent_t *ent, key;
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(cur_dir, "current")) {
        parse_name(path, sizeof path, addr);
        ent = find_entry(path, &key, cur_dir);
        if (ent && ent->attribs & ATTR_EXISTS) {
            if (ent->attribs & ATTR_IS_DIR)
                return ent;
            else
                adfs_error(err_baddir);
        } else
            adfs_error(err_notfound);
    }
    return NULL;
}

static void cmd_dir(uint16_t addr)
{
    vdfs_ent_t *ent;

    if ((ent = lookup_dir(addr))) {
        prev_dir = cur_dir;
        cur_dir = ent;
    }
}

static void cmd_lib(uint16_t addr)
{
    vdfs_ent_t *ent;

    if ((ent = lookup_dir(addr)))
        lib_dir = ent;
}

static void run_file(const char *err)
{
    uint16_t addr;
    vdfs_ent_t *ent, key;
    FILE *fp;
    int ch;
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(cur_dir, "current")) {
        cmd_tail = parse_name(path, sizeof path, (y << 8) | x);
        ent = find_entry(path, &key, cur_dir);
        if (!(ent && ent->attribs & ATTR_EXISTS))
            ent = find_entry(path, &key, lib_dir);
        if (ent && ent->attribs & ATTR_EXISTS) {
            if (ent->attribs & ATTR_IS_DIR)
                adfs_error(err_wont);
            else if ((fp = fopen(ent->host_path, "rb"))) {
                addr = ent->load_addr;
                if (addr > 0xffff0000 || curtube == -1) {
                    log_debug("vdfs: run_file: writing to I/O proc memory at %08X", addr);
                    while ((ch = getc(fp)) != EOF)
                        writemem(addr++, ch);
                    pc = ent->exec_addr;
                } else {
                    log_debug("vdfs: run_file: writing to tube proc memory at %08X", addr);
                    writemem32(0xc0, ent->exec_addr); // set up for tube execution.
                    while ((ch = getc(fp)) != EOF)
                        tube_writemem(addr++, ch);
                    p.c = 1; // carry set means execute in tube to VDFS ROM.
                    log_debug("vdfs: run_file: write complete");
                }
                fclose(fp);
            } else {
                log_warn("vdfs: unable to run file '%s': %s\n", ent->host_fn, strerror(errno));
                adfs_hosterr(errno);
            }
        } else
            adfs_error(err);
    }
}

static void rename_tail(vdfs_ent_t *old_ent, vdfs_ent_t *new_ent)
{
    if (rename(old_ent->host_path, new_ent->host_path) == 0) {
        log_debug("vdfs: '%s' renamed to '%s'", old_ent->host_path, new_ent->host_path);
        mark_extant(new_ent);
        new_ent->load_addr  = old_ent->load_addr;
        new_ent->exec_addr  = old_ent->exec_addr;
        new_ent->length     = old_ent->length;
        new_ent->acorn_tree = old_ent->acorn_tree;
        new_ent->host_tree  = old_ent->host_tree;
        new_ent->cat_tab    = old_ent->cat_tab;
        new_ent->cat_size   = old_ent->cat_size;
        new_ent->scan_seq   = old_ent->scan_seq;
        new_ent->scan_mtime = old_ent->scan_mtime;
        old_ent->acorn_tree = NULL;
        old_ent->host_tree  = NULL;
        old_ent->cat_tab    = NULL;
        old_ent->cat_size   = 0;
        mark_deleted(old_ent);
        delete_inf(old_ent);
        write_back(new_ent);
    } else {
        adfs_hosterr(errno);
        log_debug("vdfs: failed to rename '%s' to '%s': %s", old_ent->host_path, new_ent->host_path, strerror(errno));
    }
}

static void rename_file(uint16_t addr)
{
    vdfs_ent_t *old_ent, old_key, *new_ent, new_key;
    char old_path[MAX_ACORN_PATH], new_path[MAX_ACORN_PATH];

    parse_name(new_path, sizeof new_path, parse_name(old_path, sizeof old_path, addr));
    if (*old_path && *new_path) {
        if ((old_ent = find_entry(old_path, &old_key, cur_dir))) {
            if ((new_ent = find_entry(new_path, &new_key, cur_dir))) {
                if (new_ent->attribs & ATTR_EXISTS) {
                    if (new_ent->attribs & ATTR_IS_DIR) {
                        if ((new_ent = add_new_file(new_ent, old_ent->acorn_fn)))
                            rename_tail(old_ent, new_ent);
                    } else {
                        log_debug("vdfs: new file '%s' for rename already exists", new_key.acorn_fn);
                        adfs_error(err_exists);
                    }
                } else
                    rename_tail(old_ent, new_ent);
            } else if (new_key.parent && (new_ent = add_new_file(new_key.parent, new_key.acorn_fn)))
                rename_tail(old_ent, new_ent);
        } else {
            log_debug("vdfs: old file '%s' for rename not found", old_key.acorn_fn);
            adfs_error(err_notfound);
        }
    } else {
        log_debug("vdfs: rename attempted with an empty filename");
        adfs_error(err_badren);
    }
}

const struct cmdent ctab_filing[] = {
    { "ACcess",  VDFS_ACT_NOP     },
    { "BAck",    VDFS_ACT_BACK    },
    { "BACKUp",  VDFS_ACT_NOP     },
    { "CDir",    VDFS_ACT_CDIR    },
    { "COMpact", VDFS_ACT_NOP     },
    { "COpy",    VDFS_ACT_NOP     },
    { "DELete",  VDFS_ACT_DELETE  },
    { "DEStroy", VDFS_ACT_NOP     },
    { "DIR",     VDFS_ACT_DIR     },
    { "DRive",   VDFS_ACT_NOP     },
    { "ENable",  VDFS_ACT_NOP     },
    { "FORM",    VDFS_ACT_NOP     },
    { "FRee",    VDFS_ACT_NOP     },
    { "LIB",     VDFS_ACT_LIB     },
    { "MAP",     VDFS_ACT_NOP     },
    { "MOunt",   VDFS_ACT_NOP     },
    { "REname",  VDFS_ACT_RENAME  },
    { "REScan",  VDFS_ACT_RESCAN  },
    { "TItle",   VDFS_ACT_NOP     },
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
    } while (ch != ' ' && ch != '\r' && ch != '.');

    return addr;
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
        if ((!tab_ch && (cmd_ch == ' ' || cmd_ch == '\r')) || (tab_ch >= 'a' && cmd_ch == '.'))
            return tab_ptr;
        tab_ptr++;
    }
    return NULL;
}

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*a))

static void vdfs_do(enum vdfs_action act, uint16_t addr)
{
    log_debug("vdfs: vdfs_do, act=%d, addr=%04X", act, addr);
    switch(act)
    {
    case VDFS_ROM_DUMP:
    case VDFS_ROM_LIST:
    case VDFS_ROM_PRINT:
    case VDFS_ROM_TYPE:
        x = addr & 0xff;
        y = addr >> 8;
        rom_dispatch(act);
    case VDFS_ACT_NOP:
        break;
    case VDFS_ACT_QUIT:
        main_setquit();
        break;
    case VDFS_ACT_SRLOAD:
        cmd_srload(addr);
        break;
    case VDFS_ACT_SRSAVE:
        cmd_srsave(addr);
        break;
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
    case VDFS_ACT_LIB:
        cmd_lib(addr);
        break;
    case VDFS_ACT_RENAME:
        rename_file(addr);
        break;
    case VDFS_ACT_RESCAN:
        scan_seq++;
        break;
    default:
        rom_dispatch(act);
    }
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
            vdfs_do(ent->act, addr);
            return;
        }
    }
    run_file(err_badcmd);
}

static unsigned cat_seq = 0;

static bool cat_prep(void)
{
    vdfs_ent_t *ent, key;
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(cur_dir, "current")) {
        parse_name(path, sizeof path, x + (y << 8));
        if (*path) {
            ent = find_entry(path, &key, cur_dir);
            if (ent && ent->attribs & ATTR_EXISTS) {
                if (ent->attribs & ATTR_IS_DIR) {
                    cat_dir = ent;
                } else {
                    adfs_error(err_baddir);
                    return false;
                }
            } else {
                adfs_error(err_notfound);
                return false;
            }
        } else
            cat_dir = cur_dir;
        if (gen_cat_tab(cat_dir)) {
            adfs_error(err_notfound);
            return false;
        }
        cat_seq = 0;
        return true;
    }
    return false;
}

static void ent2guest(vdfs_ent_t *ent)
{
    uint16_t mem_ptr = 0x100;
    const char *ptr = ent->acorn_fn;
    int i, ch;

    for (i = MAX_FILE_NAME; i > 0 && (ch = *ptr++); i--)
        writemem(mem_ptr++, ch);
    while (i-- > 0)
        writemem(mem_ptr++, ' ');
    writemem16(mem_ptr, ent->attribs);
    writemem32(mem_ptr+2, ent->load_addr);
    writemem32(mem_ptr+6, ent->exec_addr);
    writemem32(mem_ptr+10, ent->length);
}

static void cat_next(void)
{
    if (cat_dir && cat_seq < cat_dir->cat_size) {
        ent2guest(cat_dir->cat_tab[cat_seq++]);
        p.c = 0;
    }
    else
        p.c = 1;
}

static void osfsc_info(void)
{
    vdfs_ent_t *ent, key;
    char path[MAX_ACORN_PATH];

    if (check_valid_dir(cur_dir, "current")) {
        simple_name(path, sizeof path, (y << 8) | x);
        if ((ent = find_entry(path, &key, cur_dir)) && ent->attribs & ATTR_EXISTS) {
            ent2guest(ent);
            rom_dispatch(VDFS_ROM_INFO);
            return;
        }
        adfs_error(err_notfound);
    }
}

static void osfsc(void)
{
    FILE *fp;

    log_debug("vdfs: osfsc(A=%02X, X=%02X, Y=%02X)", a, x, y);

    p.c = 0;
    switch(a) {
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
            if (cat_prep())
                rom_dispatch(VDFS_ROM_CAT);
            break;
        case 0x06: // new filesystem taking over.
            break;
        case 0x09:
            if (cat_prep())
                rom_dispatch(VDFS_ROM_EX);
            break;
        case 0x0a:
            osfsc_info();
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
    uint8_t sects = readmem(pb+9);
    uint16_t ssize;
    switch((sects & 0xe0) >> 5) {
        case 0:  ssize = 128; break;
        case 2:  ssize = 512; break;
        default: ssize = 256; break;
    }
    FILE *fp = sdf_owseek(drive & 1, readmem(pb+8), readmem(pb+7), drive >> 1, ssize);
    if (fp) {
        uint32_t addr = readmem32(pb+1);
        uint8_t cmd   = readmem(pb+6);
        size_t bytes = (sects & 0x0f) << 8;
        if (cmd == 0x53) {
            if (addr > 0xffff0000 || curtube == -1) {
                int ch;
                log_debug("vdfs: osword: writing to I/O proc memory at %08X", addr);
                while (bytes-- && (ch = getc(fp)) != EOF)
                    writemem(addr++, ch);
            }
            else {
                int ch;
                log_debug("vdfs: osword: writing to tube memory at %08X", addr);
                while (bytes-- && (ch = getc(fp)) != EOF)
                    tube_writemem(addr++, ch);
            }
            p.z = 1;
        }
        else if (cmd == 0x4b) {
            if (addr > 0xffff0000 || curtube == -1) {
                log_debug("vdfs: osword: reading from I/O proc memory at %08X", addr);
                while (bytes--)
                    putc(readmem(addr++), fp);
            }
            else {
                log_debug("vdfs: osword: reading from tube memory at %08X", addr);
                while (bytes--)
                    putc(tube_readmem(addr++), fp);
            }
            p.z = 1;
        }
        writemem(pb+10, 0);
    }
    else {
        log_debug("vdfs: osword attempting to read invalid/empty drive %d", drive);
        writemem(pb+10, 0x14); // track 0 not found.
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

 static void check_ram(void) {
    p.c = 0;
    if (y >= 0 && y <= 15)
        if (rom_slots[y].swram)
            p.c = 1;
}

const struct cmdent ctab_always[] = {
    { "QUIT",    VDFS_ACT_QUIT    },
    { "Dump",    VDFS_ROM_DUMP    },
    { "List",    VDFS_ROM_LIST    },
    { "Print",   VDFS_ROM_PRINT   },
    { "Type",    VDFS_ROM_TYPE    },
    { "Roms",    VDFS_ROM_ROMS    },
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
        if ((ent = lookup_cmd(ctab_always, ARRAY_SIZE(ctab_always), cmd))) {
            vdfs_do(ent->act, addr);
            a = 0;
        }
    }
}

static void service(void)
{
    log_debug("vdfs: rom service a=%02X, x=%02X, y=%02X", a, x, y);
    switch(a)
    {
    case 0x03: // filing system boot.
        if (vdfs_enabled && (!key_any_down() || key_code_down(ALLEGRO_KEY_S)))
            rom_dispatch(VDFS_ROM_FSBOOT);
        break;
    case 0x04: // unrecognised command.
        serv_cmd();
        break;
    case 0x08: // OSWORD
        osword();
        break;
    case 0x09: // *HELP
        break;
    case 0x12: // Select filing system.
        if (y == FSNO_VDFS || ((fs_flags & CLAIM_ADFS) && y == FSNO_ADFS) || ((fs_flags & CLAIM_DFS) && y == FSNO_DFS)) {
            fs_num = y;
            rom_dispatch(VDFS_ROM_FSSTART);
        }
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
        case 0x08: cat_next();  break;
        case 0x09: check_ram(); break;
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
