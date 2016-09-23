/*
 * VDFS for B-EM
 * Steve Fosdick 2016
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

#define _GNU_SOURCE

#include "b-em.h"
#include "6502.h"
#include "mem.h"
#include "model.h"
#include "tube.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>

#include <alloca.h>
#include <search.h>
#include <sys/stat.h>

int vdfs_enabled = 1;

/*
 * The definition of the VDFS entry that follows is the key data
 * structure for this module and models the association between
 * a file/directory as seen by the BBC and as it exists on the host.
 *
 * In the event this is a directory rather than a file this entry
 * will contain a pointer to the parent VDFS entry and also two
 * binary search trees allowing the contents of the directory to be
 * searched by either Acorn filename or host filename.
 */

#define MAX_FILE_NAME    10

#define ATTR_USER_READ   0x0001
#define ATTR_USER_WRITE  0x0002
#define ATTR_USER_EXEC   0x0004
#define ATTR_USER_LOCKD  0x0008
#define ATTR_OTHR_READ   0x0010
#define ATTR_OTHR_WRITE  0x0020
#define ATTR_OTHR_EXEC   0x0040
#define ATTR_OTHR_LOCKD  0x0080
#define ATTR_IS_DIR      0x4000
#define ATTR_DELETED     0x8000

typedef struct _vdfs_entry vdfs_ent_t;

struct _vdfs_entry {
    char       *host_fn;
    char       acorn_fn[MAX_FILE_NAME+1];
    uint16_t   attribs;
    unsigned   load_addr;
    unsigned   exec_addr;
    unsigned   length;
    time_t     inf_read;
    // the following only apply to a directory.
    void       *acorn_tree;
    void       *host_tree;
    vdfs_ent_t **cat_tab;
    size_t     cat_size;
    char       *host_path;
    unsigned   scan_seq;
    vdfs_ent_t *parent;
};

static vdfs_ent_t **cat_ptr;    // used by the tree walk callback.

static vdfs_ent_t root_dir;     // root as seen by BBC, not host.
static vdfs_ent_t *cur_dir;
static vdfs_ent_t *lib_dir;
static vdfs_ent_t *prev_dir;
static unsigned   scan_seq;

/*
 * Open files.  An open file is an association between a host OS file
 * pointer, i.e. the host file is kept open too, and a catalogue
 * entry.  Normally for an open file both pointers are set and for a
 * closed file both are NULL but to emulate the OSFIND * call correctly
 * a directory can be opened and becomes "half-open", i.e. it can be
 * closed again but any attempt to read or write it will fail.
 */

#define MIN_CHANNEL      32
#define MAX_CHANNEL      64
#define NUM_CHANNELS     (MAX_CHANNEL-MIN_CHANNEL)

typedef struct {
    FILE       *fp;
    vdfs_ent_t *ent;
} vdfs_file_t;

static vdfs_file_t vdfs_chan[NUM_CHANNELS];

static uint8_t  reg_a;
static uint8_t  claim_fs = 0xff;
static uint8_t  fs_flag  = 0x11;
static uint16_t cmd_tail;

static uint16_t readmem16(uint16_t addr) {
    uint32_t value;

    value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    return value;
}

static uint32_t readmem32(uint16_t addr) {
    uint32_t value;

    value = readmem(addr);
    value |= (readmem(addr+1) << 8);
    value |= (readmem(addr+2) << 16);
    value |= (readmem(addr+3) << 24);
    return value;
}

static void writemem16(uint16_t addr, uint16_t value) {
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
}

static void writemem32(uint16_t addr, uint32_t value) {
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
    writemem(addr+2, (value >> 16) & 0xff);
    writemem(addr+3, (value >> 24) & 0xff);
}

static void flush_all() {
    int channel;
    FILE *fp;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        if ((fp = vdfs_chan[channel].fp))
            fflush(fp);
}

static void free_noop(void *ptr) { }

static void free_tree_node(void *ptr) {
    vdfs_ent_t *ent = (vdfs_ent_t *)ptr;
    char *host_fn;

    if ((ptr = ent->acorn_tree))
        tdestroy(ptr, free_noop);
    if ((ptr = ent->host_tree))
        tdestroy(ptr, free_tree_node);
    if ((ptr = ent->cat_tab))
        free(ptr);
    if ((ptr = ent->host_path))
        free(ptr);
    if ((host_fn = ent->host_fn) && host_fn != ent->acorn_fn)
        free(host_fn);
    free(ent);
}

void vdfs_reset() {
        flush_all();
}

static int host_comp(const void *a, const void *b) {
    vdfs_ent_t *ca = (vdfs_ent_t *)a;
    vdfs_ent_t *cb = (vdfs_ent_t *)b;
    return strcmp(ca->host_fn, cb->host_fn);
}

static int acorn_comp(const void *a, const void *b) {
    vdfs_ent_t *ca = (vdfs_ent_t *)a;
    vdfs_ent_t *cb = (vdfs_ent_t *)b;
    return strcasecmp(ca->acorn_fn, cb->acorn_fn);
}

