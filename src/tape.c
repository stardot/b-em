#include "b-em.h"
#include "tape.h"
#include "serial.h"
#include "tapenoise.h"
#include "uef.h"
#include "csw.h"

int tapelcount,tapellatch;

bool tape_loaded = false;
bool fasttape = false;
ALLEGRO_PATH *tape_fn = NULL;

static struct
{
        char *ext;
        void (*load)(const char *fn);
        void (*close)();
}
loaders[]=
{
        {"UEF", uef_load, uef_close},
        {"CSW", csw_load, csw_close},
        {0,0,0}
};

static int tape_loader;

void tape_load(ALLEGRO_PATH *fn)
{
        int c = 0;
        const char *p, *cpath;

        if (!fn) return;
        p = al_get_path_extension(fn);
        if (!p || !*p) return;
        if (*p == '.')
            p++;
        cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
        log_info("tape: Loading %s %s", cpath, p);
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext))
                {
                        tape_loader = c;
                        loaders[c].load(cpath);
                        return;
                }
                c++;
        }
        tape_loaded = 0;
}

void tape_close()
{
        if (tape_loaded && tape_loader < 2)
           loaders[tape_loader].close();
        tape_loaded = 0;
}

/*Every 128 clocks, ie 15.625khz*/
/*Div by 13 gives roughly 1200hz*/

static uint16_t newdat;

void tape_poll(void) {
    if (motor) {
        if (csw_ena) csw_poll();
        else         uef_poll();

        if (newdat & 0x100) {
            newdat&=0xFF;
            tapenoise_adddat(newdat);
        }
        else if (csw_toneon || uef_toneon)
            tapenoise_addhigh();
    }
}

void tape_receive(ACIA *acia, uint8_t data) {
    newdat = data | 0x100;
}
