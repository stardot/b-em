/*B-em v2.2 by Tom Walker
  65816 parasite CPU emulation
  Originally from Snem, with some bugfixes*/

#include <stdio.h>
#include "b-em.h"
#include "tube.h"
#include "ssinline.h"
#include "65816.h"

#define printf log_debug

#define W65816_ROM_SIZE  0x8000
#define W65816_RAM_SIZE 0x80000

static uint8_t *w65816ram, *w65816rom;
/*Registers*/
typedef union {
    uint16_t w;
    struct {
        uint8_t l, h;
    } b;
} reg;

static reg w65816a, w65816x, w65816y, w65816s;
static uint32_t pbr, dbr;
static uint16_t w65816pc, dp;

static uint32_t wins = 0;

w65816p_t w65816p;

static int inwai;

/*CPU modes : 0 = X1M1
              1 = X1M0
              2 = X0M1
              3 = X0M0
              4 = emulation*/

static int cpumode;
static void (**modeptr)(void);

/*Current opcode*/
static uint8_t w65816opcode;

#define a w65816a
#define x w65816x
#define y w65816y
#define s w65816s
#define pc w65816pc
#define p w65816p

#define cycles tubecycles
#define opcode w65816opcode

static int def = 1, divider = 0, banking = 0, banknum = 0;
static uint32_t w65816mask = 0xFFFF;
static uint16_t toldpc;

static const char *dbg65816_reg_names[] = { "AB", "X", "Y", "S", "P", "PC", "DP", "DB", "PB", NULL };

static int dbg_w65816 = 0;

static int dbg_debug_enable(int newvalue)
{
    int oldvalue = dbg_w65816;
    dbg_w65816 = newvalue;
    return oldvalue;
};

static inline uint8_t pack_flags(void)
{
    uint8_t flags = 0;

    if (p.c)
        flags |= 0x01;
    if (p.z)
        flags |= 0x02;
    if (p.i)
        flags |= 0x04;
    if (p.d)
        flags |= 0x08;
    if (p.ex)
        flags |= 0x10;
    if (p.m)
        flags |= 0x20;
    if (p.v)
        flags |= 0x40;
    if (p.n)
        flags |= 0x80;
    return flags;
}

static inline uint8_t pack_flags_em(uint8_t flags)
{

    if (p.c)
        flags |= 0x01;
    if (p.z)
        flags |= 0x02;
    if (p.i)
        flags |= 0x4;
    if (p.d)
        flags |= 0x08;
    if (p.v)
        flags |= 0x40;
    if (p.n)
        flags |= 0x80;
    return flags;
}

static inline void unpack_flags(uint8_t flags)
{
    p.c = flags & 0x01;
    p.z = flags & 0x02;
    p.i = flags & 0x04;
    p.d = flags & 0x08;
    p.ex = flags & 0x10;
    p.m = flags & 0x20;
    p.v = flags & 0x40;
    p.n = flags & 0x80;
}

static inline void unpack_flags_em(uint8_t flags)
{
    p.c = flags & 0x01;
    p.z = flags & 0x02;
    p.i = flags & 0x04;
    p.d = flags & 0x08;
    p.ex = p.m = 0;
    p.v = flags & 0x40;
    p.n = flags & 0x80;
}

static uint32_t dbg_reg_get(int which)
{
    switch (which) {
        case REG_A:
            return w65816a.w;
        case REG_X:
            return w65816x.w;
        case REG_Y:
            return w65816y.w;
        case REG_S:
            return w65816s.w;
        case REG_P:
            return pack_flags();
        case REG_PC:
            return pc;
        case REG_DP:
            return dp;
        case REG_DB:
            return dbr;
        case REG_PB:
            return pbr;
        default:
            log_warn("65816: attempt to get non-existent register");
            return 0;
    }
}

static void dbg_reg_set(int which, uint32_t value)
{
    switch (which) {
        case REG_A:
            w65816a.w = value;
            break;
        case REG_X:
            w65816x.w = value;
            break;
        case REG_Y:
            w65816y.w = value;
            break;
        case REG_S:
            w65816s.w = value;
            break;
        case REG_P:
            unpack_flags(value);
            break;
        case REG_PC:
            pc = value;
            break;
        case REG_DP:
            dp = value;
            break;
        case REG_DB:
            dbr = value;
            break;
        case REG_PB:
            pbr = value;
            break;
        default:
            log_warn("65816: attempt to set non-existent register");
    }
}

size_t dbg65816_print_flags(char *buf, size_t bufsize)
{
    if (bufsize >= 8) {
        *buf++ = p.n  ? 'N' : ' ';
        *buf++ = p.v  ? 'V' : ' ';
        *buf++ = p.m  ? 'M' : ' ';
        *buf++ = p.ex ? 'X' : ' ';
        *buf++ = p.d  ? 'D' : ' ';
        *buf++ = p.i  ? 'I' : ' ';
        *buf++ = p.z  ? 'Z' : ' ';
        *buf++ = p.c  ? 'C' : ' ';
        return 6;
    }
    return 0;
}

static size_t dbg_reg_print(int which, char *buf, size_t bufsize)
{
    switch (which) {
        case REG_P:
            return dbg65816_print_flags(buf, bufsize);
        default:
            return snprintf(buf, bufsize, "%04X", dbg_reg_get(which));
    }
}

static void dbg_reg_parse(int which, char *str)
{
    uint32_t value = strtol(str, NULL, 16);
    dbg_reg_set(which, value);
}

static uint32_t do_readmem65816(uint32_t addr);
static void do_writemem65816(uint32_t addr, uint32_t val);
static uint32_t dbg_disassemble(uint32_t addr, char *buf, size_t bufsize);

static uint32_t dbg_get_instr_addr(void)
{
    return toldpc;
}

cpu_debug_t tube65816_cpu_debug = {
    .cpu_name       = "65816",
    .debug_enable   = dbg_debug_enable,
    .memread        = do_readmem65816,
    .memwrite       = do_writemem65816,
    .disassemble    = dbg_disassemble,
    .reg_names      = dbg65816_reg_names,
    .reg_get        = dbg_reg_get,
    .reg_set        = dbg_reg_set,
    .reg_print      = dbg_reg_print,
    .reg_parse      = dbg_reg_parse,
    .get_instr_addr = dbg_get_instr_addr
};

static uint32_t dbg_disassemble(uint32_t addr, char *buf, size_t bufsize)
{
    return dbg6502_disassemble(&tube65816_cpu_debug, addr, buf, bufsize, W65816);
}

static uint32_t do_readmem65816(uint32_t a)
{
    uint8_t temp;
    a &= w65816mask;
    cycles--;
    if ((a & ~7) == 0xFEF8) {
        temp = tube_parasite_read(a);
        return temp;
    }
    if ((a & 0x78000) == 0x8000 && (def || (banking & 8)))
        return w65816rom[a & 0x7FFF];
    if ((a & 0x7C000) == 0x4000 && !def && (banking & 1))
        return w65816ram[(a & 0x3FFF) | ((banknum & 7) << 14)];
    if ((a & 0x7C000) == 0x8000 && !def && (banking & 2))
        return w65816ram[(a & 0x3FFF) | (((banknum >> 3) & 7) << 14)];
    return w65816ram[a];
}

uint8_t readmem65816(uint32_t addr)
{
    uint32_t value = do_readmem65816(addr);
    if (dbg_w65816)
        debug_memread(&tube65816_cpu_debug, addr, value, 1);
    return value;
}

static uint16_t readmemw65816(uint32_t a)
{
    uint16_t value;

    a &= w65816mask;
    value = do_readmem65816(a) | (do_readmem65816(a + 1) << 8);
    if (dbg_w65816)
        debug_memread(&tube65816_cpu_debug, a, value, 2);
    return value;
}

int endtimeslice;

void do_writemem65816(uint32_t a, uint32_t v)
{
    a &= w65816mask;
    cycles--;
    if ((a & ~7) == 0xFEF0) {
        switch (v & 7) {
            case 0:
            case 1:
                def = v & 1;
                break;
            case 2:
            case 3:
                divider = (divider >> 1) | ((v & 1) << 3);
                break;
            case 4:
            case 5:
                banking = (banking >> 1) | ((v & 1) << 3);
                break;
            case 6:
            case 7:
                banknum = (banknum >> 1) | ((v & 1) << 5);
                break;
        }
        if (def || !(banking & 4))
            w65816mask = 0xFFFF;
        else
            w65816mask = 0x7FFFF;
        return;
    }
    if ((a & ~7) == 0xFEF8) {
        tube_parasite_write(a, v);
        endtimeslice = 1;
        return;
    }
    if ((a & 0x7C000) == 0x4000 && !def && (banking & 1)) {
        w65816ram[(a & 0x3FFF) | ((banknum & 7) << 14)] = v;
        return;
    }
    if ((a & 0x7C000) == 0x8000 && !def && (banking & 2)) {
        w65816ram[(a & 0x3FFF) | (((banknum >> 3) & 7) << 14)] = v;
        return;
    }
    w65816ram[a] = v;
}

void writemem65816(uint32_t addr, uint8_t val)
{
    if (dbg_w65816)
        debug_memwrite(&tube65816_cpu_debug, addr, val, 1);
    do_writemem65816(addr, val);
}

static void writememw65816(uint32_t a, uint16_t v)
{
    if (dbg_w65816)
        debug_memwrite(&tube65816_cpu_debug, a, v, 2);
    a &= w65816mask;
    do_writemem65816(a, v);
    do_writemem65816(a + 1, v >> 8);
}

#define readmem(a)     readmem65816(a)
#define readmemw(a)    readmemw65816(a)
#define writemem(a,v)  writemem65816(a,v)
#define writememw(a,v) writememw65816(a,v)

#define clockspc(c)

static void updatecpumode(void);
static int inwai = 0;
/*Temporary variables*/
static uint32_t addr;

/*Addressing modes*/
static inline uint32_t absolute(void)
{
    uint32_t temp = readmemw(pbr | pc);
    pc += 2;
    return temp | dbr;
}

static inline uint32_t absolutex(void)
{
    uint32_t temp = (readmemw(pbr | pc)) + x.w + dbr;
    pc += 2;
    return temp;
}

static inline uint32_t absolutey(void)
{
    uint32_t temp = (readmemw(pbr | pc)) + y.w + dbr;
    pc += 2;
    return temp;
}

static inline uint32_t absolutelong(void)
{
    uint32_t temp = readmemw(pbr | pc);
    pc += 2;
    temp |= (readmem(pbr | pc) << 16);
    pc++;
    return temp;
}

static inline uint32_t absolutelongx(void)
{
    uint32_t temp = (readmemw(pbr | pc)) + x.w;
    pc += 2;
    temp += (readmem(pbr | pc) << 16);
    pc++;
    return temp;
}

static inline uint32_t zeropage(void)
{
    /* It's actually direct page, but I'm used to calling it zero page */
    uint32_t temp = readmem(pbr | pc);
    pc++;
    temp += dp;
    if (dp & 0xFF) {
        cycles--;
        clockspc(6);
    }
    return temp & 0xFFFF;
}

static inline uint32_t zeropagex(void)
{
    uint32_t temp = readmem(pbr | pc) + x.w;
    pc++;
    if (p.e)
        temp &= 0xFF;
    temp += dp;
    if (dp & 0xFF) {
        cycles--;
        clockspc(6);
    }
    return temp & 0xFFFF;
}

static inline uint32_t zeropagey(void)
{
    uint32_t temp = readmem(pbr | pc) + y.w;
    pc++;
    if (p.e)
        temp &= 0xFF;
    temp += dp;
    if (dp & 0xFF) {
        cycles--;
        clockspc(6);
    }
    return temp & 0xFFFF;
}

static inline uint32_t stack(void)
{
    uint32_t temp = readmem(pbr | pc);
    pc++;
    temp += s.w;
    return temp & 0xFFFF;
}

static inline uint32_t indirect(void)
{
    uint32_t temp = (readmem(pbr | pc) + dp) & 0xFFFF;
    pc++;
    return (readmemw(temp)) + dbr;
}

static inline uint32_t indirectx(void)
{
    uint32_t temp = (readmem(pbr | pc) + dp + x.w) & 0xFFFF;
    pc++;
    return (readmemw(temp)) + dbr;
}

static inline uint32_t jindirectx(void)
{
    /* JSR (,x) uses PBR instead of DBR, and 2 byte address insted of 1 + dp */
    uint32_t temp = (readmem(pbr | pc) + (readmem((pbr | pc) + 1) << 8) + x.w) + pbr;
    pc += 2;
    return temp;
}

static inline uint32_t indirecty(void)
{
    uint32_t temp = (readmem(pbr | pc) + dp) & 0xFFFF;
    pc++;
    return (readmemw(temp)) + y.w + dbr;
}

static inline uint32_t sindirecty(void)
{
    uint32_t temp = (readmem(pbr | pc) + s.w) & 0xFFFF;
    pc++;
    return (readmemw(temp)) + y.w + dbr;
}

static inline uint32_t indirectl(void)
{
    uint32_t temp, addr;
    temp = (readmem(pbr | pc) + dp) & 0xFFFF;
    pc++;
    addr = readmemw(temp) | (readmem(temp + 2) << 16);
    return addr;
}

static inline uint32_t indirectly(void)
{
    uint32_t temp, addr;
    temp = (readmem(pbr | pc) + dp) & 0xFFFF;
    pc++;
    addr = (readmemw(temp) | (readmem(temp + 2) << 16)) + y.w;
    return addr;
}

/*Flag setting*/
#define setzn8(v)  p.z=!(v); p.n=(v)&0x80
#define setzn16(v) p.z=!(v); p.n=(v)&0x8000

/*ADC/SBC macros*/
#define ADC8()  tempw=a.b.l+temp+((p.c)?1:0);                          \
                p.v=(!((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));       \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw&0x100;

#define ADC16() templ=a.w+tempw+((p.c)?1:0);                           \
                p.v=(!((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));      \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ&0x10000;

#define ADCBCD8()                                                       \
                tempw=(a.b.l&0xF)+(temp&0xF)+(p.c?1:0);                 \
                if (tempw>9)                                            \
                {                                                       \
                        tempw+=6;                                       \
                }                                                       \
                tempw+=((a.b.l&0xF0)+(temp&0xF0));                      \
                if (tempw>0x9F)                                         \
                {                                                       \
                        tempw+=0x60;                                    \
                }                                                       \
                p.v=(!((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));       \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw>0xFF;                                         \
                cycles--; clockspc(6);

#define ADCBCD16()                                                      \
                templ=(a.w&0xF)+(tempw&0xF)+(p.c?1:0);                  \
                if (templ>9)                                            \
                {                                                       \
                        templ+=6;                                       \
                }                                                       \
                templ+=((a.w&0xF0)+(tempw&0xF0));                       \
                if (templ>0x9F)                                         \
                {                                                       \
                        templ+=0x60;                                    \
                }                                                       \
                templ+=((a.w&0xF00)+(tempw&0xF00));                     \
                if (templ>0x9FF)                                        \
                {                                                       \
                        templ+=0x600;                                   \
                }                                                       \
                templ+=((a.w&0xF000)+(tempw&0xF000));                   \
                if (templ>0x9FFF)                                       \
                {                                                       \
                        templ+=0x6000;                                  \
                }                                                       \
                p.v=(!((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));      \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ>0xFFFF;                                       \
                cycles--; clockspc(6);

#define SBC8()  tempw=a.b.l-temp-((p.c)?0:1);                          \
                p.v=(((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));        \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw<=0xFF;

#define SBC16() templ=a.w-tempw-((p.c)?0:1);                           \
                p.v=(((a.w^tempw)&(a.w^templ))&0x8000);                 \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ<=0xFFFF;

#define SBCBCD8()                                                       \
                tempw=(a.b.l&0xF)-(temp&0xF)-(p.c?0:1);                 \
                if (tempw>9)                                            \
                {                                                       \
                        tempw-=6;                                       \
                }                                                       \
                tempw+=((a.b.l&0xF0)-(temp&0xF0));                      \
                if (tempw>0x9F)                                         \
                {                                                       \
                        tempw-=0x60;                                    \
                }                                                       \
                p.v=(((a.b.l^temp)&0x80)&&((a.b.l^tempw)&0x80));        \
                a.b.l=tempw&0xFF;                                       \
                setzn8(a.b.l);                                          \
                p.c=tempw<=0xFF;                                        \
                cycles--; clockspc(6);

#define SBCBCD16()                                                      \
                templ=(a.w&0xF)-(tempw&0xF)-(p.c?0:1);                  \
                if (templ>9)                                            \
                {                                                       \
                        templ-=6;                                       \
                }                                                       \
                templ+=((a.w&0xF0)-(tempw&0xF0));                       \
                if (templ>0x9F)                                         \
                {                                                       \
                        templ-=0x60;                                    \
                }                                                       \
                templ+=((a.w&0xF00)-(tempw&0xF00));                     \
                if (templ>0x9FF)                                        \
                {                                                       \
                        templ-=0x600;                                   \
                }                                                       \
                templ+=((a.w&0xF000)-(tempw&0xF000));                   \
                if (templ>0x9FFF)                                       \
                {                                                       \
                        templ-=0x6000;                                  \
                }                                                       \
                p.v=(((a.w^tempw)&0x8000)&&((a.w^templ)&0x8000));       \
                a.w=templ&0xFFFF;                                       \
                setzn16(a.w);                                           \
                p.c=templ<=0xFFFF;                                      \
                cycles--; clockspc(6);

/*Instructions*/
static void inca8(void)
{
    readmem(pbr | pc);
    a.b.l++;
    setzn8(a.b.l);
}

static void inca16(void)
{
    readmem(pbr | pc);
    a.w++;
    setzn16(a.w);
}

static void inx8(void)
{
    readmem(pbr | pc);
    x.b.l++;
    setzn8(x.b.l);
}

static void inx16(void)
{
    readmem(pbr | pc);
    x.w++;
    setzn16(x.w);
}

static void iny8(void)
{
    readmem(pbr | pc);
    y.b.l++;
    setzn8(y.b.l);
}

static void iny16(void)
{
    readmem(pbr | pc);
    y.w++;
    setzn16(y.w);
}

static void deca8(void)
{
    readmem(pbr | pc);
    a.b.l--;
    setzn8(a.b.l);
}

static void deca16(void)
{
    readmem(pbr | pc);
    a.w--;
    setzn16(a.w);
}

static void dex8(void)
{
    readmem(pbr | pc);
    x.b.l--;
    setzn8(x.b.l);
}

static void dex16(void)
{
    readmem(pbr | pc);
    x.w--;
    setzn16(x.w);
}

static void dey8(void)
{
    readmem(pbr | pc);
    y.b.l--;
    setzn8(y.b.l);
}

static void dey16(void)
{
    readmem(pbr | pc);
    y.w--;
    setzn16(y.w);
}

/*INC group*/
static void incZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn8(temp);
    writemem(addr, temp);
}

static void incZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn16(temp);
    writememw(addr, temp);
}

static void incZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn8(temp);
    writemem(addr, temp);
}

static void incZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn16(temp);
    writememw(addr, temp);
}

static void incAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn8(temp);
    writemem(addr, temp);
}

