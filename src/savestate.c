/*B-em v2.2 by Tom Walker
  Savestate handling*/
#include <stdio.h>
#include "b-em.h"

#include "6502.h"
#include "acia.h"
#include "adc.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "savestate.h"
#include "serial.h"
#include "sn76489.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"

int savestate_wantsave, savestate_wantload;
char savestate_name[260];

void savestate_save()
{
//        bem_debug("Save state\n");
        savestate_wantsave = 1;
}

void savestate_load()
{
        savestate_wantload = 1;
}

void savestate_dosave()
{
        FILE *f;
        f = fopen(savestate_name, "wb");
//        bem_debug("DoSave state\n");
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
        acia_savestate(f);
        serial_savestate(f);

        fclose(f);

        savestate_wantsave = 0;
}

void savestate_doload()
{
        int c;
        char id[9];
        FILE *f = fopen(savestate_name, "rb");
        for (c = 0; c < 8; c++) id[c] = getc(f);
        id[8] = 0;
        if (strcmp(id, "BEMSNAP1"))
        {
                fclose(f);
                bem_error("Not a B-em v2.x save state.");
                return;
        }

        curmodel = getc(f);
        selecttube = curtube = -1;
        bem_debug("Restart BBC\n");
        main_restart();
        bem_debug("Done!\n");

        m6502_loadstate(f);
        mem_loadstate(f);
        sysvia_loadstate(f);
        uservia_loadstate(f);
        videoula_loadstate(f);
        crtc_loadstate(f);
        video_loadstate(f);
        sn_loadstate(f);
        adc_loadstate(f);
        acia_loadstate(f);
        serial_loadstate(f);

        bem_debug("Loadstate done!\n");

        fclose(f);

        savestate_wantload = 0;
}
