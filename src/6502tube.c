/*B-em v2.2 by Tom Walker
  6502 parasite CPU emulation*/

#include <stdio.h>

#include "b-em.h"
#include "model.h"
#include "tube.h"
#include "6502tube.h"
#include "6502debug.h"

#define a tubea
#define x tubex
#define y tubey
#define s tubesp
#define pc tubepc

static int tube_6502_skipint;
static int tube_6502_oldnmi;

/*6502 registers*/
static uint8_t a, x, y, s;
static uint16_t pc;
static PREG tubep;

/*Memory structures*/
static uint8_t *tuberam;
static size_t tuberamsize;
static uint8_t *tuberom;

bool tube_6502_rom_in = true;

#define TUBE_6502_RAM_SIZE    0x10000
#define TURBO_6502_RAM_SIZE 0x1000000

void tube_6502_close()
{
    if (tuberam) {
        free(tuberam);
        tuberam = NULL;
        tuberamsize = 0;
    }
}

static int dbg_tube6502 = 0;

static int dbg_debug_enable(int newvalue) {
    int oldvalue = dbg_tube6502;
    dbg_tube6502 = newvalue;
    return oldvalue;
};

static inline uint8_t pack_flags(uint8_t flags) {
    if (tubep.c)
        flags |= 1;
    if (tubep.z)
        flags |= 2;
    if (tubep.i)
        flags |= 4;
    if (tubep.d)
        flags |= 8;
    if (tubep.v)
        flags |= 0x40;
    if (tubep.n)
        flags |= 0x80;
    return flags;
}

void tube_6502_savestate(ZFILE *zfp)
{
    unsigned char bytes[9];

    bytes[0] = tube_6502_skipint;
    bytes[1] = tube_6502_oldnmi;
    bytes[2] = a;
    bytes[3] = x;
    bytes[4] = y;
    bytes[5] = pack_flags(0x30);
    bytes[6] = s;
    bytes[7] = pc & 0xff;
    bytes[8] = pc >> 8;
    savestate_zwrite(zfp, bytes, sizeof bytes);
    savestate_zwrite(zfp, tuberam, tuberamsize);
    savestate_zwrite(zfp, tuberom, tubes[curtube].rom_size);
}

static inline void unpack_flags(uint8_t flags) {
    tubep.c = flags & 1;
    tubep.z = flags & 2;
    tubep.i = flags & 4;
    tubep.d = flags & 8;
    tubep.v = flags & 0x40;
    tubep.n = flags & 0x80;
}

void tube_6502_loadstate(ZFILE *zfp)
{
    unsigned char bytes[9];

    savestate_zread(zfp, bytes, sizeof bytes);
    tube_6502_skipint = bytes[0];
    tube_6502_oldnmi = bytes[1];
    a = bytes[2];
    x = bytes[3];
    y = bytes[4];
    unpack_flags(bytes[5]);
    s = bytes[6];
    pc = bytes[7];
    pc |= bytes[8] << 8;

    savestate_zread(zfp, tuberam, tuberamsize);
    savestate_zread(zfp, tuberom, tubes[curtube].rom_size);
}

static uint32_t dbg_reg_get(int which) {
    switch (which) {
        case REG_A:
            return tubea;
        case REG_X:
            return tubex;
        case REG_Y:
            return tubey;
        case REG_S:
            return tubesp;
        case REG_P:
            return pack_flags(0x30);
        case REG_PC:
            return tubepc;
        default:
            log_warn("6502tube: attempt to get non-existent register");
            return 0;
    }
}

static void dbg_reg_set(int which, uint32_t value) {
    switch (which) {
        case REG_A:
            tubea = value;
            break;
        case REG_X:
            tubex = value;
            break;
        case REG_Y:
            tubey = value;
            break;
        case REG_S:
            tubesp = value;
            break;
        case REG_P:
            unpack_flags(value);
            break;
        case REG_PC:
            tubepc = value;
            break;
        default:
            log_warn("6502tube: attempt to set non-existent register");
    }
}

static size_t dbg_reg_print(int which, char *buf, size_t bufsize) {
    switch (which) {
        case REG_P:
            return dbg6502_print_flags(&tubep, buf, bufsize);
            break;
        case REG_PC:
            return snprintf(buf, bufsize, "%04X", tubepc);
            break;
        default:
            return snprintf(buf, bufsize, "%02X", dbg_reg_get(which));
    }
}

static void dbg_reg_parse(int which, const char *str) {
    uint32_t value = strtol(str, NULL, 16);
    dbg_reg_set(which, value);
}

static uint32_t do_readmem(uint32_t addr)
{
    if ((addr & ~7) == 0xFEF8)
        return tube_parasite_read(addr);
    if ((addr & ~0xFFF) == 0xF000 && tube_6502_rom_in)
        return tuberom[addr & 0x7FF];
    return tuberam[addr];
}

static int endtimeslice;

static void disable_turbo(void);
static void enable_turbo(void);

static void do_writemem(uint32_t addr, uint32_t value)
{
    if ((addr & ~7) == 0xFEF8) {
        tube_parasite_write(addr, value);
        endtimeslice = 1;
        return;
    }
    tuberam[addr] = value;
    if (addr == 0xfef0 && tuberamsize > 0x10000) {
        if (value & 0x80)
            enable_turbo();
        else
            disable_turbo();
    }
}