static void get_filename(FILE *fp, char *dest) {
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

static unsigned get_hex(FILE *fp) {
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

static inline void hst2bbc(const char *host_fn, char *acorn_fn) {
    int ch;
    const char *ptr;
    char *end = acorn_fn + MAX_FILE_NAME;

    while ((ch = *host_fn++) && acorn_fn < end) {
        if ((ptr = strchr(hst_chars, ch)))
            ch = bbc_chars[ptr-hst_chars];
        *acorn_fn++ = ch;
    }
}

static inline void bbc2hst(const char *acorn_fn, char *host_fn) {
    int ch;
    const char *ptr;

    while ((ch = *acorn_fn++)) {
        if ((ptr = strchr(bbc_chars, ch)))
            ch = hst_chars[ptr-bbc_chars];
        *host_fn++ = ch;
    }
}

// Populate a VDFS entry from host information.

static void scan_entry(vdfs_ent_t *ent) {
    char *host_dir_path, *host_file_path, *ptr;
    FILE *fp;
    struct stat stb;

    // build name of .inf file
    host_dir_path = ent->parent->host_path;
    ptr = host_file_path = alloca(strlen(host_dir_path) + strlen(ent->host_fn) + 6);
    if (host_dir_path[0] != '.' || host_dir_path[1] != '\0') {
        ptr = stpcpy(ptr, host_dir_path);
        *ptr++ = '/';
    }
    ptr = stpcpy(ptr, ent->host_fn);
    strcpy(ptr, ".inf");

    // open and parse .inf file
    if ((fp = fopen(host_file_path, "rt"))) {
        get_filename(fp, ent->acorn_fn);
        ent->load_addr = get_hex(fp);
        ent->exec_addr = get_hex(fp);
        fclose(fp);
    } else if (ent->acorn_fn[0] == '\0')
        hst2bbc(ent->host_fn, ent->acorn_fn);

    // trim .inf to get back to host path and get attributes
    *ptr = '\0';
    if (stat(host_file_path, &stb) == -1)
        bem_warnf("unable to stat '%s': %s\n", host_file_path, strerror(errno));
    else {
        ent->length = stb.st_size;
        if (S_ISDIR(stb.st_mode))
            ent->attribs |= ATTR_IS_DIR;
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
    }
    bem_debugf("vdfs: scan_entry: acorn=%s, host=%s, attr=%04X, load=%08X, exec=%08X\n", ent->acorn_fn, ent->host_fn, ent->attribs, ent->load_addr, ent->exec_addr);
}

// Create VDFS entry for a new file.

static vdfs_ent_t *new_entry(vdfs_ent_t *dir, const char *host_fn) {
    vdfs_ent_t *ent, **ptr;
    int name_len, dir_len, seq_ch = '0';
    char *host_path;

    if ((ent = malloc(sizeof(vdfs_ent_t)))) {
        memset(ent, 0, sizeof(vdfs_ent_t));
        if ((ent->host_fn = strdup(host_fn))) {
            ent->parent = dir;
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
                bem_debugf("vdfs: new_entry: unique name %s used\n", ent->acorn_fn);
            }
            tsearch(ent, &dir->host_tree, host_comp);
            if (ent->attribs & ATTR_IS_DIR) {
                dir_len = strlen(dir->host_path);
                if (!(host_path = malloc(dir_len + strlen(ent->host_fn) + 2)))
                    return NULL;
                strcpy(host_path, dir->host_path);
                host_path[dir_len] = '/';
                strcpy(host_path + dir_len + 1, ent->host_fn);
                ent->host_path = host_path;
            }
            bem_debugf("vdfs: new_entry: returing new entry %p\n", ent);
            return ent;
        }
        free(ent);
    }
    return NULL;
}

static void tree_visit(const void *nodep, const VISIT which, const int depth) {
    if (which == postorder || which == leaf)
        *cat_ptr++ = *(vdfs_ent_t **)nodep;
}

// Given a VDFS entty reporesting a dir scan the corresponding host dir.

static int scan_dir(vdfs_ent_t *dir) {
    int  count = 0;
    DIR  *dp;
    struct dirent *dep;
    vdfs_ent_t **ptr, **end, *ent, key;

    if (dir->acorn_tree && dir->scan_seq >= scan_seq)
        return 0; // scanned before.

    if ((dp = opendir(dir->host_path))) {
        // Mark all previosly seen entries deleted but leave them
        // in the tree.
        ptr = dir->cat_tab;
        end = ptr + dir->cat_size;
        while (ptr < end) {
            ent = *ptr++;
            ent->attribs |= ATTR_DELETED;
        }
        count = dir->cat_size;

        // Go through the entries in the host dir which are not
        // in sorted order, find each in the tree and remove the
        // deleted atrubute, if found or create a new entry if not.
        while ((dep = readdir(dp))) {
            if (*(dep->d_name) != '.') {
                key.host_fn = dep->d_name;
                if ((ptr = tfind(&key, &dir->host_tree, host_comp))) {
                    ent = *ptr;
                    ent->attribs &= ~ATTR_DELETED;
                    scan_entry(ent);
                } else if ((ent = new_entry(dir, dep->d_name)))
                    count++;
                else {
                    count = -1;
                    break;
                }
            }
        }
        closedir(dp);
        bem_debugf("vdfs: scan_dir count=%d\n", count);

        // create an array sorted in Acorn order for *CAT.
        if (count >= 0) {
            if (dir->cat_tab) {
                free(dir->cat_tab);
                dir->cat_tab = NULL;
            }
            if (count == 0) {
                dir->scan_seq = scan_seq;
                return 0;
            }
            if ((dir->cat_tab = malloc(sizeof(vdfs_ent_t *)*count))) {
                cat_ptr = dir->cat_tab;
                twalk(dir->acorn_tree, tree_visit);
                dir->cat_size = count;
                dir->scan_seq = scan_seq;
                return 0;
            }
        }
    } else
        bem_warnf("unable to opendir '%s': %s\n", dir->host_path, strerror(errno));
    return 1;
}

// Given the address in BBC RAM of a filename find the VDFS entry.

static vdfs_ent_t *find_file(uint16_t fn_addr, vdfs_ent_t *key, vdfs_ent_t *ent, uint16_t *tail_addr) {
    int i, ch;
    char *fn_ptr;
    vdfs_ent_t **ptr;

    if (scan_dir(ent) == 0) {
        memset(key, 0, sizeof(vdfs_ent_t));
        bem_debugf("vdfs: find_file: fn_addr=%04x\n", fn_addr);
        for (;;) {
            fn_ptr = key->acorn_fn;
            for (i = 0; i < MAX_FILE_NAME; i++) {
                ch = readmem(fn_addr++);
                if (ch == '\r' || ch == '.' || ch == ' ')
                    break;
                *fn_ptr++ = ch;
            }
            *fn_ptr = '\0';
            if (tail_addr)
                *tail_addr = fn_addr;
            if (key->acorn_fn[0] == '$' && key->acorn_fn[1] == '\0')
                ent = &root_dir;
            else if (key->acorn_fn[0] == '^' && key->acorn_fn[1] == '\0')
                ent = ent->parent;
            else if ((ptr = tfind(key, &ent->acorn_tree, acorn_comp)))
                ent = *ptr;
            else
                return NULL; // not found
            if (ch != '.')
                return ent;
            if (!(ent->attribs & ATTR_IS_DIR))
                return NULL; // file in pathname where dir should be
            scan_dir(ent);
        }
    }
    return NULL;
}

static vdfs_ent_t *add_new_file(vdfs_ent_t *dir, const char *name) {
    int new_size;
    vdfs_ent_t **new_tab, *new_ent;

    new_size = dir->cat_size + 1;
    if ((new_tab = realloc(dir->cat_tab, new_size * (sizeof(vdfs_ent_t *))))) {
        dir->cat_tab = new_tab;
        if ((new_ent = malloc(sizeof(vdfs_ent_t)))) {
            memset(new_ent, 0, sizeof(vdfs_ent_t));
            strncpy(new_ent->acorn_fn, name, MAX_FILE_NAME);
            if ((new_ent->host_fn = malloc(strlen(new_ent->acorn_fn)+1)) == NULL) {
                free(new_ent);
                return NULL;
            }
            bbc2hst(new_ent->acorn_fn, new_ent->host_fn);
            new_ent->parent = dir;
            tsearch(new_ent, &dir->acorn_tree, acorn_comp);
            tsearch(new_ent, &dir->host_tree, host_comp);
            cat_ptr = dir->cat_tab;
            twalk(dir->acorn_tree, tree_visit);
            dir->cat_size = new_size;
        }
        return new_ent;
    }
    return NULL;
}

// Write changed attributes back to the .inf file.

static void write_back(vdfs_ent_t *ent) {
    char *host_dir_path, *host_file_path, *ptr;
    FILE *fp;

    host_dir_path = ent->parent->host_path;
    ptr = host_file_path = alloca(strlen(host_dir_path) + strlen(ent->host_fn) + 6);
    if (host_dir_path[0] != '.' || host_dir_path[1] != '\0') {
        ptr = stpcpy(ptr, host_dir_path);
        *ptr++ = '/';
    }
    ptr = stpcpy(ptr, ent->host_fn);
    strcpy(ptr, ".inf");

    if ((fp = fopen(host_file_path, "wt"))) {
        fprintf(fp, "%-*s %08X %08X %08X\n", MAX_FILE_NAME, ent->acorn_fn, ent->load_addr, ent->exec_addr, ent->length);
        fclose(fp);
    } else
        bem_warnf("vdfs: unable to create INF file '%s' for '%s': %s\n", host_file_path, ent->host_fn, strerror(errno));
}

static FILE *open_file(vdfs_ent_t *ent, const char *mode) {
    char *host_dir_path, *host_file_path, *ptr;

    host_dir_path = ent->parent->host_path;
    ptr = host_file_path = alloca(strlen(host_dir_path) + strlen(ent->host_fn) + 2);
    if (host_dir_path[0] != '.' || host_dir_path[1] != '\0') {
        ptr = stpcpy(ptr, host_dir_path);
        *ptr++ = '/';
    }
    strcpy(ptr, ent->host_fn);
    return fopen(host_file_path, mode);
}

static void close_file(int channel) {
    vdfs_ent_t *ent;
    FILE *fp;

    if ((ent = vdfs_chan[channel].ent)) {
        if ((fp = vdfs_chan[channel].fp)) {
            fclose(fp);
            vdfs_chan[channel].fp = NULL;
        }
        write_back(ent);
        vdfs_chan[channel].ent = NULL;
    }
}

static void close_all() {
    int channel;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        close_file(channel);
}

void vdfs_close(void) {
    void *ptr;

    close_all();
    tdestroy(root_dir.acorn_tree, free_noop);
    root_dir.acorn_tree = NULL;
    tdestroy(root_dir.host_tree, free_tree_node);
    root_dir.host_tree = NULL;
    if ((ptr = root_dir.cat_tab)) {
        free(ptr);
        root_dir.cat_tab = NULL;
        root_dir.cat_size = 0;
    }
    if ((ptr = root_dir.host_path)) {
        free(ptr);
        root_dir.host_path = NULL;
    }
}

void vdfs_new_root(const char *root) {
    root_dir.host_fn = root_dir.host_path = strdup(root);
    scan_entry(&root_dir);
    cur_dir = lib_dir = prev_dir = &root_dir;
    scan_seq++;
}

void vdfs_set_root(const char *root) {
    if (root_dir.host_path == NULL || strcmp(root_dir.host_path, root)) {
        vdfs_close();
        vdfs_new_root(root);
    }
}

const char *vdfs_get_root() {
    return root_dir.host_path;
}

// Initialise the VDFS module.

void vdfs_init(void) {
    char *root;

    root_dir.acorn_fn[0] = '$';
    root_dir.parent = &root_dir;
    scan_seq = 0;
    if ((root = getenv("BEM_VDFS_ROOT")) == NULL)
        root = ".";
    vdfs_new_root(root);
}

// ADFS Error messages (used)

static const char err_wont[]     = "\x93" "Won't";
static const char err_badparms[] = "\x94" "Bad parms";
static const char err_access[]   = "\xbd" "Access violation";
static const char err_nfile[]    = "\xc0" "Too many open files";
static const char err_exists[]   = "\xc4" "Already exists";
static const char err_discerr[]  = "\xc7" "Disc error";
static const char err_notfound[] = "\xd6" "Not found";
static const char err_channel[]  = "\xde" "Channel";
static const char err_badcmd[]   = "\xfe" "Bad command";
static const char err_badaddr[]  = "\x94" "Bad parms";
static const char err_no_swr[]   = "\x93" "No SWRAM at that address";
static const char err_too_big[]  = "\x94" "Too big";

/* Other ADFS messages not used.

static const char err_aborted[]  = "\x92" "Aborted";
static const char err_delcsd[]   = "\x97" "Can't delete CSD";
static const char err_dellib[]   = "\x98" "Can't delete library";
static const char err_badcsum[]  = "\xaa" "Bad checksum";
static const char err_badren[]   = "\xb0" "Bad rename";
static const char err_notempty[] = "\xb4" "Dir not empty";
static const char err_outside[]  = "\xb7" "Outside file";
static const char err_nupdate[]  = "\xc1" "Not open for update";
static const char err_isopen[]   = "\xc2" "Already open";
static const char err_locked[]   = "\xc3" "Locked";
static const char err_full[]     = "\xc6" "Disc full";
static const char err_datalost[] = "\xca" "Data lost, channel";
static const char err_badopt[]   = "\xcb" "Bad opt";
static const char err_badname[]  = "\xcc" "Bad name";
static const char err_eof[]      = "\xdf" "EOF";
static const char err_wildcard[] = "\xfd" "Wild cards";

*/

static void adfs_error(const char *err) {
    uint16_t addr = 0x100;
    int ch;

    writemem(addr++, 0);  // BRK instruction.
    do {
        ch = *err++;
        writemem(addr++, ch);
    } while (ch);
    pc = 0x100;          // jump to BRK sequence just created.
}

static void adfs_hosterr(int errnum) {
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
        case ENOENT:
            msg = err_notfound;
            break;
        default:
            msg = err_discerr;
    }
    adfs_error(msg);
}

