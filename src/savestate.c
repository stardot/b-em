#define _DEBUG
/*B-em v2.2 by Tom Walker
  Savestate handling*/
#include "b-em.h"
#include <zlib.h>

#include "6502.h"
#include "adc.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "music5000.h"
#include "paula.h"
#include "savestate.h"
#include "serial.h"
#include "sn76489.h"
#include "sysacia.h"
#include "via.h"
#include "sysvia.h"
#include "tube.h"
#include "uservia.h"
#include "video.h"
#include "vdfs.h"

struct _sszfile {
    z_stream zs;
    size_t togo;
    unsigned char buf[BUFSIZ];
};

int savestate_wantsave, savestate_wantload;
char *savestate_name;
FILE *savestate_fp;

void savestate_save(const char *name)
{
    log_debug("savestate: save, name=%s", name);
    if (savestate_fp)
        log_error("savestate: an operation is already in progress");
    else if (curtube != -1 && !tube_proc_savestate)
        log_error("savestate: current tube processor does not support saving state");
    else {
        size_t name_len = strlen(name);
        char *name_copy = malloc(name_len + 5);
        if (name_copy) {
            memcpy(name_copy, name, name_len+1);
            char *ext = strrchr(name_copy, '.');
            if (ext) {
                if (strcasecmp(ext, ".snp"))
                    strcpy(ext, ".snp");
            }
            else
                strcpy(name_copy + name_len, ".snp");
            if ((savestate_fp = fopen(name_copy, "wb"))) {
                if (savestate_name)
                    free(savestate_name);
                savestate_name = name_copy;
                savestate_wantsave = 1;
            }
            else
                log_error("savestate: unable to open %s for writing: %s", name, strerror(errno));
        }
        else
            log_error("savestate: out of memory copying filename");
    }
}

void savestate_load(const char *name)
{
    log_debug("savestate: load, name=%s", name);
    if (savestate_fp)
        log_error("savestate: an operation is already in progress");
    else {
        FILE *fp = fopen(name, "rb");
        if (fp) {
            unsigned char magic[8];
            if (fread(magic, 8, 1, fp) == 1 && memcmp(magic, "BEMSNAP", 7) == 0) {
                int vers = magic[7];
                if (vers >= '1' && vers <= '3') {
                    char *name_copy = strdup(name);
                    if (name_copy) {
                        if (savestate_name)
                            free(savestate_name);
                        savestate_name = name_copy;
                        savestate_fp = fp;
                        savestate_wantload = vers;
                    }
                    else
                        log_error("savestate: out of memory copying filename");
                }
                else
                    log_error("savestate: unable to load snapshot file version %c", magic[7]);
            }
            else
                log_error("savestate: file %s is not a B-Em snapshot file", name);
        }
        else
            log_error("savestate: unable to open %s for reading: %s", name, strerror(errno));
    }
}

static void sysacia_savestate(FILE *f) {
    acia_savestate(&sysacia, f);
}

static void sysacia_loadstate(FILE *f) {
    acia_loadstate(&sysacia, f);
}

static void save_tail(FILE *fp, int key, long start, long end, long size)
{
    fseek(fp, start, SEEK_SET);
    if (size > 0) {
        unsigned char hdr[5];
        hdr[0] = key;
        hdr[1] = size;
        hdr[2] = size >> 8;
        long hsize = 3;
        if (key & 0x80) {
            key &= 0x7f;
            hsize = 5;
            hdr[3] = size >> 16;
            hdr[4] = size >> 24;
        }
        fwrite(hdr, hsize, 1, fp);
        fseek(fp, end, SEEK_SET);
        log_debug("savestate: section %c from %08lX to %08lX, size %lu", key, start, end, size);
    }
}

static void save_sect(FILE *fp, int key, void (*save_func)(FILE *f))
{
    const unsigned hsize = 3;
    long start = ftell(fp);
    fseek(fp, hsize, SEEK_CUR);
    save_func(fp);
    long end = ftell(fp);
    long size = end - start - hsize;
    save_tail(fp, key, start, end, size);
}

