/*B-em v2.2 by Tom Walker
  Savestate handling*/
#include <stdio.h>
#include "b-em.h"

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

typedef struct {
    char key;
    void (*save_func)(FILE *f);
    void (*load_func)(FILE *f);
} savesect_t;

static void sysacia_savestate(FILE *f) {
    acia_savestate(&sysacia, f);
}

static void sysacia_loadstate(FILE *f) {
    acia_loadstate(&sysacia, f);
}

static void save_sect(int key, void (*save_func)(FILE *f))
{
    long start, end, size;

    putc(key, savestate_fp);
    start = ftell(savestate_fp);
    fseek(savestate_fp, 3, SEEK_CUR);
    save_func(savestate_fp);
    end = ftell(savestate_fp);
    fseek(savestate_fp, start, SEEK_SET);
    size = end - start - 3;
    log_debug("savestate: section %c saved, %ld bytes", key, size);
    putc(size & 0xff, savestate_fp);
    putc((size >> 8) & 0xff, savestate_fp);
    putc((size >> 16) & 0xff, savestate_fp);
    fseek(savestate_fp, end, SEEK_SET);
}

void savestate_dosave(void)
{
    fwrite("BEMSNAP2", 8, 1, savestate_fp);
    putc(curmodel, savestate_fp);
    putc(curtube, savestate_fp);
    save_sect('6', m6502_savestate);
    save_sect('M', mem_savestate);
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
    save_sect('m', music5000_savestate);
    if (curtube != -1) {
        save_sect('T', tube_ula_savestate);
        save_sect('P', tube_proc_savestate);
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

static void load_state_two(void)
{
    int ch;
    long start, end, size;

    curmodel = getc(savestate_fp);
    selecttube = getc(savestate_fp);
    if (selecttube == 0xff)
        selecttube = -1;
    curtube = selecttube;
    main_restart();

    while ((ch = getc(savestate_fp)) != EOF) {
        size = getc(savestate_fp);
        size |= getc(savestate_fp) << 8;
        size |= getc(savestate_fp) << 16;
        start = ftell(savestate_fp);

        switch(ch) {
            case '6':
                m6502_loadstate(savestate_fp);
                break;
            case 'M':
                mem_loadstate(savestate_fp);
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
            case 'm':
                music5000_loadstate(savestate_fp);
                break;
            case 'T':
                if (curtube != -1)
                    tube_ula_loadstate(savestate_fp);
                break;
            case 'P':
                if (tube_proc_loadstate)
                    tube_proc_loadstate(savestate_fp);
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

    for (;;) {
        byte = var & 0x7f;
        var >>= 7;
        if (var == 0)
            break;
        putc(byte, f);
    }
    putc(byte | 0x80, f);
}

unsigned savestate_load_var(FILE *f) {
    unsigned var, lshift;
    int      ch;

    var = lshift = 0;
    while ((ch = getc(f)) != EOF) {
        if (ch & 0x80)
            return var | ((ch & 0x7f) << lshift);
        var |= ch << lshift;
        lshift += 7;
    }
    return var;
}
