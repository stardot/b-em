/*B-em v2.2 by Tom Walker
  6502/65c02 host CPU emulation*/

#include "b-em.h"

#include "6502.h"
#include "adc.h"
#include "disc.h"
#include "i8271.h"
#include "ide.h"
#include "mem.h"
#include "model.h"
#include "cmos.h"
#include "mouse.h"
#include "music2000.h"
#include "music5000.h"
#include "paula.h"
#include "serial.h"
#include "scsi.h"
#include "sid_b-em.h"
#include "sound.h"
#include "sysacia.h"
#include "tape.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "wd1770.h"

static int dbg_core6502 = 0;

static int dbg_debug_enable(int newvalue) {
    int oldvalue = dbg_core6502;
    dbg_core6502 = newvalue;
    return oldvalue;
};

uint8_t a, x, y, s;
uint16_t pc;
PREG p;

static inline uint8_t pack_flags(uint8_t flags) {
    if (p.c)
        flags |= 1;
    if (p.z)
        flags |= 2;
    if (p.i)
        flags |= 4;
    if (p.d)
        flags |= 8;
    if (p.v)
        flags |= 0x40;
    if (p.n)
        flags |= 0x80;
    return flags;
}

static inline void unpack_flags(uint8_t flags) {
    p.c = flags & 1;
    p.z = flags & 2;
    p.i = flags & 4;
    p.d = flags & 8;
    p.v = flags & 0x40;
    p.n = flags & 0x80;
}

static uint32_t dbg_reg_get(int which) {
    switch (which)
    {
    case REG_A:
        return a;
    case REG_X:
        return x;
    case REG_Y:
        return y;
    case REG_S:
        return s;
    case REG_P:
        return pack_flags(0x30);
    case REG_PC:
        return pc;
    default:
        log_warn("6502: attempt to get non-existent register");
        return 0;
    }
}

static void dbg_reg_set(int which, uint32_t value) {
    switch (which)
    {
    case REG_A:
        a = (uint8_t)value;
        break;
    case REG_X:
        x = (uint8_t)value;
        break;
    case REG_Y:
        y = (uint8_t)value;
        break;
    case REG_S:
        s = (uint8_t)value;
        break;
    case REG_P:
        unpack_flags((uint8_t)value);
        break;
    case REG_PC:
        pc = (uint16_t)value;
    default:
        log_warn("6502: attempt to get non-existent register");
    }
}

static size_t dbg_reg_print(int which, char *buf, size_t bufsize) {
    switch (which)
    {
    case REG_P:
        return dbg6502_print_flags(&p, buf, bufsize);
        break;
    case REG_PC:
        return snprintf(buf, bufsize, "%04X", pc);
        break;
    default:
        return snprintf(buf, bufsize, "%02X", dbg_reg_get(which));
    }
}

static void dbg_reg_parse(int which, const char *str) {
    uint32_t value = strtol(str, NULL, 16);
    dbg_reg_set(which, value);
}

static size_t dbg_print_addr(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize, bool include_symbols)
{
    size_t ret;
    uint32_t msw = addr & 0xf0000000;

    const char *sym = NULL;

    if (include_symbols && symbol_find_by_addr(cpu->symbols, addr, &sym)) {
        if (msw) {
            ret = snprintf(buf, bufsize, "%1X:%04X \\ (%s)", msw >> 28, addr & 0xFFFF, sym);
        }
        else {
            ret = snprintf(buf, bufsize, "%04X \\ (%s)", addr & 0xFFFF, sym);
        }
    }
    else {
        if (msw) {
            ret = snprintf(buf, bufsize, "%1X:%04X", msw >> 28, addr & 0xFFFF);
        }
        else {
            ret = snprintf(buf, bufsize, "%04X", addr & 0xFFFF);
        }
    }


    if (ret > bufsize)
        return bufsize;
    else
        return ret;
}

static uint32_t do_readmem(uint32_t addr);
static void     do_writemem(uint32_t addr, uint32_t val);
static uint32_t dbg_do_readmem(uint32_t addr);
static void     dbg_do_writemem(uint32_t addr, uint32_t val);
static uint32_t dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize);

static uint16_t pc3, oldpc, oldoldpc;
static uint8_t opcode;

static uint32_t dbg_get_instr_addr(void) {
    return oldpc;
}

static const char *trap_names[] = { "BRK", NULL };

cpu_debug_t core6502_cpu_debug = {
    .cpu_name       = "core6502",
    .debug_enable   = dbg_debug_enable,
    .memread        = dbg_do_readmem,
    .memwrite       = dbg_do_writemem,
    .disassemble    = dbg_disassemble,
    .reg_names      = dbg6502_reg_names,
    .reg_get        = dbg_reg_get,
    .reg_set        = dbg_reg_set,
    .reg_print      = dbg_reg_print,
    .reg_parse      = dbg_reg_parse,
    .get_instr_addr = dbg_get_instr_addr,
    .trap_names     = trap_names,
    .print_addr     = dbg_print_addr
};

static uint32_t dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize) {
  return dbg6502_disassemble(cpu, addr, buf, bufsize, x65c02 ? M65C02 : M6502);
}

int tubecycle;

int output = 0;
static int timetolive = 0;

int cycles;
static int otherstuffcount = 0;
int romsel;

static inline void polltime(int c)
{
    cycles -= c;
    via_poll(&sysvia, c);
    via_poll(&uservia, c);
    video_poll(c, 1);
    otherstuffcount -= c;
    if (motoron) {
        if (fdc_time) {
            fdc_time -= c;
            if (fdc_time <= 0)
                fdc_callback();
        }
        disc_time -= c;
        if (disc_time <= 0) {
            disc_time += 16;
            disc_poll();
        }
    }
    tubecycle += c;
}