static uint32_t dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize);

static uint16_t oldtpc, oldtpc2;

static uint32_t dbg_get_instr_addr(void) {
    return oldtpc;
}

static const char *trap_names[] = { "BRK", "TRAP", NULL };

cpu_debug_t tube6502_cpu_debug = {
    .cpu_name       = "tube6502",
    .debug_enable   = dbg_debug_enable,
    .memread        = do_readmem,
    .memwrite       = do_writemem,
    .disassemble    = dbg_disassemble,
    .reg_names      = dbg6502_reg_names,
    .reg_get        = dbg_reg_get,
    .reg_set        = dbg_reg_set,
    .reg_print      = dbg_reg_print,
    .reg_parse      = dbg_reg_parse,
    .get_instr_addr = dbg_get_instr_addr,
    .trap_names     = trap_names,
    .print_addr     = debug_print_addr16,
    .parse_addr     = debug_parse_addr
};

static uint32_t dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize) {
    return dbg6502_disassemble(cpu, addr, buf, bufsize, M65C02);
}

#undef printf
/*static void tubedumpregs()
{
        FILE *f=x_fopen("tuberam.dmp","wb");
        fwrite(tuberam,65536,1,f);
        fclose(f);
        log_debug("Tube 65c12 registers :\n");
        log_debug("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n",a,x,y,s,pc);
        log_debug("Status : %c%c%c%c%c%c\n",(tubep.n)?'N':' ',(tubep.v)?'V':' ',(tubep.d)?'D':' ',(tubep.i)?'I':' ',(tubep.z)?'Z':' ',(tubep.c)?'C':' ');
}*/

#define polltime(c) { tubecycles-=c; }

static uint8_t tube_6502_readmem(uint32_t addr) {
    uint32_t val = do_readmem(addr);
    if (dbg_tube6502)
        debug_memread(&tube6502_cpu_debug, addr, val, 1);
    return val;
}

static void tube_6502_writemem(uint32_t addr, uint8_t value) {
    if (dbg_tube6502)
        debug_memwrite(&tube6502_cpu_debug, addr, value, 1);
    do_writemem(addr, value);
}

static uint8_t readmem(uint16_t addr)
{
    return tube_6502_readmem(addr);
}

static void writemem(uint16_t addr, uint8_t value)
{
    tube_6502_writemem(addr, value);
}

#define getw() (readmem(pc)|(readmem(pc+1)<<8)); pc+=2

#define read_zp_indirect(zp) (readmem(zp & 0xff) + (readmem((zp + 1) & 0xff) << 8))

void tube_6502_reset()
{
    tube_6502_rom_in = true;
        pc = readmem(0xFFFC) | (readmem(0xFFFD) << 8);
        tubep.i = 1;
        tube_irq = 0;
        tube_6502_skipint = 0;
}

static bool common_init(void *rom, size_t memsize)
{
    if (tuberamsize != memsize) {
        if (tuberam) {
            free(tuberam);
            tuberamsize = 0;
        }
        tuberam = (uint8_t *) malloc(memsize);
        if (!tuberam) {
            log_error("6502tube: unable to allocate RAM");
            return false;
        }
        tuberamsize = memsize;
    }
    memset(tuberam, 0, memsize);
    tuberom = rom;
    tube_type = TUBE6502;
    tube_readmem = tube_6502_readmem;
    tube_writemem = tube_6502_writemem;
    tube_exec  = tube_6502_exec;
    tube_proc_savestate = tube_6502_savestate;
    tube_proc_loadstate = tube_6502_loadstate;
    tube_6502_reset();
    return true;
}

static uint8_t read_zp_iy_normal(uint8_t zp)
{
    uint32_t addr1 = read_zp_indirect(zp);
    uint32_t addr2 = addr1 + y;
    if ((addr1 & 0xFF00) ^ (addr2 & 0xFF00))
        polltime(1);
    return tube_6502_readmem(addr2 & 0xffff);
}

static void write_zp_iy_normal(uint8_t zp, uint8_t val)
{
    uint32_t addr1 = read_zp_indirect(zp);
    uint32_t addr2 = addr1 + y;
    if ((addr1 & 0xFF00) ^ (addr2 & 0xFF00))
        polltime(1);
    writemem(addr2, val);
}

static uint8_t read_zp_nr_normal(uint8_t zp)
{
    uint32_t addr1 = read_zp_indirect(zp);
    return tube_6502_readmem(addr1 & 0xffff);
}

static void write_zp_nr_normal(uint8_t zp, uint8_t val)
{
    uint32_t addr1 = read_zp_indirect(zp);
    writemem(addr1 & 0xffff, val);
}

static uint8_t read_zp_iy_turbo(uint8_t zp)
{
    uint32_t addr1 = tube_6502_readmem(zp) | (tube_6502_readmem(zp+1) << 8) | ((tube_6502_readmem(0x301+zp) & 0x03) << 16);
    uint32_t addr2 = addr1 + y;
    if ((addr1 & 0xFF00) ^ (addr2 & 0xFF00))
        polltime(1);
    return tube_6502_readmem(addr2);
}

