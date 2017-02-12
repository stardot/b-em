/*CRTC (6845)*/
void    crtc_reset();
void    crtc_write(uint16_t addr, uint8_t val);
uint8_t crtc_read(uint16_t addr);
void    crtc_latchpen();
void    crtc_savestate(FILE *f);
void    crtc_loadstate(FILE *f);

extern uint8_t crtc[32];
extern int crtc_i;

extern int hc, vc, sc, vadj;
extern uint16_t ma, maback;


/*Video ULA (VIDPROC)*/
void videoula_write(uint16_t addr, uint8_t val);
void videoula_savestate(FILE *f);
void videoula_loadstate(FILE *f);

extern uint8_t ula_ctrl;
extern uint8_t ula_palbak[16];


void video_init();
void video_reset();
void video_poll(int clocks);
void video_savestate(FILE *f);
void video_loadstate(FILE *f);


extern uint16_t vidbank;

void mode7_makechars();
extern int interlline;
