#include <allegro.h>
#include "b-em.h"
#include "tape.h"
#include "serial.h"
#include "tapenoise.h"
#include "uef.h"
#include "csw.h"

int tapelcount,tapellatch;

int tape_loaded = 0;
char tape_fn[260] = "";

static struct
{
        char *ext;
        void (*load)(char *fn);
        void (*close)();
}
loaders[]=
{
        {"UEF", uef_load, uef_close},
        {"CSW", csw_load, csw_close},
        {0,0,0}
};

static int tape_loader;

void tape_load(char *fn)
{
        int c = 0;
        char *p;

        if (!fn) return;
        p = get_extension(fn);
        if (!p || !*p) return;
        log_info("tape: Loading %s %s", fn, p);
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext))
                {
                        tape_loader = c;
                        loaders[c].load(fn);
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
    static uint16_t newdat;

    if (motor) {
        startblit();
        if (csw_ena) csw_poll();
        else         uef_poll();
        endblit();
    
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