static void write_zp_iy_turbo(uint8_t zp, uint8_t val)
{
    uint32_t addr1 = tube_6502_readmem(zp) | (tube_6502_readmem(zp+1) << 8) | ((tube_6502_readmem(0x301+zp) & 0x03) << 16);
    uint32_t addr2 = addr2 = addr1 + y;
    if ((addr1 & 0xFF00) ^ (addr2 & 0xFF00))
        polltime(1);
    tube_6502_writemem(addr2, val);
}

static uint8_t read_zp_nr_turbo(uint8_t zp)
{
    uint32_t addr1 = tube_6502_readmem(zp) | (tube_6502_readmem(zp+1) << 8) | ((tube_6502_readmem(0x301+zp) & 0x03) << 16);
    return tube_6502_readmem(addr1);
}

static void write_zp_nr_turbo(uint8_t zp, uint8_t val)
{
    uint32_t addr1 = tube_6502_readmem(zp) | (tube_6502_readmem(zp+1) << 8) | ((tube_6502_readmem(0x301+zp) & 0x03) << 16);
    tube_6502_writemem(addr1, val);
}

static uint8_t (*read_zp_indirect_y)(uint8_t zp);
static void (*write_zp_indirect_y)(uint8_t zp, uint8_t val);
static uint8_t (*read_zp_indirect_nr)(uint8_t zp);
static void (*write_zp_indirect_nr)(uint8_t zp, uint8_t val);

static void disable_turbo(void)
{
    read_zp_indirect_y = read_zp_iy_normal;
    write_zp_indirect_y = write_zp_iy_normal;
    read_zp_indirect_nr = read_zp_nr_normal;
    write_zp_indirect_nr = write_zp_nr_normal;
}

static void enable_turbo(void)
{
    read_zp_indirect_y = read_zp_iy_turbo;
    write_zp_indirect_y = write_zp_iy_turbo;
    read_zp_indirect_nr = read_zp_nr_turbo;
    write_zp_indirect_nr = write_zp_nr_turbo;
}

bool tube_6502_init(void *rom)
{
    if (common_init(rom, TUBE_6502_RAM_SIZE)) {
        disable_turbo();
        return true;
    }
    return false;
}

bool tube_6502_iturb(void *rom)
{
    if (common_init(rom, TURBO_6502_RAM_SIZE)) {
        enable_turbo();
        return true;
    }
    return false;
}

static inline void setzn(uint8_t v)
{
    tubep.z = !v;
    tubep.n = (v) & 0x80;
}

static inline void push(uint8_t v)
{
    writemem(0x100+(s--), v);
}

static inline uint8_t pull(void)
{
    return readmem(0x100+(++s));
}

static inline void adc_cmos(uint8_t temp)
{
    int al, ah;
    int16_t tempw;

    if (tubep.d) {
        ah = 0;
        al = (a & 0xF) + (temp & 0xF) + (tubep.c ? 1 : 0);
        if (al > 9) {
            al -= 10;
            al &= 0xF;
            ah = 1;
        }
        ah += ((a >> 4) + (temp >> 4));
        tubep.v = (((ah << 4) ^ a) & 0x80) && !((a ^ temp) & 0x80);
        tubep.c = 0;
        if (ah > 9) {
            tubep.c = 1;
            ah -= 10;
            ah &= 0xF;
        }
        a = (al & 0xF) | (ah << 4);
        setzn(a);
        polltime(1);
    }
    else {
        tempw = (a + temp + (tubep.c ? 1 : 0));
        tubep.v = (!((a ^ temp) & 0x80) && ((a ^ tempw) & 0x80));
        a = tempw & 0xFF;
        tubep.c = tempw & 0x100;
        setzn(a);
    }
}

static inline void sbc_cmos(uint8_t temp)
{
    int al, tempv;
    int16_t tempw;

    if (tubep.d) {
        al = (a & 15) - (temp & 15) - (tubep.c ? 0 : 1);
        tempw = a-temp-(tubep.c ? 0 : 1);
        tempv = (signed char)a -(signed char)temp-(tubep.c ? 0 : 1);
        tubep.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);
        tubep.c = tempw >= 0;
        if (tempw < 0)
           tempw -= 0x60;
        if (al < 0)
           tempw -= 0x06;
        a = tempw & 0xFF;
        setzn(a);
        polltime(1);
    }
    else {
        tempw = a-temp-(tubep.c ? 0 : 1);
        tempv = (signed char)a -(signed char)temp-(tubep.c ? 0 : 1);
        tubep.v = ((tempw & 0x80) > 0) ^ ((tempv & 0x100) != 0);
        tubep.c = tempw >= 0;
        a = tempw & 0xFF;
        setzn(a);
    }
}

static inline void rmb(uint8_t mask)
{
    uint8_t ea = readmem(pc); pc++;
    writemem(ea, readmem(ea) & ~mask);
    polltime(5);
}

static inline void smb(uint8_t mask)
{
    uint8_t ea = readmem(pc); pc++;
    writemem(ea, readmem(ea) | mask);
    polltime(5);
}

