/*B-em v2.2 by Tom Walker
  Serial ULA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "led.h"
#include "serial.h"
#include "sysacia.h"
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
        led_update(LED_CASSETTE_MOTOR, false, 0);
}

void serial_write(uint16_t addr, uint8_t val)
{
        serial_reg = val;
        serial_transmit_rate = val & 0x7;
        serial_recive_rate = (val >> 3) & 0x7;
        int new_motor = val & 0x80;
        if (new_motor && !motor) {
            log_debug("serial: cassette motor on");
            tapenoise_motorchange(1);
            led_update(LED_CASSETTE_MOTOR, 1, 0);
            motor = tape_loaded;
        }
        else if (!new_motor) {
            log_debug("serial: cassette motor off");
            tapenoise_motorchange(0);
            motor = 0;
            tapeledcount = 2;
        }
        if (val & 0x40)
        {
            /*RS423*/
            sysacia.status_reg &= ~12; /*Clear acia DCD and CTS*/
            acia_is_tape = 0;
        }
        else
        {
            /*Tape*/
            sysacia.status_reg &= ~8; /*Clear acia CTS*/
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