static void incAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn16(temp);
    writememw(addr, temp);
}

static void incAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn8(temp);
    writemem(addr, temp);
}

static void incAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp++;
    setzn16(temp);
    writememw(addr, temp);
}

/*DEC group*/
static void decZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn8(temp);
    writemem(addr, temp);
}

static void decZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn16(temp);
    writememw(addr, temp);
}

static void decZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn8(temp);
    writemem(addr, temp);
}

static void decZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn16(temp);
    writememw(addr, temp);
}

static void decAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn8(temp);
    writemem(addr, temp);
}

static void decAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn16(temp);
    writememw(addr, temp);
}

static void decAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn8(temp);
    writemem(addr, temp);
}

static void decAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    temp--;
    setzn16(temp);
    writememw(addr, temp);
}

/*Flag group*/
static void clc(void)
{
    readmem(pbr | pc);
    p.c = 0;
}

static void cld(void)
{
    readmem(pbr | pc);
    p.d = 0;
}

static void cli(void)
{
    readmem(pbr | pc);
    p.i = 0;
}

static void clv(void)
{
    readmem(pbr | pc);
    p.v = 0;
}

static void sec(void)
{
    readmem(pbr | pc);
    p.c = 1;
}

static void sed(void)
{
    readmem(pbr | pc);
    p.d = 1;
}

static void sei(void)
{
    readmem(pbr | pc);
    p.i = 1;
}

static void xce(void)
{
    int temp = p.c;
    p.c = p.e;
    p.e = temp;
    readmem(pbr | pc);
    updatecpumode();
}

static void sep(void)
{
    uint8_t temp = readmem(pbr | pc);
    pc++;
    if (temp & 1)
        p.c = 1;
    if (temp & 2)
        p.z = 1;
    if (temp & 4)
        p.i = 1;
    if (temp & 8)
        p.d = 1;
    if (temp & 0x40)
        p.v = 1;
    if (temp & 0x80)
        p.n = 1;
    if (!p.e) {
        if (temp & 0x10)
            p.ex = 1;
        if (temp & 0x20)
            p.m = 1;
        updatecpumode();
    }
}

static void rep65816(void)
{
    uint8_t temp = readmem(pbr | pc);
    pc++;
    if (temp & 1)
        p.c = 0;
    if (temp & 2)
        p.z = 0;
    if (temp & 4)
        p.i = 0;
    if (temp & 8)
        p.d = 0;
    if (temp & 0x40)
        p.v = 0;
    if (temp & 0x80)
        p.n = 0;
    if (!p.e) {
        if (temp & 0x10)
            p.ex = 0;
        if (temp & 0x20)
            p.m = 0;
        updatecpumode();
    }
}

/*Transfer group*/
static void tax8(void)
{
    readmem(pbr | pc);
    x.b.l = a.b.l;
    setzn8(x.b.l);
}

static void tay8(void)
{
    readmem(pbr | pc);
    y.b.l = a.b.l;
    setzn8(y.b.l);
}

static void txa8(void)
{
    readmem(pbr | pc);
    a.b.l = x.b.l;
    setzn8(a.b.l);
}

static void tya8(void)
{
    readmem(pbr | pc);
    a.b.l = y.b.l;
    setzn8(a.b.l);
}

static void tsx8(void)
{
    readmem(pbr | pc);
    x.b.l = s.b.l;
    setzn8(x.b.l);
}

static void txs8(void)
{
    readmem(pbr | pc);
    s.b.l = x.b.l;
}

static void txy8(void)
{
    readmem(pbr | pc);
    y.b.l = x.b.l;
    setzn8(y.b.l);
}

static void tyx8(void)
{
    readmem(pbr | pc);
    x.b.l = y.b.l;
    setzn8(x.b.l);
}

static void tax16(void)
{
    readmem(pbr | pc);
    x.w = a.w;
    setzn16(x.w);
}

static void tay16(void)
{
    readmem(pbr | pc);
    y.w = a.w;
    setzn16(y.w);
}

static void txa16(void)
{
    readmem(pbr | pc);
    a.w = x.w;
    setzn16(a.w);
}

static void tya16(void)
{
    readmem(pbr | pc);
    a.w = y.w;
    setzn16(a.w);
}

static void tsx16(void)
{
    readmem(pbr | pc);
    x.w = s.w;
    setzn16(x.w);
}

static void txs16(void)
{
    readmem(pbr | pc);
    s.w = x.w;
}

static void txy16(void)
{
    readmem(pbr | pc);
    y.w = x.w;
    setzn16(y.w);
}

static void tyx16(void)
{
    readmem(pbr | pc);
    x.w = y.w;
    setzn16(x.w);
}

/*LDX group*/
static void ldxImm8(void)
{
    x.b.l = readmem(pbr | pc);
    pc++;
    setzn8(x.b.l);
}

static void ldxZp8(void)
{
    addr = zeropage();
    x.b.l = readmem(addr);
    setzn8(x.b.l);
}

static void ldxZpy8(void)
{
    addr = zeropagey();
    x.b.l = readmem(addr);
    setzn8(x.b.l);
}

static void ldxAbs8(void)
{
    addr = absolute();
    x.b.l = readmem(addr);
    setzn8(x.b.l);
}

static void ldxAbsy8(void)
{
    addr = absolutey();
    x.b.l = readmem(addr);
    setzn8(x.b.l);
}

static void ldxImm16(void)
{
    x.w = readmemw(pbr | pc);
    pc += 2;
    setzn16(x.w);
}

static void ldxZp16(void)
{
    addr = zeropage();
    x.w = readmemw(addr);
    setzn16(x.w);
}

static void ldxZpy16(void)
{
    addr = zeropagey();
    x.w = readmemw(addr);
    setzn16(x.w);
}

static void ldxAbs16(void)
{
    addr = absolute();
    x.w = readmemw(addr);
    setzn16(x.w);
}

static void ldxAbsy16(void)
{
    addr = absolutey();
    x.w = readmemw(addr);
    setzn16(x.w);
}

/*LDY group*/
static void ldyImm8(void)
{
    y.b.l = readmem(pbr | pc);
    pc++;
    setzn8(y.b.l);
}

static void ldyZp8(void)
{
    addr = zeropage();
    y.b.l = readmem(addr);
    setzn8(y.b.l);
}

static void ldyZpx8(void)
{
    addr = zeropagex();
    y.b.l = readmem(addr);
    setzn8(y.b.l);
}

static void ldyAbs8(void)
{
    addr = absolute();
    y.b.l = readmem(addr);
    setzn8(y.b.l);
}

static void ldyAbsx8(void)
{
    addr = absolutex();
    y.b.l = readmem(addr);
    setzn8(y.b.l);
}

static void ldyImm16(void)
{
    y.w = readmemw(pbr | pc);
    pc += 2;
    setzn16(y.w);
}

static void ldyZp16(void)
{
    addr = zeropage();
    y.w = readmemw(addr);
    setzn16(y.w);
}

static void ldyZpx16(void)
{
    addr = zeropagex();
    y.w = readmemw(addr);
    setzn16(y.w);
}

static void ldyAbs16(void)
{
    addr = absolute();
    y.w = readmemw(addr);
    setzn16(y.w);
}

static void ldyAbsx16(void)
{
    addr = absolutex();
    y.w = readmemw(addr);
    setzn16(y.w);
}

/*LDA group*/
static void ldaImm8(void)
{
    a.b.l = readmem(pbr | pc);
    pc++;
    setzn8(a.b.l);
}

