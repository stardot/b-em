/*B-em v2.2 by Tom Walker
  Z80 emulation
  I can't remember what emulator I originally wrote this for... probably ZX82
  I think a few bugs left*/

#include <stdio.h>

#include "b-em.h"
#include "tube.h"
#include "z80.h"
#include "z80dis.h"
#include "daa.h"

#define pc z80pc
#define ins z80ins
#define output z80output
#define cyc z80cyc
#define cycles z80cycles

 /*CPU*/
typedef union {
    uint16_t w;
    struct {
        uint8_t l, h;
    } b;
} z80reg;

static z80reg af, bc, de, hl, ix, iy, ir, saf, sbc, sde, shl;
static uint16_t pc, sp;
static int iff1, iff2;
static int z80int;
static int im;

#define Z80_RAM_SIZE 0x10000

static uint8_t *z80ram, *z80rom;
static int z80_oldnmi;

#define S_FLAG 0x80
#define Z_FLAG 0x40
#define Y_FLAG 0x20
#define H_FLAG 0x10
#define X_FLAG 0x08
#define P_FLAG 0x04
#define V_FLAG 0x04
#define N_FLAG 0x02
#define C_FLAG 0x01

static int cycles;
static uint16_t opc, oopc;
static int tempc;
static int output = 0;
static int ins = 0;
static uint8_t znptable[256], znptablenv[256], znptable16[65536];
static uint8_t intreg;

static bool z80_rom_in = true;
static int dbg_tube_z80 = 0;

static inline uint8_t z80_do_readmem(uint16_t a)
{
    if (a < 0x1000 && z80_rom_in)
        return z80rom[a & 0xFFF];
    return z80ram[a];
}

cpu_debug_t tubez80_cpu_debug;

static inline uint8_t z80_readmem(uint16_t a)
{
    uint8_t v = z80_do_readmem(a);
    if (dbg_tube_z80)
        debug_memread(&tubez80_cpu_debug, a, v, 1);
    return v;
}

uint8_t tube_z80_readmem(uint32_t addr)
{
    return z80_readmem(addr & 0xffff);
}

static uint32_t dbg_z80_readmem(uint32_t addr)
{
    return z80_do_readmem(addr & 0xffff);
}

static inline void z80_do_writemem(uint16_t a, uint8_t v)
{
    z80ram[a] = v;
}

static inline void z80_writemem(uint16_t a, uint8_t v)
{
    if (dbg_tube_z80)
        debug_memwrite(&tubez80_cpu_debug, a, v, 1);
    z80_do_writemem(a, v);
}

void tube_z80_writemem(uint32_t addr, uint8_t byte)
{
    z80_writemem(addr & 0xffff, byte);
}

static void dbg_z80_writemem(uint32_t addr, uint32_t value)
{
    z80_writemem(addr & 0xffff, value);
}

static inline void z80out(uint16_t a, uint8_t v)
{
    tube_parasite_write(a, v);
}

static inline uint8_t z80in(uint16_t a)
{
    return tube_parasite_read(a);
}

static inline void setzn(uint8_t v)
{
    af.b.l = znptable[v];
/*        af.b.l&=~(S_FLAG|Z_FLAG|V_FLAG|0x28|C_FLAG);
        af.b.l|=znptable[v];*/
}

static inline void setand(uint8_t v)
{
    af.b.l = znptable[v] | H_FLAG;
/*        af.b.l&=~(S_FLAG|Z_FLAG|V_FLAG|0x28|C_FLAG);
        af.b.l|=znptable[v];*/
}

static inline void setbit(uint8_t v)
{
    af.b.l = ((znptable[v] | H_FLAG) & 0xFE) | (af.b.l & 1);
}

static inline void setbit2(uint8_t v, uint8_t v2)
{
    af.b.l = ((znptable[v] | H_FLAG) & 0xD6) | (af.b.l & 1) | (v2 & 0x28);
}

static inline void setznc(uint8_t v)
{
    af.b.l &= ~(S_FLAG | Z_FLAG | V_FLAG | 0x28);
    af.b.l |= znptable[v];
}

