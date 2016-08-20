#define _GNU_SOURCE

#include "b-em.h"
#include "6502.h"
#include "mem.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>

#include <alloca.h>
#include <search.h>
#include <sys/stat.h>

#define MIN_CHANNEL      32
#define MAX_CHANNEL      96
#define NUM_CHANNELS     (MAX_CHANNEL-MIN_CHANNEL)

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

// A catalogue entry.  This represents a single file, i.e. an
// entry in a directory and contains information cache from any
// .inf file associated with the main file as well as some info
// gathered from the host OS.

typedef struct _cat_dir cat_dir_t;

typedef struct {
        char      *host_fn;
        char      acorn_fn[MAX_FILE_NAME+1];
        uint16_t  attribs;
        unsigned  load_addr;
        unsigned  exec_addr;
        unsigned  length;
        cat_dir_t *dir;
        time_t    inf_read;
} cat_ent_t;

// A directory, i.e. a group of the above catalogue entries.  In
// this case a tree is used to maintain the entries in sorted
// order and then an array is produced from that when items are
// inserted.

struct _cat_dir {
        void      *acorn_tree;
        void      *host_tree;
        cat_ent_t **cat_tab;
        size_t    cat_size;
        char      *host_path;
        unsigned  scan_seq;
};

static cat_ent_t  **cat_ptr; // used by the tree walk callback.

static cat_dir_t  root_dir;
static cat_dir_t  *cur_dir;
static cat_dir_t  *lib_dir;
static unsigned   scan_seq;

// Open files.  An open file is an association between a host OS
// file pointer and a catalogue entry.

typedef struct {
        FILE      *fp;
        cat_ent_t *ent;
} vdfs_file_t;

static vdfs_file_t vdfs_chan[NUM_CHANNELS];

static uint8_t reg_a;

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

static void writemem32(uint16_t addr, uint32_t value) {
    writemem(addr, value & 0xff);
    writemem(addr+1, (value >> 8) & 0xff);
    writemem(addr+2, (value >> 16) & 0xff);
    writemem(addr+3, (value >> 24) & 0xff);
}

static inline void flush_file(int channel) {
    FILE *fp;

    if ((fp = vdfs_chan[channel].fp))
        fflush(fp);
}

static void flush_all() {
    int channel;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        flush_file(channel);
}

static void close_file(int channel) {
    FILE *fp;

    if ((fp = vdfs_chan[channel].fp)) {
        fclose(fp);
        vdfs_chan[channel].fp = NULL;
    }
    vdfs_chan[channel].ent = NULL;
}

static void close_all() {
    int channel;

    for (channel = 0; channel < NUM_CHANNELS; channel++)
        close_file(channel);
}

static void free_cat_dir(cat_dir_t *cat_dir);

static void free_tree_node(void *ptr) {
    cat_ent_t *ent = (cat_ent_t *)ptr;
    cat_dir_t *dir;
    char *host_fn;

    if ((dir = ent->dir)) {
        free_cat_dir(dir);
        free(dir->host_path);
        free(dir);
    }
    if ((host_fn = ent->host_fn) && host_fn != ent->acorn_fn)
        free(host_fn);
    free(ent);
}

static void free_noop(void *ptr) { }

static void free_cat_dir(cat_dir_t *cat_dir) {
    tdestroy(cat_dir->acorn_tree, free_noop);
    tdestroy(cat_dir->host_tree, free_tree_node);
    if (cat_dir->cat_tab)
        free(cat_dir->cat_tab);
}

void vdfs_init(void) {
    char *root;

    if ((root = getenv("BEM_VDFS_ROOT")) == NULL)
        root = ".";
    root_dir.host_path = root;
    cur_dir = lib_dir = &root_dir;
    scan_seq = 1;
}

void vdfs_reset() {
        flush_all();
}

void vdfs_close(void) {
    close_all();
    free_cat_dir(&root_dir);
}

static int host_comp(const void *a, const void *b) {
    cat_ent_t *ca = (cat_ent_t *)a;
    cat_ent_t *cb = (cat_ent_t *)b;
    return strcmp(ca->host_fn, cb->host_fn);
}

