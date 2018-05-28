#include "b-em.h"

#include "main.h"
#include "model.h"
#include "config.h"
#include "cmos.h"
#include "mem.h"
#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "wd1770.h"
#include "x86_tube.h"
#include "z80.h"

fdc_type_t fdc_type;
bool BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;
int curtube;
int oldmodel, model_count;
MODEL *models;
ALLEGRO_PATH *tube_dir;

static const char fdc_names[FDC_MAX][8] =
{
    "none",
    "i8271",
    "acorn",
    "master",
    "opus",
    "stl",
    "watford"
};

#define NUM_ROM_SETUP 5
static rom_setup_t rom_setups[NUM_ROM_SETUP] =
{
    { "swram",   mem_romsetup_swram   },
    { "os01",    mem_romsetup_os01    },
    { "std",     mem_romsetup_std     },
    { "bp128",   mem_romsetup_bp128   },
    { "master",  mem_romsetup_master  }
};

/*
 * The number of tube cycles to run for each core 6502 processor cycle
 * is calculated by mutliplying the multiplier in this table with the
 * one in the general tube speed table and dividing by two.
 */

extern cpu_debug_t n32016_cpu_debug;

TUBE tubes[NUM_TUBES]=
{
    {"6502 Internal",  tube_6502_init,  tube_6502_reset, &tube6502_cpu_debug,  "6502Intern",       4 },
    {"ARM",            tube_arm_init,   arm_reset,       &tubearm_cpu_debug,   "ARMeval_100",      4 },
    {"Z80",            tube_z80_init,   z80_reset,       &tubez80_cpu_debug,   "Z80_120",          8 },
    {"80186",          tube_x86_init,   x86_reset,       &tubex86_cpu_debug,   "BIOS",             8 },
    {"65816",          tube_65816_init, w65816_reset,    &tube65816_cpu_debug, "ReCo6502ROM_816", 16 },
    {"32016",          tube_32016_init, n32016_reset,    &n32016_cpu_debug,    "",                 8 },
    {"6502 External",  tube_6502_init,  tube_6502_reset, &tube6502_cpu_debug,  "6502Tube",         3 }
};

static fdc_type_t model_find_fdc(const char *name, const char *model)
{
    fdc_type_t fdc_type;

    for (fdc_type = FDC_NONE; fdc_type < FDC_MAX; fdc_type++)
        if (strcmp(fdc_names[fdc_type], name) == 0)
            return fdc_type;
    log_warn("model: invalid fdc type '%s' in model '%s', using 'none' instead", name, model);
    return FDC_NONE;
}

static rom_setup_t *model_find_romsetup(const char *name, const char *model)
{
    for (int i = 0; i < NUM_ROM_SETUP; i++)
        if (strcmp(rom_setups[i].name, name) == 0)
            return rom_setups + i;
    log_warn("model: invalid rom setup type '%s' in model '%s', using 'swram' instead", name, model);
    return rom_setups;
}

static int model_find_tube(const char *name, const char *model)
{
    if (strcmp(name, "none")) {
        for (int i = 0; i < NUM_TUBES; i++)
            if (strcmp(tubes[i].name, name) == 0)
                return i;
        log_warn("model: invalid tube name '%s' in model '%s', no tube will be used", name, model);
    }
    return -1;
}

