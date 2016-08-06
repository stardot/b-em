#include <allegro.h>
#include "b-em.h"
#include "tape.h"
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
        if (!p) return;
        bem_debugf("Loading %s %s\n", fn, p);
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
        {
                tape_loaded = 0;
                loaders[tape_loader].close();
        }
}