static inline void bbr(uint8_t mask)
{
    uint8_t ea = readmem(pc); pc++;
    uint8_t offset = readmem(pc); pc++;
    if (!(readmem(ea) & mask))
        pc += offset;
    polltime(5);
}

static inline void bbs(uint8_t mask)
{
    uint8_t ea = readmem(pc); pc++;
    uint8_t offset = readmem(pc); pc++;
    if (readmem(ea) & mask)
        pc += offset;
    polltime(5);
}

#ifdef TRACE_TUBE
static INLINE void tube_6502_trace(uint8_t opcode)
{
        uint8_t cyc;

        if (trace_fp) {
                flockfile(trace_fp);
                cyc = tubecycles;
                putc_unlocked(cyc, trace_fp);
                putc_unlocked(pc & 0xff, trace_fp);
                putc_unlocked(pc >> 8, trace_fp);
                putc_unlocked(opcode, trace_fp);
                putc_unlocked(readmem(pc), trace_fp);
                putc_unlocked(readmem(pc + 1), trace_fp);
                putc_unlocked(a, trace_fp);
                putc_unlocked(x, trace_fp);
                putc_unlocked(y, trace_fp);
                putc_unlocked(s, trace_fp);
                putc_unlocked(pack_flags(), trace_fp);
                funlockfile(trace_fp);
        }
}
#endif

