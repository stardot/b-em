#ifndef __INC_ACIA_H
#define __INC_ACIA_H

typedef struct acia ACIA;

struct acia {
    uint8_t control_reg;
    uint8_t status_reg;
    uint8_t rx_data_reg;
    uint8_t tx_data_reg;
    void (*set_params)(ACIA *acia, uint8_t val);
    void (*rx_hook)(ACIA *acia, uint8_t byte);
    void (*tx_hook)(ACIA *acia, uint8_t byte);
    void (*tx_end)(ACIA *acia);
};

void acia_reset(ACIA *acia);
uint8_t acia_read(ACIA *acia, uint16_t addr);
void acia_write(ACIA *acia, uint16_t addr, uint8_t val);
void acia_poll(ACIA *acia);
void acia_receive(ACIA *acia, uint8_t val);

void acia_savestate(ACIA *acia, FILE *f);
void acia_loadstate(ACIA *acia, FILE *f);

void acia_dcdhigh(ACIA *acia);
void acia_dcdlow(ACIA *acia);

#endif
