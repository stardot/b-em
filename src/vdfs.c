#define _GNU_SOURCE

#include "b-em.h"
#include "6502.h"
#include "mem.h"

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

#define ATTR_USER_READ   0x001
#define ATTR_USER_WRITE  0x002
#define ATTR_USER_EXEC   0x004
#define ATTR_USER_LOCKD  0x008
#define ATTR_OTHR_READ   0x010
#define ATTR_OTHR_WRITE  0x020
#define ATTR_OTHR_EXEC   0x040
#define ATTR_OTHR_LOCKD  0x080
#define ATTR_IS_DIR      0x100

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
} cat_ent_t;

// A directory, i.e. a group of the above catalogue entries.  In
// this case a tree is used to maintain the entries in sorted
// order and then an array is produced from that when items are
// inserted.

struct _cat_dir {
        void      *cat_tree;
        cat_ent_t **cat_tab;
        size_t     cat_size;
};

static cat_ent_t  **cat_ptr; // used by the tree walk callback.

static const char *vdfs_root; // the root on the host.
static cat_dir_t  *root_dir;
//static cat_dir_t  *cur_dir;
//static cat_dir_t  *lib_dir;

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
    cat_ent_t *ent = *(cat_ent_t **)ptr;

    if (ent->dir)
        free_cat_dir(ent->dir);
    if (ent->host_fn && ent->host_fn != ent->acorn_fn)
        free(ent->host_fn);
    free(ptr);
}

static void free_cat_dir(cat_dir_t *cat_dir) {
    tdestroy(cat_dir->cat_tree, free_tree_node);
    free(cat_dir->cat_tab);
    free(cat_dir);
}

void vdfs_init(void) {
    const char *root;

    if ((root = getenv("BEM_VDFS_ROOT")) == NULL)
        root = ".";
    vdfs_root = root;
}

void vdfs_reset() {
        flush_all();
}

void vdfs_close(void) {
    close_all();
    if (root_dir)
        free_cat_dir(root_dir);
}

static cat_ent_t *scan_file(const char *host_fn) {
    cat_ent_t *new_ent;
    int  name_len;
    char *inf_fn;
    FILE *fp;
    struct stat stb;

    if ((new_ent = malloc(sizeof(cat_ent_t)))) {
        memset(new_ent, 0, sizeof(cat_ent_t));
        name_len = strlen(host_fn);
        if ((new_ent->host_fn = malloc(name_len+1)) == NULL) {
            free(new_ent);
            return NULL;
        }
        strcpy(new_ent->host_fn, host_fn);
        inf_fn = alloca(name_len+5);
        strcpy(inf_fn, host_fn);
        strcpy(inf_fn+name_len, ".inf");
        if ((fp = fopen(inf_fn, "rt"))) {
            if (fscanf(fp, "%10s %X, %X", (char *)&new_ent->acorn_fn, &new_ent->load_addr, &new_ent->exec_addr) < 1)
                strncpy(new_ent->acorn_fn, host_fn, MAX_FILE_NAME);
            fclose(fp);
        } else
            strncpy(new_ent->acorn_fn, host_fn, MAX_FILE_NAME);
        if (stat(host_fn, &stb) == -1)
            bem_warnf("unable to stat '%s': %s\n", host_fn, strerror(errno));
        else {
            new_ent->length = stb.st_size;
            if (S_ISDIR(stb.st_mode))
                new_ent->attribs |= ATTR_IS_DIR;
            if (stb.st_mode & S_IRUSR)
                new_ent->attribs |= ATTR_USER_READ;
            if (stb.st_mode & S_IWUSR)
                new_ent->attribs |= ATTR_USER_WRITE;
            if (stb.st_mode & S_IXUSR)
                new_ent->attribs |= ATTR_USER_EXEC;
            if (stb.st_mode & (S_IRGRP|S_IROTH))
                new_ent->attribs |= ATTR_OTHR_READ;
            if (stb.st_mode & (S_IWGRP|S_IWOTH))
                new_ent->attribs |= ATTR_OTHR_WRITE;
            if (stb.st_mode & (S_IXGRP|S_IXOTH))
                new_ent->attribs |= ATTR_OTHR_EXEC;
        }
        return new_ent;
    }
    return NULL;
}

