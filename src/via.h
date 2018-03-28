#ifndef __INC_VIA_H
#define __INC_VIA_H

typedef struct VIA
{
        uint8_t  ora,   orb,   ira,   irb;
        uint8_t  ddra,  ddrb;
        uint8_t  sr;
        uint32_t t1l,   t2l;
        int      t1c,   t2c;
        uint8_t  acr,   pcr,   ifr,   ier;
        int      t1hit, t2hit;
        int      ca1,   ca2,   cb1,   cb2;
        int      intnum;
        int      sr_count;

        uint8_t  (*read_portA)(void);
        uint8_t  (*read_portB)(void);
        void     (*write_portA)(uint8_t val);
        void     (*write_portB)(uint8_t val);

        void     (*set_ca1)(int level);
        void     (*set_ca2)(int level);
        void     (*set_cb1)(int level);
        void     (*set_cb2)(int level);
        void     (*timer_expire1)(void);
} VIA;

uint8_t via_read(VIA *v, uint16_t addr);
void    via_write(VIA *v, uint16_t addr, uint8_t val);
void    via_reset(VIA *v);
void    via_updatetimers(VIA *v);
void    via_shift(VIA *v, int cycles);

void via_set_ca1(VIA *v, int level);
void via_set_ca2(VIA *v, int level);
void via_set_cb1(VIA *v, int level);
void via_set_cb2(VIA *v, int level);

void via_savestate(VIA *v, FILE *f);
void via_loadstate(VIA *v, FILE *f);

static inline void via_poll(VIA *v, int cycles) {
    v->t1c -= cycles;
    if (!(v->acr & 0x20))
        v->t2c -= cycles;
    if (v->t1c < -3 || v->t2c < -3)
        via_updatetimers(v);
    if (v->acr & 0x1c)
        via_shift(v, cycles);
}

#endif
