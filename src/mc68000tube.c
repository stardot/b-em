#include "b-em.h"
#include "cpu_debug.h"
#include "tube.h"
#include "mc68000tube.h"
#include "musahi/m68k.h"

static uint8_t *mc68000_ram, *mc68000_rom;
static bool mc68000_debug_enabled = false;
static bool rom_low;

static uint8_t readmem(uint32_t addr)
{
    if (rom_low) {
        if (addr & 0x40000) {
            rom_low = false;
            log_debug("mc68000: readmem paging out ROM");
        }
        else if (addr < MC68000_ROM_SIZE) {
            uint8_t data = mc68000_rom[addr & 0x7FFF];
            //log_debug("mc68000: read %08X as low ROM -> %02X", addr, data);
            return data;
        }
    }
    if (addr < MC68000_RAM_SIZE) {
        uint8_t data = mc68000_ram[addr];
        //log_debug("mc68000: read %08X as RAM -> %02X", addr, data);
        return data;
    }
    else {
        uint32_t top = addr & 0xFFFF0000;
        if (top == 0xFFFF0000) {
            uint8_t data = mc68000_rom[addr & 0x7FFF];
            //log_debug("mc68000: read %08X as high ROM -> %02X", addr, data);
            return data;
        }
        else if (top == 0xFFFE0000) {
            uint8_t data = tube_parasite_read(addr);
            //log_debug("mc68000: read %08X as I/O -> %02X (%d cycles left)", addr, data, m68k_cycles_remaining());
            return data;
        }
    }
    log_debug("mc68000: read %08X unmapped", addr);
    return 0xff;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    uint32_t data = readmem(address);
    if (mc68000_debug_enabled)
        debug_memread(&mc68000_cpu_debug, address, data, 1);
    return data;
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return readmem(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    uint32_t data = (readmem(address) << 8) | readmem(address+1);
    if (mc68000_debug_enabled)
        debug_memread(&mc68000_cpu_debug, address, data, 2);
    return data;
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return (readmem(address) << 8) | readmem(address+1);
}

unsigned int  m68k_read_memory_32(unsigned int address)
{
    uint32_t data = (readmem(address) << 24) | (readmem(address+1) << 16) | (readmem(address+2) << 8) | readmem(address+3);
    if (mc68000_debug_enabled)
        debug_memread(&mc68000_cpu_debug, address, data, 4);
    return data;
}

unsigned int m68k_read_disassembler_32 (unsigned int address)
{
    return (readmem(address) << 24) | (readmem(address+1) << 16) | (readmem(address+2) << 8) | readmem(address+3);
}

static void writemem(uint32_t addr, uint8_t data)
{
    if (addr < MC68000_RAM_SIZE) {
        //log_debug("mc68000: write %08X as RAM <- %02X", addr, data);
        mc68000_ram[addr] = data;
    }
    else {
        uint32_t top = addr & 0xFFFF0000;
        if (top == 0xfffe0000) {
            //log_debug("mc68000: write %09X as I/O <- %02X", addr, data);
            tube_parasite_write(addr, data);
        }
        else if (top == 0xffff0000)
            log_debug("mc68000: write %08X as ROM (ignored) <- %02X", addr, data);
        else
            log_debug("mc68000: write %08X as unmapped (ignored) <- %02X", addr, data);
    }
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (mc68000_debug_enabled)
        debug_memwrite(&mc68000_cpu_debug, address, value, 1);
    writemem(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (mc68000_debug_enabled)
        debug_memwrite(&mc68000_cpu_debug, address, value, 2);
    writemem(address, value >> 8);
    writemem(address+1, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (mc68000_debug_enabled)
        debug_memwrite(&mc68000_cpu_debug, address, value, 4);
    writemem(address, value >> 24);
    writemem(address+1, value >> 16);
    writemem(address+2, value >> 8);
    writemem(address+3, value);
}

static void mc6809nc_exec(void)
{
    m68k_execute(tubecycles);
    tubecycles = 0;
}

void tube_68000_rst(void)
{
    rom_low = true;
    m68k_pulse_reset();
}

static void mc68000_savestate(ZFILE *zfp)
{
    int bytes = m68k_context_size();
    void *buf = malloc(bytes);
    if (buf) {
        m68k_get_context(buf);
        savestate_zwrite(zfp, buf, bytes);
        free(buf);
        savestate_zwrite(zfp, mc68000_ram, MC68000_RAM_SIZE);
        savestate_zwrite(zfp, mc68000_rom, MC68000_ROM_SIZE);
    }
    else
        log_warn("mc68000: out of memory trying to save 68000 state");
}

static void mc68000_loadstate(ZFILE *zfp)
{
    int bytes = m68k_context_size();
    void *buf = malloc(bytes);
    if (buf) {
        savestate_zread(zfp, buf, bytes);
        m68k_set_context(buf);
        free(buf);
        savestate_zread(zfp, mc68000_ram, MC68000_RAM_SIZE);
        savestate_zread(zfp, mc68000_rom, MC68000_ROM_SIZE);
    }
    else
        log_warn("mc68000: out of memory trying to load 68000 state");
}

bool tube_68000_init(void *rom)
{
    log_debug("mc68000: init");
    if (!mc68000_ram) {
        mc68000_ram = malloc(MC68000_RAM_SIZE);
        if (!mc68000_ram) {
            log_error("mc68000: unable to allocate RAM: %s", strerror(errno));
            return false;
        }
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68020);
    }
    mc68000_rom = rom;
    tube_type = TUBE68000;
    tube_readmem = readmem;
    tube_writemem = writemem;
    tube_exec  = mc6809nc_exec;
    tube_proc_savestate = mc68000_savestate;
    tube_proc_loadstate = mc68000_loadstate;
    rom_low = true;
    m68k_pulse_reset();
    return true;
}

static int dbg_debug_enable(int newvalue)
{
    int oldvalue = mc68000_debug_enabled;
    mc68000_debug_enabled = newvalue;
    return oldvalue;
}

static uint32_t dbg_readmem(uint32_t addr)
{
    return readmem(addr);
}

static void dbg_writemem(uint32_t addr, uint32_t value)
{
    writemem(addr, value);
}

static uint32_t dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize)
{
    char *ptr = buf + snprintf(buf, bufsize, "%08X: ", addr);
    unsigned isize = m68k_disassemble(ptr, bufsize-10, addr, M68K_CPU_TYPE_68020);
    return addr + isize;
}

static const char *reg_names[] = {
    "D0",       /* Data registers */
    "D1",
    "D2",
    "D3",
    "D4",
    "D5",
    "D6",
    "D7",
    "A0",       /* Address registers */
    "A1",
    "A2",
    "A3",
    "A4",
    "A5",
    "A6",
    "A7",
    "PC",       /* Program Counter */
    "SR",       /* Status Register */
    "SP",       /* The current Stack Pointer (located in A7) */
    "USP",      /* User Stack Pointer */
    "ISP",      /* Interrupt Stack Pointer */
    "MSP",      /* Master Stack Pointer */
    "SFC",      /* Source Function Code */
    "DFC2",     /* Destination Function Code */
    "VBR",      /* Vector Base Register */
    "CACR",     /* Cache Control Register */
    "CAAR",     /* Cache Address Register */
    NULL
};

static uint32_t dbg_reg_get(int which)
{
    return m68k_get_reg(NULL, which);
}

static void dbg_reg_set(int which, uint32_t value)
{
    return m68k_set_reg(which, value);
}

static size_t dbg_reg_print(int which, char *buf, size_t bufsize)
{
    return snprintf(buf, bufsize, "%08X", m68k_get_reg(NULL, which));
}

static void dbg_reg_parse(int which, const char *strval)
{
    return m68k_set_reg(which, strtoul(strval, NULL, 16));
}

static uint32_t dbg_get_instr_addr(void)
{
    return m68k_get_reg(NULL, M68K_REG_PPC);
}

cpu_debug_t mc68000_cpu_debug = {
    .cpu_name       = "MC68000",
    .debug_enable   = dbg_debug_enable,
    .memread        = dbg_readmem,
    .memwrite       = dbg_writemem,
    .disassemble    = dbg_disassemble,
    .reg_names      = reg_names,
    .reg_get        = dbg_reg_get,
    .reg_set        = dbg_reg_set,
    .reg_print      = dbg_reg_print,
    .reg_parse      = dbg_reg_parse,
    .get_instr_addr = dbg_get_instr_addr,
    .print_addr     = debug_print_addr32,
    .parse_addr     = debug_parse_addr
};

void mc68000_cpu_preexec(unsigned pc)
{
    if (mc68000_debug_enabled)
        debug_preexec(&mc68000_cpu_debug, pc);
}