static int FEslowdown[8] = { 1, 0, 1, 1, 0, 0, 1, 0 };
static int RAMbank[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static uint8_t *memlook[2][256];
static int memstat[2][256];
static int vis20k = 0;
uint8_t ram1k, ram4k, ram8k;

static uint8_t acccon;

static uint16_t buf_remv = 0xffff;
static uint16_t buf_cnpv = 0xffff;
static unsigned char *clip_paste_str, *clip_paste_ptr;
static int os_paste_ch;

void os_paste_start(char *str)
{
    if (str) {
        if (clip_paste_str)
            al_free(clip_paste_str);
        clip_paste_str = clip_paste_ptr = (unsigned char *)str;
        os_paste_ch = -1;
        log_debug("6502: paste start, clip_paste_str=%p", clip_paste_str);
    }
}

static void os_paste_remv(void)
{
    int ch;

    if (os_paste_ch >= 0) {
        if (p.v)
            a = (uint8_t)os_paste_ch;
        else {
            a = y = (uint8_t)os_paste_ch;
            os_paste_ch = -1;
        }
    }
    else {
        do {
            ch = *clip_paste_ptr++;
            if (!ch) {
                al_free(clip_paste_str);
                clip_paste_str = clip_paste_ptr = NULL;
                opcode = readmem(pc);
                return;
            }
            if (ch == 0xc2 && *clip_paste_ptr == 0xa3) {
                ch = 0x60; // convert UTF-8 pound into BBC pound.
                clip_paste_ptr++;
            }
            else if (ch == 0x0d && *clip_paste_ptr == 0x0a)
                clip_paste_ptr++;
            else if (ch == 0x0a)
                ch = 0x0d;
        } while (ch >= 128);
        if (p.v)
            a = os_paste_ch = ch;
        else
            a = y = (uint8_t)ch;
    }
    p.c = 0;
    opcode = 0x60; // RTS
}

static void os_paste_cnpv(void)
{
    if (!p.v && !p.c) {
        int len = strlen((char *)clip_paste_ptr);
        x = len & 0xff;
        y = len >> 8;
        opcode = 0x60; // RTS
        return;
    }
    opcode = readmem(pc);
}

static inline uint32_t debug_addr(uint32_t addr)
{
    if (addr >= 0x8000 && addr < 0xC000)
        addr |= ram_fe30 << 28;
    return addr;
}

static inline void fetch_opcode(void)
{
    pc3 = oldoldpc;
    oldoldpc = oldpc;
    oldpc = pc;
    vis20k = RAMbank[pc >> 12];

    if (dbg_core6502)
        debug_preexec(&core6502_cpu_debug, debug_addr(pc));
    if (pc == buf_remv && x == 0 && clip_paste_ptr)
        os_paste_remv();
    else if (pc == buf_cnpv && x == 0 && clip_paste_ptr)
        os_paste_cnpv();
    else
        opcode = readmem(pc);
    pc++;
}

static inline uint16_t read_zp_indirect(uint16_t zp)
{
    return readmem(zp & 0xff) + (readmem((zp + 1) & 0xff) << 8);
}

static uint32_t dbg_do_readmem(uint32_t addr) {
    uint32_t romno = addr & 0xF0000000;
    addr = addr & 0xFFFF;
    if (romno && addr >= 0x8000 && addr < 0xC000) {
            return rom[(romno >> 14) + addr - 0x8000];
    }
    else
        return do_readmem(addr);
}

static uint32_t do_readmem(uint32_t addr)
{
    addr &= 0xffff;

        if (pc == addr)
            fetchc[addr] = 31;
        else
            readc[addr] = 31;

        if (memstat[vis20k][addr >> 8])
                return memlook[vis20k][addr >> 8][addr];
        if (MASTER && (acccon & 0x40) && addr >= 0xFC00)
                return os[addr & 0x3FFF];
        if (addr < 0xFE00 || FEslowdown[(addr >> 5) & 7]) {
                if (cycles & 1) {
                        polltime(2);
                } else {
                        polltime(1);
                }
        }

        if (sound_paula) {
            if (addr >= 0xFCFD && addr <= 0xFDFF) {
                uint8_t r;
                if (paula_read(addr, &r))
                    return r;
            }
        }


        switch (addr & ~3) {
            case 0xFC08:
            case 0xFC0C:
                if (sound_music5000)
                    return music2000_read(addr);
                break;
        case 0xFC20:
        case 0xFC24:
        case 0xFC28:
        case 0xFC2C:
        case 0xFC30:
        case 0xFC34:
        case 0xFC38:
        case 0xFC3C:
                if (sound_beebsid)
                        return sid_read((uint16_t)addr);
                break;

        case 0xFC40:
        case 0xFC44:
        case 0xFC48:
        case 0xFC4C:
        case 0xFC50:
        case 0xFC54:
        case 0xFC58:
                if (scsi_enabled)
                        return scsi_read((uint16_t)addr);
                if (ide_enable)
                        return ide_read((uint16_t)addr);
                break;

        case 0xFC5C:
                return vdfs_read((uint16_t)addr);
                break;

        case 0xFE00:
        case 0xFE04:
                return crtc_read((uint16_t)addr);

        case 0xFE08:
        case 0xFE0C:
                return acia_read(&sysacia, (uint16_t)addr);

        case 0xFE10:
        case 0xFE14:
                return serial_read((uint16_t)addr);

        case 0xFE18:
                if (MASTER)
                        return adc_read((uint16_t)addr);
                break;

        case 0xFE24:
        case 0xFE28:
                if (MASTER)
                        return wd1770_read((uint16_t)addr);
                break;

        case 0xFE34:
                if (MASTER)
                        return acccon;
                break;

        case 0xFE3C:
            if (integra)
                return cmos_read_data_integra();

        case 0xFE40:
        case 0xFE44:
        case 0xFE48:
        case 0xFE4C:
        case 0xFE50:
        case 0xFE54:
        case 0xFE58:
        case 0xFE5C:
                return sysvia_read((uint16_t)addr);

        case 0xFE60:
        case 0xFE64:
        case 0xFE68:
        case 0xFE6C:
        case 0xFE70:
        case 0xFE74:
        case 0xFE78:
        case 0xFE7C:
                return uservia_read((uint16_t)addr);

        case 0xFE80:
        case 0xFE84:
        case 0xFE88:
        case 0xFE8C:
        case 0xFE90:
        case 0xFE94:
        case 0xFE98:
        case 0xFE9C:
            switch(fdc_type) {
                case FDC_NONE:
                case FDC_MASTER:
                    break;
                case FDC_I8271:
                    return i8271_read((uint16_t)addr);
                default:
                    return wd1770_read((uint16_t)addr);
            }
            break;

        case 0xFEC0:
        case 0xFEC4:
        case 0xFEC8:
        case 0xFECC:
        case 0xFED0:
        case 0xFED4:
        case 0xFED8:
        case 0xFEDC:
                if (!MASTER)
                        return adc_read((uint16_t)addr);
                break;

        case 0xFEE0:
        case 0xFEE4:
        case 0xFEE8:
        case 0xFEEC:
        case 0xFEF0:
        case 0xFEF4:
        case 0xFEF8:
        case 0xFEFC:
                return tube_host_read((uint16_t)addr);
        }
        if (addr >= 0xFC00 && addr < 0xFE00)
                return 0xFF;
        return addr >> 8;
}

uint8_t readmem(uint16_t addr)
{
    uint32_t value = do_readmem(addr);
    if (dbg_core6502)
        debug_memread(&core6502_cpu_debug, debug_addr(addr), value, 1);
        //TODO: check why?debug_memread(&core6502_cpu_debug, addr, debug_addr(value), 1);
    return (uint8_t)value;
}

static void dbg_do_writemem(uint32_t addr, uint32_t val) {
    uint32_t romno = addr & 0xF0000000;
    addr = addr & 0xFFFF;
    if (romno && addr >= 0x8000 && addr < 0xC000) {
        if (rom_slots[romno >> 28].swram)
            rom[(romno >> 14) + addr - 0x8000] = (uint8_t)val;
    }
    else
        do_writemem(addr, val);
}

static inline void shadow_mem(int enabled)
{
    uint8_t *base = enabled ? ram + 0x8000 : ram;
    for (int c = 0x30; c < 0x80; c++)
        memlook[0][c] = base;
}

static inline void page_rom(int rom_no, int rom_sel, int start, int end)
{
    int memtype = rom_slots[rom_no].swram ? 1 : 2;
    uint8_t *base = rom + rom_sel - 0x8000;
    for (int c = start; c < end; c++) {
        memlook[0][c] = memlook[1][c] = base;
        memstat[0][c] = memstat[1][c] = memtype;
    }
}

#define INTEGRA_SHEN   0x80
#define INTEGRA_PRVS1  0x40
#define INTEGRA_PRVS4  0x20
#define INTEGRA_PRVS8  0x10

#define INTEGRA_MEMSEL 0x80
#define INTEGRA_PRVEN  0x40

static void write_romsel(int val)
{
    ram1k = ram4k = ram8k = 0;
    int rom_no = val & 0x0f;
    romsel = rom_no << 14;
    page_rom(rom_no, romsel, 0x80, 0xc0);
    if (MASTER)
        ram4k = val & 0x80;
    else if (BPLUS)
        ram4k = ram8k = (val & 0x80);
    else if (integra) {
        if ((val ^ ram_fe30) & INTEGRA_MEMSEL)
            shadow_mem(!(val & INTEGRA_MEMSEL) && (ram_fe34 & INTEGRA_SHEN));
        if (val & INTEGRA_PRVEN) {
            ram8k = ram_fe34 & INTEGRA_PRVS8;
            ram4k = ram_fe34 & INTEGRA_PRVS4;
            ram1k = ram_fe34 & INTEGRA_PRVS1;
        }
    }
    RAMbank[0xA] = ram8k;
    if (ram4k) {
        for (int c = 0x80; c < 0x90; c++) {
            memlook[0][c] = memlook[1][c] = ram;
            memstat[0][c] = memstat[1][c] = 1;
        }
    }
    else if (ram1k) {
        for (int c = 0x80; c < 0x84; c++) {
            memlook[0][c] = memlook[1][c] = ram;
            memstat[0][c] = memstat[1][c] = 1;
        }
    }
    if (ram8k) {
        for (int c = 0x90; c < 0xb0; c++) {
            memlook[0][c] = memlook[1][c] = ram;
            memstat[0][c] = memstat[1][c] = 1;
        }
    }
    ram_fe30 = val;
}

static void write_acccon_master(int val)
{
    acccon = val;
    int changes = val ^ ram_fe34;
    vidbank = (val & 1) ? 0x8000 : 0;
    if (val & 2)
        RAMbank[0xC] = RAMbank[0xD] = 1;
    else
        RAMbank[0xC] = RAMbank[0xD] = 0;
    if (changes & 4)
        shadow_mem(val & 4);
    if (changes & 8) {
        if (val & 8) { /* 8K filing system RAM */
            uint8_t *base = ram - 0x3000;
            for (int c = 0xc0; c < 0xe0; c++) {
                memlook[0][c] = memlook[1][c] = base;
                memstat[0][c] = memstat[1][c] = 1;
            }
        }
        else {
            uint8_t *base = os - 0xC000;
            for (int c = 0xc0; c < 0xe0; c++) {
                memlook[0][c] = memlook[1][c] = base;
                memstat[0][c] = memstat[1][c] = 2;
            }
        }
    }
}

static void write_acccon_bplus(int val)
{
    acccon = val;
    vidbank = (val & 0x80) << 8;
    if (val & 0x80)
        RAMbank[0xC] = RAMbank[0xD] = 1;
    else
        RAMbank[0xC] = RAMbank[0xD] = 0;
}

static void write_acccon_integra(int val)
{
    int changes = val ^ ram_fe34;
    if (changes & INTEGRA_SHEN)
        shadow_mem((val & INTEGRA_SHEN) && !(ram_fe30 & INTEGRA_MEMSEL));
    if (changes & (INTEGRA_PRVS1|INTEGRA_PRVS4|INTEGRA_PRVS8)) {
        if (val & INTEGRA_PRVS4) {
            for (int c = 0x80; c < 0x90; c++) {
                memlook[0][c] = memlook[1][c] = ram;
                memstat[0][c] = memstat[1][c] = 1;
            }
        }
        else if (val & INTEGRA_PRVS1) {
            for (int c = 0x80; c < 0x84; c++) {
                memlook[0][c] = memlook[1][c] = ram;
                memstat[0][c] = memstat[1][c] = 1;
            }
            page_rom(ram_fe30 & 0x0f, romsel, 0x84, 0x90);
        }
        else
            page_rom(ram_fe30 & 0x0f, romsel, 0x80, 0x90);

        if (val & INTEGRA_PRVS8) {
            for (int c = 0x90; c < 0xb0; c++) {
                memlook[0][c] = memlook[1][c] = ram;
                memstat[0][c] = memstat[1][c] = 1;
            }
        }
        else
            page_rom(ram_fe30 & 0x0f, romsel, 0x90, 0xb0);
    }
}

static void write_fe34(int val)
{
    if (MASTER) {
        write_acccon_master(val);
        ram_fe34 = val;
    }
    else if (BPLUS) {
        write_acccon_bplus(val);
        ram_fe34 = val;
    }
    else if (integra) {
        write_acccon_integra(val);
        ram_fe34 = val;
    }
    else
        write_romsel(val);
}

static void do_writemem(uint32_t addr, uint32_t val)
{
        int c;

    addr &= 0xffff;

        writec[addr] = 31;

        c = memstat[vis20k][addr >> 8];
        if (c == 1) {
            memlook[vis20k][addr >> 8][addr] = (uint8_t)val;
                switch(addr) {
                    case 0x022c:
                        buf_remv = (buf_remv & 0xff00) | val;
                        break;
                    case 0x022d:
                        buf_remv = (buf_remv & 0xff) | (val << 8);
                        break;
                    case 0x022e:
                        buf_cnpv = (buf_cnpv & 0xff00) | val;
                        break;
                    case 0x022f:
                        buf_cnpv = (buf_cnpv & 0xff) | (val << 8);
                        break;
                }
                return;
        } else if (c == 2) {
                log_debug("6502: attempt to write to ROM %x:%04x=%02x\n", vis20k, addr, val);
                return;
        }
        if (addr < 0xFC00 || addr >= 0xFF00)
                return;
        if (addr < 0xFE00 || FEslowdown[(addr >> 5) & 7]) {
                if (cycles & 1) {
                        polltime(2);
                } else {
                        polltime(1);
                }
        }

        if (sound_music5000) {
           if (addr >= 0xFCFF && addr <= 0xFDFF) {
              music5000_write((uint16_t)addr, (uint8_t)val);
              //return -- removed DB need to write to all users of paging register
           }
        }
        if (sound_paula)
        {
            if (addr >= 0xFCFD && addr <= 0xFDFF) {
                paula_write(addr, val);
           }
        }

        switch (addr & ~3) {
            case 0xFC08:
            case 0xFC0C:
                if (sound_music5000)
                    music2000_write(addr, (uint8_t)val);
                break;
        case 0xFC20:
        case 0xFC24:
        case 0xFC28:
        case 0xFC2C:
        case 0xFC30:
        case 0xFC34:
        case 0xFC38:
        case 0xFC3C:
                if (sound_beebsid)
                        sid_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFC40:
        case 0xFC44:
        case 0xFC48:
        case 0xFC4C:
        case 0xFC50:
        case 0xFC54:
        case 0xFC58:
                if (scsi_enabled)
                        scsi_write((uint16_t)addr, (uint8_t)val);
                else if (ide_enable)
                        ide_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFC5C:
                vdfs_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE00:
        case 0xFE04:
                crtc_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE08:
        case 0xFE0C:
                acia_write(&sysacia, (uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE10:
        case 0xFE14:
                serial_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE18:
                if (MASTER)
                        adc_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE20:
                videoula_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE24:
                if (MASTER)
                        wd1770_write((uint16_t)addr, (uint8_t)val);
                else
                        videoula_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE28:
                if (MASTER)
                        wd1770_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE30:
            write_romsel(val);
            break;

        case 0xFE34:
            write_fe34(val);
            break;

        case 0xFE38:
            if (integra)
                cmos_write_addr_integra(val);
            else if (!MASTER && !BPLUS)
                write_romsel(val);
            break;

        case 0xFE3C:
            if (integra)
                cmos_write_data_integra(val);
            else if (!MASTER && !BPLUS)
                write_romsel(val);
            break;

        case 0xFE40:
        case 0xFE44:
        case 0xFE48:
        case 0xFE4C:
        case 0xFE50:
        case 0xFE54:
        case 0xFE58:
        case 0xFE5C:
                sysvia_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE60:
        case 0xFE64:
        case 0xFE68:
        case 0xFE6C:
        case 0xFE70:
        case 0xFE74:
        case 0xFE78:
        case 0xFE7C:
                uservia_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFE80:
        case 0xFE84:
        case 0xFE88:
        case 0xFE8C:
        case 0xFE90:
        case 0xFE94:
        case 0xFE98:
        case 0xFE9C:
            switch(fdc_type) {
                case FDC_NONE:
                case FDC_MASTER:
                    break;
                case FDC_I8271:
                    i8271_write((uint16_t)addr, (uint8_t)val);
                    break;
                default:
                    wd1770_write((uint16_t)addr, (uint8_t)val);
            }
            break;

        case 0xFEC0:
        case 0xFEC4:
        case 0xFEC8:
        case 0xFECC:
        case 0xFED0:
        case 0xFED4:
        case 0xFED8:
        case 0xFEDC:
                if (!MASTER)
                        adc_write((uint16_t)addr, (uint8_t)val);
                break;

        case 0xFEE0:
        case 0xFEE4:
        case 0xFEE8:
        case 0xFEEC:
        case 0xFEF0:
        case 0xFEF4:
        case 0xFEF8:
        case 0xFEFC:
                tube_host_write((uint16_t)addr, (uint8_t)val);
                break;
        }
}

void writemem(uint16_t addr, uint8_t val)
{
    if (dbg_core6502)
        debug_memwrite(&core6502_cpu_debug, debug_addr(addr), val, 1);
    do_writemem(addr, val);
}

int nmi, oldnmi, interrupt, takeint;

void m6502_reset(void)
{
        int c;
        for (c = 0; c < 16; c++)
                RAMbank[c] = 0;
        for (c = 0; c < 128; c++)
                memstat[0][c] = memstat[1][c] = 1;
        for (c = 128; c < 256; c++)
                memstat[0][c] = memstat[1][c] = 2;
        for (c = 0; c < 128; c++)
                memlook[0][c] = memlook[1][c] = ram;
        if (MODELA) {
                for (c = 0; c < 64; c++)
                        memlook[0][c] = memlook[1][c] = ram + 16384;
        }
        for (c = 48; c < 128; c++)
                memlook[1][c] = ram + 32768;
        for (c = 128; c < 192; c++)
                memlook[0][c] = memlook[1][c] = rom - 0x8000;
        for (c = 192; c < 256; c++)
                memlook[0][c] = memlook[1][c] = os - 0xC000;
        memstat[0][0xFC] = memstat[0][0xFD] = memstat[0][0xFE] = 0;
        memstat[1][0xFC] = memstat[1][0xFD] = memstat[1][0xFE] = 0;

        cycles = 0;

        pc = readmem(0xFFFC) | (readmem(0xFFFD) << 8);
        p.i = 1;
        nmi = oldnmi = 0;
        output = 0;
        tubecycle = tubecycles = 0;
        log_debug("PC : %04X\n", pc);
}

void dumpregs(void)
{
        log_debug("6502 registers :\n");
        log_debug("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n", a, x, y, s, pc);
        log_debug("Status : %c%c%c%c%c%c\n", (p.n) ? 'N' : ' ', (p.v) ? 'V' : ' ',
               (p.d) ? 'D' : ' ', (p.i) ? 'I' : ' ', (p.z) ? 'Z' : ' ',
               (p.c) ? 'C' : ' ');
        log_debug("ROMSEL %02X\n", romsel >> 14);
}

static inline uint16_t getsw(void)
{
        uint16_t temp = readmem(pc);
        pc++;
        temp |= (readmem(pc) << 8);
        pc++;
        return temp;
}

static void otherstuff_poll(void) {
    otherstuffcount += 128;
    acia_poll(&sysacia);
    if (sound_music5000)
        music2000_poll();
    sound_poll();
    if (!tapelcount) {
        tape_poll();
        tapelcount = tapellatch;
    }
    tapelcount--;
    if (motorspin) {
        motorspin--;
        if (!motorspin)
            fdc_spindown();
    }
    if (ide_count) {
        ide_count -= 200;
        if (ide_count <= 0)
            ide_callback();
    }
    if (adc_time) {
        adc_time--;
        if (!adc_time)
            adc_poll();
    }
    mcount--;
    if (!mcount) {
        mcount = 6;
        mouse_poll();
    }
}

#define getw() getsw()

static inline void setzn(uint8_t v)
{
    p.z = !v;
    p.n = (v) & 0x80;
}

static inline void push(uint8_t v)
{
    writemem(0x100 + s--, v);
}

static inline uint8_t pull(void)
{
    return readmem(0x100 + ++s);
}

static inline void adc_nmos(uint8_t temp)
{
    int al, ah;
    uint8_t tempb;
    int16_t tempw;

    if (p.d) {
        ah = 0;
        p.z = p.n = 0;
        tempb = a + temp + (p.c ? 1:0);
        if (!tempb)
           p.z = 1;
        al = (a & 0xF) + (temp & 0xF) + (p.c ? 1 : 0);
        if (al > 9) {
            al -= 10;
            al &= 0xF;
            ah = 1;
        }
        ah += ((a >> 4) + (temp >> 4));
        if (ah & 8)
            p.n = 1;
        p.v = (((ah << 4) ^ a) & 128) && !((a ^ temp) & 128);
        p.c = 0;
        if (ah > 9) {
            p.c = 1;
            ah -= 10;
            ah &= 0xF;
        }
        a = (al & 0xF) | (ah << 4);                              \
    }
    else {
        tempw = (a + temp + (p.c ? 1 : 0));
        p.v = (!((a ^ temp) & 0x80) && ((a ^ tempw) & 0x80));
        a = tempw & 0xFF;
        p.c = tempw & 0x100;
        setzn(a);
    }
}

static inline void sbc_nmos(uint8_t temp)
{
    int hc6, al, ah, tempv;
    uint8_t tempb;
    int16_t tempw;

    if (p.d) {
        hc6 = 0;
        p.z = p.n = 0;
        tempb = a - temp - ((p.c) ? 0 : 1);
        if (!(tempb))
           p.z = 1;
        al = (a & 15) - (temp & 15) - (p.c ? 0 : 1);
        if (al & 16) {
            al -= 6;
            al &= 0xF;
            hc6 = 1;
        }
        ah = (a >> 4) - (temp >> 4);
        if (hc6)
            ah--;                       \
        if ((a - (temp + (p.c ? 0 : 1))) & 0x80)
           p.n = 1;
        p.v = ((a ^ temp) & 0x80) && ((a ^ tempb) & 0x80);
        p.c = 1;
        if (ah & 16) {
            p.c = 0;
            ah -= 6;
            ah &= 0xF;
        }
        a = (al & 0xF) | ((ah & 0xF) << 4);
    }
    else {
        tempw = a-temp-(p.c ? 0 : 1);
        tempv = (signed char)a -(signed char)temp-(p.c ? 0 : 1);
        p.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);
        p.c = tempw >= 0;
        a = tempw & 0xFF;
        setzn(a);
    }
}

static inline void adc_cmos(uint8_t temp)
{
    int al, ah;
    int16_t tempw;

    if (p.d) {
        ah = 0;
        al = (a & 0xF) + (temp & 0xF) + (p.c ? 1 : 0);
        if (al > 9) {
            al -= 10;
            al &= 0xF;
            ah = 1;
        }
        ah += ((a >> 4) + (temp >> 4));
        p.v = (((ah << 4) ^ a) & 0x80) && !((a ^ temp) & 0x80);
        p.c = 0;
        if (ah > 9) {
            p.c = 1;
            ah -= 10;
            ah &= 0xF;
        }
        a = (al & 0xF) | (ah << 4);
        setzn(a);
        polltime(1);
    }
    else {
        tempw = (a + temp + (p.c ? 1 : 0));
        p.v = (!((a ^ temp) & 0x80) && ((a ^ tempw) & 0x80));
        a = tempw & 0xFF;
        p.c = tempw & 0x100;
        setzn(a);
    }
}

static inline void sbc_cmos(uint8_t temp)
{
    int al, tempv;
    int16_t tempw;

    if (p.d) {
        al = (a & 15) - (temp & 15) - (p.c ? 0 : 1);
        tempw = a-temp-(p.c ? 0 : 1);
        tempv = (signed char)a -(signed char)temp-(p.c ? 0 : 1);
        p.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);
        p.c = tempw >= 0;
        if (tempw < 0)
           tempw -= 0x60;
        if (al < 0)
           tempw -= 0x06;
        a = tempw & 0xFF;
        setzn(a);
        polltime(1);
    }
    else {
        tempw = a-temp-(p.c ? 0 : 1);
        tempv = (signed char)a -(signed char)temp-(p.c ? 0 : 1);
        p.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);
        p.c = tempw >= 0;
        a = tempw & 0xFF;
        setzn(a);
    }
}

