/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "tape.h"

int sysacia_tapespeed=0;

static void sysvia_set_params(ACIA *acia, uint8_t val) {
    switch (val & 3) {
        case 1: sysacia_tapespeed=0; break;
        case 2: sysacia_tapespeed=1; break;
    }
}

static void sysacia_tx_hook(ACIA *acia, uint8_t data) {
    putchar(data);
}

static void sysacia_tx_end(ACIA *acia) {
    fflush(stdout);
}

ACIA sysacia = {
    .set_params = sysvia_set_params,
    .rx_hook    = tape_receive,
    .tx_hook    = sysacia_tx_hook,
    .tx_end     = sysacia_tx_end
};