static int acorn_comp(const void *a, const void *b) {
    cat_ent_t *ca = (cat_ent_t *)a;
    cat_ent_t *cb = (cat_ent_t *)b;
    int res = strcasecmp(ca->acorn_fn, cb->acorn_fn);
    if (res == 0 && ca->host_fn && cb->host_fn)
        res = strcmp(ca->host_fn, cb->host_fn);
    return res;
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

static void scan_entry(cat_dir_t *dir, cat_ent_t *ent) {
    int name_len, dir_len;
    char *inf_fn, *host_dir_path, *host_file_path;
    FILE *fp;
    struct stat stb;

    name_len = strlen(ent->host_fn);
    inf_fn = alloca(name_len + 5);
    strcpy(inf_fn, ent->host_fn);
    strcpy(inf_fn+name_len, ".inf");
    if ((fp = fopen(inf_fn, "rt"))) {
        get_filename(fp, ent->acorn_fn);
        ent->load_addr = get_hex(fp);
        ent->exec_addr = get_hex(fp);
        fclose(fp);
    } else
        strncpy(ent->acorn_fn, ent->host_fn, MAX_FILE_NAME);

    host_dir_path = dir->host_path;
    if (host_dir_path[0] == '.' && host_dir_path[1] == '\0')
        host_file_path = ent->host_fn;
    else {
        dir_len = strlen(dir->host_path);
        host_file_path = alloca(dir_len + strlen(ent->host_fn) + 3);
        strcpy(host_file_path, host_dir_path);
        host_file_path[dir_len] = '/';
        strcpy(host_file_path + dir_len + 1, ent->host_fn);
    }

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

static cat_ent_t *new_cat_entry(cat_dir_t *dir, const char *host_fn) {
    cat_ent_t *ent, **ptr;
    int name_len, seq_ch = '0';

    if ((ent = malloc(sizeof(cat_ent_t)))) {
        memset(ent, 0, sizeof(cat_ent_t));
        if ((ent->host_fn = strdup(host_fn))) {

            scan_entry(dir, ent);
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
                    if (seq_ch == '0')
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
            }
            tsearch(ent, &dir->host_tree, host_comp);
            bem_debugf("vdfs: returing new entry %p\n", ent);
            return ent;
        }
        free(ent);
    }
    return NULL;
}

static void tree_visit(const void *nodep, const VISIT which, const int depth) {
    if (which == postorder || which == leaf)
        *cat_ptr++ = *(cat_ent_t **)nodep;
}

static int scan_dir(cat_dir_t *dir) {
    int  count = 0;
    DIR  *dp;
    struct dirent *dep;
    cat_ent_t **ptr, **end, *ent, key;

    if (dir->acorn_tree && dir->scan_seq < scan_seq)
        return 0;

    if ((dp = opendir(dir->host_path))) {
        ptr = dir->cat_tab;
        end = ptr + dir->cat_size;
        while (ptr < end) {
            ent = *ptr++;
            ent->attribs |= ATTR_DELETED;
        }
        while ((dep = readdir(dp))) {
            if (*(dep->d_name) != '.') {
                key.host_fn = dep->d_name;
                if ((ptr = tfind(&key, &dir->host_tree, host_comp))) {
                    ent = *ptr;
                    ent->attribs &= ~ATTR_DELETED;
                    scan_entry(dir, ent);
                }
                else if ((ent = new_cat_entry(dir, dep->d_name)))
                    count++;
                else {
                    count = -1;
                    break;
                }
            }
        }
        closedir(dp);
        if (count >= 0) {
            bem_debugf("count=%d\n", count);
            if (dir->cat_tab)
                free(dir->cat_tab);
            if ((dir->cat_tab = malloc(sizeof(cat_ent_t *)*count))) {
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

static cat_ent_t *find_file(uint32_t fn_addr, cat_ent_t *key) {
    int i, ch;
    char *fn_ptr;
    cat_ent_t **ptr, *ent;

    memset(key, 0, sizeof(cat_ent_t));
    bem_debugf("vdfs: find_file: fn_addr=%04x\n", fn_addr);
    fn_ptr = key->acorn_fn;
    for (i = 0; i < MAX_FILE_NAME; i++) {
        ch = readmem(fn_addr++);
        if (ch == '\r')
            break;
        *fn_ptr++ = ch;
    }
    *fn_ptr = '\0';
    bem_debugf("vdfs: find_file: looking for acorn name '%s'\n", key->acorn_fn);
    if ((ptr = tfind(key, &cur_dir->acorn_tree, acorn_comp))) {
        ent = *ptr;
        bem_debugf("vdfs: find_file: found at %p, host_fn=%s\n", ent, ent->host_fn);
        return ent;
    }
    return NULL;
}

static cat_ent_t *add_new_file(cat_dir_t *dir, const char *name) {
    int new_size;
    cat_ent_t **new_tab, *new_ent;

    new_size = dir->cat_size + 1;
    if ((new_tab = realloc(dir->cat_tab, new_size * (sizeof(cat_ent_t *))))) {
        dir->cat_tab = new_tab;
        if ((new_ent = malloc(sizeof(cat_ent_t)))) {
            memset(new_ent, 0, sizeof(cat_ent_t));
            strncpy(new_ent->acorn_fn, name, MAX_FILE_NAME);
            new_ent->host_fn = new_ent->acorn_fn;
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

static void write_back(cat_ent_t *ent) {
    int  name_len;
    char *inf_fn;
    FILE *fp;

    name_len = strlen(ent->host_fn);
    inf_fn = alloca(name_len+5);
    strcpy(inf_fn, ent->host_fn);
    strcpy(inf_fn+name_len, ".inf");
    if ((fp = fopen(inf_fn, "wt"))) {
        fprintf(fp, "%-*s %08X, %08X %08X\n", MAX_FILE_NAME, ent->acorn_fn, ent->load_addr, ent->exec_addr, ent->length);
        fclose(fp);
    } else
        bem_warnf("vdfs: unable to create INF file '%s' for '%s': %s\n", inf_fn, ent->host_fn, strerror(errno));
}

static inline void osfsc() {
    bem_debugf("vdfs: osfsc unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

static inline void osfind() {
    int channel;
    cat_ent_t *ent, key;
    const char *mode;
    FILE *fp;

    if (reg_a == 0) {
        channel = y;
        if (channel == 0)
            close_all();
        else if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL)
            close_file(channel-MIN_CHANNEL);
    } else {
        mode = NULL;
        for (channel = 0; channel < MAX_CHANNEL; channel++) {
            if (vdfs_chan[channel].fp == NULL) {
                if ((ent = find_file((y << 8) | x, &key))) {
                    if (reg_a == 0x40)
                        mode = "rb";
                    else if (reg_a == 0x80)
                        mode = "wb";
                    else if (reg_a == 0xc0)
                        mode = "rb+";
                } else {
                    if (reg_a == 0x80) {
                        ent = add_new_file(cur_dir, key.acorn_fn);
                        mode = "wb";
                    }
                    else if (reg_a == 0xc0) {
                        ent = add_new_file(cur_dir, key.acorn_fn);
                        mode = "wb+";
                    }
                }
                break;
            }
        }
        reg_a = 0;
        if (mode) {
            if ((fp = fopen(ent->host_fn, mode))) {
                vdfs_chan[channel].fp = fp;
                vdfs_chan[channel].ent = ent;
                reg_a = MIN_CHANNEL + channel;
            } else
                bem_warnf("vdfs: osfind: unable to open file '%s' in mode '%s': %s\n", ent->host_fn, mode, strerror(errno));
        }
    }
}

static inline void osgbpb() {
    int      status = 0;
    uint32_t pb = (y << 8) | x;
    uint32_t seq_ptr, mem_ptr, n;
    cat_ent_t *cat_ptr;
    char *ptr;

    switch (reg_a)
    {
        case 0x09: // list files in current directory.
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
                    for (ptr = cat_ptr->acorn_fn; *ptr; )
                        writemem(mem_ptr++, *ptr++);
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
            bem_debugf("vdfs: osgbpb unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
            bem_debugf("vdfs: osgbpb pb.channel=%d, data=%04X num=%04X, ptr=%04X\n", readmem(pb), readmem32(pb+1), readmem32(pb+6), readmem32(pb+9));
    }
    p.c = status;
}

static inline void osbput() {
    int channel;
    FILE *fp;

    channel = y;
    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL)
        if ((fp = vdfs_chan[channel-MIN_CHANNEL].fp))
            putc(reg_a, fp);
}

static inline void osbget() {
    int channel, ch;
    FILE *fp;

    p.c = 1;
    channel = y;
    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL) {
        if ((fp = vdfs_chan[channel-MIN_CHANNEL].fp)) {
            if ((ch = getc(fp)) != EOF) {
                a = ch;
                p.c = 0;
            }
        }
    }
}

static inline void osargs() {
    FILE *fp;
    long temp;

    if (y == 0) {
        switch (reg_a)
        {
            case 0:
                reg_a = 4; // say disc filing selected.
                break;
            case 0xff:
                flush_all();
                break;
            default:
                bem_debugf("vdfs: osargs: y=0, a=%d not implemented", reg_a);
        }
    } else if (y < MAX_CHANNEL) {
        if ((fp = vdfs_chan[y].fp)) {
            switch (reg_a)
            {
                case 0:
                    writemem32(x, ftell(fp));
                    break;
                case 1:
                    fseek(fp, readmem32(x), SEEK_SET);
                    break;
                case 2:
                    temp =- ftell(fp);
                    fseek(fp, 0, SEEK_END);
                    writemem32(x, ftell(fp));
                    fseek(fp, temp, SEEK_SET);
                    break;
                case 0xff:
                    fflush(fp);
                    break;
                default:
                    bem_debugf("vdfs: osargs: unrecognised function code a=%d for channel y=%d", reg_a, y);
            }
        }
        else
            bem_debugf("vdfs: osargs: closed channel y=%d", y);
    } else
        bem_debugf("vdfs: osargs: invalid channel y=%d", y);
}

static void osfile_save(uint32_t pb, cat_ent_t *ent) {
    FILE *fp;
    uint32_t start_addr, end_addr, ptr;

    if ((fp = fopen(ent->host_fn, "wb"))) {
        start_addr = readmem32(pb+0x0a);
        end_addr = readmem32(pb+0x0e);
        for (ptr = start_addr; ptr < end_addr; ptr++)
            putc(readmem(ptr), fp);
        fclose(fp);
        ent->load_addr = readmem32(pb+0x02);
        ent->exec_addr = readmem32(pb+0x06);
        ent->length = end_addr-start_addr;
        write_back(ent);
    } else
        bem_warnf("vdfs: unable to create file '%s': %s\n", ent->host_fn, strerror(errno));
}

static void osfile_load(uint32_t pb, cat_ent_t *ent) {
    FILE *fp;
    uint32_t addr;
    int ch;

    if ((fp = fopen(ent->host_fn, "rb"))) {
        if (readmem(pb+0x06) == 0)
            addr = readmem32(pb+0x02);
        else
            addr = ent->load_addr;
        while ((ch = getc(fp)) != EOF)
            writemem(addr++, ch);
        fclose(fp);
    } else
        bem_warnf("vdfs: unable to load file '%s': %s\n", ent->host_fn, strerror(errno));
}

static inline void osfile()
{
    cat_ent_t *ent, key;
    uint32_t pb = (y << 8) | x;

    ent = find_file(readmem16(pb), &key);

    switch (reg_a) {
        case 0x00:
            if (!ent)
                ent = add_new_file(cur_dir, key.acorn_fn);
            if (ent)
                osfile_save(pb, ent);
            break;

        case 0x01:
            if (ent) {
                ent->load_addr = readmem32(pb+0x02);
                ent->exec_addr = readmem32(pb+0x06);
                write_back(ent);
            }
            break;

        case 0x02:
            if (ent) {
                ent->load_addr = readmem32(pb+0x02);
                write_back(ent);
            }
            break;

        case 0x03:
            if (ent) {
                ent->exec_addr = readmem32(pb+0x06);
                write_back(ent);
            }
            break;

        case 0x04:
            break; // attributes not written.

        case 0x05:
            if (ent) {
                writemem32(pb+0x02, ent->load_addr);
                writemem32(pb+0x06, ent->exec_addr);
                writemem32(pb+0x0a, ent->length);
                writemem32(pb+0x0e, ent->attribs);
            }
            break;

        case 0xff:
            if (ent)
                osfile_load(pb, ent);
            break;

        default:
            bem_debugf("vdfs: osfile unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
    }
    a = (ent == NULL) ? 0 : (ent->attribs & ATTR_IS_DIR) ? 2 : 1;
}

static inline void srload() {
    bem_debugf("vdfs: srload unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
}

static inline void srwrite() {
    bem_debugf("vdfs: srwrite unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
}

static inline void drive() {
    bem_debugf("vdfs: drive unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
}

static inline void back() {
    bem_debugf("vdfs: back unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
}

static inline void mount() {
    bem_debugf("vdfs: mount unimplemented for a=%d, x=%d, y=%d\n", a, x, y);
}

static inline void dispatch(uint8_t value) {
    switch(value) {
        case 0x00: osfsc();   break;
        case 0x01: osfind();  break;
        case 0x02: osgbpb();  break;
        case 0x03: osbput();  break;
        case 0x04: osbget();  break;
        case 0x05: osargs();  break;
        case 0x06: osfile();  break;
        case 0xd0: srload();  break;
        case 0xd1: srwrite(); break;
        case 0xd2: drive();   break;
        case 0xd5: back();    break;
        case 0xd6: mount();   break;
        default: bem_warnf("vdfs: function code %d not recognised\n", value);
    }
}

void vdfs_write(uint16_t addr, uint8_t value) {
    if (addr & 1)
        reg_a = value;
    else {
        a = reg_a;
        dispatch(value);
    }
}
