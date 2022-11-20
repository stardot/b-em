#define _DEBUG
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
#include "mem.h"
#include "via.h"
#include "sysvia.h"
#include "model.h"
#include "cmos.h"
#include "compactcmos.h"
#include <time.h>

static const char integra_magic[] = "B-Em Integra-B CMOS 1";

struct cmos_integra {
    char magic[32];
    uint8_t cmos[64];
    char slots[16];
};

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
    if (rtc_epoc_ref) {
        // The RTC has been set since it was last read so convert
        // the time components set back to seconds since an epoc.

        if (cmos[11] & 4 ) { // Register B DM bit.
            // binary
            rtc_tm.tm_sec = cmos[0];
            rtc_tm.tm_min = cmos[2];
            rtc_tm.tm_hour = cmos[4];
            rtc_tm.tm_wday = cmos[6] - 1;
            rtc_tm.tm_mday = cmos[7];
            rtc_tm.tm_mon = cmos[8] - 1;
            rtc_tm.tm_year = guess_century(cmos[9]);
        }
        else {
            // BCD mode.
            rtc_tm.tm_sec = bcd2bin(cmos[0]);
            rtc_tm.tm_min = bcd2bin(cmos[2]);
            rtc_tm.tm_hour = bcd2bin(cmos[4]);
            rtc_tm.tm_wday = bcd2bin(cmos[6] - 1);
            rtc_tm.tm_mday = bcd2bin(cmos[7]);
            rtc_tm.tm_mon = bcd2bin(cmos[8] - 1);
            rtc_tm.tm_year = guess_century(bcd2bin(cmos[9]));
        }
        rtc_tm.tm_isdst = -1;
        rtc_epoc_adj = mktime(&rtc_tm) - now;
        rtc_epoc_ref = 0;
        rtc_last = 0;
    }
    now += rtc_epoc_adj;
    if (now > rtc_last && (tp = localtime(&now))) {
        rtc_tm = *tp;
        rtc_last = now;
    }
    switch (addr)
    {
        case 0:
            return bin_or_bcd(rtc_tm.tm_sec);
        case 2:
            return bin_or_bcd(rtc_tm.tm_min);
        case 4:
            return bin_or_bcd(rtc_tm.tm_hour);
        case 6:
            return bin_or_bcd(rtc_tm.tm_wday + 1);
        case 7:
            return bin_or_bcd(rtc_tm.tm_mday);
        case 8:
            return bin_or_bcd(rtc_tm.tm_mon + 1);
        case 9:
            return bin_or_bcd(rtc_tm.tm_year % 100);
        default:
            return cmos[addr];
    }
}

static uint8_t get_cmos(unsigned addr)
{
    if ((addr <= 6 && !(addr & 1)) || (addr >= 7 && addr <= 9))
        return read_cmos_rtc(addr);
    else {
        uint8_t value = cmos[addr];
        if (addr == 0x1e && autoboot)
            value |= 0x10;
        return value;
    }
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
    // log_debug("CMOS update %i %i %i\n",cmos_rw,cmos_strobe,cmos_old);
    if (cmos_strobe && cmos_ena) {
        if (!cmos_rw && !(IC32 & 4))        /*Write triggered on low -> high on D*/
            set_cmos(cmos_addr, sdbval);
        if (cmos_rw && (IC32 & 4))          /*Read data output while D high*/
            cmos_data = get_cmos(cmos_addr);
    }
}

void cmos_writeaddr(uint8_t val)
{
    if (val&0x80) /*Latch address*/
        cmos_addr = sdbval & 63;
    cmos_ena = val & 0x40;
    // log_debug("CMOS writeaddr %02X %02X %02X\n",val,sdbval,cmos_addr);
}

uint8_t cmos_read()
{
    // log_debug("CMOS read ORAnh %02X %02X %i %02X %i\n",cmos_addr,cmos[cmos_addr],cmos_ena,IC32,cmos_rw);
    if (cmos_ena && (IC32 & 4) && cmos_rw)  // To drive bus, CMOS must be enabled,
        return cmos_data;                   // D must be high, RW must be high.
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
    set_cmos(cmos_addr, val);
}

static int uip_count = 0;

