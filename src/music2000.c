#include "music2000.h"
#include "acia.h"

typedef enum {
    MS_GROUND,
    MS_ONE_OF_ONE,
    MS_ONE_OF_TWO,
    MS_TWO_OF_TWO,
    MS_SYSEX
} midi_state_t;

typedef struct {
    midi_dev_t   *dev;
    midi_state_t state;
    uint8_t buffer[4];
    uint8_t rtmsg;
} m2000_dev_t;

m2000_dev_t m2000_out1;
m2000_dev_t m2000_out2;
m2000_dev_t m2000_out3;

static void acia_tx(ACIA *acia, uint8_t data) {
    m2000_dev_t *m2000 = acia->udata;

    if (data & 0x80) {               // status byte
        switch(data >> 4) {
            case 0x8: // note off
            case 0x9: // note on
            case 0xa: // polyphonic pressure
            case 0xb: // control change
            case 0xe: // pitch bend
                m2000->buffer[0] = data;
                m2000->state = MS_ONE_OF_TWO;
                break;
            case 0xc: // program change
            case 0xd: // channel pressure
                m2000->buffer[0] = data;
                m2000->state = MS_ONE_OF_ONE;
                break;
            case 0xf:
                if (data & 0x8) { // realtime message.
                    m2000->rtmsg = data;
                    midi_send_msg(m2000->dev, &m2000->rtmsg, 1);
                } else {
                    m2000->buffer[0] = data;
                    switch (data & 0x0f) {
                        case 0xf1: // time code quarter frame
                        case 0xf3: // song select
                            m2000->state = MS_ONE_OF_ONE;
                            break;
                        case 0xf2: // song position
                            m2000->state = MS_ONE_OF_TWO;
                            break;
                        case 0xf4: // undefined.
                        case 0xf5: // undefined.
                        case 0xf6: // tune request
                            midi_send_msg(m2000->dev, m2000->buffer, 1);
                            break;
                        case 0xf0:
                            m2000->state = MS_SYSEX;
                            break;
                        case 0xf7:
                            m2000->state = MS_GROUND;
                    }
                }
        }
    } else {
        switch(m2000->state) {
            case MS_GROUND: // data with no previous status.
            case MS_SYSEX:  // SysEx also ignore for now.
                break;
            case MS_ONE_OF_ONE:
                m2000->buffer[1] = data;
                midi_send_msg(m2000->dev, m2000->buffer, 2);
                break;
            case MS_ONE_OF_TWO:
                m2000->buffer[1] = data;
                m2000->state = MS_TWO_OF_TWO;
                break;
            case MS_TWO_OF_TWO:
                m2000->buffer[2] = data;
                midi_send_msg(m2000->dev, m2000->buffer, 3);
                m2000->state = MS_ONE_OF_TWO;
        }
    }
}

static ACIA music2000_acia1 = {
    .tx_hook = acia_tx,
    .udata   = &m2000_out1,
    .intnum  = 0x100
};

static ACIA music2000_acia2 = {
    .tx_hook = acia_tx,
    .udata   = &m2000_out2,
    .intnum  = 0x200
};

static ACIA music2000_acia3 = {
    .tx_hook = acia_tx,
    .udata   = &m2000_out3,
    .intnum  = 0x300
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
    m2000_out1.dev = out1;
    m2000_out2.dev = out2;
    m2000_out3.dev = out3;
}    
