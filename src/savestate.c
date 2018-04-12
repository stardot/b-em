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
    int flush;
    unsigned char buf[BUFSIZ];
};

int savestate_wantsave, savestate_wantload;
char *savestate_name;
FILE *savestate_fp;

void savestate_save(const char *name)
{
    char *name_copy;

    log_debug("savestate: save, name=%s", name);
    if (savestate_fp)
        log_error("savestate: an operation is already in progress");
    else if (curtube != -1 && !tube_proc_savestate)
        log_error("savestate: current tube processor does not support saving state");
    else if ((savestate_fp = fopen(name, "wb"))) {
        if ((name_copy = strdup(name))) {
            if (savestate_name)
                free(savestate_name);
            savestate_name = name_copy;
            savestate_wantsave = 1;
        }
        else
            log_error("savestate: out of memory copying filename");
    }
    else
        log_error("savestate: unable to open %s for writing: %s", name, strerror(errno));
}

void savestate_load(const char *name)
{
    char *name_copy;
    char magic[8];

    log_debug("savestate: load, name=%s", name);
    if (savestate_fp)
        log_error("savestate: an operation is already in progress");
    else if ((savestate_fp = fopen(name, "rb"))) {
        if (fread(magic, 8, 1, savestate_fp) == 1 && memcmp(magic, "BEMSNAP", 7) == 0) {
            if ((name_copy = strdup(name))) {
                if (savestate_name)
                    free(savestate_name);
                savestate_name = name_copy;
                switch(magic[7]) {
                    case '1':
                        savestate_wantload = 1;
                        return;
                    case '2':
                        savestate_wantload = 2;
                        return;
                    default:
                        log_error("savestate: unable to load snapshot file version %c", magic[7]);
                }
            }
            else
                log_error("savestate: out of memory copying filename");
        }
        else
            log_error("savestate: file %s is not a B-Em snapshot file", name);
        fclose(savestate_fp);
    }
    else
        log_error("savestate: unable to open %s for reading: %s", name, strerror(errno));
}

static void sysacia_savestate(FILE *f) {
    acia_savestate(&sysacia, f);
}

static void sysacia_loadstate(FILE *f) {
    acia_loadstate(&sysacia, f);
}

static void save_tail(long start, long end, long size)
{
    fseek(savestate_fp, start, SEEK_SET);
    putc(size & 0xff, savestate_fp);
    putc((size >> 8) & 0xff, savestate_fp);
    putc((size >> 16) & 0xff, savestate_fp);
    fseek(savestate_fp, end, SEEK_SET);
}

static void save_sect(int key, void (*save_func)(FILE *f))
{
    long start, end, size;

    putc(key, savestate_fp);
    start = ftell(savestate_fp);
    fseek(savestate_fp, 3, SEEK_CUR);
    save_func(savestate_fp);
    end = ftell(savestate_fp);
    size = end - start - 3;
    log_debug("savestate: section %c saved, %ld bytes", key, size);
    save_tail(start, end, size);
}