uint8_t cmos_read_data_integra(void)
{
    unsigned addr = cmos_addr;
    uint8_t val;

    if ((addr <= 6 && !(addr & 1)) || (addr >= 7 && addr <= 9)) {
        val = read_cmos_rtc(addr);
        log_debug("cmos: read_data_integra, return clock data %02X", val);
    }
    else {
        val = cmos[addr];
        if (addr == 0x0a) {
            val &= 0x7f;
            if (++uip_count == 100) {
                val |= 0x80;
                uip_count = 0;
                log_debug("cmos: read_data_integra, faking update");
            }
            log_debug("cmos: read_data_integra, return register A %02X", val);
        }
        else
            log_debug("cmos: read_data_integra, return RAM data %02X", val);
    }
    return val;
}

void cmos_reset(void)
{
    cmos[0xb] &= 0x87; /* clear bits in register B */
    cmos[0xc] = 0;
}

static size_t load_integra(FILE *f, const char *fn)
{
    struct cmos_integra ci;
    if (fread(&ci, sizeof(ci), 1, f) != 1)
        log_error("cmos: error reading header/RTC from file '%s'", fn);
    else if (memcmp(ci.magic, integra_magic, sizeof(integra_magic)))
        log_error("cmos: file '%s' is not an integra CMOS file", fn);
    else {
        memcpy(cmos, ci.cmos, sizeof(cmos));
        if (fread(ram + 0x8000, 0x3000, 1, f) != 1)
            log_error("cmos: error reading private RAM from file '%s'", fn);
        else {
            for (int slot = 0; slot < ROM_NSLOT; slot++) {
                if (ci.slots[slot]) {
                    if (rom_slots[slot].swram) {
                        if (fread(rom + slot * ROM_SIZE, ROM_SIZE, 1, f) != 1)
                            log_error("cmos: error reading slot #%dfrom file '%s'", slot, fn);
                    }
                    else
                        fseek(f, ROM_SIZE, SEEK_CUR); // Skip as slot is ROM.
                }
            }
        }
    }
    return sizeof(cmos);
}

void cmos_load(const MODEL *m)
{
    FILE *f;
    ALLEGRO_PATH *path;
    const char *cpath;

    if (!m->cmos[0])
        return;
    if (m->compact)
        compactcmos_load(m);
    else {
        memset(cmos, 0, sizeof cmos);
        rtc_epoc_ref = rtc_epoc_adj = 0;
        if ((path = find_cfg_file(m->cmos, ".bin"))) {
            cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            if ((f = fopen(cpath, "rb"))) {
                size_t nbytes = m->integra ? load_integra(f, cpath) : fread(cmos, 1, sizeof cmos, f);
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
            log_warn("cmos: CMOS file %s not found", m->cmos);
    }
}

static bool rom_is_used(uint8_t *rom)
{
    uint8_t *end = rom + ROM_SIZE;
    while (rom < end)
        if (*rom++)
            return true;
    return false;
}

static void save_integra(FILE *f)
{
    struct cmos_integra ci;
    memcpy(ci.magic, integra_magic, sizeof(integra_magic));
    memcpy(ci.cmos, cmos, sizeof(cmos));
    for (int slot = 0; slot < ROM_NSLOT; slot++)
        ci.slots[slot] = rom_slots[slot].swram && rom_is_used(rom + slot);
    fwrite(&ci, sizeof(ci), 1, f);
    fwrite(ram + 0x8000, 0x3000, 1, f); // 12K private RAM.
    for (int slot = 0; slot < ROM_NSLOT; slot++)
        if (ci.slots[slot])
            fwrite(rom + slot * ROM_SIZE, ROM_SIZE, 1, f);
}

void cmos_save(const MODEL *m) {
    FILE *f;
    ALLEGRO_PATH *path;
    const char *cpath;

    if (!m->cmos[0])
        return;
    if (m->compact)
        compactcmos_save(m);
    else {
        if ((path = find_cfg_dest(m->cmos, ".bin"))) {
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
                if (m->integra)
                    save_integra(f);
                else
                    fwrite(cmos, sizeof cmos, 1, f);
                fclose(f);
            }
            else
                log_error("unable to save CMOS file %s: %s", cpath, strerror(errno));
            al_destroy_path(path);
        } else
            log_error("unable to save CMOS file %s: no suitable destination", m->cmos);
    }
}
