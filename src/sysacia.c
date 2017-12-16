/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "csw.h"
#include "uef.h"
#include "tapenoise.h"

#define DCD     4
#define RECIEVE 1

int sysacia_tapespeed=0;
static uint16_t newdat;

static void sysvia_set_params(ACIA *acia, uint8_t val) {
    switch (val & 3) {
        case 1: sysacia_tapespeed=0; break;
        case 2: sysacia_tapespeed=1; break;
    }
}

static void sysacia_rx_hook(ACIA *acia, uint8_t data) {
    newdat = data | 0x100;
}

static void sysacia_tx_hook(ACIA *acia, uint8_t data) {
    putchar(data);
}

static void sysacia_tx_end(ACIA *acia) {
    fflush(stdout);
}

ACIA sysacia = {
    .set_params = sysvia_set_params,
    .rx_hook    = sysacia_rx_hook,
    .tx_hook    = sysacia_tx_hook,
    .tx_end     = sysacia_tx_end
};

/*Every 128 clocks, ie 15.625khz*/
/*Div by 13 gives roughly 1200hz*/

extern int ueftoneon,cswtoneon;

void sysacia_poll() {
    if (motor) {
        startblit();
        if (csw_ena) csw_poll();
        else         uef_poll();
        endblit();
    
        if (newdat & 0x100) {
            newdat&=0xFF;
            tapenoise_adddat(newdat);
        }
        else if (csw_toneon || uef_toneon)
            tapenoise_addhigh();
    }
}