void model_loadcfg(void)
{
    ALLEGRO_CONFIG_SECTION *siter;
    const char *sect;
    int num, max = -1;
    MODEL *ptr;

    for (sect = al_get_first_config_section(bem_cfg, &siter); sect; sect = al_get_next_config_section(&siter)) {
        if (strncmp(sect, "model_", 6) == 0) {
            num = atoi(sect+6);
            log_debug("model: pass1, found model#%02d", num);
            if (num > max)
                max = num;
        }
    }
    if (max < 0) {
        log_fatal("model: no models defined in config file");
        exit(1);
    }
    if (!(models = malloc(++max * sizeof(MODEL)))) {
        log_fatal("model: out of memory allocating models");
        exit(1);
    }
    for (sect = al_get_first_config_section(bem_cfg, &siter); sect; sect = al_get_next_config_section(&siter)) {
        if (strncmp(sect, "model_", 6) == 0) {
            num = atoi(sect+6);
            log_debug("model: pass2, found model#%02d", num);
            ptr = models + num;
            ptr->cfgsect = sect;
            ptr->name = get_config_string(sect, "name", sect);
            ptr->fdc_type = model_find_fdc(get_config_string(sect, "fdc", "none"), ptr->name);
            ptr->x65c02  = get_config_bool(sect, "65c02",  false);
            ptr->bplus   = get_config_bool(sect, "b+",   false);
            ptr->master  = get_config_bool(sect, "master",  false);
            ptr->modela  = get_config_bool(sect, "modela",  false);
            ptr->os01    = get_config_bool(sect, "os01",    false);
            ptr->compact = get_config_bool(sect, "compact", false);
            ptr->os      = get_config_string(sect, "os", "os12");
            ptr->cmos    = get_config_string(sect, "cmos", "");
            ptr->romsetup = model_find_romsetup(get_config_string(sect, "romsetup", "swram"), ptr->name);
            ptr->tube = model_find_tube(get_config_string(sect, "tube", "none"), ptr->name);
        }
    }
    model_count = max;
}

void model_check(void) {
    const int defmodel = 3;

    if (curmodel < 0 || curmodel >= model_count) {
        log_warn("No model #%d, using #%d (%s) instead", curmodel, defmodel, models[defmodel].name);
        curmodel = defmodel;
    }
    if (models[curmodel].tube != -1)
        curtube = models[curmodel].tube;
    else
        curtube = selecttube;
    if (curtube < -1 || curtube >= NUM_TUBES) {
        log_warn("No tube #%d, running with no tube instead", curtube);
        curtube = -1;
    }
}

static void tube_init(void)
{
    ALLEGRO_PATH *path;
    const char *cpath;
    FILE *romf;
    bool res;

    if (curtube!=-1) {
        if (!tubes[curtube].bootrom[0]) // no boot ROM needed
            tubes[curtube].init(NULL);
        else {
            if (!tube_dir)
                tube_dir = al_create_path_for_directory("roms/tube");
            if ((path = find_dat_file(tube_dir, tubes[curtube].bootrom, ".rom"))) {
                cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
                if ((romf = fopen(cpath, "rb"))) {
                    res = tubes[curtube].init(romf);
                    fclose(romf);
                    if (res) {
                        tube_updatespeed();
                        tube_reset();
                    }
                    else {
                        log_error("model: error reading boot rom %s for tube %s: %s", cpath, tubes[curtube].name, strerror(errno));
                        curtube = -1;
                    }
                } else
                    log_error("model: unable to open boot rom %s for tube %s: %s", cpath, tubes[curtube].name, strerror(errno));
                al_destroy_path(path);
            } else {
                log_error("model: boot rom %s for tube %s not found", tubes[curtube].bootrom, tubes[curtube].name);
                curtube = -1;
            }
        }
    }
}

void model_init()
{
    model_check();

    if (curtube == -1)
        log_info("model: starting emulation as model #%d, %s", curmodel, models[curmodel].name);
    else
        log_info("model: starting emulation as model #%d, %s with tube #%d, %s", curmodel, models[curmodel].name, curtube, tubes[curtube].name);

    fdc_type    = models[curmodel].fdc_type;
    BPLUS       = models[curmodel].bplus;
    x65c02      = models[curmodel].x65c02;
    MASTER      = models[curmodel].master;
    MODELA      = models[curmodel].modela;
    OS01        = models[curmodel].os01;
    compactcmos = models[curmodel].compact;

    mem_clearroms();
    models[curmodel].romsetup->func();
    tube_init();
    cmos_load(models[curmodel]);
}

