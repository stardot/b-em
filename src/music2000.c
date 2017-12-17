#include "music2000.h"
#include "acia.h"

static void set_params(ACIA *acia, uint8_t val) {
    log_debug("music2000: set params: %s to %02X", (char *)acia->udata, val);
}

static void send_data(ACIA *acia, uint8_t data) {
    log_debug("music2000: send to %s: %02X", (char *)acia->udata, data);
}

ACIA music2000_acia1 = {
    .set_params = set_params,
    .tx_hook = send_data,
    .udata   = "Music 2000 1"
};

ACIA music2000_acia2 = {
    .set_params = set_params,
    .tx_hook = send_data,
    .udata   = "Music 2000 2"
};

ACIA music2000_acia3 = {
    .set_params = set_params,
    .tx_hook = send_data,
    .udata   = "Music 2000 3"
};

uint8_t music2000_read(uint32_t addr) {
    uint8_t val;

    switch(addr & 0xe) {
        case 0x8:
            val = acia_read(&music2000_acia1, addr);
            //log_debug("music2000: read Music 2000 1: %04X, %02X", addr, val);
            break;
        case 0xA:
            val = acia_read(&music2000_acia2, addr);
            //log_debug("music2000: read Music 2000 2: %04X, %02X", addr, val);
            break;
        case 0xC:
            val = acia_read(&music2000_acia3, addr);
            //log_debug("music2000: read Music 2000 3: %04X, %02X", addr, val);
            break;
        default:
            val = 0xff;
    }
    return val;
}

void music2000_write(uint32_t addr, uint8_t val) {
    switch(addr & 0xe) {
        case 0x8:
            log_debug("music2000: write Music 2000 1: %04X, %02X", addr, val);
            acia_write(&music2000_acia1, addr, val);
            break;
        case 0xA:
            log_debug("music2000: write Music 2000 2: %04X, %02X", addr, val);
            acia_write(&music2000_acia2, addr, val);
            break;
        case 0xC:
            log_debug("music2000: write Music 2000 3: %04X, %02X", addr, val);
            acia_write(&music2000_acia3, addr, val);
            break;
    }
}