static inline void z80_setadd(uint8_t a, uint8_t b)
{
    uint8_t r = a + b;
    af.b.l = (r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG;
    af.b.l |= (r & 0x28);       /* undocumented flag bits 5+3 */
    if ((r & 0x0f) < (a & 0x0f))
        af.b.l |= H_FLAG;
    if (r < a)
        af.b.l |= C_FLAG;
    if ((b ^ a ^ 0x80) & (b ^ r) & 0x80)
        af.b.l |= V_FLAG;
}

static inline uint8_t setinc(uint8_t v)
{
    uint8_t res = (v + 1) & 0xff;
    af.b.l &= ~(S_FLAG | Z_FLAG | V_FLAG | 0x28 | H_FLAG | N_FLAG);
    af.b.l |= znptablenv[res];
    if (v == 0x7F)
        af.b.l |= V_FLAG;
    else
        af.b.l &= ~V_FLAG;
    if (((v & 0xF) + 1) & 0x10)
        af.b.l |= H_FLAG;
    return res;
}

static inline uint8_t setdec(uint8_t v)
{
    uint8_t res = (v - 1) & 0xff;
    af.b.l &= ~(S_FLAG | Z_FLAG | V_FLAG | 0x28 | H_FLAG);
    af.b.l |= znptablenv[res] | N_FLAG;
    if (v == 0x80)
        af.b.l |= V_FLAG;
    else
        af.b.l &= ~V_FLAG;
    if (!(v & 8) && ((v - 1) & 8))
        af.b.l |= H_FLAG;
    return res;
}

static inline void setadc(uint8_t a, uint8_t b)
{
    uint8_t r = a + b + (af.b.l & C_FLAG);
    if (af.b.l & C_FLAG) {
        af.b.l = (r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG;
        af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
        if ((r & 0x0f) <= (a & 0x0f))
            af.b.l |= H_FLAG;
        if (r <= a)
            af.b.l |= C_FLAG;
        if ((b ^ a ^ 0x80) & (b ^ r) & 0x80)
            af.b.l |= V_FLAG;
    }
    else {
        af.b.l = (r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG;
        af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
        if ((r & 0x0f) < (a & 0x0f))
            af.b.l |= H_FLAG;
        if (r < a)
            af.b.l |= C_FLAG;
        if ((b ^ a ^ 0x80) & (b ^ r) & 0x80)
            af.b.l |= V_FLAG;
    }
}

static inline void setadc16(uint16_t a, uint16_t b)
{
    uint32_t r = a + b + (af.b.l & 1);
    af.b.l = (((a ^ r ^ b) >> 8) & H_FLAG) |
        ((r >> 16) & C_FLAG) |
        ((r >> 8) & (S_FLAG | 0x28)) |
        ((r & 0xffff) ? 0 : Z_FLAG) |
        (((b ^ a ^ 0x8000) & (b ^ r) & 0x8000) >> 13);
}

static inline void z80_setadd16(uint16_t a, uint16_t b)
{
    uint32_t r = a + b;
    af.b.l = (af.b.l & (S_FLAG | Z_FLAG | V_FLAG)) |
        (((a ^ r ^ b) >> 8) & H_FLAG) |
        ((r >> 16) & C_FLAG) | ((r >> 8) & 0x28);
}

static inline void setsbc(uint8_t a, uint8_t b)
{
    uint8_t r = a - (b + (af.b.l & C_FLAG));
    if (af.b.l & C_FLAG) {
        af.b.l = N_FLAG | ((r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG);
        af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
        if ((r & 0x0f) >= (a & 0x0f))
            af.b.l |= H_FLAG;
        if (r >= a)
            af.b.l |= C_FLAG;
        if ((b ^ a) & (a ^ r) & 0x80)
            af.b.l |= V_FLAG;
    }
    else {
        af.b.l = N_FLAG | ((r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG);
        af.b.l |= (r & 0x28);   /* undocumented flag bits 5+3 */
        if ((r & 0x0f) > (a & 0x0f))
            af.b.l |= H_FLAG;
        if (r > a)
            af.b.l |= C_FLAG;
        if ((b ^ a) & (a ^ r) & 0x80)
            af.b.l |= V_FLAG;
    }
}

static inline void setsbc16(uint16_t a, uint16_t b)
{
    uint32_t r = a - b - (af.b.l & C_FLAG);
    af.b.l = (((a ^ r ^ b) >> 8) & H_FLAG) | N_FLAG |
        ((r >> 16) & C_FLAG) |
        ((r >> 8) & (S_FLAG | 0x28)) |
        ((r & 0xffff) ? 0 : Z_FLAG) | (((b ^ a) & (a ^ r) & 0x8000) >> 13);
}

static inline void setcpED(uint8_t a, uint8_t b)
{
    uint8_t r = a - b;
    af.b.l &= C_FLAG;
    af.b.l |= N_FLAG | ((r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG);
    af.b.l |= (b & 0x28);       /* undocumented flag bits 5+3 */
    if ((r & 0x0f) > (a & 0x0f))
        af.b.l |= H_FLAG;
}

static inline void setcp(uint8_t a, uint8_t b)
{
    uint8_t r = a - b;
    af.b.l = N_FLAG | ((r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG);
    af.b.l |= (b & 0x28);       /* undocumented flag bits 5+3 */
    if ((r & 0x0f) > (a & 0x0f))
        af.b.l |= H_FLAG;
    if (r > a)
        af.b.l |= C_FLAG;
    if ((b ^ a) & (a ^ r) & 0x80)
        af.b.l |= V_FLAG;
}

static inline void z80_setsub(uint8_t a, uint8_t b)
{
    uint8_t r = a - b;
    af.b.l = N_FLAG | ((r) ? ((r & 0x80) ? S_FLAG : 0) : Z_FLAG);
    af.b.l |= (r & 0x28);       /* undocumented flag bits 5+3 */
    if ((r & 0x0f) > (a & 0x0f))
        af.b.l |= H_FLAG;
    if (r > a)
        af.b.l |= C_FLAG;
    if ((b ^ a) & (a ^ r) & 0x80)
        af.b.l |= V_FLAG;
}

static void makeznptable()
{
    int c, d, e, f, g;
    for (c = 0; c < 256; c++) {
//                d|=(c&0xA8);
        e = c;
        f = 0;
        for (g = 0; g < 8; g++) {
            if (e & 1)
                f++;
            e >>= 1;
        }
        d = c ? (c & S_FLAG) : Z_FLAG;
        d |= (c & 0x28);
        d |= (f & 1) ? 0 : V_FLAG;
/*                if (!(f&1))
                   d|=4;*/
        znptable[c] = d;
        znptablenv[c] = d & ~V_FLAG;
    }
//        znptable[0]|=0x40;
    for (c = 0; c < 65536; c++) {
        d = 0;
        if (c & 0x8000)
            d |= 0x80;
        e = c;
        f = 0;
        for (g = 0; g < 16; g++) {
            if (e & 1)
                f++;
            e >>= 1;
        }
        if (!(f & 1))
            d |= 4;
        znptable16[c] = d;
    }
    znptable16[0] |= 0x40;
}

static int dbg_debug_enable(int newvalue)
{
    int oldvalue = dbg_tube_z80;
    dbg_tube_z80 = newvalue;
    return oldvalue;
};

static const char *dbg_z80_reg_names[] =
    { "A", "F", "BC", "DE", "HL", "IX", "IY", "SP", "PC", NULL };

enum { REG_A, REG_F, REG_BC, REG_DE, REG_HL, REG_IX, REG_IY, REG_SP,
    REG_PC
} reg_num;

static uint32_t dbg_z80_reg_get(int which)
{
    switch (which) {
        case REG_A:
            return af.b.h;
        case REG_F:
            return af.b.l;
        case REG_BC:
            return bc.w;
        case REG_DE:
            return de.w;
        case REG_HL:
            return hl.w;
        case REG_IX:
            return ix.w;
        case REG_IY:
            return iy.w;
        case REG_SP:
            return sp;
        case REG_PC:
            return pc;
        default:
            log_warn("z80: attempt to read non-existent register");
            return 0;
    }
}

static void dbg_z80_reg_set(int which, uint32_t value)
{
    switch (which) {
        case REG_A:
            af.b.h = value;
            break;
        case REG_F:
            af.b.l = value;
            break;
        case REG_BC:
            bc.w = value;
            break;
        case REG_DE:
            de.w = value;
            break;
        case REG_HL:
            hl.w = value;
            break;
        case REG_IX:
            ix.w = value;
            break;
        case REG_IY:
            iy.w = value;
            break;
        case REG_SP:
            sp = value;
            break;
        case REG_PC:
            pc = value;
            break;
        default:
            log_warn("z80: attempt to write non-existent register");
    }
}

static size_t z80_decode_flags(uint32_t flags, char *buf, size_t bufsize)
{
    if (bufsize >= 9) {
        buf[0] = flags & 0x80 ? 'S' : '-';
        buf[1] = flags & 0x40 ? 'Z' : '-';
        buf[2] = flags & 0x20 ? 'Y' : '-';
        buf[3] = flags & 0x10 ? 'H' : '-';
        buf[4] = flags & 0x08 ? 'X' : '-';
        buf[5] = flags & 0x04 ? 'V' : '-';
        buf[6] = flags & 0x02 ? 'N' : '-';
        buf[7] = flags & 0x01 ? 'C' : '-';
        buf[8] = 0;
        return 8;
    }
    return 0;
}

static size_t dbg_z80_reg_print(int which, char *buf, size_t bufsize)
{
    uint32_t value = dbg_z80_reg_get(which);
    if (which == REG_F)
        return z80_decode_flags(value, buf, bufsize);
    return snprintf(buf, bufsize, which <= REG_F ? "%02X" : "%04X", value);
}

static void dbg_z80_reg_parse(int which, const char *str)
{
    uint32_t value = strtol(str, NULL, 16);
    dbg_z80_reg_set(which, value);
}

static uint32_t dbg_z80_get_instr_addr()
{
    return opc;
}

cpu_debug_t tubez80_cpu_debug = {
    .cpu_name       = "Z80",
    .debug_enable   = dbg_debug_enable,
    .memread        = dbg_z80_readmem,
    .memwrite       = dbg_z80_writemem,
    .disassemble    = z80_disassemble,
    .reg_names      = dbg_z80_reg_names,
    .reg_get        = dbg_z80_reg_get,
    .reg_set        = dbg_z80_reg_set,
    .reg_print      = dbg_z80_reg_print,
    .reg_parse      = dbg_z80_reg_parse,
    .get_instr_addr = dbg_z80_get_instr_addr,
    .print_addr     = debug_print_addr16,
    .parse_addr     = debug_parse_addr
};

void z80_close(void)
{
    if (z80ram) {
        free(z80ram);
        z80ram = NULL;
    }
}

static void z80_savestate(ZFILE * zfp)
{
    unsigned char bytes[44];

    bytes[0] = af.b.l;
    bytes[1] = af.b.h;
    bytes[2] = bc.b.l;
    bytes[3] = bc.b.h;
    bytes[4] = de.b.l;
    bytes[5] = de.b.h;
    bytes[6] = hl.b.l;
    bytes[7] = hl.b.h;
    bytes[8] = ix.b.l;
    bytes[9] = ix.b.h;
    bytes[10] = iy.b.l;
    bytes[11] = iy.b.h;
    bytes[12] = ir.b.l;
    bytes[13] = ir.b.h;
    bytes[14] = saf.b.l;
    bytes[15] = saf.b.h;
    bytes[16] = sbc.b.l;
    bytes[17] = sbc.b.h;
    bytes[18] = sde.b.l;
    bytes[19] = sde.b.h;
    bytes[20] = shl.b.l;
    bytes[21] = shl.b.h;
    bytes[22] = sp;
    bytes[23] = sp >> 8;
    bytes[24] = pc;
    bytes[25] = pc >> 8;
    bytes[26] = opc;
    bytes[27] = opc >> 8;
    bytes[28] = oopc;
    bytes[29] = oopc >> 8;
    bytes[30] = iff1;
    bytes[31] = iff2;
    bytes[32] = z80int;
    bytes[33] = im;
    bytes[34] = cycles;
    bytes[35] = cycles >> 8;
    bytes[36] = cycles >> 16;
    bytes[37] = cycles >> 24;
    bytes[38] = ins;
    bytes[39] = ins >> 8;
    bytes[40] = ins >> 16;
    bytes[41] = ins >> 24;
    bytes[42] = z80_rom_in;
    bytes[43] = intreg;

    savestate_zwrite(zfp, bytes, sizeof bytes);
    savestate_zwrite(zfp, z80ram, sizeof z80ram);
    savestate_zwrite(zfp, z80rom, sizeof z80rom);
}

static void z80_loadstate(ZFILE * zfp)
{
    unsigned char bytes[44];

    savestate_zread(zfp, bytes, sizeof bytes);

    af.b.l = bytes[0];
    af.b.h = bytes[1];
    bc.b.l = bytes[2];
    bc.b.h = bytes[3];
    de.b.l = bytes[4];
    de.b.h = bytes[5];
    hl.b.l = bytes[6];
    hl.b.h = bytes[7];
    ix.b.l = bytes[8];
    ix.b.h = bytes[9];
    iy.b.l = bytes[10];
    iy.b.h = bytes[11];
    ir.b.l = bytes[12];
    ir.b.h = bytes[13];
    saf.b.l = bytes[14];
    saf.b.h = bytes[15];
    sbc.b.l = bytes[16];
    sbc.b.h = bytes[17];
    sde.b.l = bytes[18];
    sde.b.h = bytes[19];
    shl.b.l = bytes[20];
    shl.b.h = bytes[21];
    sp   = bytes[22] | (bytes[23] << 8);
    pc   = bytes[24] | (bytes[25] << 8);
    opc  = bytes[26] | (bytes[27] << 8);
    oopc = bytes[28] | (bytes[29] << 8);
    iff1 = bytes[30];
    iff2 = bytes[31];
    z80int = bytes[32];
    im = bytes[33];
    cycles = bytes[34] | (bytes[35] << 8) | (bytes[36] << 16) | (bytes[37] << 24);
    ins = bytes[38] | (bytes[39] << 8) | (bytes[40] << 16) | (bytes[41] << 24);
    z80_rom_in = bytes[42];
    intreg = bytes[43];

    savestate_zread(zfp, z80ram, sizeof z80ram);
    savestate_zread(zfp, z80rom, sizeof z80rom);
}

bool z80_init(void *rom)
{
    if (!z80ram) {
        z80ram = malloc(Z80_RAM_SIZE);
        if (!z80ram) {
            log_error("z80: unable to allocate RAM");
            return false;
        }
    }
    z80rom = rom;
    makeznptable();
    tube_readmem = tube_z80_readmem;
    tube_writemem = tube_z80_writemem;
    tube_exec = z80_exec;
    tube_proc_savestate = z80_savestate;
    tube_proc_loadstate = z80_loadstate;
    tube_type = TUBEZ80;
    z80_reset();
    return true;
}

void z80_dumpregs()
{
    char buf[9];
    log_debug("AF =%04X BC =%04X DE =%04X HL =%04X IX=%04X IY=%04X\n",
              af.w, bc.w, de.w, hl.w, ix.w, iy.w);
    log_debug("AF'=%04X BC'=%04X DE'=%04X HL'=%04X IR=%04X\n", saf.w,
              sbc.w, sde.w, shl.w, ir.w);
    z80_decode_flags(af.b.l, buf, sizeof(buf));
    log_debug("%s   PC =%04X SP =%04X\n", buf, pc, sp);
    log_debug("%i ins  IFF1=%i IFF2=%i  %04X %04X\n", ins, iff1, iff2, opc,
              oopc);
}

void z80_mem_dump()
{
    FILE *f = x_fopen("z80ram.dmp", "wb");
    fwrite(z80ram, 0x10000, 1, f);
    fclose(f);
}

void z80_reset()
{
    pc = 0;
    z80_rom_in = true;
}

static uint16_t oopc, opc;

void z80_exec(void)
{
    uint8_t opcode, temp, temp2;
    uint16_t addr;
    int enterint = 0;

    while (tubecycles > 0) {
        oopc = opc;
        opc = pc;
        if ((tube_irq & 1) && iff1)
            enterint = 1;
        cycles = 0;
        if (pc & 0x8000)
            z80_rom_in = false;
        if (dbg_tube_z80)
            debug_preexec(&tubez80_cpu_debug, pc);
        tempc = af.b.l & C_FLAG;
        opcode = z80_readmem(pc++);
        ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
noprefix:
        switch (opcode) {
            case 0x00:          /*NOP*/
                cycles += 4;
                break;
            case 0x01:          /*LD BC,nn */
                cycles += 4;
                bc.b.l = z80_readmem(pc++);
                cycles += 3;
                bc.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x02:          /*LD (BC),A */
                cycles += 4;
                z80_writemem(bc.w, af.b.h);
                cycles += 3;
                break;
            case 0x03:          /*INC BC */
                bc.w++;
                cycles += 6;
                break;
            case 0x04:          /*INC B */
                bc.b.h = setinc(bc.b.h);
                cycles += 4;
                break;
            case 0x05:          /*DEC B */
                bc.b.h = setdec(bc.b.h);
                cycles += 4;
                break;
            case 0x06:          /*LD B,nn */
                cycles += 4;
                bc.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x07:          /*RLCA*/
                af.b.l &= ~(H_FLAG | N_FLAG);
                temp = af.b.h & 0x80;
                af.b.h <<= 1;
                if (temp)
                    af.b.h |= 1;
                if (temp)
                    af.b.l |= C_FLAG;
                else
                    af.b.l &= ~C_FLAG;
                cycles += 4;
                break;
            case 0x08:          /*EX AF,AF' */
                addr = af.w;
                af.w = saf.w;
                saf.w = addr;
                cycles += 4;
                break;
            case 0x09:          /*ADD HL,BC */
                intreg = hl.b.h;
                z80_setadd16(hl.w, bc.w);
                hl.w += bc.w;
                cycles += 11;
                break;
            case 0x0A:          /*LD A,(BC) */
                cycles += 4;
                af.b.h = z80_readmem(bc.w);
                cycles += 3;
                break;
            case 0x0B:          /*DEC BC */
                bc.w--;
                cycles += 6;
                break;
            case 0x0C:          /*INC C */
                bc.b.l = setinc(bc.b.l);
                cycles += 4;
                break;
            case 0x0D:          /*DEC C */
                bc.b.l = setdec(bc.b.l);
                cycles += 4;
                break;
            case 0x0E:          /*LD C,nn */
                cycles += 4;
                bc.b.l = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x0F:          /*RRCA*/
                af.b.l &= ~(H_FLAG | N_FLAG);
                temp = af.b.h & 1;
                af.b.h >>= 1;
                if (temp)
                    af.b.h |= 0x80;
                if (temp)
                    af.b.l |= C_FLAG;
                else
                    af.b.l &= ~C_FLAG;
                cycles += 4;
                break;

            case 0x10:          /*DJNZ*/
                cycles += 5;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                if (--bc.b.h) {
                    pc += addr;
                    cycles += 8;
                }
                else
                    cycles += 3;
                break;
            case 0x11:          /*LD DE,nn */
                cycles += 4;
                de.b.l = z80_readmem(pc++);
                cycles += 3;
                de.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x12:          /*LD (DE),A */
                cycles += 4;
                z80_writemem(de.w, af.b.h);
                cycles += 3;
                break;
            case 0x13:          /*INC DE */
                de.w++;
                cycles += 6;
                break;
            case 0x14:          /*INC D */
                de.b.h = setinc(de.b.h);
                cycles += 4;
                break;
            case 0x15:          /*DEC D */
                de.b.h = setdec(de.b.h);
                cycles += 4;
                break;
            case 0x16:          /*LD D,nn */
                cycles += 4;
                de.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x17:          /*RLA*/
                af.b.l &= ~(H_FLAG | N_FLAG);
                temp = af.b.h & 0x80;
                af.b.h <<= 1;
                if (tempc)
                    af.b.h |= 1;
                if (temp)
                    af.b.l |= C_FLAG;
                else
                    af.b.l &= ~C_FLAG;
                cycles += 4;
                break;
            case 0x18:          /*JR*/
                cycles += 4;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                pc += addr;
                intreg = pc >> 8;
                cycles += 8;
                break;
            case 0x19:          /*ADD HL,DE */
                intreg = hl.b.h;
                z80_setadd16(hl.w, de.w);
                hl.w += de.w;
                cycles += 11;
                break;
            case 0x1A:          /*LD A,(DE) */
                cycles += 4;
                af.b.h = z80_readmem(de.w);
                cycles += 3;
                break;
            case 0x1B:          /*DEC DE */
                de.w--;
                cycles += 6;
                break;
            case 0x1C:          /*INC E */
                de.b.l = setinc(de.b.l);
                cycles += 4;
                break;
            case 0x1D:          /*DEC E */
                de.b.l = setdec(de.b.l);
                cycles += 4;
                break;
            case 0x1E:          /*LD E,nn */
                cycles += 4;
                de.b.l = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x1F:          /*RRA*/
                af.b.l &= ~(H_FLAG | N_FLAG);
                temp = af.b.h & 1;
                af.b.h >>= 1;
                if (tempc)
                    af.b.h |= 0x80;
                if (temp)
                    af.b.l |= C_FLAG;
                else
                    af.b.l &= ~C_FLAG;
                cycles += 4;
                break;

            case 0x20:          /*JR NZ */
                cycles += 4;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                if (!(af.b.l & Z_FLAG)) {
                    pc += addr;
                    cycles += 8;
                }
                else
                    cycles += 3;
                break;
            case 0x21:          /*LD HL,nn */
                cycles += 4;
                hl.b.l = z80_readmem(pc++);
                cycles += 3;
                hl.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x22:          /*LD (nn),HL */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                cycles += 3;
                z80_writemem(addr, hl.b.l);
                cycles += 3;
                z80_writemem(addr + 1, hl.b.h);
                cycles += 3;
                break;
            case 0x23:          /*INC HL */
                hl.w++;
                cycles += 6;
                break;
            case 0x24:          /*INC H */
                hl.b.h = setinc(hl.b.h);
                cycles += 4;
                break;
            case 0x25:          /*DEC H */
                hl.b.h = setdec(hl.b.h);
                cycles += 4;
                break;
            case 0x26:          /*LD H,nn */
                cycles += 4;
                hl.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x27:          /*DAA*/
                addr = af.b.h;
                if (af.b.l & C_FLAG)
                    addr |= 256;
                if (af.b.l & H_FLAG)
                    addr |= 512;
                if (af.b.l & N_FLAG)
                    addr |= 1024;
                af.w = DAATable[addr];
                cycles += 4;
                break;
            case 0x28:          /*JR Z */
                cycles += 4;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                if (af.b.l & Z_FLAG) {
                    pc += addr;
                    cycles += 8;
                }
                else
                    cycles += 3;
                break;
            case 0x29:          /*ADD HL,HL */
                intreg = hl.b.h;
                z80_setadd16(hl.w, hl.w);
                hl.w += hl.w;
                cycles += 11;
                break;
            case 0x2A:          /*LD HL,(nn) */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                cycles += 3;
                hl.b.l = z80_readmem(addr);
                cycles += 3;
                hl.b.h = z80_readmem(addr + 1);
                cycles += 3;
                break;
            case 0x2B:          /*DEC HL */
                hl.w--;
                cycles += 6;
                break;
            case 0x2C:          /*INC L */
                hl.b.l = setinc(hl.b.l);
                cycles += 4;
                break;
            case 0x2D:          /*DEC L */
                hl.b.l = setdec(hl.b.l);
                cycles += 4;
                break;
            case 0x2E:          /*LD L,nn */
                cycles += 4;
                hl.b.l = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x2F:          /*CPL*/
                af.b.h ^= 0xFF;
                af.b.l |= (H_FLAG | N_FLAG);
                cycles += 4;
                break;
            case 0x30:          /*JR NC */
                cycles += 4;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                if (!(af.b.l & C_FLAG)) {
                    pc += addr;
                    cycles += 8;
                }
                else
                    cycles += 3;
                break;
            case 0x31:          /*LD SP,nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                cycles += 3;
                sp = (z80_readmem(pc++) << 8) | temp;
                cycles += 3;
                break;
            case 0x32:          /*LD (nn),A */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                cycles += 3;
                z80_writemem(addr, af.b.h);
                cycles += 3;
                break;
            case 0x33:          /*INC SP */
                sp++;
                cycles += 6;
                break;
            case 0x34:          /*INC (HL) */
                cycles += 4;
                temp = setinc(z80_readmem(hl.w));
                cycles += 3;
                z80_writemem(hl.w, temp);
                cycles += 3;
                break;
            case 0x35:          /*DEC (HL) */
                cycles += 4;
                temp = setdec(z80_readmem(hl.w));
                cycles += 3;
                z80_writemem(hl.w, temp);
                cycles += 3;
                break;
            case 0x36:          /*LD (HL),nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                cycles += 3;
                z80_writemem(hl.w, temp);
                cycles += 3;
                break;
            case 0x37:          /*SCF*/
                af.b.l = (af.b.l & ~(N_FLAG | H_FLAG)) | C_FLAG;
                cycles += 4;
                break;
            case 0x38:          /*JR C */
                cycles += 4;
                addr = z80_readmem(pc++);
                if (addr & 0x80)
                    addr |= 0xFF00;
                if (af.b.l & C_FLAG) {
                    pc += addr;
                    cycles += 8;
                }
                else
                    cycles += 3;
                break;
            case 0x39:          /*ADD HL,SP */
                intreg = hl.b.h;
                z80_setadd16(hl.w, sp);
                hl.w += sp;
                cycles += 11;
                break;
            case 0x3A:          /*LD A,(nn) */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                cycles += 3;
                af.b.h = z80_readmem(addr);
                cycles += 3;
                break;
            case 0x3B:          /*DEC SP */
                sp--;
                cycles += 6;
                break;
            case 0x3C:          /*INC A */
                af.b.h = setinc(af.b.h);
                cycles += 4;
                break;
            case 0x3D:          /*DEC A */
                af.b.h = setdec(af.b.h);
                cycles += 4;
                break;
            case 0x3E:          /*LD A,nn */
                cycles += 4;
                af.b.h = z80_readmem(pc++);
                cycles += 3;
                break;
            case 0x3F:          /*CCF*/
                af.b.l = (af.b.l & ~(H_FLAG | N_FLAG)) | ((af.b.l & C_FLAG) << 4);
                af.b.l ^= C_FLAG;
                cycles += 4;
                break;
            case 0x40:          /*LD B,B */
                bc.b.h = bc.b.h;
                cycles += 4;
                break;
            case 0x41:          /*LD B,C */
                bc.b.h = bc.b.l;
                cycles += 4;
                break;
            case 0x42:          /*LD B,D */
                bc.b.h = de.b.h;
                cycles += 4;
                break;
            case 0x43:          /*LD B,E */
                bc.b.h = de.b.l;
                cycles += 4;
                break;
            case 0x44:          /*LD B,H */
                bc.b.h = hl.b.h;
                cycles += 4;
                break;
            case 0x45:          /*LD B,L */
                bc.b.h = hl.b.l;
                cycles += 4;
                break;
            case 0x46:          /*LD B,(HL) */
                cycles += 4;
                bc.b.h = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x47:          /*LD B,A */
                bc.b.h = af.b.h;
                cycles += 4;
                break;
            case 0x48:          /*LD C,B */
                bc.b.l = bc.b.h;
                cycles += 4;
                break;
            case 0x49:          /*LD C,C */
                bc.b.l = bc.b.l;
                cycles += 4;
                break;
            case 0x4A:          /*LD C,D */
                bc.b.l = de.b.h;
                cycles += 4;
                break;
            case 0x4B:          /*LD C,E */
                bc.b.l = de.b.l;
                cycles += 4;
                break;
            case 0x4C:          /*LD C,H */
                bc.b.l = hl.b.h;
                cycles += 4;
                break;
            case 0x4D:          /*LD C,L */
                bc.b.l = hl.b.l;
                cycles += 4;
                break;
            case 0x4E:          /*LD C,(HL) */
                cycles += 4;
                bc.b.l = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x4F:          /*LD C,A */
                bc.b.l = af.b.h;
                cycles += 4;
                break;
            case 0x50:          /*LD D,B */
                de.b.h = bc.b.h;
                cycles += 4;
                break;
            case 0x51:          /*LD D,C */
                de.b.h = bc.b.l;
                cycles += 4;
                break;
            case 0x52:          /*LD D,D */
                de.b.h = de.b.h;
                cycles += 4;
                break;
            case 0x53:          /*LD D,E */
                de.b.h = de.b.l;
                cycles += 4;
                break;
            case 0x54:          /*LD D,H */
                de.b.h = hl.b.h;
                cycles += 4;
                break;
            case 0x55:          /*LD D,L */
                de.b.h = hl.b.l;
                cycles += 4;
                break;
            case 0x56:          /*LD D,(HL) */
                cycles += 4;
                de.b.h = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x57:          /*LD D,A */
                de.b.h = af.b.h;
                cycles += 4;
                break;
            case 0x58:          /*LD E,B */
                de.b.l = bc.b.h;
                cycles += 4;
                break;
            case 0x59:          /*LD E,C */
                de.b.l = bc.b.l;
                cycles += 4;
                break;
            case 0x5A:          /*LD E,D */
                de.b.l = de.b.h;
                cycles += 4;
                break;
            case 0x5B:          /*LD E,E */
                de.b.l = de.b.l;
                cycles += 4;
                break;
            case 0x5C:          /*LD E,H */
                de.b.l = hl.b.h;
                cycles += 4;
                break;
            case 0x5D:          /*LD E,L */
                de.b.l = hl.b.l;
                cycles += 4;
                break;
            case 0x5E:          /*LD E,(HL) */
                cycles += 4;
                de.b.l = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x5F:          /*LD E,A */
                de.b.l = af.b.h;
                cycles += 4;
                break;
            case 0x60:          /*LD H,B */
                hl.b.h = bc.b.h;
                cycles += 4;
                break;
            case 0x61:          /*LD H,C */
                hl.b.h = bc.b.l;
                cycles += 4;
                break;
            case 0x62:          /*LD H,D */
                hl.b.h = de.b.h;
                cycles += 4;
                break;
            case 0x63:          /*LD H,E */
                hl.b.h = de.b.l;
                cycles += 4;
                break;
            case 0x64:          /*LD H,H */
                hl.b.h = hl.b.h;
                cycles += 4;
                break;
            case 0x65:          /*LD H,L */
                hl.b.h = hl.b.l;
                cycles += 4;
                break;
            case 0x66:          /*LD H,(HL) */
                cycles += 4;
                hl.b.h = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x67:          /*LD H,A */
                hl.b.h = af.b.h;
                cycles += 4;
                break;
            case 0x68:          /*LD L,B */
                hl.b.l = bc.b.h;
                cycles += 4;
                break;
            case 0x69:          /*LD L,C */
                hl.b.l = bc.b.l;
                cycles += 4;
                break;
            case 0x6A:          /*LD L,D */
                hl.b.l = de.b.h;
                cycles += 4;
                break;
            case 0x6B:          /*LD L,E */
                hl.b.l = de.b.l;
                cycles += 4;
                break;
            case 0x6C:          /*LD L,H */
                hl.b.l = hl.b.h;
                cycles += 4;
                break;
            case 0x6D:          /*LD L,L */
                hl.b.l = hl.b.l;
                cycles += 4;
                break;
            case 0x6E:          /*LD L,(HL) */
                cycles += 4;
                hl.b.l = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x6F:          /*LD L,A */
                hl.b.l = af.b.h;
                cycles += 4;
                break;
            case 0x70:          /*LD (HL),B */
                cycles += 4;
                z80_writemem(hl.w, bc.b.h);
                cycles += 3;
                break;
            case 0x71:          /*LD (HL),C */
                cycles += 4;
                z80_writemem(hl.w, bc.b.l);
                cycles += 3;
                break;
            case 0x72:          /*LD (HL),D */
                cycles += 4;
                z80_writemem(hl.w, de.b.h);
                cycles += 3;
                break;
            case 0x73:          /*LD (HL),E */
                cycles += 4;
                z80_writemem(hl.w, de.b.l);
                cycles += 3;
                break;
            case 0x74:          /*LD (HL),H */
                cycles += 4;
                z80_writemem(hl.w, hl.b.h);
                cycles += 3;
                break;
            case 0x75:          /*LD (HL),L */
                cycles += 4;
                z80_writemem(hl.w, hl.b.l);
                cycles += 3;
                break;
            case 0x76:          /*HALT*/
                if (!enterint)
                    pc--;
                cycles += 4;
                break;
            case 0x77:          /*LD (HL),A */
                cycles += 4;
                z80_writemem(hl.w, af.b.h);
                cycles += 3;
                break;
            case 0x78:          /*LD A,B */
                af.b.h = bc.b.h;
                cycles += 4;
                break;          /*LD A,C */
            case 0x79:
                af.b.h = bc.b.l;
                cycles += 4;
                break;          /*LD A,D */
            case 0x7A:
                af.b.h = de.b.h;
                cycles += 4;
                break;
            case 0x7B:          /*LD A,E */
                af.b.h = de.b.l;
                cycles += 4;
                break;
            case 0x7C:          /*LD A,H */
                af.b.h = hl.b.h;
                cycles += 4;
                break;
            case 0x7D:          /*LD A,L */
                af.b.h = hl.b.l;
                cycles += 4;
                break;
            case 0x7E:          /*LD A,(HL) */
                cycles += 4;
                af.b.h = z80_readmem(hl.w);
                cycles += 3;
                break;
            case 0x7F:          /*LD A,A */
                af.b.h = af.b.h;
                cycles += 4;
                break;
            case 0x80:          /*ADD B */
                z80_setadd(af.b.h, bc.b.h);
                af.b.h += bc.b.h;
                cycles += 4;
                break;
            case 0x81:          /*ADD C */
                z80_setadd(af.b.h, bc.b.l);
                af.b.h += bc.b.l;
                cycles += 4;
                break;
            case 0x82:          /*ADD D */
                z80_setadd(af.b.h, de.b.h);
                af.b.h += de.b.h;
                cycles += 4;
                break;
            case 0x83:          /*ADD E */
                z80_setadd(af.b.h, de.b.l);
                af.b.h += de.b.l;
                cycles += 4;
                break;
            case 0x84:          /*ADD H */
                z80_setadd(af.b.h, hl.b.h);
                af.b.h += hl.b.h;
                cycles += 4;
                break;
            case 0x85:          /*ADD L */
                z80_setadd(af.b.h, hl.b.l);
                af.b.h += hl.b.l;
                cycles += 4;
                break;
            case 0x86:          /*ADD (HL) */
                cycles += 4;
                temp = z80_readmem(hl.w);
                z80_setadd(af.b.h, temp);
                af.b.h += temp;
                cycles += 3;
                break;
            case 0x87:          /*ADD A */
                z80_setadd(af.b.h, af.b.h);
                af.b.h += af.b.h;
                cycles += 4;
                break;
            case 0x88:          /*ADC B */
                setadc(af.b.h, bc.b.h);
                af.b.h += bc.b.h + tempc;
                cycles += 4;
                break;
            case 0x89:          /*ADC C */
                setadc(af.b.h, bc.b.l);
                af.b.h += bc.b.l + tempc;
                cycles += 4;
                break;
            case 0x8A:          /*ADC D */
                setadc(af.b.h, de.b.h);
                af.b.h += de.b.h + tempc;
                cycles += 4;
                break;
            case 0x8B:          /*ADC E */
                setadc(af.b.h, de.b.l);
                af.b.h += de.b.l + tempc;
                cycles += 4;
                break;
            case 0x8C:          /*ADC H */
                setadc(af.b.h, hl.b.h);
                af.b.h += hl.b.h + tempc;
                cycles += 4;
                break;
            case 0x8D:          /*ADC L */
                setadc(af.b.h, hl.b.l);
                af.b.h += hl.b.l + tempc;
                cycles += 4;
                break;
            case 0x8E:          /*ADC (HL) */
                cycles += 4;
                temp = z80_readmem(hl.w);
                setadc(af.b.h, temp);
                af.b.h += temp + tempc;
                cycles += 3;
                break;
            case 0x8F:          /*ADC A */
                setadc(af.b.h, af.b.h);
                af.b.h += af.b.h + tempc;
                cycles += 4;
                break;
            case 0x90:          /*SUB B */
                z80_setsub(af.b.h, bc.b.h);
                af.b.h -= bc.b.h;
                cycles += 4;
                break;
            case 0x91:          /*SUB C */
                z80_setsub(af.b.h, bc.b.l);
                af.b.h -= bc.b.l;
                cycles += 4;
                break;
            case 0x92:          /*SUB D */
                z80_setsub(af.b.h, de.b.h);
                af.b.h -= de.b.h;
                cycles += 4;
                break;
            case 0x93:          /*SUB E */
                z80_setsub(af.b.h, de.b.l);
                af.b.h -= de.b.l;
                cycles += 4;
                break;
            case 0x94:          /*SUB H */
                z80_setsub(af.b.h, hl.b.h);
                af.b.h -= hl.b.h;
                cycles += 4;
                break;
            case 0x95:          /*SUB L */
                z80_setsub(af.b.h, hl.b.l);
                af.b.h -= hl.b.l;
                cycles += 4;
                break;
            case 0x96:          /*SUB (HL) */
                cycles += 4;
                temp = z80_readmem(hl.w);
                z80_setsub(af.b.h, temp);
                af.b.h -= temp;
                cycles += 3;
                break;
            case 0x97:          /*SUB A */
                z80_setsub(af.b.h, af.b.h);
                af.b.h -= af.b.h;
                cycles += 4;
                break;
            case 0x98:          /*SBC B */
                setsbc(af.b.h, bc.b.h);
                af.b.h -= (bc.b.h + tempc);
                cycles += 4;
                break;
            case 0x99:          /*SBC C */
                setsbc(af.b.h, bc.b.l);
                af.b.h -= (bc.b.l + tempc);
                cycles += 4;
                break;
            case 0x9A:          /*SBC D */
                setsbc(af.b.h, de.b.h);
                af.b.h -= (de.b.h + tempc);
                cycles += 4;
                break;
            case 0x9B:          /*SBC E */
                setsbc(af.b.h, de.b.l);
                af.b.h -= (de.b.l + tempc);
                cycles += 4;
                break;
            case 0x9C:          /*SBC H */
                setsbc(af.b.h, hl.b.h);
                af.b.h -= (hl.b.h + tempc);
                cycles += 4;
                break;
            case 0x9D:          /*SBC L */
                setsbc(af.b.h, hl.b.l);
                af.b.h -= (hl.b.l + tempc);
                cycles += 4;
                break;
            case 0x9E:          /*SBC (HL) */
                cycles += 4;
                temp = z80_readmem(hl.w);
                setsbc(af.b.h, temp);
                af.b.h -= (temp + tempc);
                cycles += 3;
                break;
            case 0x9F:          /*SBC A */
                setsbc(af.b.h, af.b.h);
                af.b.h -= (af.b.h + tempc);
                cycles += 4;
                break;
            case 0xA0:          /*AND B */
                af.b.h &= bc.b.h;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA1:          /*AND C */
                af.b.h &= bc.b.l;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA2:          /*AND D */
                af.b.h &= de.b.h;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA3:          /*AND E */
                af.b.h &= de.b.l;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA4:          /*AND H */
                af.b.h &= hl.b.h;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA5:          /*AND L */
                af.b.h &= hl.b.l;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA6:          /*AND (HL) */
                cycles += 4;
                af.b.h &= z80_readmem(hl.w);
                setand(af.b.h);
                cycles += 3;
                break;
            case 0xA7:          /*AND A */
                af.b.h &= af.b.h;
                setand(af.b.h);
                cycles += 4;
                break;
            case 0xA8:          /*XOR B */
                af.b.h ^= bc.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xA9:          /*XOR C */
                af.b.h ^= bc.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xAA:          /*XOR D */
                af.b.h ^= de.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xAB:          /*XOR E */
                af.b.h ^= de.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xAC:          /*XOR H */
                af.b.h ^= hl.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xAD:          /*XOR L */
                af.b.h ^= hl.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xAE:          /*XOR (HL) */
                cycles += 4;
                af.b.h ^= z80_readmem(hl.w);
                setzn(af.b.h);
                cycles += 3;
                break;
            case 0xAF:          /*XOR A */
                af.b.h ^= af.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB0:          /*OR B */
                af.b.h |= bc.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB1:          /*OR C */
                af.b.h |= bc.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB2:          /*OR D */
                af.b.h |= de.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB3:          /*OR E */
                af.b.h |= de.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB4:          /*OR H */
                af.b.h |= hl.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB5:          /*OR L */
                af.b.h |= hl.b.l;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB6:          /*OR (HL) */
                cycles += 4;
                af.b.h |= z80_readmem(hl.w);
                setzn(af.b.h);
                cycles += 3;
                break;
            case 0xB7:          /*OR A */
                af.b.h |= af.b.h;
                setzn(af.b.h);
                cycles += 4;
                break;
            case 0xB8:          /*CP B */
                setcp(af.b.h, bc.b.h);
                cycles += 4;
                break;
            case 0xB9:          /*CP C */
                setcp(af.b.h, bc.b.l);
                cycles += 4;
                break;
            case 0xBA:          /*CP D */
                setcp(af.b.h, de.b.h);
                cycles += 4;
                break;
            case 0xBB:          /*CP E */
                setcp(af.b.h, de.b.l);
                cycles += 4;
                break;
            case 0xBC:          /*CP H */
                setcp(af.b.h, hl.b.h);
                cycles += 4;
                break;
            case 0xBD:          /*CP L */
                setcp(af.b.h, hl.b.l);
                cycles += 4;
                break;
            case 0xBE:          /*CP (HL) */
                cycles += 4;
                temp = z80_readmem(hl.w);
                setcp(af.b.h, temp);
                cycles += 3;
                break;
            case 0xBF:          /*CP A */
                setcp(af.b.h, af.b.h);
                cycles += 4;
                break;
            case 0xC0:          /*RET NZ */
                cycles += 5;
                if (!(af.b.l & Z_FLAG)) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xC1:          /*POP BC */
                cycles += 4;
                bc.b.l = z80_readmem(sp);
                sp++;
                cycles += 3;
                bc.b.h = z80_readmem(sp);
                sp++;
                cycles += 3;
                break;
            case 0xC2:          /*JP NZ */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & Z_FLAG))
                    pc = addr;
                cycles += 3;
                break;
            case 0xC3:          /*JP xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc = addr;
                cycles += 3;
                break;
            case 0xC4:          /*CALL NZ,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & Z_FLAG)) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xC5:          /*PUSH BC */
                cycles += 5;
                sp--;
                z80_writemem(sp, bc.b.h);
                cycles += 3;
                sp--;
                z80_writemem(sp, bc.b.l);
                cycles += 3;
                break;
            case 0xC6:          /*ADD A,nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                z80_setadd(af.b.h, temp);
                af.b.h += temp;
                cycles += 3;
                break;
            case 0xC7:          /*RST 0 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x00;
                cycles += 3;
                break;
            case 0xC8:          /*RET Z */
                cycles += 5;
                if (af.b.l & Z_FLAG) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xC9:          /*RET*/
                cycles += 4;
                pc = z80_readmem(sp);
                sp++;
                cycles += 3;
                pc |= (z80_readmem(sp) << 8);
                sp++;
                cycles += 3;
                break;
            case 0xCA:          /*JP Z */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & Z_FLAG)
                    pc = addr;
                cycles += 3;
                break;
            case 0xCB:          /*More opcodes */
                ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                cycles += 4;
                opcode = z80_readmem(pc++);
                switch (opcode) {
                    case 0x00:          /*RLC B */
                        temp = bc.b.h & 0x80;
                        bc.b.h <<= 1;
                        if (temp)
                            bc.b.h |= 1;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x01:          /*RLC C */
                        temp = bc.b.l & 0x80;
                        bc.b.l <<= 1;
                        if (temp)
                            bc.b.l |= 1;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x02:          /*RLC D */
                        temp = de.b.h & 0x80;
                        de.b.h <<= 1;
                        if (temp)
                            de.b.h |= 1;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x03:          /*RLC E */
                        temp = de.b.l & 0x80;
                        de.b.l <<= 1;
                        if (temp)
                            de.b.l |= 1;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x04:          /*RLC H */
                        temp = hl.b.h & 0x80;
                        hl.b.h <<= 1;
                        if (temp)
                            hl.b.h |= 1;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x05:          /*RLC L */
                        temp = hl.b.l & 0x80;
                        hl.b.l <<= 1;
                        if (temp)
                            hl.b.l |= 1;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x06:          /*RLC (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 0x80;
                        temp <<= 1;
                        if (tempc)
                            temp |= 1;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x07:          /*RLC A */
                        temp = af.b.h & 0x80;
                        af.b.h <<= 1;
                        if (temp)
                            af.b.h |= 1;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x08:          /*RRC B */
                        temp = bc.b.h & 1;
                        bc.b.h >>= 1;
                        if (temp)
                            bc.b.h |= 0x80;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x09:          /*RRC C */
                        temp = bc.b.l & 1;
                        bc.b.l >>= 1;
                        if (temp)
                            bc.b.l |= 0x80;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x0A:          /*RRC D */
                        temp = de.b.h & 1;
                        de.b.h >>= 1;
                        if (temp)
                            de.b.h |= 0x80;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x0B:          /*RRC E */
                        temp = de.b.l & 1;
                        de.b.l >>= 1;
                        if (temp)
                            de.b.l |= 0x80;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x0C:          /*RRC H */
                        temp = hl.b.h & 1;
                        hl.b.h >>= 1;
                        if (temp)
                            hl.b.h |= 0x80;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x0D:          /*RRC L */
                        temp = hl.b.l & 1;
                        hl.b.l >>= 1;
                        if (temp)
                            hl.b.l |= 0x80;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x0E:          /*RRC (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 1;
                        temp >>= 1;
                        if (tempc)
                            temp |= 0x80;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x0F:          /*RRC A */
                        temp = af.b.h & 1;
                        af.b.h >>= 1;
                        if (temp)
                            af.b.h |= 0x80;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x10:          /*RL B */
                        temp = bc.b.h & 0x80;
                        bc.b.h <<= 1;
                        if (tempc)
                            bc.b.h |= 1;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x11:          /*RL C */
                        temp = bc.b.l & 0x80;
                        bc.b.l <<= 1;
                        if (tempc)
                            bc.b.l |= 1;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x12:          /*RL D */
                        temp = de.b.h & 0x80;
                        de.b.h <<= 1;
                        if (tempc)
                            de.b.h |= 1;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x13:          /*RL E */
                        temp = de.b.l & 0x80;
                        de.b.l <<= 1;
                        if (tempc)
                            de.b.l |= 1;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x14:          /*RL H */
                        temp = hl.b.h & 0x80;
                        hl.b.h <<= 1;
                        if (tempc)
                            hl.b.h |= 1;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x15:          /*RL L */
                        temp = hl.b.l & 0x80;
                        hl.b.l <<= 1;
                        if (tempc)
                            hl.b.l |= 1;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x16:          /*RL (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        addr = temp & 0x80;
                        temp <<= 1;
                        if (tempc)
                            temp |= 1;
                        setzn(temp);
                        if (addr)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x17:          /*RL A */
                        temp = af.b.h & 0x80;
                        af.b.h <<= 1;
                        if (tempc)
                            af.b.h |= 1;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x18:          /*RR B */
                        temp = bc.b.h & 1;
                        bc.b.h >>= 1;
                        if (tempc)
                            bc.b.h |= 0x80;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x19:          /*RR C */
                        temp = bc.b.l & 1;
                        bc.b.l >>= 1;
                        if (tempc)
                            bc.b.l |= 0x80;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x1A:          /*RR D */
                        temp = de.b.h & 1;
                        de.b.h >>= 1;
                        if (tempc)
                            de.b.h |= 0x80;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x1B:          /*RR E */
                        temp = de.b.l & 1;
                        de.b.l >>= 1;
                        if (tempc)
                            de.b.l |= 0x80;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x1C:          /*RR H */
                        temp = hl.b.h & 1;
                        hl.b.h >>= 1;
                        if (tempc)
                            hl.b.h |= 0x80;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x1D:          /*RR L */
                        temp = hl.b.l & 1;
                        hl.b.l >>= 1;
                        if (tempc)
                            hl.b.l |= 0x80;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x1E:          /*RR (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        addr = temp & 1;
                        temp >>= 1;
                        if (tempc)
                            temp |= 0x80;
                        setzn(temp);
                        if (addr)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x1F:          /*RR A */
                        temp = af.b.h & 1;
                        af.b.h >>= 1;
                        if (tempc)
                            af.b.h |= 0x80;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x20:          /*SLA B */
                        temp = bc.b.h & 0x80;
                        bc.b.h <<= 1;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x21:          /*SLA C */
                        temp = bc.b.l & 0x80;
                        bc.b.l <<= 1;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x22:          /*SLA D */
                        temp = de.b.h & 0x80;
                        de.b.h <<= 1;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x23:          /*SLA E */
                        temp = de.b.l & 0x80;
                        de.b.l <<= 1;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x24:          /*SLA H */
                        temp = hl.b.h & 0x80;
                        hl.b.h <<= 1;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x25:          /*SLA L */
                        temp = hl.b.l & 0x80;
                        hl.b.l <<= 1;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x26:          /*SLA (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x27:          /*SLA H */
                        temp = af.b.h & 0x80;
                        af.b.h <<= 1;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x28:          /*SRA B */
                        temp = bc.b.h & 1;
                        bc.b.h >>= 1;
                        if (bc.b.h & 0x40)
                            bc.b.h |= 0x80;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x29:          /*SRA C */
                        temp = bc.b.l & 1;
                        bc.b.l >>= 1;
                        if (bc.b.l & 0x40)
                            bc.b.l |= 0x80;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x2A:          /*SRA D */
                        temp = de.b.h & 1;
                        de.b.h >>= 1;
                        if (de.b.h & 0x40)
                            de.b.h |= 0x80;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x2B:          /*SRA E */
                        temp = de.b.l & 1;
                        de.b.l >>= 1;
                        if (de.b.l & 0x40)
                            de.b.l |= 0x80;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x2C:          /*SRA H */
                        temp = hl.b.h & 1;
                        hl.b.h >>= 1;
                        if (hl.b.h & 0x40)
                            hl.b.h |= 0x80;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x2D:          /*SRA L */
                        temp = hl.b.l & 1;
                        hl.b.l >>= 1;
                        if (hl.b.l & 0x40)
                            hl.b.l |= 0x80;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x2E:          /*SRA (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 1;
                        temp >>= 1;
                        if (temp & 0x40)
                            temp |= 0x80;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x2F:          /*SRA A */
                        temp = af.b.h & 1;
                        af.b.h >>= 1;
                        if (af.b.h & 0x40)
                            af.b.h |= 0x80;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x30:          /*SLL B */
                        temp = bc.b.h & 0x80;
                        bc.b.h <<= 1;
                        bc.b.h |= 1;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x31:          /*SLL C */
                        temp = bc.b.l & 0x80;
                        bc.b.l <<= 1;
                        bc.b.l |= 1;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x32:          /*SLL D */
                        temp = de.b.h & 0x80;
                        de.b.h <<= 1;
                        de.b.h |= 1;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x33:          /*SLL E */
                        temp = de.b.l & 0x80;
                        de.b.l <<= 1;
                        de.b.l |= 1;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x34:          /*SLL H */
                        temp = hl.b.h & 0x80;
                        hl.b.h <<= 1;
                        hl.b.h |= 1;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x35:          /*SLL L */
                        temp = hl.b.l & 0x80;
                        hl.b.l <<= 1;
                        hl.b.l |= 1;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x36:          /*SLL (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 0x80;
                        temp <<= 1;
                        temp |= 1;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x37:          /*SLL H */
                        temp = af.b.h & 0x80;
                        af.b.h <<= 1;
                        af.b.h |= 1;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x38:          /*SRL B */
                        temp = bc.b.h & 1;
                        bc.b.h >>= 1;
                        setzn(bc.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x39:          /*SRL C */
                        temp = bc.b.l & 1;
                        bc.b.l >>= 1;
                        setzn(bc.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x3A:          /*SRL D */
                        temp = de.b.h & 1;
                        de.b.h >>= 1;
                        setzn(de.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x3B:          /*SRL E */
                        temp = de.b.l & 1;
                        de.b.l >>= 1;
                        setzn(de.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x3C:          /*SRL H */
                        temp = hl.b.h & 1;
                        hl.b.h >>= 1;
                        setzn(hl.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x3D:          /*SRL L */
                        temp = hl.b.l & 1;
                        hl.b.l >>= 1;
                        setzn(hl.b.l);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x3E:          /*SRL (HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        tempc = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        if (tempc)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x3F:          /*SRL H */
                        temp = af.b.h & 1;
                        af.b.h >>= 1;
                        setzn(af.b.h);
                        if (temp)
                            af.b.l |= C_FLAG;
                        cycles += 4;
                        break;
                    case 0x40:          /*BIT 0,B */
                        setbit(bc.b.h & 0x01);
                        cycles += 4;
                        break;
                    case 0x41:          /*BIT 0,C */
                        setbit(bc.b.l & 0x01);
                        cycles += 4;
                        break;
                    case 0x42:          /*BIT 0,D */
                        setbit(de.b.h & 0x01);
                        cycles += 4;
                        break;
                    case 0x43:          /*BIT 0,E */
                        setbit(de.b.l & 0x01);
                        cycles += 4;
                        break;
                    case 0x44:          /*BIT 0,H */
                        setbit(hl.b.h & 0x01);
                        cycles += 4;
                        break;
                    case 0x45:          /*BIT 0,L */
                        setbit(hl.b.l & 0x01);
                        cycles += 4;
                        break;
                    case 0x46:          /*BIT 0,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x01, intreg);
                        cycles += 4;
                        break;
                    case 0x47:          /*BIT 0,A */
                        setbit(af.b.h & 0x01);
                        cycles += 4;
                        break;
                    case 0x48:          /*BIT 1,B */
                        setbit(bc.b.h & 0x02);
                        cycles += 4;
                        break;
                    case 0x49:          /*BIT 1,C */
                        setbit(bc.b.l & 0x02);
                        cycles += 4;
                        break;
                    case 0x4A:          /*BIT 1,D */
                        setbit(de.b.h & 0x02);
                        cycles += 4;
                        break;
                    case 0x4B:          /*BIT 1,E */
                        setbit(de.b.l & 0x02);
                        cycles += 4;
                        break;
                    case 0x4C:          /*BIT 1,H */
                        setbit(hl.b.h & 0x02);
                        cycles += 4;
                        break;
                    case 0x4D:          /*BIT 1,L */
                        setbit(hl.b.l & 0x02);
                        cycles += 4;
                        break;
                    case 0x4E:          /*BIT 1,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x02, intreg);
                        cycles += 4;
                        break;
                    case 0x4F:          /*BIT 1,A */
                        setbit(af.b.h & 0x02);
                        cycles += 4;
                        break;
                    case 0x50:          /*BIT 2,B */
                        setbit(bc.b.h & 0x04);
                        cycles += 4;
                        break;
                    case 0x51:          /*BIT 2,C */
                        setbit(bc.b.l & 0x04);
                        cycles += 4;
                        break;
                    case 0x52:          /*BIT 2,D */
                        setbit(de.b.h & 0x04);
                        cycles += 4;
                        break;
                    case 0x53:          /*BIT 2,E */
                        setbit(de.b.l & 0x04);
                        cycles += 4;
                        break;
                    case 0x54:          /*BIT 2,H */
                        setbit(hl.b.h & 0x04);
                        cycles += 4;
                        break;
                    case 0x55:          /*BIT 2,L */
                        setbit(hl.b.l & 0x04);
                        cycles += 4;
                        break;
                    case 0x56:          /*BIT 2,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x04, intreg);
                        cycles += 4;
                        break;
                    case 0x57:          /*BIT 2,A */
                        setbit(af.b.h & 0x04);
                        cycles += 4;
                        break;
                    case 0x58:          /*BIT 3,B */
                        setbit(bc.b.h & 0x08);
                        cycles += 4;
                        break;
                    case 0x59:          /*BIT 3,C */
                        setbit(bc.b.l & 0x08);
                        cycles += 4;
                        break;
                    case 0x5A:          /*BIT 3,D */
                        setbit(de.b.h & 0x08);
                        cycles += 4;
                        break;
                    case 0x5B:          /*BIT 3,E */
                        setbit(de.b.l & 0x08);
                        cycles += 4;
                        break;
                    case 0x5C:          /*BIT 3,H */
                        setbit(hl.b.h & 0x08);
                        cycles += 4;
                        break;
                    case 0x5D:          /*BIT 3,L */
                        setbit(hl.b.l & 0x08);
                        cycles += 4;
                        break;
                    case 0x5E:          /*BIT 3,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x08, intreg);
                        cycles += 4;
                        break;
                    case 0x5F:          /*BIT 3,A */
                        setbit(af.b.h & 0x08);
                        cycles += 4;
                        break;
                    case 0x60:          /*BIT 4,B */
                        setbit(bc.b.h & 0x10);
                        cycles += 4;
                        break;
                    case 0x61:          /*BIT 4,C */
                        setbit(bc.b.l & 0x10);
                        cycles += 4;
                        break;
                    case 0x62:          /*BIT 4,D */
                        setbit(de.b.h & 0x10);
                        cycles += 4;
                        break;
                    case 0x63:          /*BIT 4,E */
                        setbit(de.b.l & 0x10);
                        cycles += 4;
                        break;
                    case 0x64:          /*BIT 4,H */
                        setbit(hl.b.h & 0x10);
                        cycles += 4;
                        break;
                    case 0x65:          /*BIT 4,L */
                        setbit(hl.b.l & 0x10);
                        cycles += 4;
                        break;
                    case 0x66:          /*BIT 4,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x10, intreg);
                        cycles += 4;
                        break;
                    case 0x67:          /*BIT 4,A */
                        setbit(af.b.h & 0x10);
                        cycles += 4;
                        break;
                    case 0x68:          /*BIT 5,B */
                        setbit(bc.b.h & 0x20);
                        cycles += 4;
                        break;
                    case 0x69:          /*BIT 5,C */
                        setbit(bc.b.l & 0x20);
                        cycles += 4;
                        break;
                    case 0x6A:          /*BIT 5,D */
                        setbit(de.b.h & 0x20);
                        cycles += 4;
                        break;
                    case 0x6B:          /*BIT 5,E */
                        setbit(de.b.l & 0x20);
                        cycles += 4;
                        break;
                    case 0x6C:          /*BIT 5,H */
                        setbit(hl.b.h & 0x20);
                        cycles += 4;
                        break;
                    case 0x6D:          /*BIT 5,L */
                        setbit(hl.b.l & 0x20);
                        cycles += 4;
                        break;
                    case 0x6E:          /*BIT 5,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x20, intreg);
                        cycles += 4;
                        break;
                    case 0x6F:          /*BIT 5,A */
                        setbit(af.b.h & 0x20);
                        cycles += 4;
                        break;
                    case 0x70:          /*BIT 6,B */
                        setbit(bc.b.h & 0x40);
                        cycles += 4;
                        break;
                    case 0x71:          /*BIT 6,C */
                        setbit(bc.b.l & 0x40);
                        cycles += 4;
                        break;
                    case 0x72:          /*BIT 6,D */
                        setbit(de.b.h & 0x40);
                        cycles += 4;
                        break;
                    case 0x73:          /*BIT 6,E */
                        setbit(de.b.l & 0x40);
                        cycles += 4;
                        break;
                    case 0x74:          /*BIT 6,H */
                        setbit(hl.b.h & 0x40);
                        cycles += 4;
                        break;
                    case 0x75:          /*BIT 6,L */
                        setbit(hl.b.l & 0x40);
                        cycles += 4;
                        break;
                    case 0x76:          /*BIT 6,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x40, intreg);
                        cycles += 4;
                        break;
                    case 0x77:          /*BIT 6,A */
                        setbit(af.b.h & 0x40);
                        cycles += 4;
                        break;
                    case 0x78:          /*BIT 7,B */
                        setbit(bc.b.h & 0x80);
                        cycles += 4;
                        break;
                    case 0x79:          /*BIT 7,C */
                        setbit(bc.b.l & 0x80);
                        cycles += 4;
                        break;
                    case 0x7A:          /*BIT 7,D */
                        setbit(de.b.h & 0x80);
                        cycles += 4;
                        break;
                    case 0x7B:          /*BIT 7,E */
                        setbit(de.b.l & 0x80);
                        cycles += 4;
                        break;
                    case 0x7C:          /*BIT 7,H */
                        setbit(hl.b.h & 0x80);
                        cycles += 4;
                        break;
                    case 0x7D:          /*BIT 7,L */
                        setbit(hl.b.l & 0x80);
                        cycles += 4;
                        break;
                    case 0x7E:          /*BIT 7,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w);
                        setbit2(temp & 0x80, intreg);
                        cycles += 4;
                        break;
                    case 0x7F:          /*BIT 7,A */
                        setbit(af.b.h & 0x80);
                        cycles += 4;
                        break;
                    case 0x80:          /*RES 0,B */
                        bc.b.h &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x81:          /*RES 0,C */
                        bc.b.l &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x82:          /*RES 0,D */
                        de.b.h &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x83:          /*RES 0,E */
                        de.b.l &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x84:          /*RES 0,H */
                        hl.b.h &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x85:          /*RES 0,L */
                        hl.b.l &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x86:          /*RES 0,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x01;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x87:          /*RES 0,A */
                        af.b.h &= ~0x01;
                        cycles += 4;
                        break;
                    case 0x88:          /*RES 1,B */
                        bc.b.h &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x89:          /*RES 1,C */
                        bc.b.l &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x8A:          /*RES 1,D */
                        de.b.h &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x8B:          /*RES 1,E */
                        de.b.l &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x8C:          /*RES 1,H */
                        hl.b.h &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x8D:          /*RES 1,L */
                        hl.b.l &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x8E:          /*RES 1,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x02;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x8F:          /*RES 1,A */
                        af.b.h &= ~0x02;
                        cycles += 4;
                        break;
                    case 0x90:          /*RES 2,B */
                        bc.b.h &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x91:          /*RES 2,C */
                        bc.b.l &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x92:          /*RES 2,D */
                        de.b.h &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x93:          /*RES 2,E */
                        de.b.l &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x94:          /*RES 2,H */
                        hl.b.h &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x95:          /*RES 2,L */
                        hl.b.l &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x96:          /*RES 2,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x04;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x97:          /*RES 2,A */
                        af.b.h &= ~0x04;
                        cycles += 4;
                        break;
                    case 0x98:          /*RES 3,B */
                        bc.b.h &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x99:          /*RES 3,C */
                        bc.b.l &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x9A:          /*RES 3,D */
                        de.b.h &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x9B:          /*RES 3,E */
                        de.b.l &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x9C:          /*RES 3,H */
                        hl.b.h &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x9D:          /*RES 3,L */
                        hl.b.l &= ~0x08;
                        cycles += 4;
                        break;
                    case 0x9E:          /*RES 3,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x08;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0x9F:          /*RES 3,A */
                        af.b.h &= ~0x08;
                        cycles += 4;
                        break;
                    case 0xA0:          /*RES 4,B */
                        bc.b.h &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA1:          /*RES 4,C */
                        bc.b.l &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA2:          /*RES 4,D */
                        de.b.h &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA3:          /*RES 4,E */
                        de.b.l &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA4:          /*RES 4,H */
                        hl.b.h &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA5:          /*RES 4,L */
                        hl.b.l &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA6:          /*RES 4,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x10;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xA7:          /*RES 4,A */
                        af.b.h &= ~0x10;
                        cycles += 4;
                        break;
                    case 0xA8:          /*RES 5,B */
                        bc.b.h &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xA9:          /*RES 5,C */
                        bc.b.l &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xAA:          /*RES 5,D */
                        de.b.h &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xAB:          /*RES 5,E */
                        de.b.l &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xAC:          /*RES 5,H */
                        hl.b.h &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xAD:          /*RES 5,L */
                        hl.b.l &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xAE:          /*RES 5,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x20;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xAF:          /*RES 5,A */
                        af.b.h &= ~0x20;
                        cycles += 4;
                        break;
                    case 0xB0:          /*RES 6,B */
                        bc.b.h &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB1:          /*RES 6,C */
                        bc.b.l &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB2:          /*RES 6,D */
                        de.b.h &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB3:          /*RES 6,E */
                        de.b.l &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB4:          /*RES 6,H */
                        hl.b.h &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB5:          /*RES 6,L */
                        hl.b.l &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB6:          /*RES 6,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x40;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xB7:          /*RES 6,A */
                        af.b.h &= ~0x40;
                        cycles += 4;
                        break;
                    case 0xB8:          /*RES 7,B */
                        bc.b.h &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xB9:          /*RES 7,C */
                        bc.b.l &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xBA:          /*RES 7,D */
                        de.b.h &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xBB:          /*RES 7,E */
                        de.b.l &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xBC:          /*RES 7,H */
                        hl.b.h &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xBD:          /*RES 7,L */
                        hl.b.l &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xBE:          /*RES 7,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) & ~0x80;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xBF:          /*RES 7,A */
                        af.b.h &= ~0x80;
                        cycles += 4;
                        break;
                    case 0xC0:          /*SET 0,B */
                        bc.b.h |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC1:          /*SET 0,C */
                        bc.b.l |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC2:          /*SET 0,D */
                        de.b.h |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC3:          /*SET 0,E */
                        de.b.l |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC4:          /*SET 0,H */
                        hl.b.h |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC5:          /*SET 0,L */
                        hl.b.l |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC6:          /*SET 0,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x01;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xC7:          /*SET 0,A */
                        af.b.h |= 0x01;
                        cycles += 4;
                        break;
                    case 0xC8:          /*SET 1,B */
                        bc.b.h |= 0x02;
                        cycles += 4;
                        break;
                    case 0xC9:          /*SET 1,C */
                        bc.b.l |= 0x02;
                        cycles += 4;
                        break;
                    case 0xCA:          /*SET 1,D */
                        de.b.h |= 0x02;
                        cycles += 4;
                        break;
                    case 0xCB:          /*SET 1,E */
                        de.b.l |= 0x02;
                        cycles += 4;
                        break;
                    case 0xCC:          /*SET 1,H */
                        hl.b.h |= 0x02;
                        cycles += 4;
                        break;
                    case 0xCD:          /*SET 1,L */
                        hl.b.l |= 0x02;
                        cycles += 4;
                        break;
                    case 0xCE:          /*SET 1,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x02;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xCF:          /*SET 1,A */
                        af.b.h |= 0x02;
                        cycles += 4;
                        break;
                    case 0xD0:          /*SET 2,B */
                        bc.b.h |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD1:          /*SET 2,C */
                        bc.b.l |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD2:          /*SET 2,D */
                        de.b.h |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD3:          /*SET 2,E */
                        de.b.l |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD4:          /*SET 2,H */
                        hl.b.h |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD5:          /*SET 2,L */
                        hl.b.l |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD6:          /*SET 2,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x04;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xD7:          /*SET 2,A */
                        af.b.h |= 0x04;
                        cycles += 4;
                        break;
                    case 0xD8:          /*SET 3,B */
                        bc.b.h |= 0x08;
                        cycles += 4;
                        break;
                    case 0xD9:          /*SET 3,C */
                        bc.b.l |= 0x08;
                        cycles += 4;
                        break;
                    case 0xDA:          /*SET 3,D */
                        de.b.h |= 0x08;
                        cycles += 4;
                        break;
                    case 0xDB:          /*SET 3,E */
                        de.b.l |= 0x08;
                        cycles += 4;
                        break;
                    case 0xDC:          /*SET 3,H */
                        hl.b.h |= 0x08;
                        cycles += 4;
                        break;
                    case 0xDD:          /*SET 3,L */
                        hl.b.l |= 0x08;
                        cycles += 4;
                        break;
                    case 0xDE:          /*SET 3,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x08;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xDF:          /*SET 3,A */
                        af.b.h |= 0x08;
                        cycles += 4;
                        break;
                    case 0xE0:          /*SET 4,B */
                        bc.b.h |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE1:          /*SET 4,C */
                        bc.b.l |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE2:          /*SET 4,D */
                        de.b.h |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE3:          /*SET 4,E */
                        de.b.l |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE4:          /*SET 4,H */
                        hl.b.h |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE5:          /*SET 4,L */
                        hl.b.l |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE6:          /*SET 4,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x10;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xE7:          /*SET 4,A */
                        af.b.h |= 0x10;
                        cycles += 4;
                        break;
                    case 0xE8:          /*SET 5,B */
                        bc.b.h |= 0x20;
                        cycles += 4;
                        break;
                    case 0xE9:          /*SET 5,C */
                        bc.b.l |= 0x20;
                        cycles += 4;
                        break;
                    case 0xEA:          /*SET 5,D */
                        de.b.h |= 0x20;
                        cycles += 4;
                        break;
                    case 0xEB:          /*SET 5,E */
                        de.b.l |= 0x20;
                        cycles += 4;
                        break;
                    case 0xEC:          /*SET 5,H */
                        hl.b.h |= 0x20;
                        cycles += 4;
                        break;
                    case 0xED:          /*SET 5,L */
                        hl.b.l |= 0x20;
                        cycles += 4;
                        break;
                    case 0xEE:          /*SET 5,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x20;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xEF:          /*SET 5,A */
                        af.b.h |= 0x20;
                        cycles += 4;
                        break;
                    case 0xF0:          /*SET 6,B */
                        bc.b.h |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF1:          /*SET 6,C */
                        bc.b.l |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF2:          /*SET 6,D */
                        de.b.h |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF3:          /*SET 6,E */
                        de.b.l |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF4:          /*SET 6,H */
                        hl.b.h |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF5:          /*SET 6,L */
                        hl.b.l |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF6:          /*SET 6,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x40;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xF7:          /*SET 6,A */
                        af.b.h |= 0x40;
                        cycles += 4;
                        break;
                    case 0xF8:          /*SET 7,B */
                        bc.b.h |= 0x80;
                        cycles += 4;
                        break;
                    case 0xF9:          /*SET 7,C */
                        bc.b.l |= 0x80;
                        cycles += 4;
                        break;
                    case 0xFA:          /*SET 7,D */
                        de.b.h |= 0x80;
                        cycles += 4;
                        break;
                    case 0xFB:          /*SET 7,E */
                        de.b.l |= 0x80;
                        cycles += 4;
                        break;
                    case 0xFC:          /*SET 7,H */
                        hl.b.h |= 0x80;
                        cycles += 4;
                        break;
                    case 0xFD:          /*SET 7,L */
                        hl.b.l |= 0x80;
                        cycles += 4;
                        break;
                    case 0xFE:          /*SET 7,(HL) */
                        cycles += 4;
                        temp = z80_readmem(hl.w) | 0x80;
                        cycles += 4;
                        z80_writemem(hl.w, temp);
                        cycles += 3;
                        break;
                    case 0xFF:          /*SET 7,A */
                        af.b.h |= 0x80;
                        cycles += 4;
                        break;
                }
                break;
            case 0xCC:          /*CALL Z,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & Z_FLAG) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xCD:          /*CALL xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                cycles += 4;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = addr;
                cycles += 3;
                break;
            case 0xCE:          /*ADC A,nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                setadc(af.b.h, temp);
                af.b.h += temp + tempc;
                cycles += 3;
                break;
            case 0xCF:          /*RST 8 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x08;
                cycles += 3;
                break;
            case 0xD0:          /*RET NC */
                cycles += 5;
                if (!(af.b.l & C_FLAG)) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xD1:          /*POP DE */
                cycles += 4;
                de.b.l = z80_readmem(sp);
                sp++;
                cycles += 3;
                de.b.h = z80_readmem(sp);
                sp++;
                cycles += 3;
                break;
            case 0xD2:          /*JP NC */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & C_FLAG))
                    pc = addr;
                cycles += 3;
                break;
            case 0xD3:          /*OUT (nn),A */
                cycles += 4;
                addr = z80_readmem(pc++);
                cycles += 3;
                z80out(addr, af.b.h);
                cycles += 4;
                break;
            case 0xD4:          /*CALL NC,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & C_FLAG)) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xD5:          /*PUSH DE */
                cycles += 5;
                sp--;
                z80_writemem(sp, de.b.h);
                cycles += 3;
                sp--;
                z80_writemem(sp, de.b.l);
                cycles += 3;
                break;
            case 0xD6:          /*SUB A,nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                z80_setsub(af.b.h, temp);
                af.b.h -= temp;
                cycles += 3;
                break;
            case 0xD7:          /*RST 10 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x10;
                cycles += 3;
                break;
            case 0xD8:          /*RET C */
                cycles += 5;
                if (af.b.l & C_FLAG) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xD9:          /*EXX*/
                addr = bc.w;
                bc.w = sbc.w;
                sbc.w = addr;
                addr = de.w;
                de.w = sde.w;
                sde.w = addr;
                addr = hl.w;
                hl.w = shl.w;
                shl.w = addr;
                cycles += 4;
                break;
            case 0xDA:          /*JP C */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & C_FLAG)
                    pc = addr;
                cycles += 3;
                break;
            case 0xDB:          /*IN A,(n) */
                cycles += 4;
                temp = z80_readmem(pc++);
                cycles += 3;
                af.b.h = z80in((af.b.h << 8) | temp);
                cycles += 4;
                break;
            case 0xDC:          /*CALL C,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & C_FLAG) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xDD:         /*More opcodes */
                ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                cycles += 4;
                opcode = z80_readmem(pc++);
                switch (opcode) {
                    case 0x09:          /*ADD IX,BC */
                        z80_setadd16(ix.w, bc.w);
                        ix.w += bc.w;
                        cycles += 11;
                        break;
                    case 0x19:          /*ADD IX,DE */
                        z80_setadd16(ix.w, de.w);
                        ix.w += de.w;
                        cycles += 11;
                        break;
                    case 0x21:          /*LD IX,nn */
                        cycles += 4;
                        ix.b.l = z80_readmem(pc++);
                        cycles += 3;
                        ix.b.h = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x22:          /*LD (nn),IX */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        z80_writemem(addr, ix.b.l);
                        cycles += 3;
                        z80_writemem(addr + 1, ix.b.h);
                        cycles += 3;
                        break;
                    case 0x23:          /*INC IX */
                        ix.w++;
                        cycles += 6;
                        break;
                    case 0x24:          /*INC IXh */
                        ix.b.h = setinc(ix.b.h);
                        cycles += 4;
                        break;
                    case 0x25:          /*DEC IXh */
                        ix.b.h = setdec(ix.b.h);
                        cycles += 4;
                        break;
                    case 0x26:          /*LD IXh,nn */
                        cycles += 4;
                        ix.b.h = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x29:          /*ADD IX,IX */
                        z80_setadd16(ix.w, ix.w);
                        ix.w += ix.w;
                        cycles += 11;
                        break;
                    case 0x2A:          /*LD IX,(nn) */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        ix.b.l = z80_readmem(addr);
                        cycles += 3;
                        ix.b.h = z80_readmem(addr + 1);
                        cycles += 3;
                        break;
                    case 0x2B:          /*DEC IX */
                        ix.w--;
                        cycles += 6;
                        break;
                    case 0x2C:          /*INC IXl */
                        ix.b.l = setinc(ix.b.l);
                        cycles += 4;
                        break;
                    case 0x2D:          /*DEC IXl */
                        ix.b.l = setdec(ix.b.l);
                        cycles += 4;
                        break;
                    case 0x2E:          /*LD IXl,nn */
                        cycles += 4;
                        ix.b.l = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x34:          /*INC (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        addr += ix.w;
                        cycles += 3;
                        temp = setinc(z80_readmem(addr));
                        cycles += 5;
                        z80_writemem(addr, temp);
                        cycles += 7;
                        break;
                    case 0x35:          /*DEC (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        addr += ix.w;
                        cycles += 3;
                        temp = setdec(z80_readmem(addr));
                        cycles += 5;
                        z80_writemem(addr, temp);
                        cycles += 7;
                        break;
                    case 0x36:          /*LD (IX+nn),nn */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(pc++);
                        cycles += 5;
                        z80_writemem(ix.w + addr, temp);
                        cycles += 3;
                        break;
                    case 0x39:          /*ADD IX,SP */
                        z80_setadd16(ix.w, sp);
                        ix.w += sp;
                        cycles += 11;
                        break;
                    case 0x44:          /*LD B,IXh */
                        bc.b.h = ix.b.h;
                        cycles += 3;
                        break;
                    case 0x45:          /*LD B,IXl */
                        bc.b.h = ix.b.l;
                        cycles += 3;
                        break;
                    case 0x46:          /*LD B,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        bc.b.h = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x4E:          /*LD C,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        bc.b.l = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x54:          /*LD D,IXh */
                        de.b.h = ix.b.h;
                        cycles += 3;
                        break;
                    case 0x55:          /*LD D,IXl */
                        de.b.h = ix.b.l;
                        cycles += 3;
                        break;
                    case 0x56:          /*LD D,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        de.b.h = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x5C:          /*LD E,IXh */
                        de.b.l = ix.b.h;
                        cycles += 3;
                        break;
                    case 0x5D:          /*LD E,IXl */
                        de.b.l = ix.b.l;
                        cycles += 3;
                        break;
                    case 0x5E:          /*LD E,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        de.b.l = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x60:          /*LD IXh,B */
                        ix.b.h = bc.b.h;
                        cycles += 3;
                        break;
                    case 0x61:          /*LD IXh,C */
                        ix.b.h = bc.b.l;
                        cycles += 3;
                        break;
                    case 0x62:          /*LD IXh,D */
                        ix.b.h = de.b.h;
                        cycles += 3;
                        break;
                    case 0x63:          /*LD IXh,E */
                        ix.b.h = de.b.l;
                        cycles += 3;
                        break;
                    case 0x64:          /*LD IXh,H */
                        ix.b.h = hl.b.h;
                        cycles += 3;
                        break;
                    case 0x65:          /*LD IXh,L */
                        ix.b.h = hl.b.l;
                        cycles += 3;
                        break;
                    case 0x66:          /*LD H,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        hl.b.h = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x67:          /*LD IXh,A */
                        ix.b.h = af.b.h;
                        cycles += 3;
                        break;
                    case 0x68:          /*LD IXl,B */
                        ix.b.l = bc.b.h;
                        cycles += 3;
                        break;
                    case 0x69:          /*LD IXl,C */
                        ix.b.l = bc.b.l;
                        cycles += 3;
                        break;
                    case 0x6A:          /*LD IXl,D */
                        ix.b.l = de.b.h;
                        cycles += 3;
                        break;
                    case 0x6B:          /*LD IXl,E */
                        ix.b.l = de.b.l;
                        cycles += 3;
                        break;
                    case 0x6C:          /*LD IXl,H */
                        ix.b.l = hl.b.h;
                        cycles += 3;
                        break;
                    case 0x6D:          /*LD IXl,L */
                        ix.b.l = hl.b.l;
                        cycles += 3;
                        break;
                    case 0x6E:          /*LD L,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (ix.w + addr) >> 8;
                        cycles += 7;
                        hl.b.l = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x6F:          /*LD IXl,A */
                        ix.b.l = af.b.h;
                        cycles += 3;
                        break;
                    case 0x70:          /*LD (IX+nn),B */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, bc.b.h);
                        cycles += 8;
                        break;
                    case 0x71:          /*LD (IX+nn),C */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, bc.b.l);
                        cycles += 8;
                        break;
                    case 0x72:          /*LD (IX+nn),D */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, de.b.h);
                        cycles += 8;
                        break;
                    case 0x73:          /*LD (IX+nn),E */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, de.b.l);
                        cycles += 8;
                        break;
                    case 0x74:          /*LD (IX+nn),H */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, hl.b.h);
                        cycles += 8;
                        break;
                    case 0x75:          /*LD (IX+nn),L */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, hl.b.l);
                        cycles += 8;
                        break;
                    case 0x77:          /*LD (IX+nn),A */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(ix.w + addr, af.b.h);
                        cycles += 8;
                        break;
                    case 0x7C:          /*LD A,IXh */
                        af.b.h = ix.b.h;
                        cycles += 3;
                        break;
                    case 0x7D:          /*LD A,IXl */
                        af.b.h = ix.b.l;
                        cycles += 3;
                        break;
                    case 0x7E:          /*LD A,(IX+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        af.b.h = z80_readmem(ix.w + addr);
                        cycles += 8;
                        break;
                    case 0x84:          /*ADD IXh */
                        z80_setadd(af.b.h, ix.b.h);
                        af.b.h += ix.b.h;
                        cycles += 3;
                        break;
                    case 0x85:          /*ADD IXl */
                        z80_setadd(af.b.h, ix.b.l);
                        af.b.h += ix.b.l;
                        cycles += 3;
                        break;
                    case 0x86:          /*ADD (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(ix.w + addr);
                        z80_setadd(af.b.h, temp);
                        af.b.h += temp;
                        cycles += 8;
                        break;
                    case 0x8C:          /*ADC IXh */
                        setadc(af.b.h, ix.b.h);
                        af.b.h += ix.b.h + tempc;
                        cycles += 3;
                        break;
                    case 0x8D:          /*ADC IXl */
                        setadc(af.b.h, ix.b.l);
                        af.b.h += ix.b.l + tempc;
                        cycles += 3;
                        break;
                    case 0x8E:          /*ADC (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(ix.w + addr);
                        setadc(af.b.h, temp);
                        af.b.h += (temp + tempc);
                        cycles += 8;
                        break;
                    case 0x94:          /*SUB IXh */
                        z80_setsub(af.b.h, ix.b.h);
                        af.b.h -= ix.b.h;
                        cycles += 3;
                        break;
                    case 0x95:          /*SUB IXl */
                        z80_setsub(af.b.h, ix.b.l);
                        af.b.h -= ix.b.l;
                        cycles += 3;
                        break;
                    case 0x96:          /*SUB (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(ix.w + addr);
                        z80_setsub(af.b.h, temp);
                        af.b.h -= temp;
                        cycles += 8;
                        break;
                    case 0x9C:          /*SBC IXh */
                        setsbc(af.b.h, ix.b.h);
                        af.b.h -= (ix.b.h + tempc);
                        cycles += 3;
                        break;
                    case 0x9D:          /*SBC IXl */
                        setsbc(af.b.h, ix.b.l);
                        af.b.h -= (ix.b.l + tempc);
                        cycles += 3;
                        break;
                    case 0x9E:          /*SBC (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(ix.w + addr);
                        setsbc(af.b.h, temp);
                        af.b.h -= (temp + tempc);
                        cycles += 8;
                        break;
                    case 0xA4:          /*AND IXh */
                        setand(af.b.h & ix.b.h);
                        af.b.h &= ix.b.h;
                        cycles += 3;
                        break;
                    case 0xA5:          /*AND IXl */
                        setand(af.b.h & ix.b.l);
                        af.b.h &= ix.b.l;
                        cycles += 3;
                        break;
                    case 0xA6:          /*AND (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h &= z80_readmem(ix.w + addr);
                        setand(af.b.h);
                        cycles += 8;
                        break;
                    case 0xAC:          /*XOR IXh */
                        setzn(af.b.h ^ ix.b.h);
                        af.b.h ^= ix.b.h;
                        cycles += 3;
                        break;
                    case 0xAD:          /*XOR IXl */
                        setzn(af.b.h ^ ix.b.l);
                        af.b.h ^= ix.b.l;
                        cycles += 3;
                        break;
                    case 0xAE:          /*XOR (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h ^= z80_readmem(ix.w + addr);
                        setzn(af.b.h);
                        cycles += 8;
                        break;
                    case 0xB4:          /*OR  IXh */
                        setzn(af.b.h | ix.b.h);
                        af.b.h |= ix.b.h;
                        cycles += 3;
                        break;
                    case 0xB5:          /*OR  IXl */
                        setzn(af.b.h | ix.b.l);
                        af.b.h |= ix.b.l;
                        cycles += 3;
                        break;
                    case 0xB6:          /*OR (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h |= z80_readmem(ix.w + addr);
                        setzn(af.b.h);
                        cycles += 8;
                        break;
                    case 0xBC:          /*CP  IXh */
                        setcp(af.b.h, ix.b.h);
                        cycles += 3;
                        break;
                    case 0xBD:          /*CP  IXl */
                        setcp(af.b.h, ix.b.l);
                        cycles += 3;
                        break;
                    case 0xBE:          /*CP (IX+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(ix.w + addr);
                        setcp(af.b.h, temp);
                        cycles += 8;
                        break;
                    case 0xCB:          /*More opcodes */
                        ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        opcode = z80_readmem(pc++);
                        switch (opcode) {
                            case 0x06:          /*RLC (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                if (tempc)
                                    temp |= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x0E:          /*RRC (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                if (tempc)
                                    temp |= 0x80;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x16:          /*RL (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                temp2 = temp & 0x80;
                                temp <<= 1;
                                if (tempc)
                                    temp |= 1;
                                setzn(temp);
                                if (temp2)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x1E:          /*RR (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                temp2 = temp & 1;
                                temp >>= 1;
                                if (tempc)
                                    temp |= 0x80;
                                setzn(temp);
                                if (temp2)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x26:          /*SLA (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x2E:          /*SRA (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                if (temp & 0x40)
                                    temp |= 0x80;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x36:          /*SLL (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                temp |= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x3E:          /*SRL (IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + ix.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + ix.w, temp);
                                cycles += 3;
                                break;
                            case 0x46:          /*BIT 0,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 1, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x4E:          /*BIT 1,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 2, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x56:          /*BIT 2,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 4, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x5E:          /*BIT 3,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 8, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x66:          /*BIT 4,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 0x10, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x6E:          /*BIT 5,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 0x20, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x76:          /*BIT 6,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 0x40, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x7E:          /*BIT 7,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr);
                                setbit2(temp & 0x80, ix.w + addr);
                                cycles += 4;
                                break;
                            case 0x86:          /*RES 0,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~1;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x8E:          /*RES 1,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~2;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x96:          /*RES 2,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~4;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x9E:          /*RES 3,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~8;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xA6:          /*RES 4,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~0x10;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xAE:          /*RES 5,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~0x20;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xB6:          /*RES 6,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~0x40;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xBE:          /*RES 7,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) & ~0x80;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xC6:          /*SET 0,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 1;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xCE:          /*SET 1,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 2;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xD6:          /*SET 2,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 4;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xDE:          /*SET 3,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 8;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xE6:          /*SET 4,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 0x10;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xEE:          /*SET 5,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 0x20;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xF6:          /*SET 6,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 0x40;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xFE:          /*SET 7,(IX+nn) */
                                cycles += 5;
                                temp = z80_readmem(ix.w + addr) | 0x80;
                                cycles += 4;
                                z80_writemem(ix.w + addr, temp);
                                cycles += 3;
                                break;
                            default:
                                log_debug("z80: invalid DD,CB opcode %02X", opcode);
                                break;
                        }
                        break;
                    case 0xCD:
                        pc--;
                        break;
                    case 0xE1:          /*POP IX */
                        cycles += 4;
                        ix.b.l = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        ix.b.h = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        break;
                    case 0xE3:          /*EX (SP),IX */
                        cycles += 4;
                        addr = z80_readmem(sp);
                        cycles += 3;
                        addr |= (z80_readmem(sp + 1) << 8);
                        cycles += 4;
                        z80_writemem(sp, ix.b.l);
                        cycles += 3;
                        z80_writemem(sp + 1, ix.b.h);
                        ix.w = addr;
                        cycles += 5;
                        break;
                    case 0xE5:          /*PUSH IX */
                        cycles += 5;
                        sp--;
                        z80_writemem(sp, ix.b.h);
                        cycles += 3;
                        sp--;
                        z80_writemem(sp, ix.b.l);
                        cycles += 3;
                        break;
                    case 0xE9:          /*JP (IX) */
                        pc = ix.w;
                        cycles += 4;
                        break;
                    case 0xF9:          /*LD SP,IX */
                        sp = ix.w;
                        cycles += 6;
                        break;
                    default:
                        log_debug("z80: spurious DD prefix on opcode %02X", opcode);
                        goto noprefix;
                }
                break;
            case 0xDE:          /*SBC A,nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                setsbc(af.b.h, temp);
                af.b.h -= (temp + tempc);
                cycles += 3;
                break;
            case 0xDF:          /*RST 18 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x18;
                cycles += 3;
                break;
            case 0xE0:          /*RET PO */
                cycles += 5;
                if (!(af.b.l & V_FLAG)) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xE1:          /*POP HL */
                cycles += 4;
                hl.b.l = z80_readmem(sp);
                sp++;
                cycles += 3;
                hl.b.h = z80_readmem(sp);
                sp++;
                cycles += 3;
                break;
            case 0xE2:          /*JP PO */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & V_FLAG))
                    pc = addr;
                cycles += 3;
                break;
            case 0xE3:          /*EX (SP),HL */
                cycles += 4;
                addr = z80_readmem(sp);
                cycles += 3;
                addr |= (z80_readmem(sp + 1) << 8);
                cycles += 4;
                z80_writemem(sp, hl.b.l);
                cycles += 3;
                z80_writemem(sp + 1, hl.b.h);
                hl.w = addr;
                cycles += 5;
                break;
            case 0xE4:          /*CALL PO,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & V_FLAG)) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xE5:          /*PUSH HL */
                cycles += 5;
                sp--;
                z80_writemem(sp, hl.b.h);
                cycles += 3;
                sp--;
                z80_writemem(sp, hl.b.l);
                cycles += 3;
                break;
            case 0xE6:          /*AND nn */
                cycles += 4;
                af.b.h &= z80_readmem(pc++);
                setand(af.b.h);
                cycles += 3;
                break;
            case 0xE7:          /*RST 20 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x20;
                cycles += 3;
                break;
            case 0xE8:          /*RET PE */
                cycles += 5;
                if (af.b.l & V_FLAG) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xE9:          /*JP (HL) */
                pc = hl.w;
                cycles += 4;
                break;
            case 0xEA:          /*JP PE */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & V_FLAG)
                    pc = addr;
                cycles += 3;
                break;
            case 0xEB:          /*EX DE,HL */
                addr = de.w;
                de.w = hl.w;
                hl.w = addr;
                cycles += 4;
                break;
            case 0xEC:          /*CALL PE,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & V_FLAG) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xED:          /*More opcodes */
                ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                cycles += 4;
                opcode = z80_readmem(pc++);
                switch (opcode) {
                    case 0x40:          /*IN B,(C) */
                        cycles += 4;
                        bc.b.h = z80in(bc.w);
                        setzn(bc.b.h);
                        cycles += 4;
                        break;
                    case 0x42:          /*SBC HL,BC */
                        setsbc16(hl.w, bc.w);
                        hl.w -= (bc.w + tempc);
                        cycles += 11;
                        break;
                    case 0x43:          /*LD (nn),BC */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        z80_writemem(addr, bc.b.l);
                        cycles += 3;
                        z80_writemem(addr + 1, bc.b.h);
                        cycles += 3;
                        break;
                    case 0x44:          /*NEG*/
                        z80_setsub(0, af.b.h);
                        af.b.h = 0 - af.b.h;
                        cycles += 4;
                        break;
                    case 0x45:          /*RETN*/
                        output = 0;
                        iff1 = iff2;
                        cycles += 4;
                        pc = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        pc |= (z80_readmem(sp) << 8);
                        sp++;
                        cycles += 3;
                        break;
                    case 0x47:          /*LD I,A */
                        ir.b.h = af.b.h;
                        log_debug("I now %02X %04X\n", ir.b.h, ir.w);
                        cycles += 5;
                        break;
                    case 0x4A:          /*ADC HL,BC */
                        setadc16(hl.w, bc.w);
                        hl.w += (bc.w + tempc);
                        cycles += 11;
                        break;
                    case 0x4B:          /*LD BC,(nn) */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        bc.b.l = z80_readmem(addr);
                        cycles += 3;
                        bc.b.h = z80_readmem(addr + 1);
                        cycles += 3;
                        break;
                    case 0x4D:          /*RETI*/
                        cycles += 4;
                        pc = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        pc |= (z80_readmem(sp) << 8);
                        sp++;
                        cycles += 3;
                        break;
                    case 0x4F:          /*LD R,A */
                        ir.b.l = af.b.h;
                        cycles += 5;
                        break;
                    case 0x50:          /*IN D,(C) */
                        cycles += 4;
                        de.b.h = z80in(bc.w);
                        setzn(de.b.h);
                        cycles += 4;
                        break;
                    case 0x51:          /*OUT (C),D */
                        cycles += 4;
                        z80out(bc.w, de.b.h);
                        cycles += 4;
                        break;
                    case 0x52:          /*SBC HL,DE */
                        setsbc16(hl.w, de.w);
                        hl.w -= (de.w + tempc);
                        cycles += 11;
                        break;
                    case 0x53:          /*LD (nn),DE */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        z80_writemem(addr, de.b.l);
                        cycles += 3;
                        z80_writemem(addr + 1, de.b.h);
                        cycles += 3;
                        break;
                    case 0x56:          /*IM 1 */
                        im = 1;
                        cycles += 4;
                        break;
                    case 0x57:          /*LD A,I */
                        af.b.h = ir.b.h;
                        cycles += 5;
                        break;
                    case 0x58:          /*IN E,(C) */
                        cycles += 4;
                        de.b.l = z80in(bc.w);
                        setzn(de.b.l);
                        cycles += 4;
                        break;
                    case 0x59:          /*OUT (C),E */
                        cycles += 4;
                        z80out(bc.w, de.b.l);
                        cycles += 4;
                        break;
                    case 0x5A:          /*ADC HL,DE */
                        setadc16(hl.w, de.w);
                        hl.w += (de.w + tempc);
                        cycles += 11;
                        break;
                    case 0x5B:          /*LD DE,(nn) */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        de.b.l = z80_readmem(addr);
                        cycles += 3;
                        de.b.h = z80_readmem(addr + 1);
                        cycles += 3;
                        break;
                    case 0x5E:          /*IM 2 */
                        im = 2;
                        cycles += 4;
                        break;
                    case 0x5F:          /*LD A,R */
                        af.b.h = ir.b.l;
                        af.b.l &= C_FLAG;
                        af.b.l |= (af.b.h & 0xA8);
                        if (!af.b.h)
                            af.b.l |= Z_FLAG;
                        if (iff2 && !enterint)
                            af.b.l |= V_FLAG;
                        cycles += 5;
                        break;
                    case 0x60:          /*IN H,(C) */
                        cycles += 4;
                        hl.b.h = z80in(bc.w);
                        setzn(hl.b.h);
                        cycles += 4;
                        break;
                    case 0x61:          /*OUT (C),H */
                        cycles += 4;
                        z80out(bc.w, hl.b.h);
                        cycles += 4;
                        break;
                    case 0x62:          /*SBC HL,HL */
                        setsbc16(hl.w, hl.w);
                        hl.w -= (hl.w + tempc);
                        cycles += 11;
                        break;
                    case 0x67:          /*RRD*/
                        cycles += 4;
                        af.b.l &= ~(H_FLAG | N_FLAG);
                        addr = z80_readmem(hl.w) | ((af.b.h & 0xF) << 8);
                        addr = (addr >> 4) | ((addr << 8) & 0xF00);
                        cycles += 3;
                        z80_writemem(hl.w, addr & 0xFF);
                        af.b.h = (af.b.h & 0xF0) | (addr >> 8);
                        setznc(af.b.h);
                        cycles += 7;
                        break;
                    case 0x69:          /*OUT (C),L */
                        cycles += 4;
                        z80out(bc.w, hl.b.l);
                        cycles += 4;
                        break;
                    case 0x6A:          /*ADC HL,HL */
                        setadc16(hl.w, hl.w);
                        hl.w += (hl.w + tempc);
                        cycles += 11;
                        break;
                    case 0x6F:          /*RLD*/
                        cycles += 4;
                        af.b.l &= ~(H_FLAG | N_FLAG);
                        addr = z80_readmem(hl.w) | ((af.b.h & 0xF) << 8);
                        addr = ((addr << 4) & 0xFF0) | (addr >> 8);
                        cycles += 3;
                        z80_writemem(hl.w, addr & 0xFF);
                        af.b.h = (af.b.h & 0xF0) | (addr >> 8);
                        setznc(af.b.h);
                        cycles += 7;
                        break;
                    case 0x72:          /*SBC HL,SP */
                        setsbc16(hl.w, sp);
                        hl.w -= (sp + tempc);
                        cycles += 11;
                        break;
                    case 0x73:          /*LD (nn),SP */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        z80_writemem(addr, sp);
                        cycles += 3;
                        z80_writemem(addr + 1, sp >> 8);
                        cycles += 3;
                        break;
                    case 0x78:          /*IN A,(C) */
                        cycles += 4;
                        af.b.h = z80in(bc.w);
                        setzn(af.b.h);
                        cycles += 4;
                        break;
                    case 0x79:          /*OUT (C),A */
                        cycles += 4;
                        z80out(bc.w, af.b.h);
                        cycles += 4;
                        break;
                    case 0x7A:          /*ADC HL,SP */
                        setadc16(hl.w, sp);
                        hl.w += (sp + tempc);
                        cycles += 11;
                        break;
                    case 0x7B:          /*LD SP,(nn) */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        sp = z80_readmem(addr);
                        cycles += 3;
                        sp |= (z80_readmem(addr + 1) << 8);
                        cycles += 3;
                        break;
                    case 0xA0:          /*LDI*/
                        cycles += 4;
                        temp = z80_readmem(hl.w++);
                        cycles += 3;
                        z80_writemem(de.w, temp);
                        de.w++;
                        bc.w--;
                        af.b.l &= ~(H_FLAG | N_FLAG | V_FLAG);
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        cycles += 5;
                        break;
                    case 0xA1:          /*CPI*/
                        cycles += 4;
                        temp = z80_readmem(hl.w++);
                        setcpED(af.b.h, temp);
                        bc.w--;
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        else
                            af.b.l &= ~V_FLAG;
                        cycles += 8;
                        break;
                    case 0xA2:          /*INI*/
                        cycles += 5;
                        temp = z80in(bc.w);
                        cycles += 3;
                        z80_writemem(hl.w++, temp);
                        af.b.l |= S_FLAG;
                        bc.b.h--;
                        if (!bc.b.h)
                            af.b.l |= Z_FLAG;
                        else
                            af.b.l &= ~Z_FLAG;
                        af.b.l |= N_FLAG;
                        cycles += 4;
                        break;
                    case 0xA3:          /*OUTI*/
                        cycles += 5;
                        temp = z80_readmem(hl.w++);
                        cycles += 3;
                        z80out(bc.w, temp);
                        bc.b.h--;
                        if (!bc.b.h)
                            af.b.l |= Z_FLAG;
                        else
                            af.b.l &= ~Z_FLAG;
                        af.b.l |= N_FLAG;
                        cycles += 4;
                        break;
                    case 0xA8:          /*LDD*/
                        cycles += 4;
                        temp = z80_readmem(hl.w--);
                        cycles += 3;
                        z80_writemem(de.w, temp);
                        de.w--;
                        bc.w--;
                        af.b.l &= ~(H_FLAG | N_FLAG | V_FLAG);
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        cycles += 5;
                        break;
                    case 0xA9:          /*CPD*/
                        cycles += 4;
                        temp = z80_readmem(hl.w--);
                        bc.w--;
                        setcpED(af.b.h, temp);
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        else
                            af.b.l &= ~V_FLAG;
                        cycles += 8;
                        break;
                    case 0xAB:          /*OUTD*/
                        cycles += 5;
                        temp = z80_readmem(hl.w--);
                        cycles += 3;
                        z80out(bc.w, temp);
                        bc.b.h--;
                        if (!bc.b.h)
                            af.b.l |= Z_FLAG;
                        else
                            af.b.l &= ~Z_FLAG;
                        af.b.l |= N_FLAG;
                        cycles += 4;
                        break;
                    case 0xB0:          /*LDIR*/
                        cycles += 4;
                        temp = z80_readmem(hl.w++);
                        cycles += 3;
                        z80_writemem(de.w, temp);
                        de.w++;
                        bc.w--;
                        if (bc.w) {
                            pc -= 2;
                            cycles += 5;
                        }
                        af.b.l &= ~(H_FLAG | N_FLAG | V_FLAG);
                        cycles += 5;
                        break;
                    case 0xB1:          /*CPIR*/
                        cycles += 4;
                        temp = z80_readmem(hl.w++);
                        bc.w--;
                        setcpED(af.b.h, temp);
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        else
                            af.b.l &= ~V_FLAG;
                        if (bc.w && (af.b.h != temp)) {
                            pc -= 2;
                            cycles += 13;
                        } else {
                            cycles += 8;
                        }
                        break;
                    case 0xB8:          /*LDDR*/
                        cycles += 4;
                        temp = z80_readmem(hl.w--);
                        cycles += 3;
                        z80_writemem(de.w, temp);
                        de.w--;
                        bc.w--;
                        if (bc.w) {
                            pc -= 2;
                            cycles += 5;
                        }
                        af.b.l &= ~(H_FLAG | N_FLAG | V_FLAG);
                        cycles += 5;
                        break;
                    case 0xB9:          /*CPDR*/
                        cycles += 4;
                        temp = z80_readmem(hl.w--);
                        bc.w--;
                        setcpED(af.b.h, temp);
                        if (bc.w)
                            af.b.l |= V_FLAG;
                        else
                            af.b.l &= ~V_FLAG;
                        if (bc.w && (af.b.h != temp)) {
                            pc -= 2;
                            cycles += 13;
                        } else {
                            cycles += 8;
                        }
                        break;
                    default:
                        log_debug("z80: invalid ED-prefix opcode %02X", opcode);
                        break;
                }
                break;
            case 0xEE:          /*XOR nn */
                cycles += 4;
                af.b.h ^= z80_readmem(pc++);
                af.b.l &= ~3;
                setzn(af.b.h);
                cycles += 3;
                break;
            case 0xEF:          /*RST 28 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x28;
                cycles += 3;
                break;
            case 0xF0:          /*RET P */
                cycles += 5;
                if (!(af.b.l & S_FLAG)) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xF1:          /*POP AF */
                cycles += 4;
                af.b.l = z80_readmem(sp);
                sp++;
                cycles += 3;
                af.b.h = z80_readmem(sp);
                sp++;
                cycles += 3;
                break;
            case 0xF2:          /*JP P */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & S_FLAG))
                    pc = addr;
                cycles += 3;
                break;
            case 0xF3:          /*DI*/
                iff1 = iff2 = 0;
                cycles += 4;
                break;
            case 0xF4:          /*CALL P,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (!(af.b.l & S_FLAG)) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xF5:          /*PUSH AF */
                cycles += 5;
                sp--;
                z80_writemem(sp, af.b.h);
                cycles += 3;
                sp--;
                z80_writemem(sp, af.b.l);
                cycles += 3;
                break;
            case 0xF6:          /*OR nn */
                cycles += 4;
                af.b.h |= z80_readmem(pc++);
                af.b.l &= ~3;
                setzn(af.b.h);
                cycles += 3;
                break;
            case 0xF7:          /*RST 30 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x30;
                cycles += 3;
                break;
            case 0xF8:          /*RET M */
                cycles += 5;
                if (af.b.l & S_FLAG) {
                    pc = z80_readmem(sp);
                    sp++;
                    cycles += 3;
                    pc |= (z80_readmem(sp) << 8);
                    sp++;
                    cycles += 3;
                }
                break;
            case 0xF9:          /*LD SP,HL */
                sp = hl.w;
                cycles += 6;
                break;
            case 0xFA:          /*JP M */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & S_FLAG)
                    pc = addr;
                cycles += 3;
                break;
            case 0xFB:          /*EI*/
                iff1 = iff2 = 1;
                cycles += 4;
                break;
            case 0xFC:          /*CALL M,xxxx */
                cycles += 4;
                addr = z80_readmem(pc);
                cycles += 3;
                addr |= (z80_readmem(pc + 1) << 8);
                pc += 2;
                if (af.b.l & S_FLAG) {
                    cycles += 4;
                    sp--;
                    z80_writemem(sp, pc >> 8);
                    cycles += 3;
                    sp--;
                    z80_writemem(sp, pc & 0xFF);
                    pc = addr;
                    cycles += 3;
                }
                else
                    cycles += 3;
                break;
            case 0xFD:          /*More opcodes */
                ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                cycles += 4;
                opcode = z80_readmem(pc++);
                switch (opcode) {
                    case 0x09:          /*ADD IY,BC */
                        z80_setadd16(iy.w, bc.w);
                        iy.w += bc.w;
                        cycles += 11;
                        break;
                    case 0x19:          /*ADD IY,DE */
                        z80_setadd16(iy.w, de.w);
                        iy.w += de.w;
                        cycles += 11;
                        break;
                    case 0x21:          /*LD IY,nn */
                        cycles += 4;
                        iy.b.l = z80_readmem(pc++);
                        cycles += 3;
                        iy.b.h = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x22:          /*LD (nn),IY */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        z80_writemem(addr, iy.b.l);
                        cycles += 3;
                        z80_writemem(addr + 1, iy.b.h);
                        cycles += 3;
                        break;
                    case 0x23:          /*INC IY */
                        iy.w++;
                        cycles += 6;
                        break;
                    case 0x24:          /*INC IYh */
                        iy.b.h = setinc(iy.b.h);
                        cycles += 4;
                        break;
                    case 0x25:          /*DEC IYh */
                        iy.b.h = setdec(iy.b.h);
                        cycles += 4;
                        break;
                    case 0x26:          /*LD IYh,nn */
                        cycles += 4;
                        iy.b.h = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x29:          /*ADD IY,IY */
                        z80_setadd16(iy.w, iy.w);
                        iy.w += iy.w;
                        cycles += 11;
                        break;
                    case 0x2A:          /*LD IY,(nn) */
                        cycles += 4;
                        addr = z80_readmem(pc);
                        cycles += 3;
                        addr |= (z80_readmem(pc + 1) << 8);
                        pc += 2;
                        cycles += 3;
                        iy.b.l = z80_readmem(addr);
                        cycles += 3;
                        iy.b.h = z80_readmem(addr + 1);
                        cycles += 3;
                        break;
                    case 0x2B:          /*DEC IY */
                        iy.w--;
                        cycles += 6;
                        break;
                    case 0x2C:          /*INC IYl */
                        iy.b.l = setinc(iy.b.l);
                        cycles += 4;
                        break;
                    case 0x2D:          /*DEC IYl */
                        iy.b.l = setdec(iy.b.l);
                        cycles += 4;
                        break;
                    case 0x2E:          /*LD IYl,nn */
                        cycles += 4;
                        iy.b.l = z80_readmem(pc++);
                        cycles += 3;
                        break;
                    case 0x34:          /*INC (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        addr += iy.w;
                        cycles += 3;
                        temp = setinc(z80_readmem(addr));
                        cycles += 5;
                        z80_writemem(addr, temp);
                        cycles += 7;
                        break;
                    case 0x35:          /*DEC (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        addr += iy.w;
                        cycles += 3;
                        temp = setdec(z80_readmem(addr));
                        cycles += 5;
                        z80_writemem(addr, temp);
                        cycles += 7;
                        break;
                    case 0x36:          /*LD (IY+nn),nn */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(pc++);
                        cycles += 5;
                        z80_writemem(iy.w + addr, temp);
                        cycles += 3;
                        break;
                    case 0x39:          /*ADD IY,SP */
                        z80_setadd16(iy.w, sp);
                        iy.w += sp;
                        cycles += 11;
                        break;
                    case 0x3A:
                        pc--;
                        break;
                    case 0x44:          /*LD B,IYh */
                        bc.b.h = iy.b.h;
                        cycles += 3;
                        break;
                    case 0x45:          /*LD B,IYl */
                        bc.b.h = iy.b.l;
                        cycles += 3;
                        break;
                    case 0x46:          /*LD B,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        bc.b.h = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x4C:          /*LD C,IYh */
                        bc.b.l = iy.b.h;
                        cycles += 3;
                        break;
                    case 0x4D:          /*LD C,IYl */
                        bc.b.l = iy.b.l;
                        cycles += 3;
                        break;
                    case 0x4E:          /*LD C,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        bc.b.l = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x54:          /*LD D,IYh */
                        de.b.h = iy.b.h;
                        cycles += 3;
                        break;
                    case 0x55:          /*LD D,IYl */
                        de.b.h = iy.b.l;
                        cycles += 3;
                        break;
                    case 0x56:          /*LD D,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        de.b.h = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x5C:          /*LD E,IYh */
                        de.b.l = iy.b.h;
                        cycles += 3;
                        break;
                    case 0x5D:          /*LD E,IYl */
                        de.b.l = iy.b.l;
                        cycles += 3;
                        break;
                    case 0x5E:          /*LD E,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        de.b.l = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x60:          /*LD IYh,B */
                        iy.b.h = bc.b.h;
                        cycles += 3;
                        break;
                    case 0x61:          /*LD IYh,C */
                        iy.b.h = bc.b.l;
                        cycles += 3;
                        break;
                    case 0x62:          /*LD IYh,D */
                        iy.b.h = de.b.h;
                        cycles += 3;
                        break;
                    case 0x63:          /*LD IYh,E */
                        iy.b.h = de.b.l;
                        cycles += 3;
                        break;
                    case 0x64:          /*LD IYh,H */
                        iy.b.h = hl.b.h;
                        cycles += 3;
                        break;
                    case 0x65:          /*LD IYh,L */
                        iy.b.h = hl.b.l;
                        cycles += 3;
                        break;
                    case 0x66:          /*LD H,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        hl.b.h = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x67:          /*LD IYh,A */
                        iy.b.h = af.b.h;
                        cycles += 3;
                        break;
                    case 0x68:          /*LD IYl,B */
                        iy.b.l = bc.b.h;
                        cycles += 3;
                        break;
                    case 0x69:          /*LD IYl,C */
                        iy.b.l = bc.b.l;
                        cycles += 3;
                        break;
                    case 0x6A:          /*LD IYl,D */
                        iy.b.l = de.b.h;
                        cycles += 3;
                        break;
                    case 0x6B:          /*LD IYl,E */
                        iy.b.l = de.b.l;
                        cycles += 3;
                        break;
                    case 0x6C:          /*LD IYl,H */
                        iy.b.l = hl.b.h;
                        cycles += 3;
                        break;
                    case 0x6D:          /*LD IYl,L */
                        iy.b.l = hl.b.l;
                        cycles += 3;
                        break;
                    case 0x6E:          /*LD L,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        intreg = (iy.w + addr) >> 8;
                        cycles += 7;
                        hl.b.l = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x6F:          /*LD IYl,A */
                        iy.b.l = af.b.h;
                        cycles += 3;
                        break;
                    case 0x70:          /*LD (IY+nn),B */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, bc.b.h);
                        cycles += 8;
                        break;
                    case 0x71:          /*LD (IY+nn),C */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, bc.b.l);
                        cycles += 8;
                        break;
                    case 0x72:          /*LD (IY+nn),D */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, de.b.h);
                        cycles += 8;
                        break;
                    case 0x73:          /*LD (IY+nn),E */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, de.b.l);
                        cycles += 8;
                        break;
                    case 0x74:          /*LD (IY+nn),H */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, hl.b.h);
                        cycles += 8;
                        break;
                    case 0x75:          /*LD (IY+nn),L */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, hl.b.l);
                        cycles += 8;
                        break;
                    case 0x77:          /*LD (IY+nn),A */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        z80_writemem(iy.w + addr, af.b.h);
                        cycles += 8;
                        break;
                    case 0x7C:          /*LD A,IYh */
                        af.b.h = iy.b.h;
                        cycles += 3;
                        break;
                    case 0x7D:          /*LD A,IYl */
                        af.b.h = iy.b.l;
                        cycles += 3;
                        break;
                    case 0x7E:          /*LD A,(IY+nn) */
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 7;
                        af.b.h = z80_readmem(iy.w + addr);
                        cycles += 8;
                        break;
                    case 0x84:          /*ADD IYh */
                        z80_setadd(af.b.h, iy.b.h);
                        af.b.h += iy.b.h;
                        cycles += 3;
                        break;
                    case 0x85:          /*ADD IYl */
                        z80_setadd(af.b.h, iy.b.l);
                        af.b.h += iy.b.l;
                        cycles += 3;
                        break;
                    case 0x86:          /*ADD (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(iy.w + addr);
                        z80_setadd(af.b.h, temp);
                        af.b.h += temp;
                        cycles += 8;
                        break;
                    case 0x8C:          /*ADC IYh */
                        setadc(af.b.h, iy.b.h);
                        af.b.h += iy.b.h + tempc;
                        cycles += 3;
                        break;
                    case 0x8D:          /*ADC IYl */
                        setadc(af.b.h, iy.b.l);
                        af.b.h += iy.b.l + tempc;
                        cycles += 3;
                        break;
                    case 0x8E:          /*ADC (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(iy.w + addr);
                        setadc(af.b.h, temp);
                        af.b.h += (temp + tempc);
                        cycles += 8;
                        break;
                    case 0x94:          /*SUB IYh */
                        z80_setsub(af.b.h, iy.b.h);
                        af.b.h -= iy.b.h;
                        cycles += 3;
                        break;
                    case 0x95:          /*SUB IYl */
                        z80_setsub(af.b.h, iy.b.l);
                        af.b.h -= iy.b.l;
                        cycles += 3;
                        break;
                    case 0x96:          /*SUB (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(iy.w + addr);
                        z80_setsub(af.b.h, temp);
                        af.b.h -= temp;
                        cycles += 8;
                        break;
                    case 0x9C:          /*SBC IYh */
                        setsbc(af.b.h, iy.b.h);
                        af.b.h -= (iy.b.h + tempc);
                        cycles += 3;
                        break;
                    case 0x9D:          /*SBC IYl */
                        setsbc(af.b.h, iy.b.l);
                        af.b.h -= (iy.b.l + tempc);
                        cycles += 3;
                        break;
                    case 0x9E:          /*SBC (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(iy.w + addr);
                        setsbc(af.b.h, temp);
                        af.b.h -= (temp + tempc);
                        cycles += 8;
                        break;
                    case 0xA4:          /*AND IYh */
                        setand(af.b.h & iy.b.h);
                        af.b.h &= iy.b.h;
                        cycles += 3;
                        break;
                    case 0xA5:          /*AND IYl */
                        setand(af.b.h & iy.b.l);
                        af.b.h &= iy.b.l;
                        cycles += 3;
                        break;
                    case 0xA6:          /*AND (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h &= z80_readmem(iy.w + addr);
                        setand(af.b.h);
                        cycles += 8;
                        break;
                    case 0xAC:          /*XOR IYh */
                        setzn(af.b.h ^ iy.b.h);
                        af.b.h ^= iy.b.h;
                        cycles += 3;
                        break;
                    case 0xAD:          /*XOR IYl */
                        setzn(af.b.h ^ iy.b.l);
                        af.b.h ^= iy.b.l;
                        cycles += 3;
                        break;
                    case 0xAE:          /*XOR (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h ^= z80_readmem(iy.w + addr);
                        setzn(af.b.h);
                        cycles += 8;
                        break;
                    case 0xB4:          /*OR  IYh */
                        setzn(af.b.h | iy.b.h);
                        af.b.h |= iy.b.h;
                        cycles += 3;
                        break;
                    case 0xB5:          /*OR  IYl */
                        setzn(af.b.h | iy.b.l);
                        af.b.h |= iy.b.l;
                        cycles += 3;
                        break;
                    case 0xB6:          /*OR (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        af.b.h |= z80_readmem(iy.w + addr);
                        setzn(af.b.h);
                        cycles += 8;
                        break;
                    case 0xBC:          /*CP  IYh */
                        setcp(af.b.h, iy.b.h);
                        cycles += 3;
                        break;
                    case 0xBD:          /*CP  IYl */
                        setcp(af.b.h, iy.b.l);
                        cycles += 3;
                        break;
                    case 0xBE:          /*CP (IY+nn) */
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        temp = z80_readmem(iy.w + addr);
                        setcp(af.b.h, temp);
                        cycles += 8;
                        break;
                    case 0xCB: /*More opcodes */
                        ir.b.l = ((ir.b.l + 1) & 0x7F) | (ir.b.l & 0x80);
                        cycles += 4;
                        addr = z80_readmem(pc++);
                        if (addr & 0x80)
                            addr |= 0xFF00;
                        cycles += 3;
                        opcode = z80_readmem(pc++);
                        switch (opcode) {
                            case 0x06:          /*RLC (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                if (tempc)
                                    temp |= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x0E:          /*RRC (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                if (tempc)
                                    temp |= 0x80;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x16:          /*RL (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                temp2 = temp & 0x80;
                                temp <<= 1;
                                if (tempc)
                                    temp |= 1;
                                setzn(temp);
                                if (temp2)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x1E:          /*RR (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                temp2 = temp & 1;
                                temp >>= 1;
                                if (tempc)
                                    temp |= 0x80;
                                setzn(temp);
                                if (temp2)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x26:          /*SLA (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x2E:          /*SRA (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                if (temp & 0x40)
                                    temp |= 0x80;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x36:          /*SLL (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 0x80;
                                temp <<= 1;
                                temp |= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x3E:          /*SRL (IY+nn) */
                                cycles += 5;
                                temp = z80_readmem(addr + iy.w);
                                tempc = temp & 1;
                                temp >>= 1;
                                setzn(temp);
                                if (tempc)
                                    af.b.l |= C_FLAG;
                                cycles += 4;
                                z80_writemem(addr + iy.w, temp);
                                cycles += 3;
                                break;
                            case 0x46:          /*BIT 0,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 1, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x4E:          /*BIT 1,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 2, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x56:          /*BIT 2,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 4, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x5E:          /*BIT 3,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 8, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x66:          /*BIT 4,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 0x10, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x6E:          /*BIT 5,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 0x20, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x76:          /*BIT 6,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 0x40, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x7E:          /*BIT 7,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr);
                                setbit2(temp & 0x80, iy.w + addr);
                                cycles += 4;
                                break;
                            case 0x86:          /*RES 0,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~1;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x8E:          /*RES 1,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~2;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x96:          /*RES 2,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~4;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0x9E:          /*RES 3,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~8;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xA6:          /*RES 4,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~0x10;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xAE:          /*RES 5,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~0x20;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xB6:          /*RES 6,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~0x40;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xBE:          /*RES 7,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) & ~0x80;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xC6:          /*SET 0,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 1;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xCE:          /*SET 1,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 2;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xD6:          /*SET 2,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 4;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xDE:          /*SET 3,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 8;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xE6:          /*SET 4,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 0x10;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xEE:          /*SET 5,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 0x20;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xF6:          /*SET 6,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 0x40;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            case 0xFE:          /*SET 7,(iy+nn) */
                                cycles += 5;
                                temp = z80_readmem(iy.w + addr) | 0x80;
                                cycles += 4;
                                z80_writemem(iy.w + addr, temp);
                                cycles += 3;
                                break;
                            default:
                                log_debug("z80: invalid FD,CB-prefix opcode %02X", opcode);
                                break;
                        }
                        break;
                    case 0xE1:          /*POP IY */
                        cycles += 4;
                        iy.b.l = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        iy.b.h = z80_readmem(sp);
                        sp++;
                        cycles += 3;
                        break;
                    case 0xE3:          /*EX (SP),IY */
                        cycles += 4;
                        addr = z80_readmem(sp);
                        cycles += 3;
                        addr |= (z80_readmem(sp + 1) << 8);
                        cycles += 4;
                        z80_writemem(sp, iy.b.l);
                        cycles += 3;
                        z80_writemem(sp + 1, iy.b.h);
                        iy.w = addr;
                        cycles += 5;
                        break;
                    case 0xE5:          /*PUSH IY */
                        cycles += 5;
                        sp--;
                        z80_writemem(sp, iy.b.h);
                        cycles += 3;
                        sp--;
                        z80_writemem(sp, iy.b.l);
                        cycles += 3;
                        break;
                    case 0xE9:          /*JP (IY) */
                        pc = iy.w;
                        cycles += 4;
                        break;
                    case 0xF9:          /*LD SP,IY */
                        sp = iy.w;
                        cycles += 6;
                        break;
                    default:
                        log_debug("z80: spurious FD prefix on opcode %02X", opcode);
                        goto noprefix;
                }
                break;
            case 0xFE:          /*CP nn */
                cycles += 4;
                temp = z80_readmem(pc++);
                setcp(af.b.h, temp);
                cycles += 3;
                break;
            case 0xFF:          /*RST 38 */
                cycles += 5;
                sp--;
                z80_writemem(sp, pc >> 8);
                cycles += 3;
                sp--;
                z80_writemem(sp, pc & 0xFF);
                pc = 0x38;
                cycles += 3;
                break;
            default:
                log_debug("z80: invalid opcode %02X", opcode);
                break;
        }
        ins++;

        if (output)
            printf("%04X : %04X %04X %04X %04X %04X %04X %04X %04X %02X\n",
                   pc, af.w & 0xFF00, bc.w, de.w, hl.w, ix.w, iy.w, sp, ir.w, opcode);

        if (enterint) {
            iff2 = iff1;
            iff1 = 0;
            sp--;
            z80_writemem(sp, pc >> 8);
            sp--;
            z80_writemem(sp, pc & 0xFF);
            switch (im) {
                case 0:
                case 1:
                    pc = 0x38;
                    break;
                case 2:
                    pc = z80_readmem(0xFFFE) | (z80_readmem(0xFFFF)
                                                << 8);
                    cycles += 8;
                    break;
            }
            z80int = enterint = 0;
            cycles += 11;
        }
        if (tube_irq & 2 && !z80_oldnmi) {
            iff2 = iff1;
            iff1 = 0;
            sp--;
            z80_writemem(sp, pc >> 8);
            sp--;
            z80_writemem(sp, pc & 0xFF);
            pc = 0x66;
            z80_rom_in = true;
            z80int = enterint = 0;
            cycles += 11;
        }
        z80_oldnmi = tube_irq & 2;
        tubecycles -= cycles;
    }
}
