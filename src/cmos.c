/*B-em v2.2 by Tom Walker
  Master 128 CMOS emulation*/

/*Master 128 uses a HD146818

  It is connected as -

  System VIA PB6 - enable
  System VIA PB7 - address strobe
  IC32 B1        - RW
  IC32 B2        - D
  Slow data bus  - data*/
#include <errno.h>
#include <stdio.h>
#include "b-em.h"
#include "via.h"
#include "sysvia.h"
#include "model.h"
#include "cmos.h"
#include "compactcmos.h"
#include <time.h>

static uint8_t cmos[64];

static int cmos_old, cmos_addr, cmos_ena, cmos_rw;
static time_t rtc_epoc_ref, rtc_epoc_adj, rtc_last;
static struct tm rtc_tm;

static uint8_t cmos_data;

static inline uint8_t bcd2bin(uint8_t value) {
        return ((value >> 4) * 10) + (value & 0xf);
}

static inline uint8_t bin_or_bcd(unsigned value) {
        if (cmos[11] & 4)
                return value; // binary
        return ((value / 10) << 4) | (value % 10);
}

static inline unsigned guess_century(unsigned year) {
        if (year < 80)
                year += 100;
        return year;
}

static void read_cmos_rtc(void)
{
        time_t now;
        struct tm *tp;

        time(&now);
        if (rtc_epoc_ref)
        {
                // The RTC has been set since it was last read so convert
                // the time components set back to seconds since an epoc.

                if (cmos[11] & 4 ) // Register B DM bit.
                {
                        // binary
                        rtc_tm.tm_sec = cmos[0];
                        rtc_tm.tm_min = cmos[2];
                        rtc_tm.tm_hour = cmos[4];
                        rtc_tm.tm_wday = cmos[6] - 1;
                        rtc_tm.tm_mday = cmos[7];
                        rtc_tm.tm_mon = cmos[8] - 1;
                        rtc_tm.tm_year = guess_century(cmos[9]);
                }
                else
                {
                        // BCD mode.
                        rtc_tm.tm_sec = bcd2bin(cmos[0]);
                        rtc_tm.tm_min = bcd2bin(cmos[2]);
                        rtc_tm.tm_hour = bcd2bin(cmos[4]);
                        rtc_tm.tm_wday = bcd2bin(cmos[6] - 1);
                        rtc_tm.tm_mday = bcd2bin(cmos[7]);
                        rtc_tm.tm_mon = bcd2bin(cmos[8] - 1);
                        rtc_tm.tm_year = guess_century(bcd2bin(cmos[9]));
                }
                rtc_epoc_adj = mktime(&rtc_tm) - now;
                rtc_epoc_ref = 0;
                rtc_last = 0;
        }
        now += rtc_epoc_adj;
        if (now > rtc_last && (tp = localtime(&now)))
        {
                rtc_tm = *tp;
                rtc_last = now;
        }
        switch (cmos_addr)
        {
            case 0:
                cmos_data = bin_or_bcd(rtc_tm.tm_sec);
                break;
            case 2:
                cmos_data = bin_or_bcd(rtc_tm.tm_min);
                break;
            case 4:
                cmos_data = bin_or_bcd(rtc_tm.tm_hour);
                break;
            case 6:
                cmos_data = bin_or_bcd(rtc_tm.tm_wday + 1);
                break;
            case 7:
                cmos_data = bin_or_bcd(rtc_tm.tm_mday);
                break;
            case 8:
                cmos_data = bin_or_bcd(rtc_tm.tm_mon + 1);
                break;
            case 9:
                cmos_data = bin_or_bcd(rtc_tm.tm_year % 100);
                break;
            default:
                cmos_data = cmos[cmos_addr];
        }
}

void cmos_update(uint8_t IC32, uint8_t sdbval)
{
        int cmos_strobe;
        cmos_rw = IC32 & 2;
        cmos_strobe = (IC32 & 4) ^ cmos_old;
        cmos_old = IC32 & 4;
//        bem_debugf("CMOS update %i %i %i\n",cmos_rw,cmos_strobe,cmos_old);
        if (cmos_strobe && cmos_ena)
        {
                if (!cmos_rw && !(IC32 & 4)) /*Write triggered on low -> high on D*/
                {
                        cmos[cmos_addr] = sdbval;
                        if (cmos_addr <= 9)
                                time(&rtc_epoc_ref); // RTC: record the actual time this time was set.
                }
                if (cmos_rw && (IC32 & 4))                   /*Read data output while D high*/
                {
                        if (cmos_addr <= 9 )
                                read_cmos_rtc();
                        else
                                cmos_data = cmos[cmos_addr];
                }
        }
}

void cmos_writeaddr(uint8_t val)
{
        if (val&0x80) /*Latch address*/
           cmos_addr = sdbval & 63;
        cmos_ena = val & 0x40;
//        bem_debugf("CMOS writeaddr %02X %02X %02X\n",val,sdbval,cmos_addr);
}

uint8_t cmos_read()
{
//        bem_debugf("CMOS read ORAnh %02X %02X %i %02X %i\n",cmos_addr,cmos[cmos_addr],cmos_ena,IC32,cmos_rw);
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
                bem_debugf("CMOS Opening %s\n", fn);
                f=fopen(fn, "rb");
                if (!f)
                {
                        bem_errorf("unable to load CMOS file %s: %s", fn, strerror(errno));
                        memset(cmos, 0, 64);
                        return;
                }
                fread(cmos, 64, 1, f);
                fclose(f);
        }
        rtc_epoc_ref = rtc_epoc_adj = 0;
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
                bem_debugf("CMOS Opening %s\n", fn);
                if ((f=fopen(fn, "wb")))
                {
                        fwrite(cmos, 64, 1, f);
                        fclose(f);
                }
                else
                        bem_errorf("unable to save CMOS to %s: %s", fn, strerror(errno));
        }
}