static void save_zlib(int key, void (*save_func)(ZFILE *zpf))
{
    long start, end;
    ZFILE zfile;
    int res;

    putc(key, savestate_fp);
    start = ftell(savestate_fp);
    fseek(savestate_fp, 3, SEEK_CUR);

    zfile.zs.zalloc = Z_NULL;
    zfile.zs.zfree = Z_NULL;
    zfile.zs.opaque = Z_NULL;
    deflateInit(&zfile.zs, Z_DEFAULT_COMPRESSION);
    zfile.zs.next_out = zfile.buf;
    zfile.zs.avail_out = BUFSIZ;
    save_func(&zfile);
    while ((res = deflate(&zfile.zs, Z_FINISH)) == Z_OK) {
        fwrite(zfile.buf, BUFSIZ, 1, savestate_fp);
        zfile.zs.next_out = zfile.buf;
        zfile.zs.avail_out = BUFSIZ;
    }
    if (res == Z_STREAM_END) {
        if (zfile.zs.avail_out < BUFSIZ)
            fwrite(zfile.buf, BUFSIZ - zfile.zs.avail_out, 1, savestate_fp);
        log_debug("savestate: section %c saved deflated, %ld bytes into %ld", key, zfile.zs.total_in, zfile.zs.total_out);
        save_tail(start, start + zfile.zs.total_out + 3, zfile.zs.total_out);
    }
    else {
        log_error("savestate: compression error in section %c: %d(%s)", key, res, zfile.zs.msg);
        end = ftell(savestate_fp);
        save_tail(start, end, end - start - 3);
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
    fwrite("BEMSNAP2", 8, 1, savestate_fp);
    save_sect('m', model_savestate);
    save_sect('6', m6502_savestate);
    save_zlib('M', mem_savezlib);
    save_sect('S', sysvia_savestate);
    save_sect('U', uservia_savestate);
    save_sect('V', videoula_savestate);
    save_sect('C', crtc_savestate);
    save_sect('v', video_savestate);
    save_sect('s', sn_savestate);
    save_sect('A', adc_savestate);
    save_sect('a', sysacia_savestate);
    save_sect('r', serial_savestate);
    save_sect('F', vdfs_savestate);
    save_sect('5', music5000_savestate);
    if (curtube != -1) {
        save_sect('T', tube_ula_savestate);
        save_zlib('P', tube_proc_savestate);
    }
    fclose(savestate_fp);
    savestate_wantsave = 0;
    savestate_fp = NULL;
}

static void load_state_one(void)
{
    curmodel = getc(savestate_fp);
    selecttube = curtube = -1;
    main_restart();

    m6502_loadstate(savestate_fp);
    mem_loadstate(savestate_fp);
    sysvia_loadstate(savestate_fp);
    uservia_loadstate(savestate_fp);
    videoula_loadstate(savestate_fp);
    crtc_loadstate(savestate_fp);
    video_loadstate(savestate_fp);
    sn_loadstate(savestate_fp);
    adc_loadstate(savestate_fp);
    acia_loadstate(&sysacia, savestate_fp);
    serial_loadstate(savestate_fp);
    vdfs_loadstate(savestate_fp);
    music5000_loadstate(savestate_fp);

    log_debug("savestate: loaded V1 snapshot file");
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
    int res;
    size_t chunk;

    zfp->zs.next_out = dest;
    zfp->zs.avail_out = size;
    do {
        if (zfp->zs.avail_in == 0) {
            if (zfp->togo > BUFSIZ) {
                zfp->flush = Z_NO_FLUSH;
                chunk = BUFSIZ;
            }
            else {
                zfp->flush = Z_FINISH;
                chunk = zfp->togo;
            }
            fread(zfp->buf, chunk, 1, savestate_fp);
            zfp->zs.next_in = zfp->buf;
            zfp->zs.avail_in = chunk;
            zfp->togo -= chunk;
        }
        res = inflate(&zfp->zs, zfp->flush);
    } while (res == Z_OK && zfp->zs.avail_out > 0);
}

static void load_state_two(void)
{
    int ch;
    long start, end, size;

    while ((ch = getc(savestate_fp)) != EOF) {
        size = getc(savestate_fp);
        size |= getc(savestate_fp) << 8;
        size |= getc(savestate_fp) << 16;
        start = ftell(savestate_fp);
        log_debug("savestate: found section %c of %ld bytes", ch, size);

        switch(ch) {
            case 'm':
                model_loadstate(savestate_fp);
                break;
            case '6':
                m6502_loadstate(savestate_fp);
                break;
            case 'M':
                load_zlib(size, mem_loadzlib);
                break;
            case 'S':
                sysvia_loadstate(savestate_fp);
                break;
            case 'U':
                uservia_loadstate(savestate_fp);
                break;
            case 'V':
                videoula_loadstate(savestate_fp);
                break;
            case 'C':
                crtc_loadstate(savestate_fp);
                break;
            case 'v':
                video_loadstate(savestate_fp);
                break;
            case 's':
                sn_loadstate(savestate_fp);
                break;
            case 'A':
                adc_loadstate(savestate_fp);
                break;
            case 'a':
                sysacia_loadstate(savestate_fp);
                break;
            case 'r':
                serial_loadstate(savestate_fp);
                break;
            case 'F':
                vdfs_loadstate(savestate_fp);
                break;
            case '5':
                music5000_loadstate(savestate_fp);
                break;
            case 'T':
                if (curtube != -1)
                    tube_ula_loadstate(savestate_fp);
                break;
            case 'P':
                if (tube_proc_loadstate)
                    load_zlib(size, tube_proc_loadstate);
                break;
        }
        end = ftell(savestate_fp);
        if (end == start) {
            log_warn("savestate: section %c skipped", ch);
            fseek(savestate_fp, size, SEEK_CUR);
        }
        else if (size != (end - start)) {
            log_warn("savestate: section %c, size mismatch, file=%ld, read=%ld", ch, size, end - start);
            fseek(savestate_fp, start + size, SEEK_SET);
        }
    }
    log_debug("savestate: loaded V2 snapshot file");
}

void savestate_doload(void)
{
    switch(savestate_wantload) {
        case 1:
            load_state_one();
            break;
        case 2:
            load_state_two();
            break;
    }
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
