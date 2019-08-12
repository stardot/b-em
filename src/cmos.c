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

static uint8_t read_cmos_rtc(unsigned addr)
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
        switch (addr)
        {
            case 0:
                return bin_or_bcd(rtc_tm.tm_sec);
                break;
            case 2:
                return bin_or_bcd(rtc_tm.tm_min);
                break;
            case 4:
                return bin_or_bcd(rtc_tm.tm_hour);
                break;
            case 6:
                return bin_or_bcd(rtc_tm.tm_wday + 1);
                break;
            case 7:
                return bin_or_bcd(rtc_tm.tm_mday);
                break;
            case 8:
                return bin_or_bcd(rtc_tm.tm_mon + 1);
                break;
            case 9:
                return bin_or_bcd(rtc_tm.tm_year % 100);
                break;
            default:
                return cmos[addr];
        }
}

static uint8_t get_cmos(unsigned addr)
{
    if ((addr <= 6 && !(addr & 1)) || (addr >= 7 && addr <= 9))
        return read_cmos_rtc(addr);
    return cmos[addr];
}

static void set_cmos(unsigned addr, uint8_t val)
{
    cmos[addr] = val;
    if ((addr <= 6 && !(addr & 1)) || (addr >= 7 && addr <= 9))
        time(&rtc_epoc_ref);
}

void cmos_update(uint8_t IC32, uint8_t sdbval)
{
        int cmos_strobe;
        cmos_rw = IC32 & 2;
        cmos_strobe = (IC32 & 4) ^ cmos_old;
        cmos_old = IC32 & 4;
//        log_debug("CMOS update %i %i %i\n",cmos_rw,cmos_strobe,cmos_old);
        if (cmos_strobe && cmos_ena)
        {
                if (!cmos_rw && !(IC32 & 4)) /*Write triggered on low -> high on D*/
                    set_cmos(cmos_addr, sdbval);
                if (cmos_rw && (IC32 & 4))                   /*Read data output while D high*/
                    cmos_data = get_cmos(cmos_addr);
        }
}

void cmos_writeaddr(uint8_t val)
{
        if (val&0x80) /*Latch address*/
           cmos_addr = sdbval & 63;
        cmos_ena = val & 0x40;
//        log_debug("CMOS writeaddr %02X %02X %02X\n",val,sdbval,cmos_addr);
}

uint8_t cmos_read()
{
//        log_debug("CMOS read ORAnh %02X %02X %i %02X %i\n",cmos_addr,cmos[cmos_addr],cmos_ena,IC32,cmos_rw);
        if (cmos_ena && (IC32 & 4) && cmos_rw) return cmos_data; /*To drive bus, CMOS must be enabled,
                                                                   D must be high, RW must be high*/
        return 0xff;
}

void cmos_write_addr_integra(uint8_t val)
{
    log_debug("cmos: write_addr_integra, val=%02X", val);
    cmos_addr = val & 63;
}

void cmos_write_data_integra(uint8_t val)
{
    log_debug("cmos: write_data_integra, val=%02X", val);
    cmos[cmos_addr] = val;
    if (cmos_addr <= 9)
        time(&rtc_epoc_ref);
}

static int uip_count = 0;

uint8_t cmos_read_data_integra(void)
{
    if (cmos_addr <= 9 ) {
        read_cmos_rtc();
        log_debug("cmos: read_data_integra, return clock data %02X", cmos_data);
    }
    else {
        cmos_data = cmos[cmos_addr];
        if (cmos_addr == 0x0a) {
            cmos_data &= 0x7f;
            if (++uip_count == 100) {
                cmos_data |= 0x80;
                uip_count = 0;
            }
            log_debug("cmos: read_data_integra, return register A %02X", cmos_data);
        }
        else
            log_debug("cmos: integra, return RAM data %02X", cmos_data);
    }
    return cmos_data;
}

void cmos_load(MODEL m) {
    FILE *f;
    ALLEGRO_PATH *path;
    const char *cpath;

    if (!m.cmos[0])
        return;
    if (m.compact)
        compactcmos_load(m);
    else {
        memset(cmos, 0, sizeof cmos);
        rtc_epoc_ref = rtc_epoc_adj = 0;
        if ((path = find_cfg_file(m.cmos, ".bin"))) {
            cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "rb"))) {
                size_t nbytes = fread(cmos, 1, sizeof cmos, f);
                fclose(f);
                if (nbytes < sizeof cmos)
                    log_warn("cmos: cmos file %s read incompletely, some values will be zero", cpath);
                if (nbytes >= 10) {
                    /* Reload the Epoch to which system time can be
                     * added from the date/time fields.
                     */
                    rtc_epoc_adj = cmos[0] | (cmos[2] << 8) | (cmos[4] << 16) | (cmos[6] << 24);
                }
                log_debug("cmos: loaded from %s", cpath);
            }
            else
                log_warn("cmos: unable to load CMOS file '%s': %s", cpath, strerror(errno));
            al_destroy_path(path);
        }
        else
            log_warn("cmos: CMOS file %s not found", m.cmos);
    }
}

void cmos_save(MODEL m) {
    FILE *f;
    ALLEGRO_PATH *path;
    const char *cpath;

    if (!m.cmos[0])
        return;
    if (m.compact)
        compactcmos_save(m);
    else {
        if ((path = find_cfg_dest(m.cmos, ".bin"))) {
            cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "wb"))) {
                log_debug("cmos: saving to %s", cpath);
                /* Save into the date/time fields of the CMOS RAM the
                 * Epoch to which standard system time can be added
                 */
                cmos[0] = rtc_epoc_adj & 0xff;
                cmos[2] = (rtc_epoc_adj >> 8) & 0xff;
                cmos[4] = (rtc_epoc_adj >> 16) & 0xff;
                cmos[6] = (rtc_epoc_adj >> 24) & 0xff;
                fwrite(cmos, sizeof cmos, 1, f);
                fclose(f);
            }
            else
                log_error("unable to save CMOS file %s: %s", cpath, strerror(errno));
            al_destroy_path(path);
        } else
            log_error("unable to save CMOS file %s: no suitable destination", m.cmos);
    }
}
