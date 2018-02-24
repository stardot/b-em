#include "b-em.h"

#include "mouse.h"
#include "mem.h"
#include "model.h"
#include "via.h"
#include "uservia.h"

int mouse_amx;

void mouse_axes(ALLEGRO_EVENT *event) {} 
void mouse_btn_down(ALLEGRO_EVENT *event) {}
void mouse_btn_up(ALLEGRO_EVENT *event) {}
void mouse_poll(void) {};

#if 0
int mcount = 8;
uint8_t mouse_portb = 0;

static int mx = 0,  my = 0;

static int mouse_ff = 0;

static int mouse_xff = 0, mouse_yff = 0;

#ifndef WIN32
static int mouse_ox = 0, mouse_oy = 0;
#endif

void mouse_poll()
{
        int dx, dy;

	poll_mouse();

        if (curtube == 3)
        {
                if (uservia.ifr & 0x18) return;
#ifdef WIN32
                get_mouse_mickeys(&dx,&dy);
#else
		//dx = (mouse_x - mouse_ox);
		//dy = (mouse_y - mouse_oy);
                if (dx > 0 || dy > 0)
                    log_debug("mouse: tube, dx=%d, dy=%d", dx, dy);
		mouse_ox = mouse_x;
		mouse_oy = mouse_y;
#endif
                mx += dx;
                my += dy;

                if (mx)
                {
                        if (mx > 0) mouse_portb |=  8;
                        else        mouse_portb &= ~8;
                        
                        if (mouse_xff) mouse_portb ^= 8;

                        if (mx > 0) mx--;
                        else        mx++;

                        uservia_set_cb1(mouse_xff);
                        mouse_xff = !mouse_xff;
                }

                if (my)
                {
                        if (my > 0) mouse_portb &= ~0x10;
                        else        mouse_portb |=  0x10;

                        if (mouse_yff) mouse_portb ^= 0x10;

                        if (my > 0) my--;
                        else        my++;

                        uservia_set_cb2(mouse_yff);
                        mouse_yff = !mouse_yff;
                }

                if (mouse_b & 1) mouse_portb &= ~1;
                else             mouse_portb |=  1;
                if (mouse_b & 2) mouse_portb &= ~4;
                else             mouse_portb |=  4;
                mouse_portb |= 2;
        }
        else if (mouse_amx)
        {
                mouse_ff = !mouse_ff;
                if (mouse_ff)
                {
                        uservia_set_cb1(0);
                        uservia_set_cb2(0);
                        return;
                }

#ifdef WIN32
                get_mouse_mickeys(&dx,&dy);
#else
		dx = (mouse_x - mouse_ox);
		dy = (mouse_y - mouse_oy);
		mouse_ox = mouse_x;
		mouse_oy = mouse_y;
		printf("%i,%i - %i, %i\n",mouse_x,mouse_y,dx,dy);
#endif
                
                mx += dx;
                my += dy;
                
                /*AMX mouse*/
                if (mx)
                {
                        uservia_set_cb1(1);
                        if (mx > 0) mouse_portb |=  1;
                        else        mouse_portb &= ~1;

                        if (mx > 0) mx--;
                        else        mx++;
                }
                else
                   uservia_set_cb1(0);

                if (my)
                {
                        uservia_set_cb2(1);
                        if (my < 0) mouse_portb |=  4;
                        else        mouse_portb &= ~4;

                        if (my > 0) my--;
                        else        my++;
                }
                else
                   uservia_set_cb2(0);


                if (mouse_b & 1) mouse_portb &= ~0x20;
                else             mouse_portb |=  0x20;
                if (mouse_b & 2) mouse_portb &= ~0x80;
                else             mouse_portb |=  0x80;
                if (mouse_b & 4) mouse_portb &= ~0x40;
                else             mouse_portb |=  0x40;
        }
        if (mousecapture) position_mouse(64, 64);
}
#endif        

