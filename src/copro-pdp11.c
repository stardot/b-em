/*
 * PDP11 Co Pro Emulation
 *
 * (c) 2018 David Banks
 */
#include "b-em.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pdp11/pdp11.h"
#include "copro-pdp11.h"
#include "tube.h"

#ifdef INCLUDE_DEBUGGER
#include "cpu_debug.h"
#include "pdp11/pdp11_debug.h"
#endif

static uint8_t *memory;

static uint8_t read_byte(const uint32_t addr)
{
    if ((addr & 0xFFF0) == 0xFFF0)
        return tube_parasite_read((addr >> 1) & 7);
    else
        return *(memory + addr);
}

uint8_t copro_pdp11_read8(const uint16_t addr)
{
    uint8_t data = read_byte(addr);
    if (pdp11_debug_enabled)
        debug_memread(&pdp11_cpu_debug, addr, data, 1);
    return data;
}

uint16_t copro_pdp11_read16(const uint16_t addr)
{
    uint16_t data = read_byte(addr);
    data |= read_byte(addr+1) << 8;
    if (pdp11_debug_enabled)
        debug_memread(&pdp11_cpu_debug, addr, data, 2);
    return data;
}

static void write_byte(const uint32_t addr, const uint8_t data)
{
    if ((addr & 0xFFF0) == 0xFFF0)
        tube_parasite_write((addr >> 1) & 7, data);
    else if (addr < 0xF800 || addr > 0xF800+0x800)
        *(memory + addr) = data;
    else
        log_debug("copro-pdp11: attempt to write to ROM at %0X", addr);
}

void copro_pdp11_write8(const uint16_t addr, const uint8_t data)
{
    if (pdp11_debug_enabled)
        debug_memwrite(&pdp11_cpu_debug, addr, data, 1);
    write_byte(addr, data);
}

void copro_pdp11_write16(const uint16_t addr, const uint16_t data)
{
    if (pdp11_debug_enabled)
        debug_memwrite(&pdp11_cpu_debug, addr, data, 2);
    write_byte(addr, data & 0xff);
    write_byte(addr+1, data >> 8);
}

bool tube_pdp11_init(void *rom)
{
    if (!memory) {
        memory = malloc(2*1024*1024);
        if (!memory) {
            log_error("copro-pdp11: unable to allocate RAM");
            return false;
        }
    }
    memcpy(memory + 0xF800, rom, 0x800);

    tube_readmem  = read_byte;
    tube_writemem = write_byte;
    tube_exec = pdp11_execute;
    tube_proc_savestate = NULL;
    tube_proc_loadstate = NULL;
    tube_type = TUBEPDP11;

    pdp11_reset(0xf800);
    return true;
}

void copro_pdp11_rst(void)
{
    pdp11_reset(0xF800);
}
