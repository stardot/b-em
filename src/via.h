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
        
        uint8_t  (*read_portA)();
        uint8_t  (*read_portB)();
        void     (*write_portA)(uint8_t val);
        void     (*write_portB)(uint8_t val);
        
        void     (*set_ca1)(int level);
        void     (*set_ca2)(int level);
        void     (*set_cb1)(int level);
        void     (*set_cb2)(int level);
} VIA;

uint8_t via_read(VIA *v, uint16_t addr);
void    via_write(VIA *v, uint16_t addr, uint8_t val);
void    via_reset(VIA *v);
void    via_updatetimers(VIA *v);

void via_set_ca1(VIA *v, int level);
void via_set_ca2(VIA *v, int level);
void via_set_cb1(VIA *v, int level);
void via_set_cb2(VIA *v, int level);

void via_savestate(VIA *v, FILE *f);
void via_loadstate(VIA *v, FILE *f);
