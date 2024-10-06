/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"

/* Status register flags */

#define RXD_REG_FUL 0x01
#define TXD_REG_EMP 0x02
#define DCD         0x04
#define CTS         0x08
#define FRAME_ERR   0x10
#define RX_OVERRUN  0x20
#define PARITY_ERR  0x40
#define INTERUPT    0x80

static inline int rx_int(ACIA *acia) {
    return (acia->status_reg & INTERUPT) && (acia->control_reg & INTERUPT);
}

static inline int tx_int(ACIA *acia) {
    return (acia->status_reg & TXD_REG_EMP) && ((acia->control_reg & 0x60) == 0x20);
}

static void acia_updateint(ACIA *acia) {
    if (rx_int(acia) || tx_int(acia)) {
        log_debug("acia: %s, interrupt asserted", acia->name);
        interrupt |= acia->intnum;
    }
    else {
        log_debug("acia: %s, interrupt de-asserted", acia->name);
        interrupt &= ~acia->intnum;
    }
}

uint8_t acia_read(ACIA *acia, uint16_t addr) {
    uint8_t temp;

    if (addr & 1) {
        temp = acia->rx_data_reg;
        acia->status_reg &= ~(INTERUPT | RXD_REG_FUL);
        acia_updateint(acia);
        return temp;
    }
    else {
        temp = acia->status_reg & ~INTERUPT;
        if (rx_int(acia) || tx_int(acia))
            temp |= INTERUPT;
        return temp;
    }
}

void acia_write(ACIA *acia, uint16_t addr, uint8_t val) {
    if (addr & 1) {
        acia->tx_data_reg = val;
        if (acia->tx_hook)
            acia->tx_hook(acia, val);
        acia->status_reg &= ~TXD_REG_EMP;
        acia_updateint(acia);
    }
    else if (val != acia->control_reg) {
        if ((val & 0x60) != 0x20) // interupt being turned off
            if (acia->tx_end)
                acia->tx_end(acia);
        acia->control_reg = val;
        if (val == 3) {
            log_debug("acia: %s, master reset", acia->name);
            acia->status_reg &= ~(CTS|DCD);
        }
        if (acia->set_params)
            acia->set_params(acia, val);
        acia_updateint(acia);
    }
}

void acia_dcdhigh(ACIA *acia) {
    if (acia->status_reg & DCD)
        return;
    acia->status_reg |= DCD | INTERUPT;
    acia_updateint(acia);
}

void acia_dcdlow(ACIA *acia) {
    acia->status_reg &= ~DCD;
    acia_updateint(acia);
}

void acia_ctson(ACIA *acia)
{
    if (!(acia->status_reg & CTS)) {
        acia->status_reg |= CTS|TXD_REG_EMP;
        acia_updateint(acia);
    }
}

void acia_ctsoff(ACIA *acia)
{
    if (acia->status_reg & CTS) {
        acia->status_reg &= ~(CTS|TXD_REG_EMP);
        acia_updateint(acia);
    }
}

void acia_poll(ACIA *acia)
{
    if ((acia->status_reg & (TXD_REG_EMP|CTS)) == CTS) {
        log_debug("acia: %s, setting TXDR empty flag", acia->name);
        acia->status_reg |= TXD_REG_EMP;
        acia_updateint(acia);
    }
}

void acia_receive(ACIA *acia, uint8_t val) { /*Called when the acia recives some data*/
    acia->rx_data_reg = val;
    acia->status_reg |= RXD_REG_FUL | INTERUPT;
    if (acia->rx_hook)
        acia->rx_hook(acia, val);
    acia_updateint(acia);
}

void acia_savestate(ACIA *acia, FILE *f)
{
    unsigned char bytes[2];
    bytes[0] = acia->control_reg;
    bytes[1] = acia->status_reg;
    fwrite(bytes, sizeof(bytes), 1, f);
}

void acia_loadstate(ACIA *acia, FILE *f)
{
    unsigned char bytes[2];
    fread(bytes, sizeof(bytes), 1, f);
    acia->control_reg = bytes[0];
    acia->status_reg = bytes[1];
}
