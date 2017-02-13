/*B-em v2.2 by Tom Walker
  Master 128 CMOS emulation*/
  
/*Master 128 uses a HD146818

  It is connected as -
  
  System VIA PB6 - enable
  System VIA PB7 - address strobe
  IC32 B1        - RW
  IC32 B2        - D
  Slow data bus  - data*/
#include <stdio.h>
#include "b-em.h"
#include "via.h"
#include "sysvia.h"
#include "model.h"
#include "cmos.h"
#include "compactcmos.h"

static uint8_t cmos[64];

static int cmos_old, cmos_addr, cmos_ena, cmos_rw;

static uint8_t cmos_data;

void cmos_update(uint8_t IC32, uint8_t sdbval)
{
        int cmos_strobe;
        cmos_rw = IC32 & 2;
        cmos_strobe = (IC32 & 4) ^ cmos_old;
        cmos_old = IC32 & 4;
//        rpclog("CMOS update %i %i %i\n",cmos_rw,cmos_strobe,cmos_old);
        if (cmos_strobe && cmos_ena)
        {
                if (!cmos_rw && cmos_addr > 0xB && !(IC32 & 4)) /*Write triggered on low -> high on D*/
                   cmos[cmos_addr] = sdbval;
                   
                if (cmos_rw && (IC32 & 4))                    /*Read data output while D high*/
                   cmos_data = cmos[cmos_addr];
        }
}

void cmos_writeaddr(uint8_t val)
{
        if (val&0x80) /*Latch address*/
           cmos_addr = sdbval & 63;
        cmos_ena = val & 0x40;
//        rpclog("CMOS writeaddr %02X %02X %02X\n",val,sdbval,cmos_addr);
}

uint8_t cmos_read()
{
//        rpclog("CMOS read ORAnh %02X %02X %i %02X %i\n",cmos_addr,cmos[cmos_addr],cmos_ena,IC32,cmos_rw);
        if (cmos_ena && (IC32 & 4) && cmos_rw) return cmos_data; /*To drive bus, CMOS must be enabled,
                                                                   D must be high, RW must be high*/
        return 0xff;
}

void cmos_load(MODEL m)
{
        FILE *f;
        char fn[512];
        if (!m.cmos[0]) return;
        if (m.compact) compactcmos_load(m);
        else
        {
                sprintf(fn, "%s%s", exedir, m.cmos);
                rpclog("CMOS Opening %s\n", fn);
                f=x_fopen(fn, "rb");
                if (!f)
                {
                        memset(cmos, 0, 64);
                        return;
                }
                fread(cmos, 64, 1, f);
                fclose(f);
        }
}

void cmos_save(MODEL m)
{
        FILE *f;
        char fn[512];
        if (!m.cmos[0]) return;
        if (m.compact) compactcmos_save(m);
        else
        {
                sprintf(fn, "%s%s", exedir, m.cmos);
                rpclog("CMOS Opening %s\n", fn);
                f=x_fopen(fn, "wb");
                fwrite(cmos, 64, 1, f);
                fclose(f);
        }
}
