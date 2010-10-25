/*B-em v2.1 by Tom Walker
  Master 128 CMOS emulation*/
#include <stdio.h>
#include "b-em.h"

uint8_t cmos[64];

int cmosrw,cmosstrobe,cmosold,cmosaddr,cmosena;

void cmosupdate(uint8_t IC32, uint8_t sdbval)
{
        cmosrw=IC32&2;
        cmosstrobe=(IC32&4)^cmosold;
        cmosold=IC32&4;
//        printf("CMOS update %i %i %i\n",cmosrw,cmosstrobe,cmosold);
        if (cmosstrobe && cmosena && !cmosrw && cmosaddr>0xB) cmos[cmosaddr]=sdbval; /*printf("CMOS write %02X %02X\n",cmosaddr,sdbval);*/
        if (cmosena && cmosrw) { sysvia.ora=cmos[cmosaddr]; /*printf("CMOS read %02X %02X\n",cmosaddr,sysvia.ora);*/ }
}

void cmoswriteaddr(uint8_t val)
{
        if (val&0x80) cmosaddr=sysvia.ora&63;
        cmosena=val&0x40;
//        printf("Write CMOS addr %i %02X\n",cmosaddr,val);
}

int cmosenabled()
{
        return cmosena;
}

uint8_t cmosread()
{
//        printf("CMOS read ORAnh %02X %02X\n",cmosaddr,cmos[cmosaddr]);
        return cmos[cmosaddr];
}

void loadcmos(MODEL m)
{
        FILE *f;
        char fn[512];
        if (!m.cmos[0]) return;
        if (m.compact) loadcompactcmos(m);
        else
        {
                sprintf(fn,"%s%s",exedir,m.cmos);
                rpclog("Opening %s\n",fn);
                f=fopen(fn,"rb");
                fread(cmos,64,1,f);
                fclose(f);
        }
}

void savecmos(MODEL m)
{
        FILE *f;
        char fn[512];
        if (!m.cmos[0]) return;
        if (m.compact) savecompactcmos(m);
        else
        {
                sprintf(fn,"%s%s",exedir,m.cmos);
                rpclog("Opening %s\n",fn);
                f=fopen(fn,"wb");
                fwrite(cmos,64,1,f);
                fclose(f);
        }
}
