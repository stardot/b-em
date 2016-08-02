/*B-em v2.2 by Tom Walker
  Serial ULA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "serial.h"
#include "acia.h"
#include "tape.h"
#include "tapenoise.h"

int motor, acia_is_tape;

static uint8_t serial_reg;
static uint8_t serial_transmit_rate, serial_recive_rate;

void serial_reset()
{
        /*Dunno what happens to this on reset*/
        serial_reg = serial_transmit_rate = serial_recive_rate=0;
        motor=0;
}

void serial_write(uint16_t addr, uint8_t val)
{
        serial_reg = val;
        serial_transmit_rate = val & 0x7;
        serial_recive_rate = (val >> 3) & 0x7;
        if (motor != (val & 0x80))
           tapenoise_motorchange(val>>7);
        motor = (val & 0x80) && tape_loaded;
        if (val & 0x40)
        {
                /*RS423*/
                acia_sr &= ~12; /*Clear acia DCD and CTS*/
		acia_is_tape = 0;
        }
        else
        {
                /*Tape*/
                acia_sr &= ~8; /*Clear acia CTS*/
		acia_is_tape = 1;
        }
}


uint8_t serial_read(uint16_t addr)
{
        /*Reading from this has the same effect as writing &FE*/
        serial_write(0, 0xFE);
        return 0;
}

void serial_savestate(FILE *f)
{
        putc(serial_reg, f);
}

void serial_loadstate(FILE *f)
{
        serial_write(0, getc(f));
}