void tube_6502_exec()
{
        uint8_t opcode;
        uint16_t addr;
        uint16_t tempw;
        uint8_t temp;
        int tempi;
        int8_t offset;
//        tubecycles+=(tubecycs<<1);
//        printf("Tube exec %i %04X\n",tubecycles,pc);
        while (tubecycles > 0) {
                oldtpc2 = oldtpc;
                oldtpc = pc;
        if (dbg_tube6502)
            debug_preexec(&tube6502_cpu_debug, pc);
        opcode = readmem(pc);
                pc++;
#ifdef TRACE_TUBE
                tube_6502_trace(opcode);
#endif
//                        printf("Tube opcode %02X\n",opcode);
                switch (opcode) {
                case 0x00:
                        /*BRK*/
                        if (dbg_tube6502)
                            debug_trap(&tube6502_cpu_debug, oldtpc, 0);
                        pc++;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x30));
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        tubep.i = 1;
                        tubep.d = 0;
                        polltime(7);
                        break;

                case 0x01:      /*ORA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a |= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x02:
                        if (dbg_tube6502)
                            debug_trap(&tube6502_cpu_debug, oldtpc, 1);
                        polltime(2);
                        readmem(pc++);
                        break;

                case 0x04:      /*TSB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
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
                        break;

                case 0x06:      /*ASL zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x07:
                        rmb(0x01);
                        break;

                case 0x08:      /*PHP*/
                        temp = pack_flags(0x30);
                        push(temp);
                        polltime(3);
                        break;

                case 0x09:      /*ORA imm */
                        a |= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x0A:      /*ASL A */
                        tubep.c = a & 0x80;
                        a <<= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x0C:      /*TSB abs */
                        addr = getw();
//                                printf("TSB %04X %02X\n",addr,a);
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
                        temp |= a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x0D:      /*ORA abs */
                        addr = getw();
                        polltime(4);
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
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x0F:
                        bbr(0x01);
                        break;

                case 0x10:
                        /*BPL*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x11:      /*ORA (),y */
                        a |= read_zp_indirect_y(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x12:      /*ORA () */
                        a |= read_zp_indirect_nr(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x14:      /*TRB zp */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem(addr);
                        tubep.z = !(temp & a);
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x15:      /*ORA zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        a |= readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x16:      /*ASL zp,x */
                        addr = (readmem(pc++) + x) & 0xFF;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x17:
                        rmb(0x02);
                        break;

                case 0x18:
                        /*CLC*/ tubep.c = 0;
                        polltime(2);
                        break;

                case 0x19:      /*ORA abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a |= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x1A:      /*INC A */
                        a++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x1C:      /*TRB abs */
                        addr = getw();
                        temp = readmem(addr);
                        tubep.z = !(temp & a);  //!(temp&(~a));
                        temp &= ~a;
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x1D:      /*ORA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        addr += x;
                        a |= readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x1E:      /*ASL abs,x */
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6+tempw);
                        break;

                case 0x1F:
                        bbr(0x02);
                        break;

                case 0x20:      /*JSR*/
                        addr = readmem(pc++);
                        push(pc >> 8);
                        push(pc);
                        pc = addr | (readmem(pc) << 8);
                        polltime(6);
                        break;

                case 0x21:      /*AND (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a &= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x22:
                case 0x42:
                case 0x62:
                case 0x82:
                case 0xc2:
                case 0xe2:
                    (void)readmem(pc++);
                    polltime(2);
                    break;

                case 0x24:      /*BIT zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(3);
                        break;

                case 0x25:      /*AND zp */
                        addr = readmem(pc);
                        pc++;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(3);
                        break;

                case 0x26:      /*ROL zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x27:
                        rmb(0x04);
                        break;

                case 0x28:
                        /*PLP*/ temp = pull();
                        unpack_flags(temp);
                        polltime(4);
                        break;

                case 0x29:
                        /*AND*/ a &= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x2A:      /*ROL A */
                        tempi = tubep.c;
                        tubep.c = a & 0x80;
                        a <<= 1;
                        if (tempi)
                                a |= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x2C:      /*BIT abs */
                        addr = getw();
                        temp = readmem(addr);
//                                printf("BIT %04X\n",addr);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x2D:      /*AND abs */
                        addr = getw();
                        polltime(4);
                        a &= readmem(addr);
                        setzn(a);
                        break;

                case 0x2E:      /*ROL abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0x2F:
                        bbr(0x04);
                        break;

                case 0x30:
                        /*BMI*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.n) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x31:      /*AND (),y */
                        a &= read_zp_indirect_y(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x32:      /*AND () */
                        a &= read_zp_indirect_nr(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x34:      /*BIT zp,x */
                        addr = readmem(pc);
                        pc++;
                        temp = readmem((addr + x) & 0xFF);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x35:      /*AND zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        a &= readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x36:      /*ROL zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x37:
                        rmb(0x08);
                        break;

                case 0x38:      /*SEC*/
                        tubep.c = 1;
                        polltime(2);
                        break;

                case 0x39:      /*AND abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x3A:      /*DEC A */
                        a--;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x3C:      /*BIT abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                            polltime(1);
                        addr += x;
                        temp = readmem(addr);
//                                printf("BIT abs,x %02X %04X\n",temp,addr);
                        tubep.z = !(a & temp);
                        tubep.v = temp & 0x40;
                        tubep.n = temp & 0x80;
                        polltime(4);
                        break;

                case 0x3D:      /*AND abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a &= readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x3E:      /*ROL abs,x */
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        tempi = tubep.c;
                        tubep.c = temp & 0x80;
                        temp <<= 1;
                        if (tempi)
                                temp |= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6+tempw);
                        break;

                case 0x3F:
                        bbr(0x08);
                        break;

                case 0x40: /* RTI */
                        unpack_flags(pull());
                        pc = pull();
                        pc |= (pull() << 8);
                        polltime(6);
                        break;

                case 0x41:      /*EOR (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0x44: /* NOP */
                    (void)readmem(pc++);
                    polltime(3);
                    break;

                case 0x45:      /*EOR zp */
                        addr = readmem(pc++);
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(3);
                        break;

                case 0x46:      /*LSR zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x47:
                        rmb(0x10);
                        break;

                case 0x48:
                        /*PHA*/ push(a);
                        polltime(3);
                        break;

                case 0x49:
                        /*EOR*/ a ^= readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x4A:      /*LSR A */
                        tubep.c = a & 1;
                        a >>= 1;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x4C:
                        /*JMP*/ addr = getw();
                        pc = addr;
                        polltime(3);
                        break;

                case 0x4D:      /*EOR abs */
                        addr = getw();
                        polltime(4);
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
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x4F:
                        bbr(0x10);
                        break;

                case 0x50:
                        /*BVC*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x51:      /*EOR (),y */
                        a ^= read_zp_indirect_y(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x52:      /*EOR () */
                        a ^= read_zp_indirect_nr(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0x54: /* NOP */
                case 0xd4:
                case 0xf4:
                    (void)readmem(pc++);
                    polltime(4);
                    break;

                case 0x55:      /*EOR zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        a ^= readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x56:      /*LSR zp,x */
                        addr = (readmem(pc++) + x) & 0xFF;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tubep.c = temp & 1;
                        temp >>= 1;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x57:
                        rmb(0x20);
                        break;

                case 0x58:
                        /*CLI*/ tubep.i = 0;
                        polltime(2);
                        break;

                case 0x59:      /*EOR abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + y);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x5A:
                        /*PHY*/ push(y);
                        polltime(3);
                        break;

                case 0x5c: /* NOP */
                    (void)readmem(pc++);
                    (void)readmem(pc++);
                    polltime(8);
                    break;

                case 0x5D:      /*EOR abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a ^= readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0x5E:      /*LSR abs,x */
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        tubep.c = temp & 1;
                        temp >>= 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6+tempw);
                        break;

                case 0x5F:
                        bbr(0x20);
                        break;

                case 0x60:
                        /*RTS*/ pc = pull();
                        pc |= (pull() << 8);
                        pc++;
                        polltime(6);
                        break;

                case 0x61:      /*adc_cmos (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(6);
                        break;

                case 0x64:      /*STZ zp */
                        addr = readmem(pc++);
                        writemem(addr, 0);
                        polltime(3);
                        break;

                case 0x65:      /*ADC zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(3);
                        break;

                case 0x66:      /*ROR zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(5);
                        break;

                case 0x67:
                        rmb(0x40);
                        break;

                case 0x68:
                        /*PLA*/ a = pull();
                        setzn(a);
                        polltime(4);
                        break;

                case 0x69:      /*ADC imm */
                        temp = readmem(pc);
                        pc++;
                        adc_cmos(temp);
                        polltime(2);
                        break;

                case 0x6A:      /*ROR A */
                        tempi = tubep.c;
                        tubep.c = a & 1;
                        a >>= 1;
                        if (tempi)
                                a |= 0x80;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x6C:      /*JMP () */
                        addr = getw();
                        pc = readmem(addr) | (readmem(addr + 1) << 8);
                        polltime(5);
                        break;

                case 0x6D:      /*ADC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        adc_cmos(temp);
                        break;

                case 0x6E:      /*ROR abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        break;

                case 0x6F:
                        bbr(0x40);
                        break;

                case 0x70:      /*BVS*/
                        offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.v) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x71:      /*ADC (),y */
                        adc_cmos(read_zp_indirect_y(readmem(pc++)));
                        polltime(5);
                        break;

                case 0x72:      /*ADC () */
                        adc_cmos(read_zp_indirect_nr(readmem(pc++)));
                        polltime(5);
                        break;

                case 0x74:      /*STZ zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        writemem(addr, 0);
                        polltime(4);
                        break;

                case 0x75:      /*ADC zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(4);
                        break;

                case 0x76:      /*ROR zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        setzn(temp);
                        writemem(addr, temp);
                        polltime(6);
                        break;

                case 0x77:
                        rmb(0x80);
                        break;

                case 0x78:      /*SEI*/
                        tubep.i = 1;
                        polltime(2);
//                                if (output2) printf("SEI at line %i %04X %02X %02X\n",lines,pc,tuberam[0x103+s],tuberam[0x104+s]);
                        break;

                case 0x79:      /*ADC abs,y */
                    tempw = getw();
                    addr = tempw + y;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(4+tempw);
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
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        adc_cmos(temp);
                        polltime(4+tempw);
                        break;

                case 0x7E:      /*ROR abs,x */
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        tempi = tubep.c;
                        tubep.c = temp & 1;
                        temp >>= 1;
                        if (tempi)
                                temp |= 0x80;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6+tempw);
                        break;

                case 0x7F:
                        bbr(0x80);
                        break;

                case 0x80:      /*BRA*/
                        offset = (int8_t) readmem(pc);
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
                        break;

                case 0x84:      /*STY zp */
                        addr = readmem(pc++);
                        writemem(addr, y);
                        polltime(3);
                        break;

                case 0x85:      /*STA zp */
                        addr = readmem(pc++);
                        writemem(addr, a);
                        polltime(3);
                        break;

                case 0x86:      /*STX zp */
                        addr = readmem(pc++);
                        writemem(addr, x);
                        polltime(3);
                        break;

                case 0x87:
                        smb(0x01);
                        break;

                case 0x88:      /*DEY*/
                        y--;
                        setzn(y);
                        polltime(2);
                        break;

                case 0x89:      /*BIT imm */
                        temp = readmem(pc);
                        pc++;
                        tubep.z = !(a & temp);
                        polltime(2);
                        break;

                case 0x8A:
                        /*TXA*/ a = x;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x8C:      /*STY abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, y);
                        break;

                case 0x8D:      /*STA abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, a);
                        break;

                case 0x8E:      /*STX abs */
                        addr = getw();
                        polltime(4);
                        writemem(addr, x);
                        break;

                case 0x8F:
                        bbs(0x01);
                        break;

                case 0x90:      /*BCC*/
                        offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0x91:      /*STA (),y */
                        write_zp_indirect_y(readmem(pc++), a);
                        polltime(6);
                        break;

                case 0x92:      /*STA () */
                        write_zp_indirect_nr(readmem(pc++), a);
                        polltime(6);
                        break;

                case 0x94:      /*STY zp,x */
                        addr = (readmem(pc++) + x ) & 0xff;
                        writemem(addr, y);
                        polltime(4);
                        break;

                case 0x95:      /*STA zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        writemem(addr, a);
                        polltime(4);
                        break;

                case 0x96:      /*STX zp,y */
                        addr = (readmem(pc++) + y) & 0xff;
                        writemem(addr, x);
                        polltime(4);
                        break;

                case 0x97:
                        smb(0x02);
                        break;

                case 0x98:      /*TYA*/
                        a = y;
                        setzn(a);
                        polltime(2);
                        break;

                case 0x99:      /*STA abs,y */
                        addr = getw();
                        polltime(4);
                        writemem(addr + y, a);
                        polltime(1);
                        break;

                case 0x9A:
                        /*TXS*/ s = x;
                        polltime(2);
                        break;

                case 0x9C:      /*STZ abs */
                        addr = getw();
                        polltime(3);
                        writemem(addr, 0);
                        polltime(1);
                        break;

                case 0x9D:      /*STA abs,x */
                        addr = getw();
                        polltime(4);
                        writemem(addr + x, a);
                        polltime(1);
                        break;

                case 0x9E:      /*STZ abs,x */
                        addr = getw();
                        addr += x;
                        polltime(4);
                        writemem(addr, 0);
                        polltime(1);
                        break;

                case 0x9F:
                        bbs(0x02);
                        break;

                case 0xA0:      /*LDY imm */
                        y = readmem(pc);
                        pc++;
                        setzn(y);
                        polltime(2);
                        break;

                case 0xA1:      /*LDA (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        a = readmem(addr);
                        setzn(a);
                        polltime(6);
                        break;

                case 0xA2:      /*LDX imm */
                        x = readmem(pc);
                        pc++;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xA4:      /*LDY zp */
                        addr = readmem(pc++);
                        y = readmem(addr);
                        setzn(y);
                        polltime(3);
                        break;

                case 0xA5:      /*LDA zp */
                        addr = readmem(pc++);
                        a = readmem(addr);
                        setzn(a);
                        polltime(3);
                        break;

                case 0xA6:      /*LDX zp */
                        addr = readmem(pc++);
                        x = readmem(addr);
                        setzn(x);
                        polltime(3);
                        break;

                case 0xA7:
                        smb(0x04);
                        break;

                case 0xA8:      /*TAY*/
                        y = a;
                        setzn(y);
                        break;

                case 0xA9:      /*LDA imm */
                        a = readmem(pc);
                        pc++;
                        setzn(a);
                        polltime(2);
                        break;

                case 0xAA:      /*TAX*/
                        x = a;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xAC:      /*LDY abs */
                        addr = getw();
                        polltime(4);
                        y = readmem(addr);
                        setzn(y);
                        break;

                case 0xAD:      /*LDA abs */
                        addr = getw();
                        polltime(4);
                        a = readmem(addr);
                        setzn(a);
                        break;

                case 0xAE:      /*LDX abs */
                        addr = getw();
                        polltime(4);
                        x = readmem(addr);
                        setzn(x);
                        break;

                case 0xAF:
                        bbs(0x04);
                        break;

                case 0xB0:
                        /*BCS*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.c) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xB1:      /*LDA (),y */
                        a = read_zp_indirect_y(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0xB2:      /*LDA () */
                        a = read_zp_indirect_nr(readmem(pc++));
                        setzn(a);
                        polltime(5);
                        break;

                case 0xB4:      /*LDY zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        y = readmem(addr);
                        setzn(y);
                        polltime(4);
                        break;

                case 0xB5:      /*LDA zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        a = readmem(addr);
                        setzn(a);
                        polltime(4);
                        break;

                case 0xB6:      /*LDX zp,y */
                        addr = (readmem(pc++) + y) & 0xff;
                        x = readmem(addr);
                        setzn(x);
                        polltime(4);
                        break;

                case 0xB7:
                        smb(0x08);
                        break;

                case 0xB8:
                        /*CLV*/ tubep.v = 0;
                        polltime(2);
                        break;

                case 0xB9:      /*LDA abs,y */
                        addr = getw();
                        polltime(3);
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + y);
                        setzn(a);
                        polltime(1);
                        break;

                case 0xBA:
                        /*TSX*/ x = s;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xBC:      /*LDY abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        y = readmem(addr + x);
                        setzn(y);
                        polltime(4);
                        break;

                case 0xBD:      /*LDA abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        a = readmem(addr + x);
                        setzn(a);
                        polltime(4);
                        break;

                case 0xBE:      /*LDX abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        x = readmem(addr + y);
                        setzn(x);
                        polltime(4);
                        break;

                case 0xBF:
                        bbs(0x08);
                        break;

                case 0xC0:      /*CPY imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(2);
                        break;

                case 0xC1:      /*CMP (,x) */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(6);
                        break;

                case 0xC4:      /*CPY zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(3);
                        break;

                case 0xC5:      /*CMP zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(3);
                        break;

                case 0xC6:      /*DEC zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        writemem(addr, temp);
                        writemem(addr, --temp);
                        setzn(temp);
                        polltime(5);
                        break;

                case 0xC7:
                        smb(0x10);
                        break;

                case 0xC8:
                        /*INY*/ y++;
                        setzn(y);
                        polltime(2);
                        break;

                case 0xC9:      /*CMP imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(2);
                        break;

                case 0xCA:
                        /*DEX*/ x--;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xCC:      /*CPY abs */
                        addr = getw();
                        temp = readmem(addr);
                        setzn(y - temp);
                        tubep.c = (y >= temp);
                        polltime(4);
                        break;

                case 0xCD:      /*CMP abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        break;

                case 0xCE:      /*DEC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) - 1;
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xCF:
                        bbs(0x10);
                        break;

                case 0xD0:
                        /*BNE*/ offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (!tubep.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xD1:      /*CMP (),y */
                        temp = read_zp_indirect_y(readmem(pc++));
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(5);
                        break;

                case 0xD2:      /*CMP () */
                        temp = read_zp_indirect_nr(readmem(pc++));
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(5);
                        break;

                case 0xD5:      /*CMP zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(4);
                        break;

                case 0xD6:      /*DEC zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        writemem(addr, --temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0xD7:
                        smb(0x20);
                        break;

                case 0xD8:      /*CLD*/
                        tubep.d = 0;
                        polltime(2);
                        break;

                case 0xD9:      /*CMP abs,y */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + y);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(4);
                        break;

                case 0xDA:      /*PHX*/
                        push(x);
                        polltime(3);
                        break;

                case 0xdc: /* NOP */
                case 0xfc:
                    (void)readmem(pc++);
                    (void)readmem(pc++);
                    polltime(4);
                    break;

                case 0xDD:      /*CMP abs,x */
                        addr = getw();
                        if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
                                polltime(1);
                        temp = readmem(addr + x);
                        setzn(a - temp);
                        tubep.c = (a >= temp);
                        polltime(4);
                        break;

                case 0xDE:      /*DEC abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr) - 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0xDF:
                        bbs(0x20);
                        break;

                case 0xE0:      /*CPX imm */
                        temp = readmem(pc);
                        pc++;
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(2);
                        break;

                case 0xE1:      /*sbc_cmos (,x) *//*This was missed out of every B-em version since 0.6 as it was never used! */
                        temp = readmem(pc) + x;
                        pc++;
                        addr = read_zp_indirect(temp);
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(6);
                        break;

                case 0xE4:      /*CPX zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(3);
                        break;

                case 0xE5:      /*SBC zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(3);
                        break;

                case 0xE6:      /*INC zp */
                        addr = readmem(pc++);
                        temp = readmem(addr);
                        writemem(addr, temp);
                        writemem(addr, ++temp);
                        setzn(temp);
                        polltime(5);
                        break;

                case 0xE7:
                        smb(0x40);
                        break;

                case 0xE8:      /*INX*/
                        x++;
                        setzn(x);
                        polltime(2);
                        break;

                case 0xE9:      /*SBC imm */
                        temp = readmem(pc);
                        pc++;
                        sbc_cmos(temp);
                        polltime(2);
                        break;

                case 0xEA:
                        /*NOP*/ polltime(2);
                        break;

                case 0xEC:      /*CPX abs */
                        addr = getw();
                        temp = readmem(addr);
                        setzn(x - temp);
                        tubep.c = (x >= temp);
                        polltime(3);
                        break;

                case 0xED:      /*SBC abs */
                        addr = getw();
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(4);
                        break;

                case 0xEE:      /*INC abs */
                        addr = getw();
                        polltime(4);
                        temp = readmem(addr) + 1;
                        polltime(1);
                        readmem(addr);
                        polltime(1);
                        writemem(addr, temp);
                        setzn(temp);
                        break;

                case 0xEF:
                        bbs(0x40);
                        break;

                case 0xF0:      /*BEQ*/
                        offset = (int8_t) readmem(pc);
                        pc++;
                        temp = 2;
                        if (tubep.z) {
                                temp++;
                                if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
                                        temp++;
                                pc += offset;
                        }
                        polltime(temp);
                        break;

                case 0xF1:      /*SBC (),y */
                        sbc_cmos(read_zp_indirect_y(readmem(pc++)));
                        polltime(5);
                        break;

                case 0xF2:      /*SBC () */
                        sbc_cmos(read_zp_indirect_nr(readmem(pc++)));
                        polltime(5);
                        break;

                case 0xF5:      /*SBC zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(4);
                        break;

                case 0xF6:      /*INC zp,x */
                        addr = (readmem(pc++) + x) & 0xff;
                        temp = readmem(addr);
                        writemem(addr, temp);
                        writemem(addr, ++temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0xF7:
                        smb(0x80);
                        break;

                case 0xF8:      /*SED*/
                        tubep.d = 1;
                        polltime(2);
                        break;

                case 0xF9:      /*SBC abs,y */
                    tempw = getw();
                    addr = tempw + y;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(4);
                        break;

                case 0xFA:
                        /*PLX*/ x = pull();
                        setzn(x);
                        polltime(4);
                        break;

                case 0xFD:      /*SBC abs,x */
                    tempw = getw();
                    addr = tempw + x;
                    tempw = (tempw & 0xff00) ^ (addr & 0xff00) ? 1 : 0;
                        temp = readmem(addr);
                        sbc_cmos(temp);
                        polltime(4+tempw);
                        break;

                case 0xFE:      /*INC abs,x */
                        addr = getw();
                        addr += x;
                        temp = readmem(addr) + 1;
                        writemem(addr, temp);
                        setzn(temp);
                        polltime(6);
                        break;

                case 0xFF:
                        bbs(0x80);
                        break;

                default:
                    polltime(1);
                    break;
                }
                if ((tube_irq & 2) && !tube_6502_oldnmi) {
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x20));
                        pc = readmem(0xFFFA) | (readmem(0xFFFB) << 8);
                        tubep.i = 1;
                        polltime(7);
                }
                tube_6502_oldnmi = tube_irq & 2;
                if (((tube_irq & 1) && !tubep.i && !tube_6502_skipint)
                    || tube_6502_skipint == 2) {
//                                if (skipint==2) printf("interrupt\n");
                        tube_6502_skipint = 0;
                        push(pc >> 8);
                        push(pc & 0xFF);
                        push(pack_flags(0x20));
                        pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
                        tubep.i = 1;
                        polltime(7);
//                                printf("Interrupt line %i %i %02X %02X %02X %02X\n",interrupt,lines,sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier,uservia.ier,uservia.ifr);
                }
                if ((tube_irq & 1) && !tubep.i && tube_6502_skipint) {
                        tube_6502_skipint = 2;
//                                printf("skipint=2\n");
                }
                if (endtimeslice) {
                        endtimeslice = 0;
                        return;
                }
/*                        if (tubeoutput==2) log_debug("%04X : %02X %02X %02X\n",pc,a,x,y);
                        if (tubetimetolive)
                        {
                                tubetimetolive--;
                                if (!tubetimetolive)
                                {
                                        tubeoutput=1;
                                }
                        }*/
        }
}
