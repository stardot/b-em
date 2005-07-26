/*B-em 0.81 by Tom Walker*/
/*Snapshot loading and saving*/

#include <stdio.h>

void savestate(char *fn)
{
        char s[9]="BEMSNAP1";
        FILE *f=fopen(fn,"wb");
        fwrite(s,8,1,f);
        savemachinestate(f);
        save6502state(f);
        savecrtcstate(f);
        saveulastate(f);
        savesysviastate(f);
        saveuserviastate(f);
        savekeyboardstate(f);
        savecmosstate(f);
        savesoundstate(f);
        saveadcstate(f);
        saveramstate(f);
        fclose(f);
}

void loadstate(char *fn)
{
        char s[9]="BEMSNAP1";
        FILE *f=fopen(fn,"rb");
        fread(s,8,1,f);
        loadmachinestate(f);
        load6502state(f);
        loadcrtcstate(f);
        loadulastate(f);
        loadsysviastate(f);
        loaduserviastate(f);
        loadkeyboardstate(f);
        loadcmosstate(f);
        loadsoundstate(f);
        loadadcstate(f);
        loadramstate(f);
        fclose(f);
}
