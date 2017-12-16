/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"

#define DCD     4
#define RECIEVE 1

void acia_updateint(ACIA *acia) {
    if (((acia->status_reg & 0x80) && (acia->control_reg & 0x80)) || (!(acia->status_reg & 0x02) && ((acia->control_reg & 0x60) == 0x20)))
       interrupt|=4;
    else
       interrupt&=~4;
}

void acia_reset(ACIA *acia) {
    acia->status_reg = (acia->status_reg & 8) | 6;
    acia_updateint(acia);
}

uint8_t acia_read(ACIA *acia, uint16_t addr) {
    uint8_t temp;

    if (addr & 1) {
        temp = acia->rx_data_reg;
        acia->status_reg &= ~0x81;
        acia_updateint(acia);
        return temp;
    }
    else
        return (acia->status_reg & 0x7F) | (acia->status_reg & acia->control_reg & 0x80);
}

void acia_write(ACIA *acia, uint16_t addr, uint8_t val) {
    if (addr & 1) {
        acia->tx_data_reg = val;
        if (acia->tx_hook)
            acia->tx_hook(acia, val);
    }
    else if (val != acia->control_reg) {
        if (!(val & 0x40)) // interupts disabled as serial TX buffer empties.
            if (acia->tx_end)
                acia->tx_end(acia);
        acia->control_reg = val;
        if (val == 3)
            acia_reset(acia);
        acia->set_params(acia, val);
    }
}

void acia_dcdhigh(ACIA *acia) {
    if (acia->status_reg & DCD)
        return;
    acia->status_reg |= DCD | 0x80;
    acia_updateint(acia);
}

void acia_dcdlow(ACIA *acia) {
    acia->status_reg &= ~DCD;
    acia_updateint(acia);
}

void acia_receive(ACIA *acia, uint8_t val) { /*Called when the acia recives some data*/
    acia->rx_data_reg = val;
    acia->status_reg |= RECIEVE | 0x80;
    if (acia->rx_hook)
        acia->rx_hook(acia, val);
    acia_updateint(acia);
}

void acia_savestate(ACIA *acia, FILE *f) {
    putc(acia->control_reg, f);
    putc(acia->status_reg, f);
}

void acia_loadstate(ACIA *acia, FILE *f) {
    acia->control_reg = getc(f);
    acia->status_reg = getc(f);
}
