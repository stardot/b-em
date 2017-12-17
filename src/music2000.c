#include "music2000.h"
#include "acia.h"

static void send_data(ACIA *acia, uint8_t data) {
    log_debug("music2000: send byte %02X", data);
    midi_send_byte(acia->udata, data);
}

static ACIA music2000_acia1 = {
    .tx_hook = send_data,
};

static ACIA music2000_acia2 = {
    .tx_hook = send_data,
};

static ACIA music2000_acia3 = {
    .tx_hook = send_data,
};

uint8_t music2000_read(uint32_t addr) {
    switch(addr & 0xe) {
        case 0x8:
            return acia_read(&music2000_acia1, addr);
        case 0xA:
            return acia_read(&music2000_acia2, addr);
        case 0xC:
            return acia_read(&music2000_acia3, addr);
        default:
            return 0xff;
    }
}

void music2000_write(uint32_t addr, uint8_t val) {
    switch(addr & 0xe) {
        case 0x8:
            acia_write(&music2000_acia1, addr, val);
            break;
        case 0xA:
            acia_write(&music2000_acia2, addr, val);
            break;
        case 0xC:
            acia_write(&music2000_acia3, addr, val);
            break;
    }
}

void music2000_poll(void) {
    acia_poll(&music2000_acia1);
    acia_poll(&music2000_acia2);
    acia_poll(&music2000_acia3);
}

void music2000_init(midi_dev_t *out1, midi_dev_t *out2, midi_dev_t *out3) {
    music2000_acia1.udata = out1;
    music2000_acia2.udata = out2;
    music2000_acia3.udata = out3;
}    