static void save_zlib(FILE *fp, int key, void (*save_func)(ZFILE *zpf))
{
    const unsigned hsize = 5;
    long start = ftell(fp);
    fseek(fp, hsize, SEEK_CUR);

    ZFILE zfile;
    zfile.zs.zalloc = Z_NULL;
    zfile.zs.zfree = Z_NULL;
    zfile.zs.opaque = Z_NULL;
    deflateInit(&zfile.zs, Z_DEFAULT_COMPRESSION);
    zfile.zs.next_out = zfile.buf;
    zfile.zs.avail_out = BUFSIZ;
    save_func(&zfile);
    int res;
    while ((res = deflate(&zfile.zs, Z_FINISH)) == Z_OK) {
        fwrite(zfile.buf, BUFSIZ, 1, fp);
        zfile.zs.next_out = zfile.buf;
        zfile.zs.avail_out = BUFSIZ;
    }
    if (res == Z_STREAM_END) {
        if (zfile.zs.avail_out < BUFSIZ)
            fwrite(zfile.buf, BUFSIZ - zfile.zs.avail_out, 1, fp);
        log_debug("savestate: section %c saved deflated, %ld bytes into %ld", key, zfile.zs.total_in, zfile.zs.total_out);
        save_tail(fp, key|0x80, start, start + zfile.zs.total_out + hsize, zfile.zs.total_out);
    }
    else {
        log_error("savestate: compression error in section %c: %d(%s)", key, res, zfile.zs.msg);
        long end = ftell(fp);
        save_tail(fp, key|0x80, start, end, end - start - hsize);
    }
    deflateEnd(&zfile.zs);
}

void savestate_zwrite(ZFILE *zfp, void *src, size_t size)
{
    int res;

    zfp->zs.next_in = src;
    zfp->zs.avail_in = size;
    while ((res = deflate(&zfp->zs, Z_NO_FLUSH) == Z_OK)) {
        if (zfp->zs.avail_out == 0) {
            fwrite(zfp->buf, BUFSIZ, 1, savestate_fp);
            zfp->zs.next_out = zfp->buf;
            zfp->zs.avail_out = BUFSIZ;
        }
        if (zfp->zs.avail_in == 0)
            return;
    }
    log_warn("savestate: compression error %d (%s)", res, zfp->zs.msg);
}

void savestate_dosave(void)
{
    FILE *fp = savestate_fp;
    fwrite("BEMSNAP3", 8,1, fp);
    save_sect(fp, 'm', model_savestate);
    save_sect(fp, '6', m6502_savestate);
    save_zlib(fp, 'M', mem_savezlib);
    save_sect(fp, 'S', sysvia_savestate);
    save_sect(fp, 'U', uservia_savestate);
    save_sect(fp, 'V', videoula_savestate);
    save_sect(fp, 'C', crtc_savestate);
    save_sect(fp, 'v', video_savestate);
    save_sect(fp, 's', sn_savestate);
    save_sect(fp, 'A', adc_savestate);
    save_sect(fp, 'a', sysacia_savestate);
    save_sect(fp, 'r', serial_savestate);
    save_sect(fp, 'F', vdfs_savestate);
    save_sect(fp, '5', music5000_savestate);
    save_sect(fp, 'p', paula_savestate);
    save_zlib(fp, 'J', mem_jim_savez);
    if (curtube != -1) {
        save_sect(fp, 'T', tube_ula_savestate);
        save_zlib(fp, 'P', tube_proc_savestate);
    }
    fclose(fp);
    savestate_wantsave = 0;
    savestate_fp = NULL;
}

static void load_state_one(FILE *fp)
{
    curmodel = getc(fp);
    selecttube = curtube = -1;
    main_restart();

    m6502_loadstate(fp);
    mem_loadstate(fp);
    sysvia_loadstate(fp);
    uservia_loadstate(fp);
    videoula_loadstate(fp);
    crtc_loadstate(fp);
    video_loadstate(fp);
    sn_loadstate(fp);
    adc_loadstate(fp);
    acia_loadstate(&sysacia, fp);
    serial_loadstate(fp);
    vdfs_loadstate(fp);
    music5000_loadstate(fp);
}

static void load_zlib(long size, void (*load_func)(ZFILE *zpf))
{
    ZFILE zfile;

    zfile.zs.zalloc = Z_NULL;
    zfile.zs.zfree = Z_NULL;
    zfile.zs.opaque = Z_NULL;
    inflateInit(&zfile.zs);
    zfile.zs.next_in = Z_NULL;
    zfile.zs.avail_in = 0;
    zfile.togo = size;
    load_func(&zfile);
    log_debug("savestate: inflated %ld bytes to %ld", zfile.zs.total_in, zfile.zs.total_out);
    inflateEnd(&zfile.zs);
}

void savestate_zread(ZFILE *zfp, void *dest, size_t size)
{
    int res, flush;

    zfp->zs.next_out = dest;
    zfp->zs.avail_out = size;
    do {
        flush = Z_NO_FLUSH;
        if (zfp->zs.avail_in == 0) {
            if (zfp->togo == 0)
                flush = Z_FINISH;
            else {
                size_t chunk;
                if (zfp->togo > BUFSIZ)
                    chunk = BUFSIZ;
                else
                    chunk = zfp->togo;
                if (fread(zfp->buf, chunk, 1, savestate_fp) != 1)
                    break;
                zfp->zs.next_in = zfp->buf;
                zfp->zs.avail_in = chunk;
                zfp->togo -= chunk;
            }
        }
        res = inflate(&zfp->zs, flush);
    } while (res == Z_OK && zfp->zs.avail_out > 0);
}

