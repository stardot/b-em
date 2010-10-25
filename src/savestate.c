/*B-em v2.1 by Tom Walker
  Savestate handling*/
#include <stdio.h>
#include "b-em.h"

int wantloadstate,wantsavestate;
char ssname[260];

void savestate()
{
//        rpclog("Save state\n");
        wantsavestate=1;
}

void loadstate()
{
        wantloadstate=1;
}

void dosavestate()
{
        FILE *f;
        f=fopen(ssname,"wb");
//        rpclog("DoSave state\n");
        putc('B',f); putc('E',f); putc('M',f); putc('S',f);
        putc('N',f); putc('A',f); putc('P',f); putc('1',f);

        putc(curmodel,f);

        save6502state(f);
        savememstate(f);
        savesysviastate(f);
        saveuserviastate(f);
        savevideoulastate(f);
        savecrtcstate(f);
        savesoundstate(f);
        saveadcstate(f);
        saveaciastate(f);
        saveserialulastate(f);

        fclose(f);

        wantsavestate=0;
}

void doloadstate()
{
        int c;
        char id[9];
        FILE *f=fopen(ssname,"rb");
        for (c=0;c<8;c++) id[c]=getc(f);
        id[8]=0;
        if (strcmp(id,"BEMSNAP1"))
        {
                fclose(f);
                bem_error("Not a B-em v2.1 save state.");
                return;
        }

        curmodel=getc(f);
        selecttube=curtube=-1;
        rpclog("Restart BBC\n");
        restartbbc();
        rpclog("Done!\n");

        load6502state(f);
        loadmemstate(f);
        loadsysviastate(f);
        loaduserviastate(f);
        loadvideoulastate(f);
        loadcrtcstate(f);
        loadsoundstate(f);
        loadadcstate(f);
        loadaciastate(f);
        loadserialulastate(f);

        rpclog("Loadstate done!\n");

        fclose(f);

        wantloadstate=0;
}