static FILE *getfp(int channel) {
    FILE *fp;

    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL) {
        if ((fp = vdfs_chan[channel-MIN_CHANNEL].fp))
            return fp;
        else
            bem_debugf("vdfs: attempt to use closed channel %d", channel);
    } else
        bem_debugf("vdfs: channel %d out of range\n", channel);
    adfs_error(err_channel);
    return NULL;
}

static void run_file(const char *err) {
    uint16_t addr;
    vdfs_ent_t *ent, key;
    FILE *fp;
    int ch;

    addr = (y << 8) | x;
    if (!(ent = find_file(addr, &key, cur_dir, &cmd_tail)))
        ent = find_file(addr, &key, lib_dir, &cmd_tail);
    if (ent) {
        if (ent->attribs & ATTR_IS_DIR)
            adfs_error(err_wont);
        else if ((fp = open_file(ent, "rb"))) {
            addr = ent->load_addr;
            while ((ch = getc(fp)) != EOF)
                writemem(addr++, ch);
            fclose(fp);
            pc = ent->exec_addr;
        } else {
            bem_warnf("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
            adfs_hosterr(errno);
        }
    } else
        adfs_error(err);
}

static inline void osfsc() {
    FILE *fp;

    switch(a) {
        case 0x01: // check EOF
            if ((fp = getfp(x)))
                x = feof(fp) ? 0xff : 0x00;
            break;
        case 0x02: // */ command
        case 0x04: // *RUN command
            run_file(err_notfound);
            break;
        case 0x03: // unrecognised OS command
            run_file(err_badcmd);
            break;
        case 0x06: // new filesystem taking over.
            break;
        default:
            bem_debugf("vdfs: osfsc unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
    }
}

static inline void osfind() {
    int acorn_mode, channel, chan2;
    vdfs_ent_t *ent, key;
    const char *mode;
    FILE *fp;

    if (a == 0) {   // close file.
        channel = y;
        if (channel == 0)
            close_all();
        else if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL)
            close_file(channel-MIN_CHANNEL);
    } else {        // open file.
        mode = NULL;
        acorn_mode = a;
        a = 0;
        for (channel = 0; vdfs_chan[channel].ent; channel++)
            if (channel >= NUM_CHANNELS)
                return;
        if ((ent = find_file((y << 8) | x, &key, cur_dir, NULL))) {
            for (chan2 = 0; chan2 < NUM_CHANNELS; chan2++)
                if (vdfs_chan[channel].ent == ent)
                    return;
            if (ent->attribs & ATTR_IS_DIR) {
                vdfs_chan[channel].ent = ent;  // make "half-open"
                a = MIN_CHANNEL + channel;
                return;
            }
            if (acorn_mode == 0x40)
                mode = "rb";
            else if (acorn_mode == 0x80)
                mode = "wb";
            else if (acorn_mode == 0xc0)
                mode = "rb+";
            else
                return;
        } else {
            if (acorn_mode == 0x80) {
                ent = add_new_file(cur_dir, key.acorn_fn);
                mode = "wb";
            }
            else if (acorn_mode == 0xc0) {
                ent = add_new_file(cur_dir, key.acorn_fn);
                mode = "wb+";
            }
            else
                return;
        }
        if ((fp = open_file(ent, mode))) {
            vdfs_chan[channel].fp = fp;
            vdfs_chan[channel].ent = ent;
            a = MIN_CHANNEL + channel;
        } else
            bem_warnf("vdfs: osfind: unable to open file '%s' in mode '%s': %s\n", ent->host_fn, mode, strerror(errno));
    }
}

static inline void osgbpb() {
    int      status = 0, ch;
    uint32_t pb = (y << 8) | x;
    uint32_t seq_ptr, mem_ptr, n;
    vdfs_ent_t *cat_ptr;
    char *ptr;
    FILE *fp;

    switch (a)
    {
        case 0x01: // write multiple bytes to file.
        case 0x02:
            if ((fp = getfp(readmem(pb)))) {
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
            break;

        case 0x03: // read multiple bytes from file.
        case 0x04:
            if ((fp = getfp(readmem(pb)))) {
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
            break;

        case 0x05: // get current dir title etc.
            mem_ptr = pb;
            writemem(mem_ptr++, strlen(cur_dir->acorn_fn));
            for (ptr = cur_dir->acorn_fn; (ch = *ptr++); )
                writemem(mem_ptr++, ch);
            writemem(mem_ptr++, 0); // no start-up option.
            writemem(mem_ptr, 0);   // drive is always 0.
            break;

        case 0x06: // get durrent dir
            mem_ptr = pb;
            writemem(mem_ptr++, 1);   // length of drive number.
            writemem(mem_ptr++, '0'); // drive number.
            writemem(mem_ptr++, strlen(cur_dir->acorn_fn));
            for (ptr = cur_dir->acorn_fn; (ch = *ptr++); )
                writemem(mem_ptr++, ch);
            break;

        case 0x07: // get library dir.
            mem_ptr = pb;
            writemem(mem_ptr++, 1);   // length of drive number.
            writemem(mem_ptr++, '0'); // drive number.
            writemem(mem_ptr++, strlen(lib_dir->acorn_fn));
            for (ptr = cur_dir->acorn_fn; (ch = *ptr++); )
                writemem(mem_ptr++, ch);
            break;

        case 0x08: // list files in current directory in Acorn format.
            seq_ptr = readmem32(pb+9);
            if (seq_ptr == 0) {
                if ((status = scan_dir(cur_dir)))
                    break;
            }
            if (seq_ptr < cur_dir->cat_size) {
                mem_ptr = readmem32(pb+1);
                n = readmem32(pb+5);
                bem_debugf("vdfs: seq_ptr=%d, writing max %d entries starting %04X\n", seq_ptr, n, mem_ptr);
                do {
                    cat_ptr = cur_dir->cat_tab[seq_ptr++];
                    bem_debugf("vdfs: writing acorn name %s\n", cat_ptr->acorn_fn);
                    writemem(mem_ptr++, strlen(cat_ptr->acorn_fn));
                    for (ptr = cat_ptr->acorn_fn; (ch = *ptr++); )
                        writemem(mem_ptr++, ch);
                } while (--n > 0 && seq_ptr < cur_dir->cat_size);
                bem_debugf("vdfs: finish at %04X\n", mem_ptr);
                writemem32(pb+5, n);
                writemem32(pb+9, seq_ptr);
            }
            break;

        case 0x09: // list files in current directory in VDFS ROM format.
            n = readmem(pb);
            seq_ptr = readmem32(pb+9);
            if (seq_ptr == 0) {
                if ((status = scan_dir(cur_dir)))
                    break;
            }
            if (seq_ptr < cur_dir->cat_size) {
                mem_ptr = readmem32(pb+1);
                bem_debugf("vdfs: seq_ptr=%d, writing max %d entries starting %04X\n", seq_ptr, n, mem_ptr);
                do {
                    cat_ptr = cur_dir->cat_tab[seq_ptr++];
                    bem_debugf("vdfs: writing acorn name %s\n", cat_ptr->acorn_fn);
                    for (ptr = cat_ptr->acorn_fn; (ch = *ptr++); )
                        writemem(mem_ptr++, ch);
                    writemem(mem_ptr++, '\r');
                } while (--n > 0 && seq_ptr < cur_dir->cat_size);
                bem_debugf("vdfs: finish at %04X\n", mem_ptr);
                writemem32(pb+9, seq_ptr);
            } else {
                status = 1; // no more filenames;
                writemem(pb, 0);// VDFS ROM quirk.
            }
            break;
        default:
            bem_debugf("vdfs: osgbpb unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
            bem_debugf("vdfs: osgbpb pb.channel=%d, data=%04X num=%04X, ptr=%04X\n", readmem(pb), readmem32(pb+1), readmem32(pb+6), readmem32(pb+9));
    }
    p.c = status;
}

static inline void osbput() {
    FILE *fp;

    if ((fp = getfp(y)))
        putc(a, fp);
}

static inline void osbget() {
    int ch;
    FILE *fp;

    p.c = 1;
    if ((fp = getfp(y))) {
        if ((ch = getc(fp)) != EOF) {
            a = ch;
            p.c = 0;
        }
    }
}

static inline void osargs() {
    FILE *fp;
    long temp;

    if (y == 0) {
        switch (a)
        {
            case 0:
                a = 4; // say disc filing selected.
                break;
            case 1:
                writemem32(x, cmd_tail);
                break;
            case 0xff:
                flush_all();
                break;
            default:
                bem_debugf("vdfs: osargs: y=0, a=%d not implemented", a);
        }
    } else if ((fp = getfp(y))) {
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
                bem_debugf("vdfs: osargs: unrecognised function code a=%d for channel y=%d", a, y);
        }
    }
}

static void osfile_save(uint32_t pb, vdfs_ent_t *ent) {
    FILE *fp;
    uint32_t start_addr, end_addr, ptr;

    if (ent) {
        if ((fp = open_file(ent, "wb"))) {
            start_addr = readmem32(pb+0x0a);
            end_addr = readmem32(pb+0x0e);
            if (start_addr > 0xffff0000 || curtube == -1) {
                for (ptr = start_addr; ptr < end_addr; ptr++)
                    putc(readmem(ptr), fp);
            } else {
                for (ptr = start_addr; ptr < end_addr; ptr++)
                    putc(tube_readmem(ptr), fp);
            }
            fclose(fp);
            ent->load_addr = readmem32(pb+0x02);
            ent->exec_addr = readmem32(pb+0x06);
            ent->length = end_addr-start_addr;
            write_back(ent);
        } else
            bem_warnf("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
    } else
        adfs_error(err_wont);
}

static void osfile_load(uint32_t pb, vdfs_ent_t *ent) {
    FILE *fp;
    uint32_t addr;
    int ch;

    if (ent) {
        if (ent->attribs & ATTR_IS_DIR)
            adfs_error(err_wont);
        else if ((fp = open_file(ent, "rb"))) {
            if (readmem(pb+0x06) == 0)
                addr = readmem32(pb+0x02);
            else
                addr = ent->load_addr;
            if (addr > 0xffff0000 || curtube == -1) {
                while ((ch = getc(fp)) != EOF)
                    writemem(addr++, ch);
            } else {
                while ((ch = getc(fp)) != EOF)
                    tube_writemem(addr++, ch);
            }
            fclose(fp);
        } else {
            bem_warnf("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
            adfs_hosterr(errno);
        }
    } else
        adfs_error(err_notfound);
}

static inline void osfile()
{
    vdfs_ent_t *ent, key;
    uint32_t pb = (y << 8) | x;

    ent = find_file(readmem16(pb), &key, cur_dir, NULL);

    switch (a) {
        case 0x00:  // save file.
            if (!ent)
                ent = add_new_file(cur_dir, key.acorn_fn);
            osfile_save(pb, ent);
            break;

        case 0x01:  // set all attributes.
            if (ent) {
                ent->load_addr = readmem32(pb+0x02);
                ent->exec_addr = readmem32(pb+0x06);
                write_back(ent);
            }
            break;

        case 0x02:  // set load address only.
            if (ent) {
                ent->load_addr = readmem32(pb+0x02);
                write_back(ent);
            }
            break;

        case 0x03:  // set exec address only.
            if (ent) {
                ent->exec_addr = readmem32(pb+0x06);
                write_back(ent);
            }
            break;

        case 0x04:  // write attributes.
            break;

        case 0x05:  // get addresses and attributes.
            if (ent) {
                writemem32(pb+0x02, ent->load_addr);
                writemem32(pb+0x06, ent->exec_addr);
                writemem32(pb+0x0a, ent->length);
                writemem32(pb+0x0e, ent->attribs);
            }
            break;

        case 0xff:  // load file.
            osfile_load(pb, ent);
            break;

        default:
            bem_debugf("vdfs: osfile unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
    }
    a = (ent == NULL) ? 0 : (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
}

/*
 * The following routines parse the SRLOAD and SRSAVE commands into
 * an OSWORD parameter block as used on the BBC Master.  Control is
 * returned to the emulated BBC.  If VDFS is the current filing
 * system the VDFS ROM will call back to this VDFS module for
 * execution otherwise it will try to executethe OSWORD for another
 * filing system to pick up.
 */

static uint16_t srp_fn(uint16_t *vptr) {
    uint16_t addr;
    int ch;

    addr = readmem16(0xf2) + y;
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

static uint16_t srp_hex(int ch, uint16_t addr, uint16_t *vptr) {
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
    return 0;
}

static uint16_t srp_start(uint16_t addr, uint16_t *start) {
    int ch;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');
    return srp_hex(ch, addr, start);
}

static uint16_t srp_length(uint16_t addr, uint16_t start, uint16_t *len) {
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

static void srp_tail(uint16_t addr, uint8_t flag, uint16_t fnaddr, uint16_t start, uint16_t len) {
    int ch;
    uint16_t romid = 0;

    do
        ch = readmem(addr++);
    while (ch == ' ' || ch == '\t');

    if (isxdigit(ch)) {
        flag &= ~0x40;
        addr = srp_hex(ch, addr, &romid);
        do
            ch = readmem(addr++);
        while (ch == ' ' || ch == '\t');
    }
    writemem(0x70, flag);
    writemem16(0x71, fnaddr);
    writemem(0x73, romid);
    writemem16(0x74, start);
    writemem16(0x76, len);
    writemem16(0x78, 0);
    if (ch == 'Q' || ch == 'q')
        writemem16(0x7a, 0xffff);
    else
        writemem16(0x7a, 0);
}

static inline void cmd_srload() {
    uint16_t addr, fnadd, start;

    if ((addr = srp_fn(&fnadd))) {
        if ((addr = srp_start(addr, &start))) {
            srp_tail(addr, 0xC0, fnadd, start, 0);
            return;
        }
    }
    adfs_error(err_badparms);
}

static inline void cmd_srsave() {
    uint16_t addr, fnadd, start, len;

    if ((addr = srp_fn(&fnadd))) {
        if ((addr = srp_start(addr, &start))) {
            if ((addr = srp_length(addr, start, &len))) {
                srp_tail(addr, 0x40, fnadd, start, len);
                return;
            }
        }
    }
    adfs_error(err_badparms);
}

/*
 * This is the execution part of the sideways RAM commands which
 * expects an OSWORD parameter block as used on the BBC Master and
 * executes the load/save on the host.
 */

static int16_t swr_calc_addr(uint8_t flags, uint32_t *st_ptr, uint16_t romid) {
    uint16_t start = *st_ptr;
    int banks;

    if (flags & 0x40) {
        // Pseudo addressing.  How many banks into the ram does the
        // address call for?

        banks = start / 16384;
        start %= 16384;

        // Find the nth RAM bank.

        romid = 0;
        for (;;) {
            if (swram[romid])
                if (--banks < 0)
                    break;
            if (++romid >= 16) {
                adfs_error(err_no_swr);
                return -1;
            }
        }
        bem_debugf("vdfs: swr_calc_addr: pseudo addr bank=%02d, start=%04x\n", romid, start);
    } else {
        // Absolutre addressing.

        if (start < 0x8000 || start >= 0xc000) {
            adfs_error(err_badaddr);
            return -1;
        }

        if (romid > 16) {
            adfs_error(err_no_swr);
            return -1;
        }
        bem_debugf("vdfs: exec_sram: abs addr bank=%02d, start=%04x\n", romid, start);
        start -= 0x8000;
    }
    *st_ptr = start;
    return romid;
}

static inline void exec_swr_fs() {
    uint16_t pb = (y << 8) | x;
    uint8_t flags  = readmem(pb);
    uint16_t fname = readmem16(pb+1);
    int8_t   romid = readmem(pb+3);
    uint32_t start = readmem16(pb+4);
    uint16_t pblen = readmem16(pb+6);
    uint32_t load_add;
    int len;
    vdfs_ent_t *ent, key;
    FILE *fp;

    bem_debugf("vdfs: exec_swr_fs: flags=%02x, fn=%04x, romid=%02d, start=%04x, len=%04x\n", flags, fname, romid, start, pblen);
    if ((romid = swr_calc_addr(flags, &start, romid)) >= 0) {
        ent = find_file(fname, &key, cur_dir, NULL);
        if (flags & 0x80) {
            len = 0x4000 - start;
            if (len > 0) {
                if (ent) {
                    if (ent->attribs & ATTR_IS_DIR)
                        adfs_error(err_wont);
                    else if ((fp = open_file(ent, "rb"))) {
                        fread(rom + romid * 0x4000 + start, len, 1, fp);
                        fclose(fp);
                    } else {
                        bem_warnf("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
                        adfs_hosterr(errno);
                    }
                } else
                    adfs_error(err_notfound);
            } else
                adfs_error(err_too_big);
        } else {
            len = readmem16(pb+6);
            if (len <= 16384) {
                if (!ent)
                    ent = add_new_file(cur_dir, key.acorn_fn);
                if (ent) {
                    if ((fp = open_file(ent, "wb"))) {
                        fwrite(rom + romid * 0x4000 + start, len, 1, fp);
                        fclose(fp);
                        load_add = 0xff008000 | (romid << 16) | start;
                        ent->load_addr = load_add;
                        ent->exec_addr = load_add;
                        ent->length = len;
                        write_back(ent);
                    } else
                        bem_warnf("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
                } else
                    adfs_error(err_wont);
            } else
                adfs_error(err_too_big);
        }
    }
}

static void exec_swr_ram(uint8_t flags, uint16_t ram_start, uint16_t len, uint32_t sw_start, uint8_t romid) {
    uint8_t *rom_ptr;

    bem_debugf("vdfs: exec_swr_ram: flags=%02x, ram_start=%04x, len=%04x, sw_start=%04x, romid=%02d\n", flags, ram_start, len, sw_start, romid);
    if ((romid = swr_calc_addr(flags, &sw_start, romid)) >= 0) {
        rom_ptr = rom + romid * 0x4000 + sw_start;
        if (flags & 0x80)
            while (len--)
                *rom_ptr++ = readmem(ram_start++);
        else
            while (len--)
                writemem(ram_start++, *rom_ptr++);
    }
}

static void srcopy(uint8_t flags) {
    uint16_t addr, ram_start, len, sw_start;
    uint16_t romid;
    int ch;

    addr = readmem16(0xf2) + y;
    if ((addr = srp_start(addr, &ram_start))) {
        if ((addr = srp_length(addr, ram_start, &len))) {
            if ((addr = srp_start(addr, &sw_start))) {
                do
                    ch = readmem(addr++);
                while (ch == ' ' || ch == '\t');

                if (isxdigit(ch)) {
                    flags &= ~0x40;
                    addr = srp_hex(ch, addr, &romid);
                } else {
                    flags |= 0x40;
                    romid = 0;
                }
                exec_swr_ram(flags, ram_start, len, sw_start, romid);
            }
        }
    }
    adfs_error(err_badparms);
}

static inline void cmd_srread() {
    srcopy(0);
}

static inline void cmd_srwrite() {
    srcopy(0x80);
}

static inline void back() {
    vdfs_ent_t *ent;

    ent = cur_dir;
    cur_dir = prev_dir;
    prev_dir = ent;
}

static inline void cmd_dir() {
    vdfs_ent_t *ent, key;

    if ((ent = find_file(readmem16(0xf2) + y, &key, cur_dir, NULL))) {
        if (ent->attribs & ATTR_IS_DIR) {
            bem_debugf("vdfs: new cur_dir=%s\n", ent->acorn_fn);
            prev_dir = cur_dir;
            cur_dir = ent;
        }
    } else
        adfs_error(err_notfound);
}

static inline void cmd_lib() {
    vdfs_ent_t *ent, key;

    if ((ent = find_file(readmem16(0xf2) + y, &key, cur_dir, NULL))) {
        if (ent->attribs & ATTR_IS_DIR) {
            bem_debugf("vdfs: new lib_dir=%s\n", ent->acorn_fn);
            lib_dir = ent;
        }
    } else
        adfs_error(err_notfound);
}

static inline void cmd_rescan() {
    scan_seq++;
}

static inline void vdfs_check() {
    a=0;
}

static inline void dispatch(uint8_t value) {
    switch(value) {
        case 0x00: osfsc();      break;
        case 0x01: osfind();     break;
        case 0x02: osgbpb();     break;
        case 0x03: osbput();     break;
        case 0x04: osbget();     break;
        case 0x05: osargs();     break;
        case 0x06: osfile();     break;
        case 0xd0: cmd_srload(); break;
        case 0xd1: cmd_srwrite();break;
        case 0xd2: exec_swr_fs();break;
        case 0xd3: cmd_srsave(); break;
        case 0xd4: cmd_srread(); break;
        case 0xd5: back();       break;
        case 0xd7: cmd_dir();    break;
        case 0xd8: cmd_lib();    break;
        case 0xd9: cmd_rescan(); break;
        case 0xfe: vdfs_check(); break;
        case 0xff: setquit();    break;
        default: bem_warnf("vdfs: function code %d not recognised\n", value);
    }
}

uint8_t vdfs_read(uint16_t addr) {
    switch (addr & 3) {
        case 0:
            return claim_fs;
            break;
        case 1:
            return fs_flag;
            break;
        default:
            return 0xff;
    }
}

void vdfs_write(uint16_t addr, uint8_t value) {
    switch (addr & 3) {
        case 0:
            claim_fs = value;
            break;
        case 1:
            fs_flag = value;
            break;
        case 2:
            a = reg_a;
            dispatch(value);
            break;
        case 3:
            reg_a = value;
    }
}
