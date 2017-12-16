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
#include "uservia.h"
#include "video.h"
#include "vdfs.h"

int savestate_wantsave, savestate_wantload;
char savestate_name[260];

void savestate_save()
{
//        log_debug("Save state\n");
        savestate_wantsave = 1;
}

void savestate_load()
{
        savestate_wantload = 1;
}

void savestate_dosave()
{
	FILE *f;

        if ((f = fopen(savestate_name, "wb")))
	{
	    log_debug("savestate: saving to '%s'", savestate_name);
	    putc('B', f); putc('E', f); putc('M', f); putc('S', f);
	    putc('N', f); putc('A', f); putc('P', f); putc('1', f);

	    putc(curmodel, f);

	    m6502_savestate(f);
	    mem_savestate(f);
	    sysvia_savestate(f);
	    uservia_savestate(f);
	    videoula_savestate(f);
	    crtc_savestate(f);
	    video_savestate(f);
	    sn_savestate(f);
	    adc_savestate(f);
	    acia_savestate(&sysacia, f);
	    serial_savestate(f);
        vdfs_savestate(f);
        music5000_savestate(f);

	    fclose(f);
	}
	else
	    log_error("savestate: unable to save to state file '%s': %s", savestate_name, strerror(errno));

        savestate_wantsave = 0;
}

void savestate_doload()
{
        int c;
        char id[9];
        FILE *f;

	if ((f = fopen(savestate_name, "rb")))
	{
	     for (c = 0; c < 8; c++) id[c] = getc(f);
	     id[8] = 0;
	     if (strcmp(id, "BEMSNAP1") == 0)
	     {
		 curmodel = getc(f);
		 selecttube = curtube = -1;
		 main_restart();

		 m6502_loadstate(f);
		 mem_loadstate(f);
		 sysvia_loadstate(f);
		 uservia_loadstate(f);
		 videoula_loadstate(f);
		 crtc_loadstate(f);
		 video_loadstate(f);
		 sn_loadstate(f);
		 adc_loadstate(f);
		 acia_loadstate(&sysacia, f);
		 serial_loadstate(f);
         vdfs_loadstate(f);
         music5000_loadstate(f);

		 log_debug("Loadstate done!\n");
	     }
	     else
		 log_error("savestate: %s is not a B-em v2.x save state file", savestate_name);
	     fclose(f);
	}
	else
	    log_error("savestate: unable to load from state file '%s': %s", savestate_name, strerror(errno));

        savestate_wantload = 0;
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