static int cat_comp(const void *a, const void *b) {
    cat_ent_t *ca = (cat_ent_t *)a;
    cat_ent_t *cb = (cat_ent_t *)b;
    int res = strcasecmp(ca->acorn_fn, cb->acorn_fn);
    if (res == 0 && ca->host_fn && cb->host_fn)
        res = strcmp(ca->host_fn, cb->host_fn);
    return res;
}

static void tree_visit(const void *nodep, const VISIT which, const int depth) {
    if (which == postorder || which == leaf)
        *cat_ptr++ = *(cat_ent_t **)nodep;
}

static cat_dir_t *scan_dir(const char *host_dir) {
    int  count = -0;
    DIR *dir;
    struct dirent *dp;
    cat_ent_t *new_ent;
    cat_dir_t *new_dir;

    if ((dir = opendir(host_dir))) {
        if (root_dir)
            free_cat_dir(root_dir);
        if ((new_dir = malloc(sizeof(cat_dir_t)))) {
            memset(new_dir, 0, sizeof(cat_dir_t));
            while ((dp = readdir(dir))) {
                if (*(dp->d_name) != '.') {
                    if ((new_ent = scan_file(dp->d_name))) {
                        tsearch(new_ent, &new_dir->cat_tree, cat_comp);
                        count++;
                    } else {
                        count = -1;
                        break;
                    }
                }
            }
        }
        closedir(dir);
        if (count >= 0) {
            if ((new_dir->cat_tab = malloc(sizeof(cat_ent_t *)*count))) {
                cat_ptr = new_dir->cat_tab;
                twalk(new_dir->cat_tree, tree_visit);
                new_dir->cat_size = count;
                root_dir = new_dir;
                return new_dir;
            }
        }
        tdestroy(new_dir->cat_tree, free_tree_node);
    } else
        bem_warnf("unable to opendir '%s': %s\n", host_dir, strerror(errno));
    return NULL;
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
    if ((ptr = tfind(key, &root_dir->cat_tree, cat_comp))) {
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
            tsearch(new_ent, &dir->cat_tree, cat_comp);
            cat_ptr = dir->cat_tab;
            twalk(dir->cat_tree, tree_visit);
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
        fprintf(fp, "%-10s %08X, %08X %08X\n", ent->acorn_fn, ent->load_addr, ent->exec_addr, ent->length);
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
                        ent = add_new_file(root_dir, key.acorn_fn);
                        mode = "wb";
                    }
                    else if (reg_a == 0xc0) {
                        ent = add_new_file(root_dir, key.acorn_fn);
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
    a = reg_a;
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
                if (!root_dir) {
                    if ((root_dir = scan_dir(".")) == NULL) {
                        status = 1;
                        break;
                    }
                }
            }
            if (seq_ptr < root_dir->cat_size) {
                mem_ptr = readmem32(pb+1);
                bem_debugf("vdfs: seq_ptr=%d, writing max %d entries starting %04X\n", seq_ptr, n, mem_ptr);
                do {
                    cat_ptr = root_dir->cat_tab[seq_ptr++];
                    bem_debugf("vdfs: writing acorn name %s\n", cat_ptr->acorn_fn);
                    for (ptr = cat_ptr->acorn_fn; *ptr; )
                        writemem(mem_ptr++, *ptr++);
                    writemem(mem_ptr++, '\r');
                } while (--n > 0 && seq_ptr < root_dir->cat_size);
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
    a = reg_a;
    p.c = status;
}

static inline void osbput() {
    int channel;
    FILE *fp;

    channel = y;
    if (channel >= MIN_CHANNEL && channel < MAX_CHANNEL)
        if ((fp = vdfs_chan[channel-MIN_CHANNEL].fp))
            putc(reg_a, fp);
    a = reg_a;
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
    a = reg_a;
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
                ent = add_new_file(root_dir, key.acorn_fn);
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

static inline void srload()
{
        bem_debugf("vdfs: srload unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

static inline void srwrite()
{
        bem_debugf("vdfs: srwrite unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

static inline void drive()
{
        bem_debugf("vdfs: drive unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

static inline void back()
{
        bem_debugf("vdfs: back unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

static inline void mount()
{
        bem_debugf("vdfs: mount unimplemented for a=%d, x=%d, y=%d\n", reg_a, x, y);
}

void vdfs_write(uint16_t addr, uint8_t value)
{
        if (addr & 1)
        {
                bem_debugf("vdfs: save A as %d\n", value);
                reg_a = value;
        }
        else
        {
                switch(value)
                {
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
}