static void ldaZp8(void)
{
    addr = zeropage();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaZpx8(void)
{
    addr = zeropagex();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaSp8(void)
{
    addr = stack();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaSIndirecty8(void)
{
    addr = sindirecty();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaAbs8(void)
{
    addr = absolute();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaAbsx8(void)
{
    addr = absolutex();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaAbsy8(void)
{
    addr = absolutey();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaLong8(void)
{
    addr = absolutelong();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaLongx8(void)
{
    addr = absolutelongx();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaIndirect8(void)
{
    addr = indirect();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaIndirectx8(void)
{
    addr = indirectx();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaIndirecty8(void)
{
    addr = indirecty();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaIndirectLong8(void)
{
    addr = indirectl();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaIndirectLongy8(void)
{
    addr = indirectly();
    a.b.l = readmem(addr);
    setzn8(a.b.l);
}

static void ldaImm16(void)
{
    a.w = readmemw(pbr | pc);
    pc += 2;
    setzn16(a.w);
}

static void ldaZp16(void)
{
    addr = zeropage();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaZpx16(void)
{
    addr = zeropagex();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaSp16(void)
{
    addr = stack();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaSIndirecty16(void)
{
    addr = sindirecty();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaAbs16(void)
{
    addr = absolute();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaAbsx16(void)
{
    addr = absolutex();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaAbsy16(void)
{
    addr = absolutey();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaLong16(void)
{
    addr = absolutelong();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaLongx16(void)
{
    addr = absolutelongx();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaIndirect16(void)
{
    addr = indirect();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaIndirectx16(void)
{
    addr = indirectx();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaIndirecty16(void)
{
    addr = indirecty();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaIndirectLong16(void)
{
    addr = indirectl();
    a.w = readmemw(addr);
    setzn16(a.w);
}

static void ldaIndirectLongy16(void)
{
    addr = indirectly();
    a.w = readmemw(addr);
    setzn16(a.w);
}

/*STA group*/
static void staZp8(void)
{
    addr = zeropage();
    writemem(addr, a.b.l);
}

static void staZpx8(void)
{
    addr = zeropagex();
    writemem(addr, a.b.l);
}

static void staAbs8(void)
{
    addr = absolute();
    writemem(addr, a.b.l);
}

static void staAbsx8(void)
{
    addr = absolutex();
    writemem(addr, a.b.l);
}

static void staAbsy8(void)
{
    addr = absolutey();
    writemem(addr, a.b.l);
}

static void staLong8(void)
{
    addr = absolutelong();
    writemem(addr, a.b.l);
}

static void staLongx8(void)
{
    addr = absolutelongx();
    writemem(addr, a.b.l);
}

static void staIndirect8(void)
{
    addr = indirect();
    writemem(addr, a.b.l);
}

static void staIndirectx8(void)
{
    addr = indirectx();
    writemem(addr, a.b.l);
}

static void staIndirecty8(void)
{
    addr = indirecty();
    writemem(addr, a.b.l);
}

static void staIndirectLong8(void)
{
    addr = indirectl();
    writemem(addr, a.b.l);
}

static void staIndirectLongy8(void)
{
    addr = indirectly();
    writemem(addr, a.b.l);
}

static void staSp8(void)
{
    addr = stack();
    writemem(addr, a.b.l);
}

static void staSIndirecty8(void)
{
    addr = sindirecty();
    writemem(addr, a.b.l);
}

static void staZp16(void)
{
    addr = zeropage();
    writememw(addr, a.w);
}

static void staZpx16(void)
{
    addr = zeropagex();
    writememw(addr, a.w);
}

static void staAbs16(void)
{
    addr = absolute();
    writememw(addr, a.w);
}

static void staAbsx16(void)
{
    addr = absolutex();
    writememw(addr, a.w);
}

static void staAbsy16(void)
{
    addr = absolutey();
    writememw(addr, a.w);
}

static void staLong16(void)
{
    addr = absolutelong();
    writememw(addr, a.w);
}

static void staLongx16(void)
{
    addr = absolutelongx();
    writememw(addr, a.w);
}

static void staIndirect16(void)
{
    addr = indirect();
    writememw(addr, a.w);
}

static void staIndirectx16(void)
{
    addr = indirectx();
    writememw(addr, a.w);
}

static void staIndirecty16(void)
{
    addr = indirecty();
    writememw(addr, a.w);
}

static void staIndirectLong16(void)
{
    addr = indirectl();
    writememw(addr, a.w);
}

static void staIndirectLongy16(void)
{
    addr = indirectly();
    writememw(addr, a.w);
}

static void staSp16(void)
{
    addr = stack();
    writememw(addr, a.w);
}

static void staSIndirecty16(void)
{
    addr = sindirecty();
    writememw(addr, a.w);
}

/*STX group*/
static void stxZp8(void)
{
    addr = zeropage();
    writemem(addr, x.b.l);
}

static void stxZpy8(void)
{
    addr = zeropagey();
    writemem(addr, x.b.l);
}

static void stxAbs8(void)
{
    addr = absolute();
    writemem(addr, x.b.l);
}

static void stxZp16(void)
{
    addr = zeropage();
    writememw(addr, x.w);
}

static void stxZpy16(void)
{
    addr = zeropagey();
    writememw(addr, x.w);
}

static void stxAbs16(void)
{
    addr = absolute();
    writememw(addr, x.w);
}

/*STY group*/
static void styZp8(void)
{
    addr = zeropage();
    writemem(addr, y.b.l);
}

static void styZpx8(void)
{
    addr = zeropagex();
    writemem(addr, y.b.l);
}

static void styAbs8(void)
{
    addr = absolute();
    writemem(addr, y.b.l);
}

static void styZp16(void)
{
    addr = zeropage();
    writememw(addr, y.w);
}

static void styZpx16(void)
{
    addr = zeropagex();
    writememw(addr, y.w);
}

static void styAbs16(void)
{
    addr = absolute();
    writememw(addr, y.w);
}

/*STZ group*/
static void stzZp8(void)
{
    addr = zeropage();
    writemem(addr, 0);
}

static void stzZpx8(void)
{
    addr = zeropagex();
    writemem(addr, 0);
}

static void stzAbs8(void)
{
    addr = absolute();
    writemem(addr, 0);
}

static void stzAbsx8(void)
{
    addr = absolutex();
    writemem(addr, 0);
}

static void stzZp16(void)
{
    addr = zeropage();
    writememw(addr, 0);
}

static void stzZpx16(void)
{
    addr = zeropagex();
    writememw(addr, 0);
}

static void stzAbs16(void)
{
    addr = absolute();
    writememw(addr, 0);
}

static void stzAbsx16(void)
{
    addr = absolutex();
    writememw(addr, 0);
}

/*ADC group*/
static void adcImm8(void)
{
    uint16_t tempw;
    uint8_t temp = readmem(pbr | pc);
    pc++;
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcZp8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcZpx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcSp8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = stack();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcAbs8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcAbsx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcAbsy8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutey();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcLong8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutelong();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcLongx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutelongx();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcIndirect8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirect();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcIndirectx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectx();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcIndirecty8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirecty();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcsIndirecty8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = sindirecty();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcIndirectLong8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectl();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcIndirectLongy8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectly();
    temp = readmem(addr);
    if (p.d) {
        ADCBCD8();
    } else {
        ADC8();
    }
}

static void adcImm16(void)
{
    uint32_t templ;
    uint16_t tempw;
    tempw = readmemw(pbr | pc);
    pc += 2;
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcZp16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = zeropage();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcZpx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = zeropagex();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcSp16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = stack();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcAbs16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolute();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcAbsx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutex();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcAbsy16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutey();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcLong16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutelong();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcLongx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutelongx();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcIndirect16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirect();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcIndirectx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectx();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcIndirecty16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirecty();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcsIndirecty16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = sindirecty();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcIndirectLong16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectl();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

static void adcIndirectLongy16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectly();
    tempw = readmemw(addr);
    if (p.d) {
        ADCBCD16();
    } else {
        ADC16();
    }
}

/*SBC group*/
static void sbcImm8(void)
{
    uint16_t tempw;
    uint8_t temp = readmem(pbr | pc);
    pc++;
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcZp8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcZpx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcSp8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = stack();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcAbs8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcAbsx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcAbsy8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutey();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcLong8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutelong();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcLongx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = absolutelongx();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcIndirect8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirect();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcIndirectx8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectx();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcIndirecty8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirecty();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcsIndirecty8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = sindirecty();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcIndirectLong8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectl();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcIndirectLongy8(void)
{
    uint16_t tempw;
    uint8_t temp;
    addr = indirectly();
    temp = readmem(addr);
    if (p.d) {
        SBCBCD8();
    } else {
        SBC8();
    }
}

static void sbcImm16(void)
{
    uint32_t templ;
    uint16_t tempw;
    tempw = readmemw(pbr | pc);
    pc += 2;
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcZp16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = zeropage();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcZpx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = zeropagex();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcSp16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = stack();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcAbs16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolute();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcAbsx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutex();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcAbsy16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutey();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcLong16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutelong();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcLongx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = absolutelongx();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcIndirect16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirect();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcIndirectx16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectx();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcIndirecty16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirecty();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcsIndirecty16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = sindirecty();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcIndirectLong16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectl();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

static void sbcIndirectLongy16(void)
{
    uint32_t templ;
    uint16_t tempw;
    addr = indirectly();
    tempw = readmemw(addr);
    if (p.d) {
        SBCBCD16();
    } else {
        SBC16();
    }
}

/*EOR group*/
static void eorImm8(void)
{
    a.b.l ^= readmem(pbr | pc);
    pc++;
    setzn8(a.b.l);
}

static void eorZp8(void)
{
    addr = zeropage();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorZpx8(void)
{
    addr = zeropagex();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorSp8(void)
{
    addr = stack();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorAbs8(void)
{
    addr = absolute();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorAbsx8(void)
{
    addr = absolutex();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorAbsy8(void)
{
    addr = absolutey();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorLong8(void)
{
    addr = absolutelong();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorLongx8(void)
{
    addr = absolutelongx();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorIndirect8(void)
{
    addr = indirect();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorIndirectx8(void)
{
    addr = indirectx();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorIndirecty8(void)
{
    addr = indirecty();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorsIndirecty8(void)
{
    addr = sindirecty();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorIndirectLong8(void)
{
    addr = indirectl();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorIndirectLongy8(void)
{
    addr = indirectly();
    a.b.l ^= readmem(addr);
    setzn8(a.b.l);
}

static void eorImm16(void)
{
    a.w ^= readmemw(pbr | pc);
    pc += 2;
    setzn16(a.w);
}

static void eorZp16(void)
{
    addr = zeropage();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorZpx16(void)
{
    addr = zeropagex();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorSp16(void)
{
    addr = stack();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorAbs16(void)
{
    addr = absolute();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorAbsx16(void)
{
    addr = absolutex();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorAbsy16(void)
{
    addr = absolutey();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorLong16(void)
{
    addr = absolutelong();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorLongx16(void)
{
    addr = absolutelongx();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorIndirect16(void)
{
    addr = indirect();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorIndirectx16(void)
{
    addr = indirectx();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorIndirecty16(void)
{
    addr = indirecty();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorsIndirecty16(void)
{
    addr = sindirecty();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorIndirectLong16(void)
{
    addr = indirectl();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

static void eorIndirectLongy16(void)
{
    addr = indirectly();
    a.w ^= readmemw(addr);
    setzn16(a.w);
}

/*AND group*/
static void andImm8(void)
{
    a.b.l &= readmem(pbr | pc);
    pc++;
    setzn8(a.b.l);
}

static void andZp8(void)
{
    addr = zeropage();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andZpx8(void)
{
    addr = zeropagex();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andSp8(void)
{
    addr = stack();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andAbs8(void)
{
    addr = absolute();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andAbsx8(void)
{
    addr = absolutex();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andAbsy8(void)
{
    addr = absolutey();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andLong8(void)
{
    addr = absolutelong();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andLongx8(void)
{
    addr = absolutelongx();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andIndirect8(void)
{
    addr = indirect();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andIndirectx8(void)
{
    addr = indirectx();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andIndirecty8(void)
{
    addr = indirecty();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andsIndirecty8(void)
{
    addr = sindirecty();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andIndirectLong8(void)
{
    addr = indirectl();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andIndirectLongy8(void)
{
    addr = indirectly();
    a.b.l &= readmem(addr);
    setzn8(a.b.l);
}

static void andImm16(void)
{
    a.w &= readmemw(pbr | pc);
    pc += 2;
    setzn16(a.w);
}

static void andZp16(void)
{
    addr = zeropage();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andZpx16(void)
{
    addr = zeropagex();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andSp16(void)
{
    addr = stack();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andAbs16(void)
{
    addr = absolute();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andAbsx16(void)
{
    addr = absolutex();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andAbsy16(void)
{
    addr = absolutey();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andLong16(void)
{
    addr = absolutelong();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andLongx16(void)
{
    addr = absolutelongx();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andIndirect16(void)
{
    addr = indirect();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andIndirectx16(void)
{
    addr = indirectx();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andIndirecty16(void)
{
    addr = indirecty();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andsIndirecty16(void)
{
    addr = sindirecty();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andIndirectLong16(void)
{
    addr = indirectl();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

static void andIndirectLongy16(void)
{
    addr = indirectly();
    a.w &= readmemw(addr);
    setzn16(a.w);
}

/*ORA group*/
static void oraImm8(void)
{
    a.b.l |= readmem(pbr | pc);
    pc++;
    setzn8(a.b.l);
}

static void oraZp8(void)
{
    addr = zeropage();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraZpx8(void)
{
    addr = zeropagex();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraSp8(void)
{
    addr = stack();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraAbs8(void)
{
    addr = absolute();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraAbsx8(void)
{
    addr = absolutex();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraAbsy8(void)
{
    addr = absolutey();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraLong8(void)
{
    addr = absolutelong();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraLongx8(void)
{
    addr = absolutelongx();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraIndirect8(void)
{
    addr = indirect();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraIndirectx8(void)
{
    addr = indirectx();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraIndirecty8(void)
{
    addr = indirecty();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void orasIndirecty8(void)
{
    addr = sindirecty();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraIndirectLong8(void)
{
    addr = indirectl();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraIndirectLongy8(void)
{
    addr = indirectly();
    a.b.l |= readmem(addr);
    setzn8(a.b.l);
}

static void oraImm16(void)
{
    a.w |= readmemw(pbr | pc);
    pc += 2;
    setzn16(a.w);
}

static void oraZp16(void)
{
    addr = zeropage();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraZpx16(void)
{
    addr = zeropagex();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraSp16(void)
{
    addr = stack();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraAbs16(void)
{
    addr = absolute();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraAbsx16(void)
{
    addr = absolutex();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraAbsy16(void)
{
    addr = absolutey();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraLong16(void)
{
    addr = absolutelong();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraLongx16(void)
{
    addr = absolutelongx();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraIndirect16(void)
{
    addr = indirect();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraIndirectx16(void)
{
    addr = indirectx();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraIndirecty16(void)
{
    addr = indirecty();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void orasIndirecty16(void)
{
    addr = sindirecty();
    a.w |= readmem(addr);
    setzn16(a.w);
}

static void oraIndirectLong16(void)
{
    addr = indirectl();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

static void oraIndirectLongy16(void)
{
    addr = indirectly();
    a.w |= readmemw(addr);
    setzn16(a.w);
}

/*BIT group*/
static void bitImm8(void)
{
    uint8_t temp = readmem(pbr | pc);
    pc++;
    p.z = !(temp & a.b.l);
}

static void bitImm16(void)
{
    uint16_t temp = readmemw(pbr | pc);
    pc += 2;
    p.z = !(temp & a.w);
}

static void bitZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    p.z = !(temp & a.b.l);
    p.v = temp & 0x40;
    p.n = temp & 0x80;
}

static void bitZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    p.z = !(temp & a.w);
    p.v = temp & 0x4000;
    p.n = temp & 0x8000;
}

static void bitZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    p.z = !(temp & a.b.l);
    p.v = temp & 0x40;
    p.n = temp & 0x80;
}

static void bitZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    p.z = !(temp & a.w);
    p.v = temp & 0x4000;
    p.n = temp & 0x8000;
}

static void bitAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    p.z = !(temp & a.b.l);
    p.v = temp & 0x40;
    p.n = temp & 0x80;
}

static void bitAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    p.z = !(temp & a.w);
    p.v = temp & 0x4000;
    p.n = temp & 0x8000;
}

static void bitAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    p.z = !(temp & a.b.l);
    p.v = temp & 0x40;
    p.n = temp & 0x80;
}

static void bitAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    p.z = !(temp & a.w);
    p.v = temp & 0x4000;
    p.n = temp & 0x8000;
}

/*CMP group*/
static void cmpImm8(void)
{
    uint8_t temp;
    temp = readmem(pbr | pc);
    pc++;
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpSp8(void)
{
    uint8_t temp;
    addr = stack();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpAbsy8(void)
{
    uint8_t temp;
    addr = absolutey();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpLong8(void)
{
    uint8_t temp;
    addr = absolutelong();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpLongx8(void)
{
    uint8_t temp;
    addr = absolutelongx();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpIndirect8(void)
{
    uint8_t temp;
    addr = indirect();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpIndirectx8(void)
{
    uint8_t temp;
    addr = indirectx();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpIndirecty8(void)
{
    uint8_t temp;
    addr = indirecty();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpsIndirecty8(void)
{
    uint8_t temp;
    addr = sindirecty();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpIndirectLong8(void)
{
    uint8_t temp;
    addr = indirectl();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpIndirectLongy8(void)
{
    uint8_t temp;
    addr = indirectly();
    temp = readmem(addr);
    setzn8(a.b.l - temp);
    p.c = (a.b.l >= temp);
}

static void cmpImm16(void)
{
    uint16_t temp;
    temp = readmemw(pbr | pc);
    pc += 2;
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpSp16(void)
{
    uint16_t temp;
    addr = stack();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpAbsy16(void)
{
    uint16_t temp;
    addr = absolutey();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpLong16(void)
{
    uint16_t temp;
    addr = absolutelong();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpLongx16(void)
{
    uint16_t temp;
    addr = absolutelongx();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpIndirect16(void)
{
    uint16_t temp;
    addr = indirect();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpIndirectx16(void)
{
    uint16_t temp;
    addr = indirectx();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpIndirecty16(void)
{
    uint16_t temp;
    addr = indirecty();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpsIndirecty16(void)
{
    uint16_t temp;
    addr = sindirecty();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpIndirectLong16(void)
{
    uint16_t temp;
    addr = indirectl();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

static void cmpIndirectLongy16(void)
{
    uint16_t temp;
    addr = indirectly();
    temp = readmemw(addr);
    setzn16(a.w - temp);
    p.c = (a.w >= temp);
}

/*Stack Group*/
static void phb(void)
{
    readmem(pbr | pc);
    writemem(s.w, dbr >> 16);
    s.w--;
}

static void phbe(void)
{
    readmem(pbr | pc);
    writemem(s.w, dbr >> 16);
    s.b.l--;
}

static void phk(void)
{
    readmem(pbr | pc);
    writemem(s.w, pbr >> 16);
    s.w--;
}

static void phke(void)
{
    readmem(pbr | pc);
    writemem(s.w, pbr >> 16);
    s.b.l--;
}

static void pea(void)
{
    addr = readmemw(pbr | pc);
    pc += 2;
    writemem(s.w, addr >> 8);
    s.w--;
    writemem(s.w, addr & 0xFF);
    s.w--;
}

static void pei(void)
{
    addr = indirect();
    writemem(s.w, addr >> 8);
    s.w--;
    writemem(s.w, addr & 0xFF);
    s.w--;
}

static void per(void)
{
    addr = readmemw(pbr | pc);
    pc += 2;
    addr += pc;
    writemem(s.w, addr >> 8);
    s.w--;
    writemem(s.w, addr & 0xFF);
    s.w--;
}

static void phd(void)
{
    writemem(s.w, dp >> 8);
    s.w--;
    writemem(s.w, dp & 0xFF);
    s.w--;
}

static void pld(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    dp = readmem(s.w);
    s.w++;
    dp |= (readmem(s.w) << 8);
}

static void pha8(void)
{
    readmem(pbr | pc);
    writemem(s.w, a.b.l);
    s.w--;
}

static void pha16(void)
{
    readmem(pbr | pc);
    writemem(s.w, a.b.h);
    s.w--;
    writemem(s.w, a.b.l);
    s.w--;
}

static void phx8(void)
{
    readmem(pbr | pc);
    writemem(s.w, x.b.l);
    s.w--;
}

static void phx16(void)
{
    readmem(pbr | pc);
    writemem(s.w, x.b.h);
    s.w--;
    writemem(s.w, x.b.l);
    s.w--;
}

static void phy8(void)
{
    readmem(pbr | pc);
    writemem(s.w, y.b.l);
    s.w--;
}

static void phy16(void)
{
    readmem(pbr | pc);
    writemem(s.w, y.b.h);
    s.w--;
    writemem(s.w, y.b.l);
    s.w--;
}

static void pla8(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    a.b.l = readmem(s.w);
    setzn8(a.b.l);
}

static void pla16(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    a.b.l = readmem(s.w);
    s.w++;
    a.b.h = readmem(s.w);
    setzn16(a.w);
}

static void plx8(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    x.b.l = readmem(s.w);
    setzn8(x.b.l);
}

static void plx16(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    x.b.l = readmem(s.w);
    s.w++;
    x.b.h = readmem(s.w);
    setzn16(x.w);
}

static void ply8(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    y.b.l = readmem(s.w);
    setzn8(y.b.l);
}

static void ply16(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    y.b.l = readmem(s.w);
    s.w++;
    y.b.h = readmem(s.w);
    setzn16(y.w);
}

static void plb(void)
{
    readmem(pbr | pc);
    s.w++;
    cycles--;
    clockspc(6);
    dbr = readmem(s.w) << 16;
}

static void plbe(void)
{
    readmem(pbr | pc);
    s.b.l++;
    cycles--;
    clockspc(6);
    dbr = readmem(s.w) << 16;
}

static void plp(void)
{
    unpack_flags(readmem(s.w + 1));
    s.w++;
    cycles -= 2;
    clockspc(12);
    updatecpumode();
}

static void plpe(void)
{
    s.b.l++;
    unpack_flags_em(readmem(s.w));
    cycles -= 2;
    clockspc(12);
}

static void php(void)
{
    uint8_t flags = pack_flags();
    readmem(pbr | pc);
    writemem(s.w, flags);
    s.w--;
}

static void phpe(void)
{
    readmem(pbr | pc);
    writemem(s.w, pack_flags_em(0x30));
    s.b.l--;
}

/*CPX group*/
static void cpxImm8(void)
{
    uint8_t temp = readmem(pbr | pc);
    pc++;
    setzn8(x.b.l - temp);
    p.c = (x.b.l >= temp);
}

static void cpxImm16(void)
{
    uint16_t temp = readmemw(pbr | pc);
    pc += 2;
    setzn16(x.w - temp);
    p.c = (x.w >= temp);
}

static void cpxZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    setzn8(x.b.l - temp);
    p.c = (x.b.l >= temp);
}

static void cpxZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    setzn16(x.w - temp);
    p.c = (x.w >= temp);
}

static void cpxAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    setzn8(x.b.l - temp);
    p.c = (x.b.l >= temp);
}

static void cpxAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    setzn16(x.w - temp);
    p.c = (x.w >= temp);
}

/*CPY group*/
static void cpyImm8(void)
{
    uint8_t temp = readmem(pbr | pc);
    pc++;
    setzn8(y.b.l - temp);
    p.c = (y.b.l >= temp);
}

static void cpyImm16(void)
{
    uint16_t temp = readmemw(pbr | pc);
    pc += 2;
    setzn16(y.w - temp);
    p.c = (y.w >= temp);
}

static void cpyZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    setzn8(y.b.l - temp);
    p.c = (y.b.l >= temp);
}

static void cpyZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    setzn16(y.w - temp);
    p.c = (y.w >= temp);
}

static void cpyAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    setzn8(y.b.l - temp);
    p.c = (y.b.l >= temp);
}

static void cpyAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    setzn16(y.w - temp);
    p.c = (y.w >= temp);
}

/*Branch group*/
static void bcc(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (!p.c) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bcs(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (p.c) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void beq(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (p.z) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bne(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (!p.z) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bpl(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (!p.n) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bmi(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (p.n) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bvc(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (!p.v) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bvs(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    if (p.v) {
        pc += temp;
        cycles--;
        clockspc(6);
    }
}

static void bra(void)
{
    int8_t temp = (int8_t) readmem(pbr | pc);
    pc++;
    pc += temp;
    cycles--;
    clockspc(6);
}

static void brl(void)
{
    uint16_t temp = readmemw(pbr | pc);
    pc += 2;
    pc += temp;
    cycles--;
    clockspc(6);
}

/*Jump group*/
static void jmp(void)
{
    addr = readmemw(pbr | pc);
    pc = addr;
}

static void jmplong(void)
{
    addr = readmemw(pbr | pc) | (readmem((pbr | pc) + 2) << 16);
    pc = addr & 0xFFFF;
    pbr = addr & 0xFF0000;
}

static void jmpind(void)
{
    addr = readmemw(pbr | pc);
    pc = readmemw(addr);
}

static void jmpindx(void)
{
    addr = (readmemw(pbr | pc)) + x.w + pbr;
    pc = readmemw(addr);
}

static void jmlind(void)
{
    addr = readmemw(pbr | pc);
    pc = readmemw(addr);
    pbr = readmem(addr + 2) << 16;
}

static void jsr(void)
{
    addr = readmemw(pbr | pc);
    pc++;
    readmem(pbr | pc);
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    pc = addr;
}

static void jsre(void)
{
    addr = readmemw(pbr | pc);
    pc++;
    readmem(pbr | pc);
    writemem(s.w, pc >> 8);
    s.b.l--;
    writemem(s.w, pc & 0xFF);
    s.b.l--;
    pc = addr;
}

static void jsrIndx(void)
{
    addr = jindirectx();
    pc--;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    pc = readmemw(addr);
}

static void jsrIndxe(void)
{
    addr = jindirectx();
    pc--;
    writemem(s.w, pc >> 8);
    s.b.l--;
    writemem(s.w, pc & 0xFF);
    s.b.l--;
    pc = readmemw(addr);
}

static void jsl(void)
{
    uint8_t temp;
    addr = readmemw(pbr | pc);
    pc += 2;
    temp = readmem(pbr | pc);
    writemem(s.w, pbr >> 16);
    s.w--;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    pc = addr;
    pbr = temp << 16;
}

static void jsle(void)
{
    uint8_t temp;
    addr = readmemw(pbr | pc);
    pc += 2;
    temp = readmem(pbr | pc);
    writemem(s.w, pbr >> 16);
    s.b.l--;
    writemem(s.w, pc >> 8);
    s.b.l--;
    writemem(s.w, pc & 0xFF);
    s.b.l--;
    pc = addr;
    pbr = temp << 16;
}

static void rtl(void)
{
    cycles -= 3;
    clockspc(18);
    pc = readmemw(s.w + 1);
    s.w += 2;
    pbr = readmem(s.w + 1) << 16;
    s.w++;
    pc++;
}

static void rtle(void)
{
    cycles -= 3;
    clockspc(18);
    s.b.l++;
    pc = readmem(s.w);
    s.b.l++;
    pc |= (readmem(s.w) << 8);
    s.b.l++;
    pbr = readmem(s.w) << 16;
    pc++;
}

static void rts(void)
{
    cycles -= 3;
    clockspc(18);
    pc = readmemw(s.w + 1);
    s.w += 2;
    pc++;
}

static void rtse(void)
{
    cycles -= 3;
    clockspc(18);
    s.b.l++;
    pc = readmem(s.w);
    s.b.l++;
    pc |= (readmem(s.w) << 8);
    pc++;
}

static void rti(void)
{
    cycles--;
    s.w++;
    clockspc(6);
    unpack_flags(readmem(s.w));
    s.w++;
    pc = readmem(s.w);
    s.w++;
    pc |= (readmem(s.w) << 8);
    s.w++;
    pbr = readmem(s.w) << 16;
    updatecpumode();
}

static void rtie(void)
{
    cycles--;
    s.b.l++;
    clockspc(6);
    unpack_flags_em(readmem(s.w));
    s.b.l++;
    pc = readmem(s.w);
    s.b.l++;
    pc |= (readmem(s.w) << 8);
    updatecpumode();
}

/*Shift group*/
static void asla8(void)
{
    readmem(pbr | pc);
    p.c = a.b.l & 0x80;
    a.b.l <<= 1;
    setzn8(a.b.l);
}

static void asla16(void)
{
    readmem(pbr | pc);
    p.c = a.w & 0x8000;
    a.w <<= 1;
    setzn16(a.w);
}

static void aslZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x80;
    temp <<= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void aslZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x8000;
    temp <<= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void aslZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x80;
    temp <<= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void aslZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x8000;
    temp <<= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void aslAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x80;
    temp <<= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void aslAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x8000;
    temp <<= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void aslAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x80;
    temp <<= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void aslAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 0x8000;
    temp <<= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void lsra8(void)
{
    readmem(pbr | pc);
    p.c = a.b.l & 1;
    a.b.l >>= 1;
    setzn8(a.b.l);
}

static void lsra16(void)
{
    readmem(pbr | pc);
    p.c = a.w & 1;
    a.w >>= 1;
    setzn16(a.w);
}

static void lsrZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void lsrZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void lsrZpx8(void)
{
    uint8_t temp;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void lsrZpx16(void)
{
    uint16_t temp;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void lsrAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void lsrAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void lsrAbsx8(void)
{
    uint8_t temp;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void lsrAbsx16(void)
{
    uint16_t temp;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    p.c = temp & 1;
    temp >>= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void rola8(void)
{
    readmem(pbr | pc);
    addr = p.c;
    p.c = a.b.l & 0x80;
    a.b.l <<= 1;
    if (addr)
        a.b.l |= 1;
    setzn8(a.b.l);
}

static void rola16(void)
{
    readmem(pbr | pc);
    addr = p.c;
    p.c = a.w & 0x8000;
    a.w <<= 1;
    if (addr)
        a.w |= 1;
    setzn16(a.w);
}

static void rolZp8(void)
{
    uint8_t temp;
    int tempc;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x80;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void rolZp16(void)
{
    uint16_t temp;
    int tempc;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x8000;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void rolZpx8(void)
{
    uint8_t temp;
    int tempc;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x80;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void rolZpx16(void)
{
    uint16_t temp;
    int tempc;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x8000;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void rolAbs8(void)
{
    uint8_t temp;
    int tempc;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x80;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void rolAbs16(void)
{
    uint16_t temp;
    int tempc;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x8000;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void rolAbsx8(void)
{
    uint8_t temp;
    int tempc;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x80;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn8(temp);
    writemem(addr, temp);
}

static void rolAbsx16(void)
{
    uint16_t temp;
    int tempc;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 0x8000;
    temp <<= 1;
    if (tempc)
        temp |= 1;
    setzn16(temp);
    writememw(addr, temp);
}

static void rora8(void)
{
    readmem(pbr | pc);
    addr = p.c;
    p.c = a.b.l & 1;
    a.b.l >>= 1;
    if (addr)
        a.b.l |= 0x80;
    setzn8(a.b.l);
}

static void rora16(void)
{
    readmem(pbr | pc);
    addr = p.c;
    p.c = a.w & 1;
    a.w >>= 1;
    if (addr)
        a.w |= 0x8000;
    setzn16(a.w);
}

static void rorZp8(void)
{
    uint8_t temp;
    int tempc;
    addr = zeropage();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x80;
    setzn8(temp);
    writemem(addr, temp);
}

static void rorZp16(void)
{
    uint16_t temp;
    int tempc;
    addr = zeropage();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x8000;
    setzn16(temp);
    writememw(addr, temp);
}

static void rorZpx8(void)
{
    uint8_t temp;
    int tempc;
    addr = zeropagex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x80;
    setzn8(temp);
    writemem(addr, temp);
}

static void rorZpx16(void)
{
    uint16_t temp;
    int tempc;
    addr = zeropagex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x8000;
    setzn16(temp);
    writememw(addr, temp);
}

static void rorAbs8(void)
{
    uint8_t temp;
    int tempc;
    addr = absolute();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x80;
    setzn8(temp);
    writemem(addr, temp);
}

static void rorAbs16(void)
{
    uint16_t temp;
    int tempc;
    addr = absolute();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x8000;
    setzn16(temp);
    writememw(addr, temp);
}

static void rorAbsx8(void)
{
    uint8_t temp;
    int tempc;
    addr = absolutex();
    temp = readmem(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x80;
    setzn8(temp);
    writemem(addr, temp);
}

static void rorAbsx16(void)
{
    uint16_t temp;
    int tempc;
    addr = absolutex();
    temp = readmemw(addr);
    cycles--;
    clockspc(6);
    tempc = p.c;
    p.c = temp & 1;
    temp >>= 1;
    if (tempc)
        temp |= 0x8000;
    setzn16(temp);
    writememw(addr, temp);
}

/*Misc group*/
static void xba(void)
{
    readmem(pbr | pc);
    a.w = (a.w >> 8) | (a.w << 8);
    setzn8(a.b.l);
}

static void nop(void)
{
    cycles--;
    clockspc(6);
}

static void tcd(void)
{
    readmem(pbr | pc);
    dp = a.w;
    setzn16(dp);
}

static void tdc(void)
{
    readmem(pbr | pc);
    a.w = dp;
    setzn16(a.w);
}

static void tcs(void)
{
    readmem(pbr | pc);
    s.w = a.w;
}

static void tsc(void)
{
    readmem(pbr | pc);
    a.w = s.w;
    setzn16(a.w);
}

static void trbZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    p.z = !(a.b.l & temp);
    temp &= ~a.b.l;
    cycles--;
    clockspc(6);
    writemem(addr, temp);
}

static void trbZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    p.z = !(a.w & temp);
    temp &= ~a.w;
    cycles--;
    clockspc(6);
    writememw(addr, temp);
}

static void trbAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    p.z = !(a.b.l & temp);
    temp &= ~a.b.l;
    cycles--;
    clockspc(6);
    writemem(addr, temp);
}

static void trbAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    p.z = !(a.w & temp);
    temp &= ~a.w;
    cycles--;
    clockspc(6);
    writememw(addr, temp);
}

static void tsbZp8(void)
{
    uint8_t temp;
    addr = zeropage();
    temp = readmem(addr);
    p.z = !(a.b.l & temp);
    temp |= a.b.l;
    cycles--;
    clockspc(6);
    writemem(addr, temp);
}

static void tsbZp16(void)
{
    uint16_t temp;
    addr = zeropage();
    temp = readmemw(addr);
    p.z = !(a.w & temp);
    temp |= a.w;
    cycles--;
    clockspc(6);
    writememw(addr, temp);
}

static void tsbAbs8(void)
{
    uint8_t temp;
    addr = absolute();
    temp = readmem(addr);
    p.z = !(a.b.l & temp);
    temp |= a.b.l;
    cycles--;
    clockspc(6);
    writemem(addr, temp);
}

static void tsbAbs16(void)
{
    uint16_t temp;
    addr = absolute();
    temp = readmemw(addr);
    p.z = !(a.w & temp);
    temp |= a.w;
    cycles--;
    clockspc(6);
    writememw(addr, temp);
}

static void wai(void)
{
    readmem(pbr | pc);
    inwai = 1;
    pc--;
}

static void mvp(void)
{
    uint8_t temp;
    dbr = (readmem(pbr | pc)) << 16;
    pc++;
    addr = (readmem(pbr | pc)) << 16;
    pc++;
    temp = readmem(addr | x.w);
    writemem(dbr | y.w, temp);
    x.w--;
    y.w--;
    a.w--;
    if (a.w != 0xFFFF)
        pc -= 3;
    cycles -= 2;
    clockspc(12);
}

static void mvn(void)
{
    uint8_t temp;
    dbr = (readmem(pbr | pc)) << 16;
    pc++;
    addr = (readmem(pbr | pc)) << 16;
    pc++;
    temp = readmem(addr | x.w);
    writemem(dbr | y.w, temp);
    x.w++;
    y.w++;
    a.w--;
    if (a.w != 0xFFFF)
        pc -= 3;
    cycles -= 2;
    clockspc(12);
}

static void op_brk(void)
{
    pc++;
    writemem(s.w, pbr >> 16);
    s.w--;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    writemem(s.w, pack_flags());
    s.w--;
    pc = readmemw(0xFFE6);
    pbr = 0;
    p.i = 1;
    p.d = 0;
}

static void brke(void)
{
    pc++;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    writemem(s.w, pack_flags_em(0x30));
    s.w--;
    pc = readmemw(0xFFFE);
    pbr = 0;
    p.i = 1;
    p.d = 0;
}

static void cop(void)
{
    pc++;
    writemem(s.w, pbr >> 16);
    s.w--;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    writemem(s.w, pack_flags());
    s.w--;
    pc = readmemw(0xFFE4);
    pbr = 0;
    p.i = 1;
    p.d = 0;
}

static void cope(void)
{
    pc++;
    writemem(s.w, pc >> 8);
    s.w--;
    writemem(s.w, pc & 0xFF);
    s.w--;
    writemem(s.w, pack_flags_em(0));
    s.w--;
    pc = readmemw(0xFFF4);
    pbr = 0;
    p.i = 1;
    p.d = 0;
}

static void wdm(void)
{
    readmem(pc);
    pc++;
}

static void stp(void)
{
    /* No point emulating this properly as the external support circuitry isn't there */
    pc--;
    cycles -= 600;
}

/*Opcode table*/
static void (*opcodes[5][256])() =
{
    {
        op_brk,             /* X1M1 00 */
        oraIndirectx8,      /* X1M1 01 */
        cop,                /* X1M1 02 */
        oraSp8,             /* X1M1 03 */
        tsbZp8,             /* X1M1 04 */
        oraZp8,             /* X1M1 05 */
        aslZp8,             /* X1M1 06 */
        oraIndirectLong8,   /* X1M1 07 */
        php,                /* X1M1 08 */
        oraImm8,            /* X1M1 09 */
        asla8,              /* X1M1 0a */
        phd,                /* X1M1 0b */
        tsbAbs8,            /* X1M1 0c */
        oraAbs8,            /* X1M1 0d */
        aslAbs8,            /* X1M1 0e */
        oraLong8,           /* X1M1 0f */
        bpl,                /* X1M1 10 */
        oraIndirecty8,      /* X1M1 11 */
        oraIndirect8,       /* X1M1 12 */
        orasIndirecty8,     /* X1M1 13 */
        trbZp8,             /* X1M1 14 */
        oraZpx8,            /* X1M1 15 */
        aslZpx8,            /* X1M1 16 */
        oraIndirectLongy8,  /* X1M1 17 */
        clc,                /* X1M1 18 */
        oraAbsy8,           /* X1M1 19 */
        inca8,              /* X1M1 1a */
        tcs,                /* X1M1 1b */
        trbAbs8,            /* X1M1 1c */
        oraAbsx8,           /* X1M1 1d */
        aslAbsx8,           /* X1M1 1e */
        oraLongx8,          /* X1M1 1f */
        jsr,                /* X1M1 20 */
        andIndirectx8,      /* X1M1 21 */
        jsl,                /* X1M1 22 */
        andSp8,             /* X1M1 23 */
        bitZp8,             /* X1M1 24 */
        andZp8,             /* X1M1 25 */
        rolZp8,             /* X1M1 26 */
        andIndirectLong8,   /* X1M1 27 */
        plp,                /* X1M1 28 */
        andImm8,            /* X1M1 29 */
        rola8,              /* X1M1 2a */
        pld,                /* X1M1 2b */
        bitAbs8,            /* X1M1 2c */
        andAbs8,            /* X1M1 2d */
        rolAbs8,            /* X1M1 2e */
        andLong8,           /* X1M1 2f */
        bmi,                /* X1M1 30 */
        andIndirecty8,      /* X1M1 31 */
        andIndirect8,       /* X1M1 32 */
        andsIndirecty8,     /* X1M1 33 */
        bitZpx8,            /* X1M1 34 */
        andZpx8,            /* X1M1 35 */
        rolZpx8,            /* X1M1 36 */
        andIndirectLongy8,  /* X1M1 37 */
        sec,                /* X1M1 38 */
        andAbsy8,           /* X1M1 39 */
        deca8,              /* X1M1 3a */
        tsc,                /* X1M1 3b */
        bitAbsx8,           /* X1M1 3c */
        andAbsx8,           /* X1M1 3d */
        rolAbsx8,           /* X1M1 3e */
        andLongx8,          /* X1M1 3f */
        rti,                /* X1M1 40 */
        eorIndirectx8,      /* X1M1 41 */
        wdm,                /* X1M1 42 */
        eorSp8,             /* X1M1 43 */
        mvp,                /* X1M1 44 */
        eorZp8,             /* X1M1 45 */
        lsrZp8,             /* X1M1 46 */
        eorIndirectLong8,   /* X1M1 47 */
        pha8,               /* X1M1 48 */
        eorImm8,            /* X1M1 49 */
        lsra8,              /* X1M1 4a */
        phk,                /* X1M1 4b */
        jmp,                /* X1M1 4c */
        eorAbs8,            /* X1M1 4d */
        lsrAbs8,            /* X1M1 4e */
        eorLong8,           /* X1M1 4f */
        bvc,                /* X1M1 50 */
        eorIndirecty8,      /* X1M1 51 */
        eorIndirect8,       /* X1M1 52 */
        eorsIndirecty8,     /* X1M1 53 */
        mvn,                /* X1M1 54 */
        eorZpx8,            /* X1M1 55 */
        lsrZpx8,            /* X1M1 56 */
        eorIndirectLongy8,  /* X1M1 57 */
        cli,                /* X1M1 58 */
        eorAbsy8,           /* X1M1 59 */
        phy8,               /* X1M1 5a */
        tcd,                /* X1M1 5b */
        jmplong,            /* X1M1 5c */
        eorAbsx8,           /* X1M1 5d */
        lsrAbsx8,           /* X1M1 5e */
        eorLongx8,          /* X1M1 5f */
        rts,                /* X1M1 60 */
        adcIndirectx8,      /* X1M1 61 */
        per,                /* X1M1 62 */
        adcSp8,             /* X1M1 63 */
        stzZp8,             /* X1M1 64 */
        adcZp8,             /* X1M1 65 */
        rorZp8,             /* X1M1 66 */
        adcIndirectLong8,   /* X1M1 67 */
        pla8,               /* X1M1 68 */
        adcImm8,            /* X1M1 69 */
        rora8,              /* X1M1 6a */
        rtl,                /* X1M1 6b */
        jmpind,             /* X1M1 6c */
        adcAbs8,            /* X1M1 6d */
        rorAbs8,            /* X1M1 6e */
        adcLong8,           /* X1M1 6f */
        bvs,                /* X1M1 70 */
        adcIndirecty8,      /* X1M1 71 */
        adcIndirect8,       /* X1M1 72 */
        adcsIndirecty8,     /* X1M1 73 */
        stzZpx8,            /* X1M1 74 */
        adcZpx8,            /* X1M1 75 */
        rorZpx8,            /* X1M1 76 */
        adcIndirectLongy8,  /* X1M1 77 */
        sei,                /* X1M1 78 */
        adcAbsy8,           /* X1M1 79 */
        ply8,               /* X1M1 7a */
        tdc,                /* X1M1 7b */
        jmpindx,            /* X1M1 7c */
        adcAbsx8,           /* X1M1 7d */
        rorAbsx8,           /* X1M1 7e */
        adcLongx8,          /* X1M1 7f */
        bra,                /* X1M1 80 */
        staIndirectx8,      /* X1M1 81 */
        brl,                /* X1M1 82 */
        staSp8,             /* X1M1 83 */
        styZp8,             /* X1M1 84 */
        staZp8,             /* X1M1 85 */
        stxZp8,             /* X1M1 86 */
        staIndirectLong8,   /* X1M1 87 */
        dey8,               /* X1M1 88 */
        bitImm8,            /* X1M1 89 */
        txa8,               /* X1M1 8a */
        phb,                /* X1M1 8b */
        styAbs8,            /* X1M1 8c */
        staAbs8,            /* X1M1 8d */
        stxAbs8,            /* X1M1 8e */
        staLong8,           /* X1M1 8f */
        bcc,                /* X1M1 90 */
        staIndirecty8,      /* X1M1 91 */
        staIndirect8,       /* X1M1 92 */
        staSIndirecty8,     /* X1M1 93 */
        styZpx8,            /* X1M1 94 */
        staZpx8,            /* X1M1 95 */
        stxZpy8,            /* X1M1 96 */
        staIndirectLongy8,  /* X1M1 97 */
        tya8,               /* X1M1 98 */
        staAbsy8,           /* X1M1 99 */
        txs8,               /* X1M1 9a */
        txy8,               /* X1M1 9b */
        stzAbs8,            /* X1M1 9c */
        staAbsx8,           /* X1M1 9d */
        stzAbsx8,           /* X1M1 9e */
        staLongx8,          /* X1M1 9f */
        ldyImm8,            /* X1M1 a0 */
        ldaIndirectx8,      /* X1M1 a1 */
        ldxImm8,            /* X1M1 a2 */
        ldaSp8,             /* X1M1 a3 */
        ldyZp8,             /* X1M1 a4 */
        ldaZp8,             /* X1M1 a5 */
        ldxZp8,             /* X1M1 a6 */
        ldaIndirectLong8,   /* X1M1 a7 */
        tay8,               /* X1M1 a8 */
        ldaImm8,            /* X1M1 a9 */
        tax8,               /* X1M1 aa */
        plb,                /* X1M1 ab */
        ldyAbs8,            /* X1M1 ac */
        ldaAbs8,            /* X1M1 ad */
        ldxAbs8,            /* X1M1 ae */
        ldaLong8,           /* X1M1 af */
        bcs,                /* X1M1 b0 */
        ldaIndirecty8,      /* X1M1 b1 */
        ldaIndirect8,       /* X1M1 b2 */
        ldaSIndirecty8,     /* X1M1 b3 */
        ldyZpx8,            /* X1M1 b4 */
        ldaZpx8,            /* X1M1 b5 */
        ldxZpy8,            /* X1M1 b6 */
        ldaIndirectLongy8,  /* X1M1 b7 */
        clv,                /* X1M1 b8 */
        ldaAbsy8,           /* X1M1 b9 */
        tsx8,               /* X1M1 ba */
        tyx8,               /* X1M1 bb */
        ldyAbsx8,           /* X1M1 bc */
        ldaAbsx8,           /* X1M1 bd */
        ldxAbsy8,           /* X1M1 be */
        ldaLongx8,          /* X1M1 bf */
        cpyImm8,            /* X1M1 c0 */
        cmpIndirectx8,      /* X1M1 c1 */
        rep65816,           /* X1M1 c2 */
        cmpSp8,             /* X1M1 c3 */
        cpyZp8,             /* X1M1 c4 */
        cmpZp8,             /* X1M1 c5 */
        decZp8,             /* X1M1 c6 */
        cmpIndirectLong8,   /* X1M1 c7 */
        iny8,               /* X1M1 c8 */
        cmpImm8,            /* X1M1 c9 */
        dex8,               /* X1M1 ca */
        wai,                /* X1M1 cb */
        cpyAbs8,            /* X1M1 cc */
        cmpAbs8,            /* X1M1 cd */
        decAbs8,            /* X1M1 ce */
        cmpLong8,           /* X1M1 cf */
        bne,                /* X1M1 d0 */
        cmpIndirecty8,      /* X1M1 d1 */
        cmpIndirect8,       /* X1M1 d2 */
        cmpsIndirecty8,     /* X1M1 d3 */
        pei,                /* X1M1 d4 */
        cmpZpx8,            /* X1M1 d5 */
        decZpx8,            /* X1M1 d6 */
        cmpIndirectLongy8,  /* X1M1 d7 */
        cld,                /* X1M1 d8 */
        cmpAbsy8,           /* X1M1 d9 */
        phx8,               /* X1M1 da */
        stp,                /* X1M1 db */
        jmlind,             /* X1M1 dc */
        cmpAbsx8,           /* X1M1 dd */
        decAbsx8,           /* X1M1 de */
        cmpLongx8,          /* X1M1 df */
        cpxImm8,            /* X1M1 e0 */
        sbcIndirectx8,      /* X1M1 e1 */
        sep,                /* X1M1 e2 */
        sbcSp8,             /* X1M1 e3 */
        cpxZp8,             /* X1M1 e4 */
        sbcZp8,             /* X1M1 e5 */
        incZp8,             /* X1M1 e6 */
        sbcIndirectLong8,   /* X1M1 e7 */
        inx8,               /* X1M1 e8 */
        sbcImm8,            /* X1M1 e9 */
        nop,                /* X1M1 ea */
        xba,                /* X1M1 eb */
        cpxAbs8,            /* X1M1 ec */
        sbcAbs8,            /* X1M1 ed */
        incAbs8,            /* X1M1 ee */
        sbcLong8,           /* X1M1 ef */
        beq,                /* X1M1 f0 */
        sbcIndirecty8,      /* X1M1 f1 */
        sbcIndirect8,       /* X1M1 f2 */
        sbcsIndirecty8,     /* X1M1 f3 */
        pea,                /* X1M1 f4 */
        sbcZpx8,            /* X1M1 f5 */
        incZpx8,            /* X1M1 f6 */
        sbcIndirectLongy8,  /* X1M1 f7 */
        sed,                /* X1M1 f8 */
        sbcAbsy8,           /* X1M1 f9 */
        plx8,               /* X1M1 fa */
        xce,                /* X1M1 fb */
        jsrIndx,            /* X1M1 fc */
        sbcAbsx8,           /* X1M1 fd */
        incAbsx8,           /* X1M1 fe */
        sbcLongx8,          /* X1M1 ff */
    },
    {
        op_brk,             /* X1M0 00 */
        oraIndirectx16,     /* X1M0 01 */
        cop,                /* X1M0 02 */
        oraSp16,            /* X1M0 03 */
        tsbZp16,            /* X1M0 04 */
        oraZp16,            /* X1M0 05 */
        aslZp16,            /* X1M0 06 */
        oraIndirectLong16,  /* X1M0 07 */
        php,                /* X1M0 08 */
        oraImm16,           /* X1M0 09 */
        asla16,             /* X1M0 0a */
        phd,                /* X1M0 0b */
        tsbAbs16,           /* X1M0 0c */
        oraAbs16,           /* X1M0 0d */
        aslAbs16,           /* X1M0 0e */
        oraLong16,          /* X1M0 0f */
        bpl,                /* X1M0 10 */
        oraIndirecty16,     /* X1M0 11 */
        oraIndirect16,      /* X1M0 12 */
        orasIndirecty16,    /* X1M0 13 */
        trbZp16,            /* X1M0 14 */
        oraZpx16,           /* X1M0 15 */
        aslZpx16,           /* X1M0 16 */
        oraIndirectLongy16, /* X1M0 17 */
        clc,                /* X1M0 18 */
        oraAbsy16,          /* X1M0 19 */
        inca16,             /* X1M0 1a */
        tcs,                /* X1M0 1b */
        trbAbs16,           /* X1M0 1c */
        oraAbsx16,          /* X1M0 1d */
        aslAbsx16,          /* X1M0 1e */
        oraLongx16,         /* X1M0 1f */
        jsr,                /* X1M0 20 */
        andIndirectx16,     /* X1M0 21 */
        jsl,                /* X1M0 22 */
        andSp16,            /* X1M0 23 */
        bitZp16,            /* X1M0 24 */
        andZp16,            /* X1M0 25 */
        rolZp16,            /* X1M0 26 */
        andIndirectLong16,  /* X1M0 27 */
        plp,                /* X1M0 28 */
        andImm16,           /* X1M0 29 */
        rola16,             /* X1M0 2a */
        pld,                /* X1M0 2b */
        bitAbs16,           /* X1M0 2c */
        andAbs16,           /* X1M0 2d */
        rolAbs16,           /* X1M0 2e */
        andLong16,          /* X1M0 2f */
        bmi,                /* X1M0 30 */
        andIndirecty16,     /* X1M0 31 */
        andIndirect16,      /* X1M0 32 */
        andsIndirecty16,    /* X1M0 33 */
        bitZpx16,           /* X1M0 34 */
        andZpx16,           /* X1M0 35 */
        rolZpx16,           /* X1M0 36 */
        andIndirectLongy16, /* X1M0 37 */
        sec,                /* X1M0 38 */
        andAbsy16,          /* X1M0 39 */
        deca16,             /* X1M0 3a */
        tsc,                /* X1M0 3b */
        bitAbsx16,          /* X1M0 3c */
        andAbsx16,          /* X1M0 3d */
        rolAbsx16,          /* X1M0 3e */
        andLongx16,         /* X1M0 3f */
        rti,                /* X1M0 40 */
        eorIndirectx16,     /* X1M0 41 */
        wdm,                /* X1M0 42 */
        eorSp16,            /* X1M0 43 */
        mvp,                /* X1M0 44 */
        eorZp16,            /* X1M0 45 */
        lsrZp16,            /* X1M0 46 */
        eorIndirectLong16,  /* X1M0 47 */
        pha16,              /* X1M0 48 */
        eorImm16,           /* X1M0 49 */
        lsra16,             /* X1M0 4a */
        phk,                /* X1M0 4b */
        jmp,                /* X1M0 4c */
        eorAbs16,           /* X1M0 4d */
        lsrAbs16,           /* X1M0 4e */
        eorLong16,          /* X1M0 4f */
        bvc,                /* X1M0 50 */
        eorIndirecty16,     /* X1M0 51 */
        eorIndirect16,      /* X1M0 52 */
        eorsIndirecty16,    /* X1M0 53 */
        mvn,                /* X1M0 54 */
        eorZpx16,           /* X1M0 55 */
        lsrZpx16,           /* X1M0 56 */
        eorIndirectLongy16, /* X1M0 57 */
        cli,                /* X1M0 58 */
        eorAbsy16,          /* X1M0 59 */
        phy8,               /* X1M0 5a */
        tcd,                /* X1M0 5b */
        jmplong,            /* X1M0 5c */
        eorAbsx16,          /* X1M0 5d */
        lsrAbsx16,          /* X1M0 5e */
        eorLongx16,         /* X1M0 5f */
        rts,                /* X1M0 60 */
        adcIndirectx16,     /* X1M0 61 */
        per,                /* X1M0 62 */
        adcSp16,            /* X1M0 63 */
        stzZp16,            /* X1M0 64 */
        adcZp16,            /* X1M0 65 */
        rorZp16,            /* X1M0 66 */
        adcIndirectLong16,  /* X1M0 67 */
        pla16,              /* X1M0 68 */
        adcImm16,           /* X1M0 69 */
        rora16,             /* X1M0 6a */
        rtl,                /* X1M0 6b */
        jmpind,             /* X1M0 6c */
        adcAbs16,           /* X1M0 6d */
        rorAbs16,           /* X1M0 6e */
        adcLong16,          /* X1M0 6f */
        bvs,                /* X1M0 70 */
        adcIndirecty16,     /* X1M0 71 */
        adcIndirect16,      /* X1M0 72 */
        adcsIndirecty16,    /* X1M0 73 */
        stzZpx16,           /* X1M0 74 */
        adcZpx16,           /* X1M0 75 */
        rorZpx16,           /* X1M0 76 */
        adcIndirectLongy16, /* X1M0 77 */
        sei,                /* X1M0 78 */
        adcAbsy16,          /* X1M0 79 */
        ply8,               /* X1M0 7a */
        tdc,                /* X1M0 7b */
        jmpindx,            /* X1M0 7c */
        adcAbsx16,          /* X1M0 7d */
        rorAbsx16,          /* X1M0 7e */
        adcLongx16,         /* X1M0 7f */
        bra,                /* X1M0 80 */
        staIndirectx16,     /* X1M0 81 */
        brl,                /* X1M0 82 */
        staSp16,            /* X1M0 83 */
        styZp8,             /* X1M0 84 */
        staZp16,            /* X1M0 85 */
        stxZp8,             /* X1M0 86 */
        staIndirectLong16,  /* X1M0 87 */
        dey8,               /* X1M0 88 */
        bitImm16,           /* X1M0 89 */
        txa16,              /* X1M0 8a */
        phb,                /* X1M0 8b */
        styAbs8,            /* X1M0 8c */
        staAbs16,           /* X1M0 8d */
        stxAbs8,            /* X1M0 8e */
        staLong16,          /* X1M0 8f */
        bcc,                /* X1M0 90 */
        staIndirecty16,     /* X1M0 91 */
        staIndirect16,      /* X1M0 92 */
        staSIndirecty16,    /* X1M0 93 */
        styZpx8,            /* X1M0 94 */
        staZpx16,           /* X1M0 95 */
        stxZpy8,            /* X1M0 96 */
        staIndirectLongy16, /* X1M0 97 */
        tya16,              /* X1M0 98 */
        staAbsy16,          /* X1M0 99 */
        txs8,               /* X1M0 9a */
        txy8,               /* X1M0 9b */
        stzAbs16,           /* X1M0 9c */
        staAbsx16,          /* X1M0 9d */
        stzAbsx16,          /* X1M0 9e */
        staLongx16,         /* X1M0 9f */
        ldyImm8,            /* X1M0 a0 */
        ldaIndirectx16,     /* X1M0 a1 */
        ldxImm8,            /* X1M0 a2 */
        ldaSp16,            /* X1M0 a3 */
        ldyZp8,             /* X1M0 a4 */
        ldaZp16,            /* X1M0 a5 */
        ldxZp8,             /* X1M0 a6 */
        ldaIndirectLong16,  /* X1M0 a7 */
        tay8,               /* X1M0 a8 */
        ldaImm16,           /* X1M0 a9 */
        tax8,               /* X1M0 aa */
        plb,                /* X1M0 ab */
        ldyAbs8,            /* X1M0 ac */
        ldaAbs16,           /* X1M0 ad */
        ldxAbs8,            /* X1M0 ae */
        ldaLong16,          /* X1M0 af */
        bcs,                /* X1M0 b0 */
        ldaIndirecty16,     /* X1M0 b1 */
        ldaIndirect16,      /* X1M0 b2 */
        ldaSIndirecty16,    /* X1M0 b3 */
        ldyZpx8,            /* X1M0 b4 */
        ldaZpx16,           /* X1M0 b5 */
        ldxZpy8,            /* X1M0 b6 */
        ldaIndirectLongy16, /* X1M0 b7 */
        clv,                /* X1M0 b8 */
        ldaAbsy16,          /* X1M0 b9 */
        tsx8,               /* X1M0 ba */
        tyx8,               /* X1M0 bb */
        ldyAbsx8,           /* X1M0 bc */
        ldaAbsx16,          /* X1M0 bd */
        ldxAbsy8,           /* X1M0 be */
        ldaLongx16,         /* X1M0 bf */
        cpyImm8,            /* X1M0 c0 */
        cmpIndirectx16,     /* X1M0 c1 */
        rep65816,           /* X1M0 c2 */
        cmpSp16,            /* X1M0 c3 */
        cpyZp8,             /* X1M0 c4 */
        cmpZp16,            /* X1M0 c5 */
        decZp16,            /* X1M0 c6 */
        cmpIndirectLong16,  /* X1M0 c7 */
        iny8,               /* X1M0 c8 */
        cmpImm16,           /* X1M0 c9 */
        dex8,               /* X1M0 ca */
        wai,                /* X1M0 cb */
        cpyAbs8,            /* X1M0 cc */
        cmpAbs16,           /* X1M0 cd */
        decAbs16,           /* X1M0 ce */
        cmpLong16,          /* X1M0 cf */
        bne,                /* X1M0 d0 */
        cmpIndirecty16,     /* X1M0 d1 */
        cmpIndirect16,      /* X1M0 d2 */
        cmpsIndirecty16,    /* X1M0 d3 */
        pei,                /* X1M0 d4 */
        cmpZpx16,           /* X1M0 d5 */
        decZpx16,           /* X1M0 d6 */
        cmpIndirectLongy16, /* X1M0 d7 */
        cld,                /* X1M0 d8 */
        cmpAbsy16,          /* X1M0 d9 */
        phx8,               /* X1M0 da */
        stp,                /* X1M0 db */
        jmlind,             /* X1M0 dc */
        cmpAbsx16,          /* X1M0 dd */
        decAbsx16,          /* X1M0 de */
        cmpLongx16,         /* X1M0 df */
        cpxImm8,            /* X1M0 e0 */
        sbcIndirectx16,     /* X1M0 e1 */
        sep,                /* X1M0 e2 */
        sbcSp16,            /* X1M0 e3 */
        cpxZp8,             /* X1M0 e4 */
        sbcZp16,            /* X1M0 e5 */
        incZp16,            /* X1M0 e6 */
        sbcIndirectLong16,  /* X1M0 e7 */
        inx8,               /* X1M0 e8 */
        sbcImm16,           /* X1M0 e9 */
        nop,                /* X1M0 ea */
        xba,                /* X1M0 eb */
        cpxAbs8,            /* X1M0 ec */
        sbcAbs16,           /* X1M0 ed */
        incAbs16,           /* X1M0 ee */
        sbcLong16,          /* X1M0 ef */
        beq,                /* X1M0 f0 */
        sbcIndirecty16,     /* X1M0 f1 */
        sbcIndirect16,      /* X1M0 f2 */
        sbcsIndirecty16,    /* X1M0 f3 */
        pea,                /* X1M0 f4 */
        sbcZpx16,           /* X1M0 f5 */
        incZpx16,           /* X1M0 f6 */
        sbcIndirectLongy16, /* X1M0 f7 */
        sed,                /* X1M0 f8 */
        sbcAbsy16,          /* X1M0 f9 */
        plx8,               /* X1M0 fa */
        xce,                /* X1M0 fb */
        jsrIndx,            /* X1M0 fc */
        sbcAbsx16,          /* X1M0 fd */
        incAbsx16,          /* X1M0 fe */
        sbcLongx16,         /* X1M0 ff */
    },
    {
        op_brk,             /* X0M1 00 */
        oraIndirectx8,      /* X0M1 01 */
        cop,                /* X0M1 02 */
        oraSp8,             /* X0M1 03 */
        tsbZp8,             /* X0M1 04 */
        oraZp8,             /* X0M1 05 */
        aslZp8,             /* X0M1 06 */
        oraIndirectLong8,   /* X0M1 07 */
        php,                /* X0M1 08 */
        oraImm8,            /* X0M1 09 */
        asla8,              /* X0M1 0a */
        phd,                /* X0M1 0b */
        tsbAbs8,            /* X0M1 0c */
        oraAbs8,            /* X0M1 0d */
        aslAbs8,            /* X0M1 0e */
        oraLong8,           /* X0M1 0f */
        bpl,                /* X0M1 10 */
        oraIndirecty8,      /* X0M1 11 */
        oraIndirect8,       /* X0M1 12 */
        orasIndirecty8,     /* X0M1 13 */
        trbZp8,             /* X0M1 14 */
        oraZpx8,            /* X0M1 15 */
        aslZpx8,            /* X0M1 16 */
        oraIndirectLongy8,  /* X0M1 17 */
        clc,                /* X0M1 18 */
        oraAbsy8,           /* X0M1 19 */
        inca8,              /* X0M1 1a */
        tcs,                /* X0M1 1b */
        trbAbs8,            /* X0M1 1c */
        oraAbsx8,           /* X0M1 1d */
        aslAbsx8,           /* X0M1 1e */
        oraLongx8,          /* X0M1 1f */
        jsr,                /* X0M1 20 */
        andIndirectx8,      /* X0M1 21 */
        jsl,                /* X0M1 22 */
        andSp8,             /* X0M1 23 */
        bitZp8,             /* X0M1 24 */
        andZp8,             /* X0M1 25 */
        rolZp8,             /* X0M1 26 */
        andIndirectLong8,   /* X0M1 27 */
        plp,                /* X0M1 28 */
        andImm8,            /* X0M1 29 */
        rola8,              /* X0M1 2a */
        pld,                /* X0M1 2b */
        bitAbs8,            /* X0M1 2c */
        andAbs8,            /* X0M1 2d */
        rolAbs8,            /* X0M1 2e */
        andLong8,           /* X0M1 2f */
        bmi,                /* X0M1 30 */
        andIndirecty8,      /* X0M1 31 */
        andIndirect8,       /* X0M1 32 */
        andsIndirecty8,     /* X0M1 33 */
        bitZpx8,            /* X0M1 34 */
        andZpx8,            /* X0M1 35 */
        rolZpx8,            /* X0M1 36 */
        andIndirectLongy8,  /* X0M1 37 */
        sec,                /* X0M1 38 */
        andAbsy8,           /* X0M1 39 */
        deca8,              /* X0M1 3a */
        tsc,                /* X0M1 3b */
        bitAbsx8,           /* X0M1 3c */
        andAbsx8,           /* X0M1 3d */
        rolAbsx8,           /* X0M1 3e */
        andLongx8,          /* X0M1 3f */
        rti,                /* X0M1 40 */
        eorIndirectx8,      /* X0M1 41 */
        wdm,                /* X0M1 42 */
        eorSp8,             /* X0M1 43 */
        mvp,                /* X0M1 44 */
        eorZp8,             /* X0M1 45 */
        lsrZp8,             /* X0M1 46 */
        eorIndirectLong8,   /* X0M1 47 */
        pha8,               /* X0M1 48 */
        eorImm8,            /* X0M1 49 */
        lsra8,              /* X0M1 4a */
        phk,                /* X0M1 4b */
        jmp,                /* X0M1 4c */
        eorAbs8,            /* X0M1 4d */
        lsrAbs8,            /* X0M1 4e */
        eorLong8,           /* X0M1 4f */
        bvc,                /* X0M1 50 */
        eorIndirecty8,      /* X0M1 51 */
        eorIndirect8,       /* X0M1 52 */
        eorsIndirecty8,     /* X0M1 53 */
        mvn,                /* X0M1 54 */
        eorZpx8,            /* X0M1 55 */
        lsrZpx8,            /* X0M1 56 */
        eorIndirectLongy8,  /* X0M1 57 */
        cli,                /* X0M1 58 */
        eorAbsy8,           /* X0M1 59 */
        phy16,              /* X0M1 5a */
        tcd,                /* X0M1 5b */
        jmplong,            /* X0M1 5c */
        eorAbsx8,           /* X0M1 5d */
        lsrAbsx8,           /* X0M1 5e */
        eorLongx8,          /* X0M1 5f */
        rts,                /* X0M1 60 */
        adcIndirectx8,      /* X0M1 61 */
        per,                /* X0M1 62 */
        adcSp8,             /* X0M1 63 */
        stzZp8,             /* X0M1 64 */
        adcZp8,             /* X0M1 65 */
        rorZp8,             /* X0M1 66 */
        adcIndirectLong8,   /* X0M1 67 */
        pla8,               /* X0M1 68 */
        adcImm8,            /* X0M1 69 */
        rora8,              /* X0M1 6a */
        rtl,                /* X0M1 6b */
        jmpind,             /* X0M1 6c */
        adcAbs8,            /* X0M1 6d */
        rorAbs8,            /* X0M1 6e */
        adcLong8,           /* X0M1 6f */
        bvs,                /* X0M1 70 */
        adcIndirecty8,      /* X0M1 71 */
        adcIndirect8,       /* X0M1 72 */
        adcsIndirecty8,     /* X0M1 73 */
        stzZpx8,            /* X0M1 74 */
        adcZpx8,            /* X0M1 75 */
        rorZpx8,            /* X0M1 76 */
        adcIndirectLongy8,  /* X0M1 77 */
        sei,                /* X0M1 78 */
        adcAbsy8,           /* X0M1 79 */
        ply16,              /* X0M1 7a */
        tdc,                /* X0M1 7b */
        jmpindx,            /* X0M1 7c */
        adcAbsx8,           /* X0M1 7d */
        rorAbsx8,           /* X0M1 7e */
        adcLongx8,          /* X0M1 7f */
        bra,                /* X0M1 80 */
        staIndirectx8,      /* X0M1 81 */
        brl,                /* X0M1 82 */
        staSp8,             /* X0M1 83 */
        styZp16,            /* X0M1 84 */
        staZp8,             /* X0M1 85 */
        stxZp16,            /* X0M1 86 */
        staIndirectLong8,   /* X0M1 87 */
        dey16,              /* X0M1 88 */
        bitImm8,            /* X0M1 89 */
        txa8,               /* X0M1 8a */
        phb,                /* X0M1 8b */
        styAbs16,           /* X0M1 8c */
        staAbs8,            /* X0M1 8d */
        stxAbs16,           /* X0M1 8e */
        staLong8,           /* X0M1 8f */
        bcc,                /* X0M1 90 */
        staIndirecty8,      /* X0M1 91 */
        staIndirect8,       /* X0M1 92 */
        staSIndirecty8,     /* X0M1 93 */
        styZpx16,           /* X0M1 94 */
        staZpx8,            /* X0M1 95 */
        stxZpy16,           /* X0M1 96 */
        staIndirectLongy8,  /* X0M1 97 */
        tya8,               /* X0M1 98 */
        staAbsy8,           /* X0M1 99 */
        txs16,              /* X0M1 9a */
        txy16,              /* X0M1 9b */
        stzAbs8,            /* X0M1 9c */
        staAbsx8,           /* X0M1 9d */
        stzAbsx8,           /* X0M1 9e */
        staLongx8,          /* X0M1 9f */
        ldyImm16,           /* X0M1 a0 */
        ldaIndirectx8,      /* X0M1 a1 */
        ldxImm16,           /* X0M1 a2 */
        ldaSp8,             /* X0M1 a3 */
        ldyZp16,            /* X0M1 a4 */
        ldaZp8,             /* X0M1 a5 */
        ldxZp16,            /* X0M1 a6 */
        ldaIndirectLong8,   /* X0M1 a7 */
        tay16,              /* X0M1 a8 */
        ldaImm8,            /* X0M1 a9 */
        tax16,              /* X0M1 aa */
        plb,                /* X0M1 ab */
        ldyAbs16,           /* X0M1 ac */
        ldaAbs8,            /* X0M1 ad */
        ldxAbs16,           /* X0M1 ae */
        ldaLong8,           /* X0M1 af */
        bcs,                /* X0M1 b0 */
        ldaIndirecty8,      /* X0M1 b1 */
        ldaIndirect8,       /* X0M1 b2 */
        ldaSIndirecty8,     /* X0M1 b3 */
        ldyZpx16,           /* X0M1 b4 */
        ldaZpx8,            /* X0M1 b5 */
        ldxZpy16,           /* X0M1 b6 */
        ldaIndirectLongy8,  /* X0M1 b7 */
        clv,                /* X0M1 b8 */
        ldaAbsy8,           /* X0M1 b9 */
        tsx16,              /* X0M1 ba */
        tyx16,              /* X0M1 bb */
        ldyAbsx16,          /* X0M1 bc */
        ldaAbsx8,           /* X0M1 bd */
        ldxAbsy16,          /* X0M1 be */
        ldaLongx8,          /* X0M1 bf */
        cpyImm16,           /* X0M1 c0 */
        cmpIndirectx8,      /* X0M1 c1 */
        rep65816,           /* X0M1 c2 */
        cmpSp8,             /* X0M1 c3 */
        cpyZp16,            /* X0M1 c4 */
        cmpZp8,             /* X0M1 c5 */
        decZp8,             /* X0M1 c6 */
        cmpIndirectLong8,   /* X0M1 c7 */
        iny16,              /* X0M1 c8 */
        cmpImm8,            /* X0M1 c9 */
        dex16,              /* X0M1 ca */
        wai,                /* X0M1 cb */
        cpyAbs16,           /* X0M1 cc */
        cmpAbs8,            /* X0M1 cd */
        decAbs8,            /* X0M1 ce */
        cmpLong8,           /* X0M1 cf */
        bne,                /* X0M1 d0 */
        cmpIndirecty8,      /* X0M1 d1 */
        cmpIndirect8,       /* X0M1 d2 */
        cmpsIndirecty8,     /* X0M1 d3 */
        pei,                /* X0M1 d4 */
        cmpZpx8,            /* X0M1 d5 */
        decZpx8,            /* X0M1 d6 */
        cmpIndirectLongy8,  /* X0M1 d7 */
        cld,                /* X0M1 d8 */
        cmpAbsy8,           /* X0M1 d9 */
        phx16,              /* X0M1 da */
        stp,                /* X0M1 db */
        jmlind,             /* X0M1 dc */
        cmpAbsx8,           /* X0M1 dd */
        decAbsx8,           /* X0M1 de */
        cmpLongx8,          /* X0M1 df */
        cpxImm16,           /* X0M1 e0 */
        sbcIndirectx8,      /* X0M1 e1 */
        sep,                /* X0M1 e2 */
        sbcSp8,             /* X0M1 e3 */
        cpxZp16,            /* X0M1 e4 */
        sbcZp8,             /* X0M1 e5 */
        incZp8,             /* X0M1 e6 */
        sbcIndirectLong8,   /* X0M1 e7 */
        inx16,              /* X0M1 e8 */
        sbcImm8,            /* X0M1 e9 */
        nop,                /* X0M1 ea */
        xba,                /* X0M1 eb */
        cpxAbs16,           /* X0M1 ec */
        sbcAbs8,            /* X0M1 ed */
        incAbs8,            /* X0M1 ee */
        sbcLong8,           /* X0M1 ef */
        beq,                /* X0M1 f0 */
        sbcIndirecty8,      /* X0M1 f1 */
        sbcIndirect8,       /* X0M1 f2 */
        sbcsIndirecty8,     /* X0M1 f3 */
        pea,                /* X0M1 f4 */
        sbcZpx8,            /* X0M1 f5 */
        incZpx8,            /* X0M1 f6 */
        sbcIndirectLongy8,  /* X0M1 f7 */
        sed,                /* X0M1 f8 */
        sbcAbsy8,           /* X0M1 f9 */
        plx16,              /* X0M1 fa */
        xce,                /* X0M1 fb */
        jsrIndx,            /* X0M1 fc */
        sbcAbsx8,           /* X0M1 fd */
        incAbsx8,           /* X0M1 fe */
        sbcLongx8,          /* X0M1 ff */
    },
    {
        op_brk,             /* X0M0 00 */
        oraIndirectx16,     /* X0M0 01 */
        cop,                /* X0M0 02 */
        oraSp16,            /* X0M0 03 */
        tsbZp16,            /* X0M0 04 */
        oraZp16,            /* X0M0 05 */
        aslZp16,            /* X0M0 06 */
        oraIndirectLong16,  /* X0M0 07 */
        php,                /* X0M0 08 */
        oraImm16,           /* X0M0 09 */
        asla16,             /* X0M0 0a */
        phd,                /* X0M0 0b */
        tsbAbs16,           /* X0M0 0c */
        oraAbs16,           /* X0M0 0d */
        aslAbs16,           /* X0M0 0e */
        oraLong16,          /* X0M0 0f */
        bpl,                /* X0M0 10 */
        oraIndirecty16,     /* X0M0 11 */
        oraIndirect16,      /* X0M0 12 */
        orasIndirecty16,    /* X0M0 13 */
        trbZp16,            /* X0M0 14 */
        oraZpx16,           /* X0M0 15 */
        aslZpx16,           /* X0M0 16 */
        oraIndirectLongy16, /* X0M0 17 */
        clc,                /* X0M0 18 */
        oraAbsy16,          /* X0M0 19 */
        inca16,             /* X0M0 1a */
        tcs,                /* X0M0 1b */
        trbAbs16,           /* X0M0 1c */
        oraAbsx16,          /* X0M0 1d */
        aslAbsx16,          /* X0M0 1e */
        oraLongx16,         /* X0M0 1f */
        jsr,                /* X0M0 20 */
        andIndirectx16,     /* X0M0 21 */
        jsl,                /* X0M0 22 */
        andSp16,            /* X0M0 23 */
        bitZp16,            /* X0M0 24 */
        andZp16,            /* X0M0 25 */
        rolZp16,            /* X0M0 26 */
        andIndirectLong16,  /* X0M0 27 */
        plp,                /* X0M0 28 */
        andImm16,           /* X0M0 29 */
        rola16,             /* X0M0 2a */
        pld,                /* X0M0 2b */
        bitAbs16,           /* X0M0 2c */
        andAbs16,           /* X0M0 2d */
        rolAbs16,           /* X0M0 2e */
        andLong16,          /* X0M0 2f */
        bmi,                /* X0M0 30 */
        andIndirecty16,     /* X0M0 31 */
        andIndirect16,      /* X0M0 32 */
        andsIndirecty16,    /* X0M0 33 */
        bitZpx16,           /* X0M0 34 */
        andZpx16,           /* X0M0 35 */
        rolZpx16,           /* X0M0 36 */
        andIndirectLongy16, /* X0M0 37 */
        sec,                /* X0M0 38 */
        andAbsy16,          /* X0M0 39 */
        deca16,             /* X0M0 3a */
        tsc,                /* X0M0 3b */
        bitAbsx16,          /* X0M0 3c */
        andAbsx16,          /* X0M0 3d */
        rolAbsx16,          /* X0M0 3e */
        andLongx16,         /* X0M0 3f */
        rti,                /* X0M0 40 */
        eorIndirectx16,     /* X0M0 41 */
        wdm,                /* X0M0 42 */
        eorSp16,            /* X0M0 43 */
        mvp,                /* X0M0 44 */
        eorZp16,            /* X0M0 45 */
        lsrZp16,            /* X0M0 46 */
        eorIndirectLong16,  /* X0M0 47 */
        pha16,              /* X0M0 48 */
        eorImm16,           /* X0M0 49 */
        lsra16,             /* X0M0 4a */
        phk,                /* X0M0 4b */
        jmp,                /* X0M0 4c */
        eorAbs16,           /* X0M0 4d */
        lsrAbs16,           /* X0M0 4e */
        eorLong16,          /* X0M0 4f */
        bvc,                /* X0M0 50 */
        eorIndirecty16,     /* X0M0 51 */
        eorIndirect16,      /* X0M0 52 */
        eorsIndirecty16,    /* X0M0 53 */
        mvn,                /* X0M0 54 */
        eorZpx16,           /* X0M0 55 */
        lsrZpx16,           /* X0M0 56 */
        eorIndirectLongy16, /* X0M0 57 */
        cli,                /* X0M0 58 */
        eorAbsy16,          /* X0M0 59 */
        phy16,              /* X0M0 5a */
        tcd,                /* X0M0 5b */
        jmplong,            /* X0M0 5c */
        eorAbsx16,          /* X0M0 5d */
        lsrAbsx16,          /* X0M0 5e */
        eorLongx16,         /* X0M0 5f */
        rts,                /* X0M0 60 */
        adcIndirectx16,     /* X0M0 61 */
        per,                /* X0M0 62 */
        adcSp16,            /* X0M0 63 */
        stzZp16,            /* X0M0 64 */
        adcZp16,            /* X0M0 65 */
        rorZp16,            /* X0M0 66 */
        adcIndirectLong16,  /* X0M0 67 */
        pla16,              /* X0M0 68 */
        adcImm16,           /* X0M0 69 */
        rora16,             /* X0M0 6a */
        rtl,                /* X0M0 6b */
        jmpind,             /* X0M0 6c */
        adcAbs16,           /* X0M0 6d */
        rorAbs16,           /* X0M0 6e */
        adcLong16,          /* X0M0 6f */
        bvs,                /* X0M0 70 */
        adcIndirecty16,     /* X0M0 71 */
        adcIndirect16,      /* X0M0 72 */
        adcsIndirecty16,    /* X0M0 73 */
        stzZpx16,           /* X0M0 74 */
        adcZpx16,           /* X0M0 75 */
        rorZpx16,           /* X0M0 76 */
        adcIndirectLongy16, /* X0M0 77 */
        sei,                /* X0M0 78 */
        adcAbsy16,          /* X0M0 79 */
        ply16,              /* X0M0 7a */
        tdc,                /* X0M0 7b */
        jmpindx,            /* X0M0 7c */
        adcAbsx16,          /* X0M0 7d */
        rorAbsx16,          /* X0M0 7e */
        adcLongx16,         /* X0M0 7f */
        bra,                /* X0M0 80 */
        staIndirectx16,     /* X0M0 81 */
        brl,                /* X0M0 82 */
        staSp16,            /* X0M0 83 */
        styZp16,            /* X0M0 84 */
        staZp16,            /* X0M0 85 */
        stxZp16,            /* X0M0 86 */
        staIndirectLong16,  /* X0M0 87 */
        dey16,              /* X0M0 88 */
        bitImm16,           /* X0M0 89 */
        txa16,              /* X0M0 8a */
        phb,                /* X0M0 8b */
        styAbs16,           /* X0M0 8c */
        staAbs16,           /* X0M0 8d */
        stxAbs16,           /* X0M0 8e */
        staLong16,          /* X0M0 8f */
        bcc,                /* X0M0 90 */
        staIndirecty16,     /* X0M0 91 */
        staIndirect16,      /* X0M0 92 */
        staSIndirecty16,    /* X0M0 93 */
        styZpx16,           /* X0M0 94 */
        staZpx16,           /* X0M0 95 */
        stxZpy16,           /* X0M0 96 */
        staIndirectLongy16, /* X0M0 97 */
        tya16,              /* X0M0 98 */
        staAbsy16,          /* X0M0 99 */
        txs16,              /* X0M0 9a */
        txy16,              /* X0M0 9b */
        stzAbs16,           /* X0M0 9c */
        staAbsx16,          /* X0M0 9d */
        stzAbsx16,          /* X0M0 9e */
        staLongx16,         /* X0M0 9f */
        ldyImm16,           /* X0M0 a0 */
        ldaIndirectx16,     /* X0M0 a1 */
        ldxImm16,           /* X0M0 a2 */
        ldaSp16,            /* X0M0 a3 */
        ldyZp16,            /* X0M0 a4 */
        ldaZp16,            /* X0M0 a5 */
        ldxZp16,            /* X0M0 a6 */
        ldaIndirectLong16,  /* X0M0 a7 */
        tay16,              /* X0M0 a8 */
        ldaImm16,           /* X0M0 a9 */
        tax16,              /* X0M0 aa */
        plb,                /* X0M0 ab */
        ldyAbs16,           /* X0M0 ac */
        ldaAbs16,           /* X0M0 ad */
        ldxAbs16,           /* X0M0 ae */
        ldaLong16,          /* X0M0 af */
        bcs,                /* X0M0 b0 */
        ldaIndirecty16,     /* X0M0 b1 */
        ldaIndirect16,      /* X0M0 b2 */
        ldaSIndirecty16,    /* X0M0 b3 */
        ldyZpx16,           /* X0M0 b4 */
        ldaZpx16,           /* X0M0 b5 */
        ldxZpy16,           /* X0M0 b6 */
        ldaIndirectLongy16, /* X0M0 b7 */
        clv,                /* X0M0 b8 */
        ldaAbsy16,          /* X0M0 b9 */
        tsx16,              /* X0M0 ba */
        tyx16,              /* X0M0 bb */
        ldyAbsx16,          /* X0M0 bc */
        ldaAbsx16,          /* X0M0 bd */
        ldxAbsy16,          /* X0M0 be */
        ldaLongx16,         /* X0M0 bf */
        cpyImm16,           /* X0M0 c0 */
        cmpIndirectx16,     /* X0M0 c1 */
        rep65816,           /* X0M0 c2 */
        cmpSp16,            /* X0M0 c3 */
        cpyZp16,            /* X0M0 c4 */
        cmpZp16,            /* X0M0 c5 */
        decZp16,            /* X0M0 c6 */
        cmpIndirectLong16,  /* X0M0 c7 */
        iny16,              /* X0M0 c8 */
        cmpImm16,           /* X0M0 c9 */
        dex16,              /* X0M0 ca */
        wai,                /* X0M0 cb */
        cpyAbs16,           /* X0M0 cc */
        cmpAbs16,           /* X0M0 cd */
        decAbs16,           /* X0M0 ce */
        cmpLong16,          /* X0M0 cf */
        bne,                /* X0M0 d0 */
        cmpIndirecty16,     /* X0M0 d1 */
        cmpIndirect16,      /* X0M0 d2 */
        cmpsIndirecty16,    /* X0M0 d3 */
        pei,                /* X0M0 d4 */
        cmpZpx16,           /* X0M0 d5 */
        decZpx16,           /* X0M0 d6 */
        cmpIndirectLongy16, /* X0M0 d7 */
        cld,                /* X0M0 d8 */
        cmpAbsy16,          /* X0M0 d9 */
        phx16,              /* X0M0 da */
        stp,                /* X0M0 db */
        jmlind,             /* X0M0 dc */
        cmpAbsx16,          /* X0M0 dd */
        decAbsx16,          /* X0M0 de */
        cmpLongx16,         /* X0M0 df */
        cpxImm16,           /* X0M0 e0 */
        sbcIndirectx16,     /* X0M0 e1 */
        sep,                /* X0M0 e2 */
        sbcSp16,            /* X0M0 e3 */
        cpxZp16,            /* X0M0 e4 */
        sbcZp16,            /* X0M0 e5 */
        incZp16,            /* X0M0 e6 */
        sbcIndirectLong16,  /* X0M0 e7 */
        inx16,              /* X0M0 e8 */
        sbcImm16,           /* X0M0 e9 */
        nop,                /* X0M0 ea */
        xba,                /* X0M0 eb */
        cpxAbs16,           /* X0M0 ec */
        sbcAbs16,           /* X0M0 ed */
        incAbs16,           /* X0M0 ee */
        sbcLong16,          /* X0M0 ef */
        beq,                /* X0M0 f0 */
        sbcIndirecty16,     /* X0M0 f1 */
        sbcIndirect16,      /* X0M0 f2 */
        sbcsIndirecty16,    /* X0M0 f3 */
        pea,                /* X0M0 f4 */
        sbcZpx16,           /* X0M0 f5 */
        incZpx16,           /* X0M0 f6 */
        sbcIndirectLongy16, /* X0M0 f7 */
        sed,                /* X0M0 f8 */
        sbcAbsy16,          /* X0M0 f9 */
        plx16,              /* X0M0 fa */
        xce,                /* X0M0 fb */
        jsrIndx,            /* X0M0 fc */
        sbcAbsx16,          /* X0M0 fd */
        incAbsx16,          /* X0M0 fe */
        sbcLongx16,         /* X0M0 ff */
    },
    {
        brke,               /* EMUL 00 */
        oraIndirectx8,      /* EMUL 01 */
        cope,               /* EMUL 02 */
        oraSp8,             /* EMUL 03 */
        tsbZp8,             /* EMUL 04 */
        oraZp8,             /* EMUL 05 */
        aslZp8,             /* EMUL 06 */
        oraIndirectLong8,   /* EMUL 07 */
        phpe,               /* EMUL 08 */
        oraImm8,            /* EMUL 09 */
        asla8,              /* EMUL 0a */
        phd,                /* EMUL 0b */
        tsbAbs8,            /* EMUL 0c */
        oraAbs8,            /* EMUL 0d */
        aslAbs8,            /* EMUL 0e */
        oraLong8,           /* EMUL 0f */
        bpl,                /* EMUL 10 */
        oraIndirecty8,      /* EMUL 11 */
        oraIndirect8,       /* EMUL 12 */
        orasIndirecty8,     /* EMUL 13 */
        trbZp8,             /* EMUL 14 */
        oraZpx8,            /* EMUL 15 */
        aslZpx8,            /* EMUL 16 */
        oraIndirectLongy8,  /* EMUL 17 */
        clc,                /* EMUL 18 */
        oraAbsy8,           /* EMUL 19 */
        inca8,              /* EMUL 1a */
        tcs,                /* EMUL 1b */
        trbAbs8,            /* EMUL 1c */
        oraAbsx8,           /* EMUL 1d */
        aslAbsx8,           /* EMUL 1e */
        oraLongx8,          /* EMUL 1f */
        jsre,               /* EMUL 20 */
        andIndirectx8,      /* EMUL 21 */
        jsle,               /* EMUL 22 */
        andSp8,             /* EMUL 23 */
        bitZp8,             /* EMUL 24 */
        andZp8,             /* EMUL 25 */
        rolZp8,             /* EMUL 26 */
        andIndirectLong8,   /* EMUL 27 */
        plpe,               /* EMUL 28 */
        andImm8,            /* EMUL 29 */
        rola8,              /* EMUL 2a */
        pld,                /* EMUL 2b */
        bitAbs8,            /* EMUL 2c */
        andAbs8,            /* EMUL 2d */
        rolAbs8,            /* EMUL 2e */
        andLong8,           /* EMUL 2f */
        bmi,                /* EMUL 30 */
        andIndirecty8,      /* EMUL 31 */
        andIndirect8,       /* EMUL 32 */
        andsIndirecty8,     /* EMUL 33 */
        bitZpx8,            /* EMUL 34 */
        andZpx8,            /* EMUL 35 */
        rolZpx8,            /* EMUL 36 */
        andIndirectLongy8,  /* EMUL 37 */
        sec,                /* EMUL 38 */
        andAbsy8,           /* EMUL 39 */
        deca8,              /* EMUL 3a */
        tsc,                /* EMUL 3b */
        bitAbsx8,           /* EMUL 3c */
        andAbsx8,           /* EMUL 3d */
        rolAbsx8,           /* EMUL 3e */
        andLongx8,          /* EMUL 3f */
        rtie,               /* EMUL 40 */
        eorIndirectx8,      /* EMUL 41 */
        wdm,                /* EMUL 42 */
        eorSp8,             /* EMUL 43 */
        mvp,                /* EMUL 44 */
        eorZp8,             /* EMUL 45 */
        lsrZp8,             /* EMUL 46 */
        eorIndirectLong8,   /* EMUL 47 */
        pha8,               /* EMUL 48 */
        eorImm8,            /* EMUL 49 */
        lsra8,              /* EMUL 4a */
        phke,               /* EMUL 4b */
        jmp,                /* EMUL 4c */
        eorAbs8,            /* EMUL 4d */
        lsrAbs8,            /* EMUL 4e */
        eorLong8,           /* EMUL 4f */
        bvc,                /* EMUL 50 */
        eorIndirecty8,      /* EMUL 51 */
        eorIndirect8,       /* EMUL 52 */
        eorsIndirecty8,     /* EMUL 53 */
        mvn,                /* EMUL 54 */
        eorZpx8,            /* EMUL 55 */
        lsrZpx8,            /* EMUL 56 */
        eorIndirectLongy8,  /* EMUL 57 */
        cli,                /* EMUL 58 */
        eorAbsy8,           /* EMUL 59 */
        phy8,               /* EMUL 5a */
        tcd,                /* EMUL 5b */
        jmplong,            /* EMUL 5c */
        eorAbsx8,           /* EMUL 5d */
        lsrAbsx8,           /* EMUL 5e */
        eorLongx8,          /* EMUL 5f */
        rtse,               /* EMUL 60 */
        adcIndirectx8,      /* EMUL 61 */
        per,                /* EMUL 62 */
        adcSp8,             /* EMUL 63 */
        stzZp8,             /* EMUL 64 */
        adcZp8,             /* EMUL 65 */
        rorZp8,             /* EMUL 66 */
        adcIndirectLong8,   /* EMUL 67 */
        pla8,               /* EMUL 68 */
        adcImm8,            /* EMUL 69 */
        rora8,              /* EMUL 6a */
        rtle,               /* EMUL 6b */
        jmpind,             /* EMUL 6c */
        adcAbs8,            /* EMUL 6d */
        rorAbs8,            /* EMUL 6e */
        adcLong8,           /* EMUL 6f */
        bvs,                /* EMUL 70 */
        adcIndirecty8,      /* EMUL 71 */
        adcIndirect8,       /* EMUL 72 */
        adcsIndirecty8,     /* EMUL 73 */
        stzZpx8,            /* EMUL 74 */
        adcZpx8,            /* EMUL 75 */
        rorZpx8,            /* EMUL 76 */
        adcIndirectLongy8,  /* EMUL 77 */
        sei,                /* EMUL 78 */
        adcAbsy8,           /* EMUL 79 */
        ply8,               /* EMUL 7a */
        tdc,                /* EMUL 7b */
        jmpindx,            /* EMUL 7c */
        adcAbsx8,           /* EMUL 7d */
        rorAbsx8,           /* EMUL 7e */
        adcLongx8,          /* EMUL 7f */
        bra,                /* EMUL 80 */
        staIndirectx8,      /* EMUL 81 */
        brl,                /* EMUL 82 */
        staSp8,             /* EMUL 83 */
        styZp8,             /* EMUL 84 */
        staZp8,             /* EMUL 85 */
        stxZp8,             /* EMUL 86 */
        staIndirectLong8,   /* EMUL 87 */
        dey8,               /* EMUL 88 */
        bitImm8,            /* EMUL 89 */
        txa8,               /* EMUL 8a */
        phbe,               /* EMUL 8b */
        styAbs8,            /* EMUL 8c */
        staAbs8,            /* EMUL 8d */
        stxAbs8,            /* EMUL 8e */
        staLong8,           /* EMUL 8f */
        bcc,                /* EMUL 90 */
        staIndirecty8,      /* EMUL 91 */
        staIndirect8,       /* EMUL 92 */
        staSIndirecty8,     /* EMUL 93 */
        styZpx8,            /* EMUL 94 */
        staZpx8,            /* EMUL 95 */
        stxZpy8,            /* EMUL 96 */
        staIndirectLongy8,  /* EMUL 97 */
        tya8,               /* EMUL 98 */
        staAbsy8,           /* EMUL 99 */
        txs8,               /* EMUL 9a */
        txy8,               /* EMUL 9b */
        stzAbs8,            /* EMUL 9c */
        staAbsx8,           /* EMUL 9d */
        stzAbsx8,           /* EMUL 9e */
        staLongx8,          /* EMUL 9f */
        ldyImm8,            /* EMUL a0 */
        ldaIndirectx8,      /* EMUL a1 */
        ldxImm8,            /* EMUL a2 */
        ldaSp8,             /* EMUL a3 */
        ldyZp8,             /* EMUL a4 */
        ldaZp8,             /* EMUL a5 */
        ldxZp8,             /* EMUL a6 */
        ldaIndirectLong8,   /* EMUL a7 */
        tay8,               /* EMUL a8 */
        ldaImm8,            /* EMUL a9 */
        tax8,               /* EMUL aa */
        plbe,               /* EMUL ab */
        ldyAbs8,            /* EMUL ac */
        ldaAbs8,            /* EMUL ad */
        ldxAbs8,            /* EMUL ae */
        ldaLong8,           /* EMUL af */
        bcs,                /* EMUL b0 */
        ldaIndirecty8,      /* EMUL b1 */
        ldaIndirect8,       /* EMUL b2 */
        ldaSIndirecty8,     /* EMUL b3 */
        ldyZpx8,            /* EMUL b4 */
        ldaZpx8,            /* EMUL b5 */
        ldxZpy8,            /* EMUL b6 */
        ldaIndirectLongy8,  /* EMUL b7 */
        clv,                /* EMUL b8 */
        ldaAbsy8,           /* EMUL b9 */
        tsx8,               /* EMUL ba */
        tyx8,               /* EMUL bb */
        ldyAbsx8,           /* EMUL bc */
        ldaAbsx8,           /* EMUL bd */
        ldxAbsy8,           /* EMUL be */
        ldaLongx8,          /* EMUL bf */
        cpyImm8,            /* EMUL c0 */
        cmpIndirectx8,      /* EMUL c1 */
        rep65816,           /* EMUL c2 */
        cmpSp8,             /* EMUL c3 */
        cpyZp8,             /* EMUL c4 */
        cmpZp8,             /* EMUL c5 */
        decZp8,             /* EMUL c6 */
        cmpIndirectLong8,   /* EMUL c7 */
        iny8,               /* EMUL c8 */
        cmpImm8,            /* EMUL c9 */
        dex8,               /* EMUL ca */
        wai,                /* EMUL cb */
        cpyAbs8,            /* EMUL cc */
        cmpAbs8,            /* EMUL cd */
        decAbs8,            /* EMUL ce */
        cmpLong8,           /* EMUL cf */
        bne,                /* EMUL d0 */
        cmpIndirecty8,      /* EMUL d1 */
        cmpIndirect8,       /* EMUL d2 */
        cmpsIndirecty8,     /* EMUL d3 */
        pei,                /* EMUL d4 */
        cmpZpx8,            /* EMUL d5 */
        decZpx8,            /* EMUL d6 */
        cmpIndirectLongy8,  /* EMUL d7 */
        cld,                /* EMUL d8 */
        cmpAbsy8,           /* EMUL d9 */
        phx8,               /* EMUL da */
        stp,                /* EMUL db */
        jmlind,             /* EMUL dc */
        cmpAbsx8,           /* EMUL dd */
        decAbsx8,           /* EMUL de */
        cmpLongx8,          /* EMUL df */
        cpxImm8,            /* EMUL e0 */
        sbcIndirectx8,      /* EMUL e1 */
        sep,                /* EMUL e2 */
        sbcSp8,             /* EMUL e3 */
        cpxZp8,             /* EMUL e4 */
        sbcZp8,             /* EMUL e5 */
        incZp8,             /* EMUL e6 */
        sbcIndirectLong8,   /* EMUL e7 */
        inx8,               /* EMUL e8 */
        sbcImm8,            /* EMUL e9 */
        nop,                /* EMUL ea */
        xba,                /* EMUL eb */
        cpxAbs8,            /* EMUL ec */
        sbcAbs8,            /* EMUL ed */
        incAbs8,            /* EMUL ee */
        sbcLong8,           /* EMUL ef */
        beq,                /* EMUL f0 */
        sbcIndirecty8,      /* EMUL f1 */
        sbcIndirect8,       /* EMUL f2 */
        sbcsIndirecty8,     /* EMUL f3 */
        pea,                /* EMUL f4 */
        sbcZpx8,            /* EMUL f5 */
        incZpx8,            /* EMUL f6 */
        sbcIndirectLongy8,  /* EMUL f7 */
        sed,                /* EMUL f8 */
        sbcAbsy8,           /* EMUL f9 */
        plx8,               /* EMUL fa */
        xce,                /* EMUL fb */
        jsrIndxe,           /* EMUL fc */
        sbcAbsx8,           /* EMUL fd */
        incAbsx8,           /* EMUL fe */
        sbcLongx8,          /* EMUL ff */
    }
};

/*Functions*/

static void set_cpu_mode(int mode)
{
    cpumode = mode;
    modeptr = &(opcodes[mode][0]);
}

static void updatecpumode(void)
{
    int mode;

    if (p.e) {
        mode = 4;
        x.b.h = y.b.h = 0;
    } else {
        mode = 0;
        if (!p.m)
            mode |= 1;
        if (!p.ex)
            mode |= 2;
        if (p.ex)
            x.b.h = y.b.h = 0;
    }
    set_cpu_mode(mode);
}

void w65816_reset(void)
{
    def = 1;
    if (def || (banking & 4))
        w65816mask = 0xFFFF;
    else
        w65816mask = 0x7FFFF;
    pbr = dbr = 0;
    s.w = 0x1FF;
    set_cpu_mode(4);
    p.e = 1;
    p.i = 1;
    pc = readmemw(0xFFFC);
    a.w = x.w = y.w = 0;
    p.ex = p.m = 1;
    cycles = 0;
}

void w65816_init(FILE * romf)
{
    if (!w65816rom)
        w65816rom = malloc(W65816_ROM_SIZE);
    if (!w65816ram)
        w65816ram = malloc(W65816_RAM_SIZE);
    fread(w65816rom, 0x8000, 1, romf);
}

void w65816_close(void)
{
    if (w65816ram)
        free(w65816ram);
    if (w65816rom)
        free(w65816rom);
}

static inline unsigned char *save_reg(unsigned char *ptr, reg * rp)
{
    *ptr++ = rp->b.l;
    *ptr++ = rp->b.h;
    return ptr;
}

void w65816_savestate(ZFILE * zfp)
{
    unsigned char bytes[38], *ptr;

    ptr = save_reg(bytes, &w65816a);
    ptr = save_reg(ptr, &w65816x);
    ptr = save_reg(ptr, &w65816y);
    ptr = save_reg(ptr, &w65816s);
    *ptr++ = pack_flags();
    ptr = save_uint32(ptr, pbr);
    ptr = save_uint32(ptr, dbr);
    ptr = save_uint16(ptr, w65816pc);
    ptr = save_uint16(ptr, dp);
    ptr = save_uint32(ptr, wins);
    *ptr++ = inwai;
    *ptr++ = cpumode;
    *ptr++ = w65816opcode;
    *ptr++ = def;
    *ptr++ = divider;
    *ptr++ = banking;
    *ptr++ = banknum;
    ptr = save_uint32(ptr, w65816mask);
    ptr = save_uint16(ptr, toldpc);
    savestate_zwrite(zfp, bytes, sizeof bytes);
    savestate_zwrite(zfp, w65816ram, W65816_RAM_SIZE);
    savestate_zwrite(zfp, w65816rom, W65816_ROM_SIZE);
}

static inline unsigned char *load_reg(unsigned char *ptr, reg * rp)
{
    rp->b.l = *ptr++;
    rp->b.h = *ptr++;
    return ptr;
}

void w65816_loadstate(ZFILE * zfp)
{
    unsigned char bytes[38], *ptr;

    savestate_zread(zfp, bytes, sizeof bytes);
    ptr = load_reg(bytes, &w65816a);
    ptr = load_reg(ptr, &w65816x);
    ptr = load_reg(ptr, &w65816y);
    ptr = load_reg(ptr, &w65816s);
    unpack_flags(*ptr++);
    ptr = load_uint32(ptr, &pbr);
    ptr = load_uint32(ptr, &dbr);
    ptr = load_uint16(ptr, &w65816pc);
    ptr = load_uint16(ptr, &dp);
    ptr = load_uint32(ptr, &wins);
    inwai = *ptr++;
    cpumode = *ptr++;
    w65816opcode = *ptr++;
    def = *ptr++;
    divider = *ptr++;
    banking = *ptr++;
    banknum = *ptr++;
    ptr = load_uint32(ptr, &w65816mask);
    ptr = load_uint16(ptr, &toldpc);
    savestate_zread(zfp, w65816ram, W65816_RAM_SIZE);
    savestate_zread(zfp, w65816rom, W65816_ROM_SIZE);
}

static void nmi65816(void)
{
    readmem(pbr | pc);
    cycles--;
    clockspc(6);
    if (inwai)
        pc++;
    inwai = 0;
    if (!p.e) {
        writemem(s.w, pbr >> 16);
        s.w--;
        writemem(s.w, pc >> 8);
        s.w--;
        writemem(s.w, pc & 0xFF);
        s.w--;
        writemem(s.w, pack_flags());
        s.w--;
        pc = readmemw(0xFFEA);
        pbr = 0;
        p.i = 1;
        p.d = 0;
    } else {
        writemem(s.w, pc >> 8);
        s.b.l--;
        writemem(s.w, pc & 0xFF);
        s.b.l--;
        writemem(s.w, pack_flags_em(0x30));
        s.b.l--;
        pc = readmemw(0xFFFA);
        pbr = 0;
        p.i = 1;
        p.d = 0;
    }
}

static int toutput = 0;
static void irq65816(void)
{
    readmem(pbr | pc);
    cycles--;
    clockspc(6);
    if (inwai && p.i) {
        pc++;
        inwai = 0;
        return;
    }
    if (inwai)
        pc++;
    inwai = 0;
    if (!p.e) {
        writemem(s.w, pbr >> 16);
        s.w--;
        writemem(s.w, pc >> 8);
        s.w--;
        writemem(s.w, pc & 0xFF);
        s.w--;
        writemem(s.w, pack_flags());
        s.w--;
        pc = readmemw(0xFFEE);
        pbr = 0;
        p.i = 1;
        p.d = 0;
    } else {
        writemem(s.w, pc >> 8);
        s.b.l--;
        writemem(s.w, pc & 0xFF);
        s.b.l--;
        writemem(s.w, pack_flags_em(0x20));
        s.b.l--;
        pc = readmemw(0xFFFE);
        pbr = 0;
        p.i = 1;
        p.d = 0;
    }
}

static int woldnmi = 0;

void w65816_exec(void)
{
    uint32_t ia;

    while (tubecycles > 0) {
        ia = pbr | pc;
        if (dbg_w65816)
            debug_preexec(&tube65816_cpu_debug, ia);
        opcode = readmem(ia);
        pc++;
        if (toutput)
            log_debug("%i : %02X:%04X %04X %02X %i %04X  %04X %04X %04X\n", wins, pbr, pc - 1, toldpc, opcode, cycles, s.b.l, a.w, x.w, y.w);
        toldpc = pc - 1;
        modeptr[opcode]();
        wins++;
        if ((tube_irq & 2) && !woldnmi)
            nmi65816();
        else if ((tube_irq & 1) && !p.i)
            irq65816();
        woldnmi = tube_irq & 2;
    }
}