static void branchcycles(int temp)
{
        if (temp > 2) {
                polltime(temp - 1);
                takeint = (interrupt && !p.i);
                polltime(1);
        } else {
                polltime(2);
                takeint = (interrupt && !p.i);
        }
}

void m6502_exec(void)
{
        uint16_t addr;
        uint8_t temp;
        int tempi;
        int8_t offset;
        cycles += 40000;

        while (cycles > 0) {
                fetch_opcode();
                switch (opcode) {
                case 0x00:      /* BRK */
                        if (dbg_core6502)
                            debug_trap(&core6502_cpu_debug, oldpc, 0);
                        pc++;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x30));
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        p.i = 1;
                        polltime(7);
                        takeint = 0;
                        break;

                case 0x01:      /*ORA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x03:      /*Undocumented - SLO (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x04:      /*Undocumented - NOP zp */
                        addr = readmem(pc);
                        pc++;
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x05:      /*ORA zp */
                        addr = readmem(pc);
                        pc++;
                        a |= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x06:      /*ASL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x07:      /*Undocumented - SLO zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x08:
                        /*PHP*/
                        temp = pack_flags(0x30);
                        push(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x09:      /*ORA imm */
                        a |= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0A:      /*ASL A */
                        p.c = a & 0x80;
                        a <<= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0B:      /*Undocumented - ANC imm */
                        a &= readmem(pc);
                        pc++;
                        setzn(a);
                        p.c = p.n;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0C:      /*Undocumented - NOP abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0D:      /*ORA abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x0E:      /*ASL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        break;

                case 0x0F:      /*Undocumented - SLO abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x10:
                        /*BPL*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x11:      /*ORA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x13:      /*Undocumented - SLO (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr + y, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x14:      /*Undocumented - NOP zp,x */
                        addr = readmem(pc);
                        pc++;
                        readmem((addr + x) & 0xFF);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x15:      /*ORA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a |= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x16:      /*ASL zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x17:      /*Undocumented - SLO zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x18:
                        /*CLC*/ p.c = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x19:      /*ORA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x1A:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x1B:      /*Undocumented - SLO abs,y */
                        addr = getw() + y;
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x1C:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        readmem(addr);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x1D:      /*ORA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x1E:      /*ASL abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        break;

                case 0x1F:      /*Undocumented - SLO abs,x */
                        addr = getw() + x;
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        a |= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x20:
                        /*JSR*/ addr = getw();
                        pc--;
                        push(pc >> 8);
                        push((uint8_t)pc);
                        pc = addr;
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0x21:      /*AND (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a &= readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x23:      /*Undocumented - RLA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        polltime(1);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        a &= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x24:      /*BIT zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x25:      /*AND zp */
                        addr = readmem(pc);
                        pc++;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x26:      /*ROL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x27:      /*Undocumented - RLA zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        a &= temp;
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x28:
                        /*PLP*/ temp = pull();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        unpack_flags(temp);
                        break;

                case 0x29:
                        /*AND*/ a &= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x2A:      /*ROL A */
                        tempi = p.c;
                        p.c = a & 0x80;
                        a <<= 1;
                        if (tempi)
                                a |= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x2B:      /*Undocumented - ANC imm */
                        a &= readmem(pc);
                        pc++;
                        setzn(a);
                        p.c = p.n;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x2C:      /*BIT abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        break;

                case 0x2D:      /*AND abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a &= readmem(addr);
                        setzn(a);
                        break;

                case 0x2E:      /*ROL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        takeint = ((interrupt & 128) && !p.i);  // takeint=1;
                        polltime(1);
                        if (!takeint)
                                takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0x2F:      /*Undocumented - RLA abs */
                        addr = getw();  /*Found in The Hobbit */
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        a &= temp;
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x30:
                        /*BMI*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x31:      /*AND (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x33:      /*Undocumented - RLA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        polltime(1);
                        writemem(addr + y, temp);
                        a &= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x34:      /*Undocumented - NOP zp,x */
                        addr = readmem(pc);
                        pc++;
                        readmem((addr + x) & 0xFF);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x35:      /*AND zp,x */
                        addr = readmem(pc);
                        pc++;
                        a &= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x36:      /*ROL zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x37:      /*Undocumented - RLA zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        a &= temp;
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x38:
                        /*SEC*/ p.c = 1;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x39:      /*AND abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3A:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3B:      /*Undocumented - RLA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        polltime(1);
                        writemem(addr + y, temp);
                        a &= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3C:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        readmem(addr + x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3D:      /*AND abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3E:      /*ROL abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3F:      /*Undocumented - RLA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + x);
                        polltime(1);
                        writemem(addr + x, temp);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        polltime(1);
                        writemem(addr + x, temp);
                        a &= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x40:
                        /*RTI*/ output = 0;
                        temp = pull();
                        unpack_flags(temp);
                        pc = pull();
                        pc |= (pull() << 8);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x41:      /*EOR (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x43:      /*Undocumented - SRE (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x44:      /*Undocumented - NOP zp */
                        addr = readmem(pc);
                        pc++;
                        readmem(addr);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x45:      /*EOR zp */
                        addr = readmem(pc);
                        pc++;
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x46:      /*LSR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x47:      /*Undocumented - SRE zp */
                        addr = readmem(pc);
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x48:
                        /*PHA*/ push(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x49:      /*EOR imm */
                        a ^= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4A:      /*LSR A */
                        p.c = a & 1;
                        a >>= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4B:      /*Undocumented - ASR imm */
                        a &= readmem(pc);
                        pc++;
                        p.c = a & 1;
                        a >>= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4C:
                        /*JMP*/ addr = getw();
                        pc = addr;
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4D:      /*EOR abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a ^= readmem(addr);
                        setzn(a);
                        break;

                case 0x4E:      /*LSR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        takeint = ((interrupt & 128) && !p.i);  // takeint=1;
                        polltime(1);
                        if (!takeint)
                                takeint = (interrupt && !p.i);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x4F:      /*Undocumented - SRE abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x50:
                        /*BVC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x51:      /*EOR (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x53:      /*Undocumented - SRE (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr + y, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x54:      /*Undocumented - NOP zp,x */
                        addr = readmem(pc);
                        pc++;
                        readmem((addr + x) & 0xFF);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x55:      /*EOR zp,x */
                        addr = readmem(pc);
                        pc++;
                        a ^= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x56:      /*LSR zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x57:      /*Undocumented - SRE zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x58:
                        /*CLI*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        p.i = 0;
                        break;

                case 0x59:      /*EOR abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5A:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5B:      /*Undocumented - SRE abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr + y, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5C:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        polltime(4);
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) {
                                readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                                polltime(1);
                        }
                        readmem(addr + x);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5D:      /*EOR abs,x */
                        addr = getw();
                        polltime(4);
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) {
                                readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                                polltime(1);
                        }
                        addr += x;
                        a ^= readmem(addr);
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5E:      /*LSR abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5F:      /*Undocumented - SRE abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + x);
                        polltime(1);
                        writemem(addr + x, temp);
                        p.c = temp & 1;
                        temp >>= 1;
                        polltime(1);
                        writemem(addr + x, temp);
                        a ^= temp;
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x60:
                        /*RTS*/ pc = pull();
                        pc |= (pull() << 8);
                        pc++;
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0x61:      /*ADC (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        adc_nmos(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x63:      /*Undocumented - RRA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x64:      /*Undocumented - NOP zp */
                        addr = readmem(pc);
                        pc++;
                        readmem(addr);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x65:      /*ADC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        adc_nmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x66:      /*ROR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x67:      /*Undocumented - RRA zp */
                        addr = readmem(pc);
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x68:
                        /*PLA*/ a = pull();
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x69:      /*ADC imm */
                        temp = readmem(pc);
                        pc++;
                        adc_nmos(temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6A:      /*ROR A */
                        tempi = p.c;
                        p.c = a & 1;
                        a >>= 1;
                        if (tempi)
                                a |= 0x80;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6B:      /*Undocumented - ARR */
                        a &= readmem(pc);
                        pc++;
                        tempi = p.c;
                        if (p.d) {      /*This instruction is just as broken on a real 6502 as it is here */
                                p.v = ((a >> 6) ^ (a >> 7));    /*V set if bit 6 changes in ROR */
                                a >>= 1;
                                if (tempi)
                                        a |= 0x80;
                                setzn((uint8_t)tempi);
                                p.c = 0;
                                if ((a & 0xF) + (a & 1) > 5)
                                        a = (a & 0xF0) + ((a & 0xF) + 6);       /*Do broken fixup */
                                if ((a & 0xF0) + (a & 0x10) > 0x50) {
                                        a += 0x60;
                                        p.c = 1;
                                }
                        } else {        /*V & C flag behaviours in 64doc.txt are backwards */
                                p.v = ((a >> 6) ^ (a >> 7));    /*V set if bit 6 changes in ROR */
                                a >>= 1;
                                if (tempi)
                                        a |= 0x80;
                                setzn(a);
                                p.c = a & 0x40;
                        }
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6C:      /*JMP () */
                        addr = getw();
                        if ((addr & 0xFF) == 0xFF)
                                pc = readmem(addr) | (readmem(addr - 0xFF) <<
                                                      8);
                        else
                                pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6D:      /*ADC abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        adc_nmos(temp);
                        break;

                case 0x6E:      /*ROR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        if ((interrupt & 128) && !p.i)
                                takeint = 1;
                        polltime(1);
                        if (interrupt && !p.i)
                                takeint = 1;
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x6F:      /*Undocumented - RRA abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x70:
                        /*BVS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x71:      /*ADC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        adc_nmos(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x73:      /*Undocumented - RRA (,y) */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x74:      /*Undocumented - NOP zp,x */
                        addr = readmem(pc);
                        pc++;
                        readmem((addr + x) & 0xFF);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x75:      /*ADC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        adc_nmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x76:      /*ROR zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x77:      /*Undocumented - RRA zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x78:
                        /*SEI*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        p.i = 1;
                        break;

                case 0x79:      /*ADC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        adc_nmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7A:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7B:      /*Undocumented - RRA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr + y, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7C:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        readmem(addr);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7D:      /*ADC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        temp = readmem(addr);
                        adc_nmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7E:      /*ROR abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7F:      /*Undocumented - RRA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + x);
                        polltime(1);
                        writemem(addr + x, temp);
                        temp >>= 1;
                        if (p.c)
                                temp |= 0x80;
                        polltime(1);
                        writemem(addr + x, temp);
                        adc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x80:      /*Undocumented - NOP imm */
                        readmem(pc);
                        pc++;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x81:      /*STA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        writemem(addr, a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x82:      /*Undocumented - NOP imm *//*Should sometimes lock up the machine */
                        readmem(pc);
                        pc++;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x83:      /*Undocumented - SAX (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        writemem(addr, a & x);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x84:      /*STY zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x85:      /*STA zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x86:      /*STX zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x87:      /*Undocumented - SAX zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, a & x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x88:
                        /*DEY*/ y--;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x89:      /*Undocumented - NOP imm */
                        readmem(pc);
                        pc++;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x8A:
                        /*TXA*/ a = x;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x8B:      /*Undocumented - ANE */
                        temp = readmem(pc);
                        pc++;
                        a = (a | 0xEE) & x & temp;      /*Internal parameter always 0xEE on BBC, always 0xFF on Electron */
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x8C:      /*STY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, y);
                        break;

                case 0x8D:      /*STA abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, a);
                        break;

                case 0x8E:      /*STX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, x);
                        break;

                case 0x8F:      /*Undocumented - SAX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, a & x);
                        break;

                case 0x90:
                        /*BCC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x91:      /*STA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp) + y;
                        writemem(addr, a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x93:      /*Undocumented - SHA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        writemem(addr + y, a & x & ((addr >> 8) + 1));
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x94:      /*STY zp,x */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + x) & 0xFF, y);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x95:      /*STA zp,x */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + x) & 0xFF, a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x96:      /*STX zp,y */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + y) & 0xFF, x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x97:      /*Undocumented - SAX zp,y */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + y) & 0xFF, a & x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x98:
                        /*TYA*/ a = y;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x99:      /*STA abs,y */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + y) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + y, a);
                        break;

                case 0x9A:
                        /*TXS*/ s = x;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x9B:      /*Undocumented - SHS abs,y */
                        addr = getw();
                        readmem((addr & 0xFF00) + ((addr + y) & 0xFF));
                        writemem(addr + y, a & x & ((addr >> 8) + 1));
                        s = a & x;
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x9C:      /*Undocumented - SHY abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) + ((addr + x) & 0xFF));
                        writemem(addr + x, y & ((addr >> 8) + 1));
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x9D:      /*STA abs,x */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + x, a);
                        break;

                case 0x9E:      /*Undocumented - SHX abs,y */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + y, x & ((addr >> 8) + 1));
                        break;

                case 0x9F:      /*Undocumented - SHA abs,y */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + y, a & x & ((addr >> 8) + 1));
                        break;

                case 0xA0:      /*LDY imm */
                        y = readmem(pc);
                        pc++;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA1:      /*LDA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a = readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA2:      /*LDX imm */
                        x = readmem(pc);
                        pc++;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA3:      /*Undocumented - LAX (,y) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a = x = readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA4:      /*LDY zp */
                        addr = readmem(pc);
                        pc++;
                        y = readmem(addr);
                        setzn(y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA5:      /*LDA zp */
                        addr = readmem(pc);
                        pc++;
                        a = readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA6:      /*LDX zp */
                        addr = readmem(pc);
                        pc++;
                        x = readmem(addr);
                        setzn(x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA7:      /*Undocumented - LAX zp */
                        addr = readmem(pc);
                        pc++;
                        a = x = readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA8:
                        /*TAY*/ y = a;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA9:      /*LDA imm */
                        a = readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0xAA:
                        /*TAX*/ x = a;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xAB:      /*Undocumented - LAX */
                        temp = readmem(pc);
                        pc++;
                        a = x = ((a | 0xEE) & temp);    /*WAAAAY more complicated than this, but it varies from machine to machine anyway */
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xAC:      /*LDY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        y = readmem(addr);
                        setzn(y);
                        break;

                case 0xAD:      /*LDA abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a = readmem(addr);
                        setzn(a);
                        break;

                case 0xAE:      /*LDX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        x = readmem(addr);
                        setzn(x);
                        break;

                case 0xAF:      /*LAX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a = x = readmem(addr);
                        setzn(a);
                        break;

                case 0xB0:
                        /*BCS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xB1:      /*LDA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB3:      /*LAX (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = x = readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB4:      /*LDY zp,x */
                        addr = readmem(pc);
                        pc++;
                        y = readmem((addr + x) & 0xFF);
                        setzn(y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB5:      /*LDA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a = readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB6:      /*LDX zp,y */
                        addr = readmem(pc);
                        pc++;
                        x = readmem((addr + y) & 0xFF);
                        setzn(x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB7:      /*LAX zp,y */
                        addr = readmem(pc);
                        pc++;
                        a = x = readmem((addr + y) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB8:
                        /*CLV*/ p.v = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB9:      /*LDA abs,y */
                        addr = getw();
                        polltime(3);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBA:
                        /*TSX*/ x = s;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBB:      /*Undocumented - LAS abs,y */
                        addr = getw();
                        polltime(3);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = x = s = s & readmem(addr + y);      /*No, really! */
                        setzn(a);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBC:      /*LDY abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        y = readmem(addr + x);
                        setzn(y);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBD:      /*LDA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBE:      /*LDX abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        x = readmem(addr + y);
                        setzn(x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBF:      /*LAX abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = x = readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC0:      /*CPY imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(y - temp);
                        p.c = (y >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC1:      /*CMP (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC2:      /*Undocumented - NOP imm *//*Should sometimes lock up the machine */
                        readmem(pc);
                        pc++;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC3:      /*Undocumented - DCP (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp--;
                        polltime(1);
                        writemem(addr, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC4:      /*CPY zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(y - temp);
                        p.c = (y >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC5:      /*CMP zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC6:      /*DEC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC7:      /*Undocumented - DCP zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC8:
                        /*INY*/ y++;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC9:      /*CMP imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xCA:
                        /*DEX*/ x--;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xCB:      /*Undocumented - SBX imm */
                        temp = readmem(pc);
                        pc++;
                        setzn((a & x) - temp);
                        p.c = ((a & x) >= temp);
                        x = (a & x) - temp;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xCC:      /*CPY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(y - temp);
                        p.c = (y >= temp);
                        break;

                case 0xCD:      /*CMP abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        break;

                case 0xCE:      /*DEC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) - 1;
                        polltime(1);
//                                takeint=(interrupt && !p.i);
                        writemem(addr, temp + 1);
                        takeint = ((interrupt & 128) && !p.i);  // takeint=1;
                        polltime(1);
                        if (!takeint)
                                takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xCF:      /*Undocumented - DCP abs */
                        addr = getw();
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD0:
                        /*BNE*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xD1:      /*CMP (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD3:      /*Undocumented - DCP (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr) - 1;
                        polltime(1);
                        writemem(addr, temp + 1);
                        polltime(1);
                        writemem(addr, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD4:      /*Undocumented - NOP zp,x */
                        addr = readmem(pc);
                        pc++;
                        readmem((addr + x) & 0xFF);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD5:      /*CMP zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD6:      /*DEC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF) - 1;
                        setzn(temp);
                        writemem((addr + x) & 0xFF, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD7:      /*Undocumented - DCP zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD8:
                        /*CLD*/ p.d = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD9:      /*CMP abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDA:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDB:      /*Undocumented - DCP abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + y) - 1;
                        polltime(1);
                        writemem(addr + y, temp + 1);
                        polltime(1);
                        writemem(addr + y, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDC:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        readmem(addr + x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDD:      /*CMP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDE:      /*DEC abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp + 1);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDF:      /*Undocumented - DCP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        polltime(4);
                        temp = readmem(addr + x) - 1;
                        polltime(1);
                        writemem(addr + x, temp + 1);
                        polltime(1);
                        writemem(addr + x, temp);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE0:      /*CPX imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(x - temp);
                        p.c = (x >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE1:      /*SBC (,x) *//*This was missed out of every B-em version since 0.6 as it was never used! */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        sbc_nmos(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE2:      /*Undocumented - NOP imm *//*Should sometimes lock up the machine */
                        readmem(pc);
                        pc++;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE3:      /*Undocumented - ISB (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE4:      /*CPX zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(x - temp);
                        p.c = (x >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE5:      /*SBC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        sbc_nmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE6:      /*INC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr) + 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE7:      /*Undocumented - ISB zp */
                        addr = readmem(pc);
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE8:
                        /*INX*/ x++;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE9:      /*SBC imm */
                        temp = readmem(pc);
                        pc++;
                        sbc_nmos(temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xEA:
                        /*NOP*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xEB:      /*Undocumented - SBC imm */
                        temp = readmem(pc);
                        pc++;
                        sbc_nmos(temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xEC:      /*CPX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(x - temp);
                        p.c = (x >= temp);
                        break;

                case 0xED:      /*SBC abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        sbc_nmos(temp);
                        break;

                case 0xEE:      /*INC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) + 1;
                        polltime(1);
                        writemem(addr, temp - 1);
                        if ((interrupt & 128) && !p.i)
                                takeint = 1;
                        polltime(1);
                        if (interrupt && !p.i)
                                takeint = 1;
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xEF:      /*Undocumented - ISB abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF0:
                        /*BEQ*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
//                                        if (pc<0x8000) printf("%04X %02X\n",(pc&0xFF00)^((pc+offset)&0xFF00),temp);
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xF1:      /*SBC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        sbc_nmos(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF3:      /*Undocumented - ISB (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        polltime(5);
                        temp = readmem(addr + y);
                        polltime(1);
                        writemem(addr + y, temp);
                        temp++;
                        polltime(1);
                        writemem(addr + y, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF4:      /*Undocumented - NOP zpx */
                        addr = readmem(pc);
                        pc++;
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF5:      /*SBC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        sbc_nmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF6:      /*INC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF) + 1;
                        writemem((addr + x) & 0xFF, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF7:      /*Undocumented - ISB zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        polltime(3);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF8:
                        /*SED*/ p.d = 1;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF9:      /*SBC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        sbc_nmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFA:      /*Undocumented - NOP */
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFB:      /*Undocumented - ISB abs,y */
                        addr = getw() + y;
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFC:      /*Undocumented - NOP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        readmem(addr + x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFD:      /*SBC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        sbc_nmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFE:      /*INC abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr) + 1;
                        writemem(addr, temp - 1);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFF:      /*Undocumented - ISB abs,x */
                        addr = getw() + x;
                        polltime(5);
                        temp = readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        temp++;
                        polltime(1);
                        writemem(addr, temp);
                        sbc_nmos(temp);
                        takeint = (interrupt && !p.i);
                        break;

#if 0
                case 0x02:      /*TFS opcode - OSFSC */
/*                                if (uefena)
                                {
                                        pc+=2;
                                }
                                else
                                {*/
                        printf("OSFSC!\n");
                        c = OSFSC();
                        if (c == 6 || c == 8 || c == 0 || c == 5) {
                                pc = pull();
                                pc += (pull() << 8) + 1;
                        }
                        if (c == 0x80) {
                                temp = ram[pc++];
                                p.c = (a >= temp);
                                setzn(a - temp);
                        }
//                                }*/
//                                pc+=2;
                        break;

                case 0x92:      /*TFS opcode - OSFILE */
/*                                if (uefena)
                                {
                                        pc+=2;
                                }
                                else
                                {*/
                        printf("OSFILE!\n");
                        a = OSFILE();
                        if (a == 0x80) {
                                push(a);
                        } else if (a != 0x7F) {
                                pc = pull();
                                pc += (pull() << 8) + 1;
                        }
//                                }*/
//                                pc+=2;
                        break;
#endif
                default:        /*Halt! */
//                                printf("HALT\n");
//                                dumpregs();
//                                exit(-1);
                        pc--;   /*PC never moves on */
                        takeint = 0;    /*Interrupts never occur */
                        oldnmi = 1;     /*NMIs never occur */
                        polltime(100000);       /*Actually lasts forever, but the above code keeps executing HLT forever */
                        break;
//                                printf("Found bad opcode %02X\n",opcode);
/*                                switch (opcode&0xF)
                                {
                                        case 0xA:
                                        break;
                                        case 0x0:
                                        case 0x2:
                                        case 0x3:
                                        case 0x4:
                                        case 0x7:
                                        case 0x9:
                                        case 0xB:
                                        pc++;
                                        break;
                                        case 0xC:
                                        case 0xE:
                                        case 0xF:
                                        pc+=2;
                                        break;
                                }*/
                }

//                output=(pc<0x172B && pc>=0x167F);
/*                if (pc==0x7112)
                {
                        output=1;
                        dumpregs();
                        mem_dump();
                        exit(-1);
                }*/
//                if (pc==0x1f00) output=1;
/*                if (pc==0x6195)
                {
                        dumpregs();
                        mem_dump();
                        exit(-1);
                }*/
//                if (pc==0xCD7A) printf("CD7A from %04X\n",oldpc);
//                if (pc==0xC565) printf("C565 from %04X\n",oldpc);
//if (pc>=0x2078 && pc<0x20CA){  output=1; log_debug("%04X\n",pc); }
//if (pc==0x2770) output=1;
//if (pc==0x277C) output=0;

//                if (skipint) skipint--;
/*                if (pc==0x6000) output=1;
                if (pc==0x6204) output=1;
                if (pc==0x6191)
                {
                        dumpregs();
                        mem_dump();
                        exit(-1);
                }
                if (pc==0x612B)
                {
                        dumpregs();
                        mem_dump();
                        exit(-1);
                }*/
/*                if (output)
                {
//                        #undef printf
                        log_debug("A=%02X X=%02X Y=%02X S=%02X PC=%04X %c%c%c%c%c%c op=%02X %02X%02X\n",a,x,y,s,pc,(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ',opcode,ram[0x29],uservia.ifr);
                }*/
//                if (pc==0x400) output=1;
                if (timetolive) {
                        timetolive--;
                        if (!timetolive)
                                output = 0;
                }
                if (takeint) {
//                        output=1;
                        interrupt &= ~128;
                        takeint = 0;
//                        skipint=0;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x20));
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        p.i = 1;
                        polltime(7);
//                        log_debug("INT\n");
                }
                interrupt &= ~128;

                if (otherstuffcount <= 0)
                    otherstuff_poll();
                if (tube_exec && tubecycle) {
                        tubecycles += (tubecycle * tube_multipler) >> 1;
                        if (tubecycles > 3)
                                tube_exec();
                        tubecycle = 0;
                }

                if (nmi && !oldnmi) {
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x20));
                        pc = readmem(0xFFFA) | (readmem(0xFFFB) << 8);
                        p.i = 1;
                        polltime(7);
                        nmi = 0;
//                        printf("NMI\n");
                }
        }
}

void m65c02_exec(void)
{
        uint16_t addr;
        uint8_t temp;
        uint16_t tempw;
        int tempi;
        int8_t offset;
        cycles += 40000;
//        log_debug("PC = %04X\n",pc);
//        log_debug("Exec cycles %i\n",cycles);
        while (cycles > 0) {
                fetch_opcode();
                switch (opcode) {
                case 0x00:      /* BRK */
                        if (dbg_core6502)
                            debug_trap(&core6502_cpu_debug, oldpc, 0);
                        pc++;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x30));
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        p.i = 1;
                        p.d = 0;
                        polltime(7);
                        takeint = 0;
                        break;

                case 0x01:      /*ORA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x04:      /*TSB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.z = !(temp & a);
                        temp |= a;
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x05:      /*ORA zp */
                        addr = readmem(pc);
                        pc++;
                        a |= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x06:      /*ASL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x08:
                        /*PHP*/
                        temp = pack_flags(0x30);
                        push(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x09:      /*ORA imm */
                        a |= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0A:      /*ASL A */
                        p.c = a & 0x80;
                        a <<= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x0C:      /*TSB abs */
                        addr = getw();
                        temp = readmem(addr);
                        p.z = !(temp & a);
                        temp |= a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x0D:      /*ORA abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x0E:      /*ASL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        break;

                case 0x10:
                        /*BPL*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x11:      /*ORA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x12:      /*ORA () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        a |= readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x14:      /*TRB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.z = !(temp & a);
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x15:      /*ORA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a |= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x16:      /*ASL zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x18:
                        /*CLC*/ p.c = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x19:      /*ORA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x1A:      /*INC A */
                        a++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x1C:      /*TRB abs */
                        addr = getw();
                        temp = readmem(addr);
                        p.z = !(temp & a);
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x1D:      /*ORA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a |= readmem(addr);
                        setzn(a);
                        break;

                case 0x1E:      /*ASL abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        tempw =
                            ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) ? 1 : 0;
                        addr += x;
                        temp = readmem(addr);
                        readmem(addr);
                        p.c = temp & 0x80;
                        temp <<= 1;
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6 + tempw);
                        break;

                case 0x20:
                        /*JSR*/ addr = getw();
                        pc--;
                        push(pc >> 8);
                        push((uint8_t)pc);
                        pc = addr;
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0x21:      /*AND (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a &= readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x24:      /*BIT zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x25:      /*AND zp */
                        addr = readmem(pc);
                        pc++;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x26:      /*ROL zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x28:
                        /*PLP*/ temp = pull();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        unpack_flags(temp);
                        break;

                case 0x29:
                        /*AND*/ a &= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x2A:      /*ROL A */
                        tempi = p.c;
                        p.c = a & 0x80;
                        a <<= 1;
                        if (tempi)
                                a |= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x2C:      /*BIT abs */
                        addr = getw();
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        temp = readmem(addr);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        break;

                case 0x2D:      /*AND abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a &= readmem(addr);
                        setzn(a);
                        break;

                case 0x2E:      /*ROL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0x30:
                        /*BMI*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x31:      /*AND (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x32:      /*AND () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        a &= readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x34:      /*BIT zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x35:      /*AND zp,x */
                        addr = readmem(pc);
                        pc++;
                        a &= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x36:      /*ROL zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x38:
                        /*SEC*/ p.c = 1;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x39:      /*AND abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3A:      /*DEC A */
                        a--;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x3C:      /*BIT abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr);
                        p.z = !(a & temp);
                        p.v = temp & 0x40;
                        p.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x3D:      /*AND abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x3E:      /*ROL abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        tempw =
                            ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) ? 1 : 0;
                        addr += x;
                        temp = readmem(addr);
                        readmem(addr);
                        tempi = p.c;
                        p.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6 + tempw);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x40:
                        /*RTI*/ temp = pull();
                        unpack_flags(temp);
                        pc = pull();
                        pc |= (pull() << 8);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x41:      /*EOR (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x45:      /*EOR zp */
                        addr = readmem(pc);
                        pc++;
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x46:      /*LSR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x48:
                        /*PHA*/ push(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x49:      /*EOR imm */
                        a ^= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4A:      /*LSR A */
                        p.c = a & 1;
                        a >>= 1;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4C:
                        /*JMP*/ addr = getw();
                        pc = addr;
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x4D:      /*EOR abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a ^= readmem(addr);
                        setzn(a);
                        break;

                case 0x4E:      /*LSR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        break;

                case 0x50:
                        /*BVC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x51:      /*EOR (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x52:      /*EOR () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0x55:      /*EOR zp,x */
                        addr = readmem(pc);
                        pc++;
                        a ^= readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x56:      /*LSR zp,x */
                        addr = (readmem(pc) + x) & 0xFF;
                        pc++;
                        temp = readmem(addr);
                        p.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x58:
                        /*CLI*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        p.i = 0;
                        break;

                case 0x59:      /*EOR abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5A:
                        /*PHY*/ push(y);
                        polltime(3);
                        break;

                case 0x5D:      /*EOR abs,x */
                        addr = getw();
                        polltime(4);
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) {
                                readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                                polltime(1);
                        }
                        addr += x;
                        a ^= readmem(addr);
                        setzn(a);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x5E:      /*LSR abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        tempw =
                            ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) ? 1 : 0;
                        addr += x;
                        temp = readmem(addr);
                        readmem(addr);
                        p.c = temp & 1;
                        temp >>= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6 + tempw);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x60:
                        /*RTS*/ pc = pull();
                        pc |= (pull() << 8);
                        pc++;
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0x61:      /*ADC (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x64:      /*STZ zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, 0);
                        polltime(3);
                        break;

                case 0x65:      /*ADC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x66:      /*ROR zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x68:
                        /*PLA*/ a = pull();
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x69:      /*ADC imm */
                        temp = readmem(pc);
                        pc++;
                        adc_cmos(temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6A:      /*ROR A */
                        tempi = p.c;
                        p.c = a & 1;
                        a >>= 1;
                        if (tempi)
                                a |= 0x80;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6C:      /*JMP () */
                        addr = getw();
                        pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x6D:      /*ADC abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        break;

                case 0x6E:      /*ROR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        readmem(addr);
                        if ((interrupt & 128) && !p.i)
                                takeint = 1;
                        polltime(1);
                        if (interrupt && !p.i)
                                takeint = 1;
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x70:
                        /*BVS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x71:      /*ADC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        adc_cmos(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x72:      /*ADC () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(5);
                        break;

                case 0x74:      /*STZ zp,x */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + x) & 0xFF, 0);
                        polltime(3);
                        break;

                case 0x75:      /*ADC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        adc_cmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x76:      /*ROR zp,x */
                        addr = readmem(pc);
                        pc++;
                        addr += x;
                        addr &= 0xFF;
                        temp = readmem(addr);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x78:
                        /*SEI*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        p.i = 1;
                        break;

                case 0x79:      /*ADC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        adc_cmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7A:
                        /*PLY*/ y = pull();
                        setzn(y);
                        polltime(4);
                        break;

                case 0x7C:      /*JMP (,x) */
                        addr = getw();
                        addr += x;
                        pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(6);
                        break;

                case 0x7D:      /*ADC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x7E:      /*ROR abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        tempw =
                            ((addr & 0xFF00) ^ ((addr + x) & 0xFF00)) ? 1 : 0;
                        addr += x;
                        temp = readmem(addr);
                        readmem(addr);
                        tempi = p.c;
                        p.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6 + tempw);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x80:
                        /*BRA*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 3;
                        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                temp++;
                        pc += offset;
                        polltime(temp);
                        break;

                case 0x81:      /*STA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        writemem(addr, a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x84:      /*STY zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x85:      /*STA zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x86:      /*STX zp */
                        addr = readmem(pc);
                        pc++;
                        writemem(addr, x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x88:
                        /*DEY*/ y--;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x89:      /*BIT imm */
                        temp = readmem(pc);
                        pc++;
                        p.z = !(a & temp);
                        polltime(2);
                        break;

                case 0x8A:
                        /*TXA*/ a = x;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x8C:      /*STY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, y);
                        break;

                case 0x8D:      /*STA abs */
                        addr = getw();
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        writemem(addr, a);
                        break;

                case 0x8E:      /*STX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, x);
                        break;

                case 0x90:
                        /*BCC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0x91:      /*STA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp) + y;
                        writemem(addr, a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x92:      /*STA () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        writemem(addr, a);
                        polltime(6);
                        break;

                case 0x94:      /*STY zp,x */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + x) & 0xFF, y);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x95:      /*STA zp,x */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + x) & 0xFF, a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x96:      /*STX zp,y */
                        addr = readmem(pc);
                        pc++;
                        writemem((addr + y) & 0xFF, x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x98:
                        /*TYA*/ a = y;
                        setzn(a);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x99:      /*STA abs,y */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + y) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + y, a);
                        break;

                case 0x9A:
                        /*TXS*/ s = x;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0x9C:      /*STZ abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        writemem(addr, 0);
                        break;

                case 0x9D:      /*STA abs,x */
                        addr = getw();
                        polltime(4);
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr + x, a);
                        break;

                case 0x9E:      /*STZ abs,x */
                        addr = getw();
                        addr += x;
                        polltime(4);
                        writemem(addr, 0);
                        polltime(1);
                        break;

                case 0xA0:      /*LDY imm */
                        y = readmem(pc);
                        pc++;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA1:      /*LDA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a = readmem(addr);
                        setzn(a);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA2:      /*LDX imm */
                        x = readmem(pc);
                        pc++;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA4:      /*LDY zp */
                        addr = readmem(pc);
                        pc++;
                        y = readmem(addr);
                        setzn(y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA5:      /*LDA zp */
                        addr = readmem(pc);
                        pc++;
                        a = readmem(addr);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA6:      /*LDX zp */
                        addr = readmem(pc);
                        pc++;
                        x = readmem(addr);
                        setzn(x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA8:
                        /*TAY*/ y = a;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xA9:      /*LDA imm */
                        a = readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        polltime(1);
                        break;

                case 0xAA:
                        /*TAX*/ x = a;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xAC:      /*LDY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        y = readmem(addr);
                        setzn(y);
                        break;

                case 0xAD:      /*LDA abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        a = readmem(addr);
                        setzn(a);
                        break;

                case 0xAE:      /*LDX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        x = readmem(addr);
                        setzn(x);
                        break;

                case 0xB0:
                        /*BCS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xB1:      /*LDA (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB2:      /*LDA () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        a = readmem(addr);
                        setzn(a);
                        polltime(5);
                        break;

                case 0xB4:      /*LDY zp,x */
                        addr = readmem(pc);
                        pc++;
                        y = readmem((addr + x) & 0xFF);
                        setzn(y);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB5:      /*LDA zp,x */
                        addr = readmem(pc);
                        pc++;
                        a = readmem((addr + x) & 0xFF);
                        setzn(a);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB6:      /*LDX zp,y */
                        addr = readmem(pc);
                        pc++;
                        x = readmem((addr + y) & 0xFF);
                        setzn(x);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB8:
                        /*CLV*/ p.v = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xB9:      /*LDA abs,y */
                        addr = getw();
                        polltime(3);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBA:
                        /*TSX*/ x = s;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBC:      /*LDY abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        y = readmem(addr + x);
                        setzn(y);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBD:      /*LDA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xBE:      /*LDX abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        x = readmem(addr + y);
                        setzn(x);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC0:      /*CPY imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(y - temp);
                        p.c = (y >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC1:      /*CMP (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC4:      /*CPY zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(y - temp);
                        p.c = (y >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC5:      /*CMP zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC6:      /*DEC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC8:
                        /*INY*/ y++;
                        setzn(y);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xC9:      /*CMP imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xCA:
                        /*DEX*/ x--;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;
#if 0
                case 0xCB:
                        /*WAI*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        if (!takeint)
                                pc--;
                        break;
#endif

                case 0xCC:      /*CPY abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(y - temp);
                        p.c = (y >= temp);
                        break;

                case 0xCD:      /*CMP abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        break;

                case 0xCE:      /*DEC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) - 1;
                        polltime(1);
//                                takeint=(interrupt && !p.i);
                        readmem(addr);
                        takeint = ((interrupt & 128) && !p.i);  // takeint=1;
                        polltime(1);
                        if (!takeint)
                                takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xD0:
                        /*BNE*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!p.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xD1:      /*CMP (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD2:      /*CMP () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(5);
                        break;

                case 0xD5:      /*CMP zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD6:      /*DEC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF) - 1;
                        writemem((addr + x) & 0xFF, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD8:
                        /*CLD*/ p.d = 0;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xD9:      /*CMP abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDA:
                        /*PHX*/ push(x);
                        polltime(3);
                        break;

                case 0xDD:      /*CMP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        setzn(a - temp);
                        p.c = (a >= temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xDE:      /*DEC abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr) - 1;
                        readmem(addr);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE0:      /*CPX imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(x - temp);
                        p.c = (x >= temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE1:      /*SBC (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(6);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE4:      /*CPX zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        setzn(x - temp);
                        p.c = (x >= temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE5:      /*SBC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE6:      /*INC zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr) + 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE8:
                        /*INX*/ x++;
                        setzn(x);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xE9:      /*SBC imm */
                        temp = readmem(pc);
                        pc++;
                        sbc_cmos(temp);
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xEA:
                        /*NOP*/ polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xEC:      /*CPX abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        setzn(x - temp);
                        p.c = (x >= temp);
                        break;

                case 0xED:      /*SBC abs */
                        addr = getw();
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        break;

                case 0xEE:      /*INC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) + 1;
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        takeint = (interrupt && !p.i);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xF0:
                        /*BEQ*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (p.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
//                                        if (pc<0x8000) printf("%04X %02X\n",(pc&0xFF00)^((pc+offset)&0xFF00),temp);
                                pc += offset;
                        }
                        branchcycles(temp);
                        break;

                case 0xF1:      /*SBC (),y */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        sbc_cmos(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF2:      /*SBC () */
                        temp = readmem(pc);
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(5);
                        break;

                case 0xF5:      /*SBC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        sbc_cmos(temp);
                        polltime(3);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF6:      /*INC zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF) + 1;
                        writemem((addr + x) & 0xFF, temp);
                        setzn(temp);
                        polltime(5);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF8:
                        /*SED*/ p.d = 1;
                        polltime(2);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xF9:      /*SBC abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        sbc_cmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFA:
                        /*PLX*/ x = pull();
                        setzn(x);
                        polltime(4);
                        break;

                case 0xFD:      /*SBC abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        sbc_cmos(temp);
                        polltime(4);
                        takeint = (interrupt && !p.i);
                        break;

                case 0xFE:      /*INC abs,x */
                        addr = getw();
                        readmem((addr & 0xFF00) | ((addr + x) & 0xFF));
                        addr += x;
                        temp = readmem(addr) + 1;
                        readmem(addr);
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(7);
                        takeint = (interrupt && !p.i);
                        break;

                default:
                        switch (opcode & 0xF) {
                        case 2:
                                pc++;
                                polltime(2);
                                break;
                        case 3:
                        case 7:
                        case 0xB:
                        case 0xF:
                                polltime(1);
                                break;
                        case 4:
                                pc++;
                                if (opcode == 0x44) {
                                        polltime(3);
                                } else {
                                        polltime(4);
                                }
                                break;
                        case 0xC:
                                pc += 2;
                                if (opcode == 0x5C) {
                                        polltime(7);
                                } else {
                                        polltime(4);
                                }
                                break;
                        }
                        takeint = (interrupt && !p.i);
                        break;

                        /* TODO: DB: was this intentionally excluded
                        printf("Found bad opcode %02X\n", opcode);
                        dumpregs();
                        mem_dump();
                        exit(-1);
                        */
                }
/*                if (output | 1)
                {
                        log_debug("A=%02X X=%02X Y=%02X S=%02X PC=%04X %c%c%c%c%c%c op=%02X %02X%02X %02X%02X %02X  %08X\n",a,x,y,s,pc,(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ',opcode,ram[0x21],ram[0x20],ram[0x7F],ram[0x7E],ram[0x7D],memlook[pc>>8]);
                }*/
/*                if (timetolive)
                {
                        timetolive--;
                        if (!timetolive) output=0;
                }*/
                if (takeint) {
                        interrupt &= ~128;
                        takeint = 0;
//                        skipint=0;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        temp = 0x20;
                        if (p.c)
                                temp |= 1;
                        if (p.z)
                                temp |= 2;
                        if (p.i)
                                temp |= 4;
                        if (p.d)
                                temp |= 8;
                        if (p.v)
                                temp |= 0x40;
                        if (p.n)
                                temp |= 0x80;
                        push(temp);
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        p.i = 1;
                        p.d = 0;
                        polltime(7);
//                        log_debug("INT\n");
//                        printf("Interrupt - %02X %02X\n",sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier);
//                        printf("INT\n");
                }
                interrupt &= ~128;
                if (tube_exec && tubecycle) {
//                        log_debug("tubeexec %i %i %i\n",tubecycles,tubecycle,tube_shift);
                        tubecycles += (tubecycle * tube_multipler) >> 1;
                        if (tubecycles > 3)
                                tube_exec();
                        tubecycle = 0;
                }

                if (otherstuffcount <= 0)
                    otherstuff_poll();
                if (nmi && !oldnmi) {
                        push(pc >> 8);
                        push(pc & 0xFF);
                        temp = pack_flags(0x20);
                        push(temp);
                        pc = readmem(0xFFFA) | (readmem(0xFFFB) << 8);
                        p.i = 1;
                        polltime(7);
                        nmi = 0;
                        p.d = 0;
//                        log_debug("NMI\n");
//                        printf("NMI\n");
                }
                oldnmi = nmi;
        }
}

void m6502_savestate(FILE * f)
{
    unsigned char bytes[13];

    bytes[0] = a;
    bytes[1] = x;
    bytes[2] = y;
    bytes[3] = pack_flags(0x30);
    bytes[4] = s;
    bytes[5] = pc & 0xFF;
    bytes[6] = pc >> 8;
    bytes[7] = nmi;
    bytes[8] = interrupt;
    bytes[9] = cycles;
    bytes[10] = cycles >> 8;
    bytes[11] = cycles >> 16;
    bytes[12] = cycles >> 24;
    fwrite(bytes, sizeof(bytes), 1, f);
}

void m6502_loadstate(FILE * f)
{
    unsigned char bytes[13];
    fread(bytes, sizeof(bytes), 1, f);
    a = bytes[0];
    x = bytes[1];
    y = bytes[2];
    unpack_flags(bytes[3]);
    s = bytes[4];
    pc = bytes[5] | (bytes[6] << 8);
    nmi = bytes[7];
    interrupt = bytes[8];
    cycles = bytes[9] | (bytes[10] << 8) | (bytes[11] << 16) | (bytes[12] << 24);
}
