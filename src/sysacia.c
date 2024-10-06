/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "tape.h"

int sysacia_tapespeed=0;
FILE *sysacia_fp = NULL;

static void sysvia_set_params(ACIA *acia, uint8_t val) {
    switch (val & 3) {
        case 1: sysacia_tapespeed=0; break;
        case 2: sysacia_tapespeed=1; break;
    }
}

static void sysacia_tx_hook(ACIA *acia, uint8_t data)
{
    if (sysacia_fp)
        putc(data, sysacia_fp);
}

static void sysacia_tx_end(ACIA *acia)
{
    if (sysacia_fp)
        fflush(sysacia_fp);
}

ACIA sysacia = {
    .set_params = sysvia_set_params,
    .rx_hook    = tape_receive,
    .tx_hook    = sysacia_tx_hook,
    .tx_end     = sysacia_tx_end,
    .intnum     = 0x04
};

void sysacia_rec_stop(void)
{
    if (sysacia_fp) {
        fclose(sysacia_fp);
        sysacia_fp = NULL;
        acia_ctsoff(&sysacia);
    }
}

FILE *sysacia_rec_start(const char *filename)
{
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        sysacia_fp = fp;
        acia_ctson(&sysacia);
    }
    else
        log_error("unable to open %s for writing: %s", filename, strerror(errno));
    return fp;
}
