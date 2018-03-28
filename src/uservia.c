/*B-em v2.1 by Tom Walker
  User VIA + Master 512 mouse emulation*/

#include <stdio.h>
#include "b-em.h"
#include "via.h"
#include "uservia.h"
#include "model.h"
#include "compact_joystick.h"
#include "mouse.h"
#include "music4000.h"
#include "sound.h"

VIA uservia;

uint8_t lpt_dac;
ALLEGRO_USTR *prt_clip_str;

void uservia_set_ca1(int level)
{
        via_set_ca1(&uservia, level);
}
void uservia_set_ca2(int level)
{
        via_set_ca2(&uservia, level);
}
void uservia_set_cb1(int level)
{
        via_set_cb1(&uservia, level);
}
void uservia_set_cb2(int level)
{
        via_set_cb2(&uservia, level);
}

void uservia_write_portA(uint8_t val)
{
    if (prt_clip_str) {
        // Print to clipboard.
        if (val == 0x60)
            val = 0xa3; // pound sign.
        al_ustr_append_chr(prt_clip_str, val);
        via_set_ca1(&uservia, 1);
        log_debug("uservia: set CA1 low for printer");
    }
    else
        lpt_dac = val; /*Printer port - no printer, just 8-bit DAC*/
}

void printer_set_ca2(int level)
{
    if (level && prt_clip_str) {
        via_set_ca1(&uservia, 0);
        log_debug("uservia: set CA1 high for printer");
    }
}

void uservia_write_portB(uint8_t val)
{
    /*User port - nothing emulated*/
    log_debug("uservia_write_portB: %02X", val);
}

uint8_t uservia_read_portA()
{
        return 0xff; /*Printer port - read only*/
}

uint8_t uservia_read_portB()
{
    if (curtube == 3 || mouse_amx)
        return mouse_portb;
    if (compactcmos)
        return compact_joystick_read();
    if (sound_music5000)
        return music4000_read();
    return 0xff; /*User port - nothing emulated*/
}

void uservia_write(uint16_t addr, uint8_t val)
{
    via_write(&uservia, addr, val);
}

uint8_t uservia_read(uint16_t addr)
{
    return via_read(&uservia, addr);
}

void uservia_reset()
{
        via_reset(&uservia);

        uservia.read_portA = uservia_read_portA;
        uservia.read_portB = uservia_read_portB;

        uservia.write_portA = uservia_write_portA;
        uservia.write_portB = uservia_write_portB;

        uservia.set_ca2 = printer_set_ca2;
        uservia.set_cb2 = music4000_shift;

        uservia.intnum = 2;
}

void dumpuservia()
{
        log_debug("T1 = %04X %04X T2 = %04X %04X\n",uservia.t1c,uservia.t1l,uservia.t2c,uservia.t2l);
        log_debug("%02X %02X  %02X %02X\n",uservia.ifr,uservia.ier,uservia.pcr,uservia.acr);
}

void uservia_savestate(FILE *f)
{
        via_savestate(&uservia, f);
}

void uservia_loadstate(FILE *f)
{
        via_loadstate(&uservia, f);
}
