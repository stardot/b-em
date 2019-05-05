#ifndef __INC_VIDEO_H
#define __INC_VIDEO_H

#define CLOCKS_PER_FRAME 80000

/*CRTC (6845)*/
void    crtc_reset(void);
void    crtc_write(uint16_t addr, uint8_t val);
uint8_t crtc_read(uint16_t addr);
void    crtc_latchpen(void);
void    crtc_savestate(FILE *f);
void    crtc_loadstate(FILE *f);

extern uint8_t crtc[32];
extern int crtc_i;

extern int hc, vc, sc;
extern uint16_t ma;

/*Video ULA (VIDPROC)*/
void videoula_write(uint16_t addr, uint8_t val);
void videoula_savestate(FILE *f);
void videoula_loadstate(FILE *f);

extern uint8_t ula_ctrl;
extern uint8_t ula_palbak[16];
extern int nula_collook[16];
extern uint8_t nula_flash[8];

extern uint8_t nula_palette_mode;
extern uint8_t nula_horizontal_offset;
extern uint8_t nula_left_blank;
extern uint8_t nula_disable;
extern uint8_t nula_attribute_mode;
extern uint8_t nula_attribute_text;

ALLEGRO_DISPLAY *video_init(void);
void video_reset(void);
void video_poll(int clocks, int timer_enable);
void video_savestate(FILE *f);
void video_loadstate(FILE *f);

void nula_default_palette(void);

extern uint16_t vidbank;

void mode7_makechars(void);
extern int interlline;

#endif