void model_savestate(FILE *f)
{
    MODEL *model = models + curmodel;
    savestate_save_var(curmodel, f);
    savestate_save_str(model->name, f);
    savestate_save_str(model->os, f);
    savestate_save_str(model->cmos, f);
    savestate_save_str(model->romsetup->name, f);
    savestate_save_str(fdc_names[model->fdc_type], f);
    putc(model->x65c02, f);
    putc(model->bplus, f);
    putc(model->master, f);
    putc(model->modela, f);
    putc(model->os01, f);
    putc(model->compact, f);
    if (model->tube >= 0) {
        putc(1, f);
        savestate_save_str(tubes[model->tube].name, f);
    }
    else if (curtube >= 0) {
        putc(2, f);
        savestate_save_str(tubes[curtube].name, f);
    }
    else
        putc(0, f);
}

static bool model_cmp(int modelno, MODEL *nmodel)
{
    MODEL *tmodel = models + modelno;

    return strcmp(tmodel->name, nmodel->name) == 0 &&
           strcmp(tmodel->os,   nmodel->os)   == 0 &&
           strcmp(tmodel->cmos, nmodel->cmos) == 0 &&
           tmodel->romsetup == nmodel->romsetup    &&
           tmodel->fdc_type == nmodel->fdc_type    &&
           tmodel->x65c02   == nmodel->x65c02      &&
           tmodel->bplus    == nmodel->bplus       &&
           tmodel->master   == nmodel->master      &&
           tmodel->modela   == nmodel->modela      &&
           tmodel->compact  == nmodel->compact     &&
           tmodel->tube     == nmodel->tube;
}

void model_loadstate(FILE *f)
{
    int newmodel, i;
    MODEL model;
    char *rom_setup, *fdc_name, *tube_name;

    newmodel   = savestate_load_var(f);
    model.name = savestate_load_str(f);
    model.os   = savestate_load_str(f);
    model.cmos = savestate_load_str(f);
    rom_setup = savestate_load_str(f);
    model.romsetup = model_find_romsetup(rom_setup, model.name);
    fdc_name = savestate_load_str(f);
    model.fdc_type = model_find_fdc(fdc_name, model.name);
    model.x65c02  = getc(f);
    model.bplus   = getc(f);
    model.master  = getc(f);
    model.modela  = getc(f);
    model.os01    = getc(f);
    model.compact = getc(f);

    switch(getc(f)) {
        case 1:
            tube_name = savestate_load_str(f);
            model.tube = model_find_tube(tube_name, model.name);
            break;
        case 2:
            tube_name = savestate_load_str(f);
            model.tube = -1;
            selecttube = model_find_tube(tube_name, model.name);
            break;
        default:
            tube_name = NULL;
            model.tube = -1;
            selecttube = -1;
    }

    if (newmodel < model_count && model_cmp(newmodel, &model)) {
        log_debug("model: found savestate model at expected model #%d", newmodel);
        curmodel = newmodel;
        free((char *)model.name);
        free((char *)model.os);
        free((char *)model.cmos);
    }
    else {
        for (i = 0; i < model_count; i++)
            if (model_cmp(i, &model))
                break;
        if (i == model_count) {
            curmodel = model_count;
            if (!(models = realloc(models, ++model_count * sizeof(MODEL)))) {
                log_fatal("model: out of memory in model_loadstate");
                exit(1);
            }
            models[curmodel] = model;
            log_debug("model: added savestate model at model #%d", curmodel);
        }
        else {
            log_debug("model: found savestate model in table at model #%d", i);
            curmodel = i;
            free((char *)model.name);
            free((char *)model.os);
            free((char *)model.cmos);
        }
    }
    free(rom_setup);
    free(fdc_name);
    if (tube_name)
        free(tube_name);

    main_restart();
}

void model_savecfg(void) {
    const char *sect = models[curmodel].cfgsect;

    al_set_config_value(bem_cfg, sect, "name", models[curmodel].name);
    mem_save_romcfg(sect);
}