static void load_section(FILE *fp, int key, long size)
{
    log_debug("savestate: found section %c of %ld bytes", key, size);
    long start = ftell(fp);
    switch(key) {
        case 'm':
            model_loadstate(fp);
            break;
        case '6':
            m6502_loadstate(fp);
            break;
        case 'M':
            load_zlib(size, mem_loadzlib);
            break;
        case 'S':
            sysvia_loadstate(fp);
            break;
        case 'U':
            uservia_loadstate(fp);
            break;
        case 'V':
            videoula_loadstate(fp);
            break;
        case 'C':
            crtc_loadstate(fp);
            break;
        case 'v':
            video_loadstate(fp);
            break;
        case 's':
            sn_loadstate(fp);
            break;
        case 'A':
            adc_loadstate(fp);
            break;
        case 'a':
            sysacia_loadstate(fp);
            break;
        case 'r':
            serial_loadstate(fp);
            break;
        case 'F':
            vdfs_loadstate(fp);
            break;
        case '5':
            music5000_loadstate(fp);
            break;
        case 'T':
            if (curtube != -1)
                tube_ula_loadstate(fp);
            break;
        case 'P':
            if (tube_proc_loadstate)
                load_zlib(size, tube_proc_loadstate);
            break;
        case 'p':
            paula_loadstate(fp);
            break;
        case 'J':
            load_zlib(size, mem_jim_loadz);
    }
    long end = ftell(fp);
    if (end == start) {
        log_warn("savestate: section %c skipped", key);
        fseek(fp, size, SEEK_CUR);
    }
    else if (size != (end - start)) {
        log_warn("savestate: section %c, size mismatch, file=%ld, read=%ld", key, size, end - start);
        fseek(fp, start + size, SEEK_SET);
    }
}

static void load_state_two(FILE *fp)
{
    unsigned char hdr[4];

    while (fread(hdr, sizeof hdr, 1, fp) == 1) {
        long size = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16);
        load_section(fp, hdr[0], size);
    }
}

static void load_state_three(FILE *fp)
{
    unsigned char hdr[3];

    while (fread(hdr, sizeof hdr, 1, fp) == 1) {
        int key = hdr[0];
        long size = hdr[1] | (hdr[2] << 8);
        if (key & 0x80) {
            if (fread(hdr, 2, 1, fp) != 1) {
                log_error("savestate: unexpected EOF on file %s", savestate_name);
                return;
            }
            size |= (hdr[0] << 16) | (hdr[1] << 24);
            key &= 0x7f;
        }
        load_section(fp, key, size);
    }
}

void savestate_doload(void)
{
    FILE *fp = savestate_fp;
    switch(savestate_wantload) {
        case '1':
            load_state_one(fp);
            break;
        case '2':
            load_state_two(fp);
            break;
        case '3':
            load_state_three(fp);
            break;
    }
    if (ferror(fp))
        log_error("savestate: state not fully restored from V%c file '%s': %s", savestate_wantload, savestate_name, strerror(errno));
    else
        log_debug("savestate: loaded V%c snapshot file", savestate_wantload);
    fclose(fp);
    savestate_wantload = 0;
    savestate_fp = NULL;
}

void savestate_save_var(unsigned var, FILE *f) {
    uint8_t byte;

    log_debug("savestate: saving variable-length integer %u", var);
    for (;;) {
        byte = var & 0x7f;
        var >>= 7;
        if (var == 0)
            break;
        putc(byte, f);
    }
    putc(byte | 0x80, f);
}

void savestate_save_str(const char *str, FILE *f)
{
    size_t len = strlen(str);
    savestate_save_var(len, f);
    fwrite(str, len, 1, f);
    log_debug("savestate: saving string '%s'", str);
}

unsigned savestate_load_var(FILE *f) {
    unsigned var, lshift;
    int      ch;

    var = lshift = 0;
    while ((ch = getc(f)) != EOF) {
        if (ch & 0x80) {
            var |= ((ch & 0x7f) << lshift);
            break;
        }
        var |= ch << lshift;
        lshift += 7;
    }
    log_debug("savestate: loaded variable-length integer %u", var);
    return var;
}

char *savestate_load_str(FILE *f)
{
    size_t len = savestate_load_var(f);
    char *str = malloc(len+1);
    if (!str) {
        log_fatal("savestate: out of memory");
        exit(1);
    }
    fread(str, len, 1, f);
    str[len] = '\0';
    log_debug("savestate: loaded string '%s'", str);
    return str;
}
