#include "b-em.h"

#include "mouse.h"
#include "mem.h"
#include "model.h"
#include "via.h"
#include "uservia.h"

bool mouse_amx;
int mcount = 8;

uint8_t mouse_portb = 0xff;

static int mx = 0,  my = 0;
static int mouse_xff = 0, mouse_yff = 0;

void mouse_axes(ALLEGRO_EVENT *event)
{
    if (curtube == 3) {
        mx += event->mouse.dx;
        my += event->mouse.dy;
    }
    else if (mouse_amx) {
        mx += event->mouse.dx * 2;
        my += event->mouse.dy * 2;
    }
    log_debug("mouse: axes event, dx=%d, mx=%d, dy=%d, my=%d", event->mouse.dx, mx, event->mouse.dy, my);
}

void mouse_btn_down(ALLEGRO_EVENT *event)
{
    int button = event->mouse.button;
    log_debug("mouse: button #%d down", button);
    switch(button) {
        case 1:
            if (curtube == 3)
                mouse_portb &= ~1;
            else if (mouse_amx)
                mouse_portb &= ~0x20;
            break;
        case 2:
            if (curtube == 3)
                mouse_portb &= ~4;
            else if (mouse_amx)
                mouse_portb &= ~0x80;
            break;
        case 3:
            if (curtube == 3)
                mouse_portb &= ~2;
            else if (mouse_amx)
                mouse_portb &= ~0x40;
            break;
    }
}

void mouse_btn_up(ALLEGRO_EVENT *event) {
    int button = event->mouse.button;
    log_debug("mouse: button #%d up", button);
    switch(button) {
        case 1:
            if (curtube == 3)
                mouse_portb |=  1;
            else if (mouse_amx)
                mouse_portb |=  0x20;
            break;
        case 2:
            if (curtube == 3)
                mouse_portb |=  4;
            else if (mouse_amx)
                mouse_portb |=  0x80;
            break;
        case 3:
            if (curtube == 3)
                mouse_portb |= 2;
            else if (mouse_amx)
                mouse_portb |=  0x40;
            break;
    }
}

static void mouse_poll_x86(int xmask, int ymask)
{
    if (uservia.ifr & 0x18)
        return;

    if (mx) {
        if (mx > 0) {
            mouse_portb |=  xmask;
            mx -= 2;
            if (mx < 0)
                mx = 0;
        } else {
            mouse_portb &= ~xmask;
            mx += 2;
            if (mx > 0)
                mx = 0;
        }
        if (mouse_xff)
            mouse_portb ^= xmask;

        uservia_set_cb1(mouse_xff);
        mouse_xff = !mouse_xff;
    }

    if (my) {
        if (my > 0) {
            mouse_portb &= ~ymask;
            my -= 2;
            if (my < 0)
                my = 0;
        } else {
            mouse_portb |=  ymask;
            my += 2;
            if (my > 0)
                my = 0;;
        }
        if (mouse_yff)
            mouse_portb ^= ymask;

        uservia_set_cb2(mouse_yff);
        mouse_yff = !mouse_yff;
    }
}

void mouse_poll()
{
    if (curtube == 3)
        mouse_poll_x86(0x08, 0x10);
    else if (mouse_amx)
        mouse_poll_x86(0x01, 0x04);
}
