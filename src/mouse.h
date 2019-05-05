#ifndef __INC_MOUSE_H
#define __INC_MOUSE_H

extern void mouse_axes(ALLEGRO_EVENT *event);
extern void mouse_btn_down(ALLEGRO_EVENT *event);
extern void mouse_btn_up(ALLEGRO_EVENT *event);

void mouse_poll(void);

extern int mcount;
extern uint8_t mouse_portb;
extern bool mouse_amx;

#endif
