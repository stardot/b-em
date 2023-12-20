#include "b-em.h"

#include "mouse.h"
#include "mem.h"
#include "model.h"
#include "via.h"
#include "uservia.h"
#include "video_render.h"

bool mouse_amx, mouse_stick;
int mcount = 8;

uint8_t mouse_portb = 0xff;

static int mx = 0,  my = 0;
static int mouse_xff = 0, mouse_yff = 0;

static float stick_calc(double posn, double limit)
{
    double value = posn / limit;
    value = (value - 0.5) * 2.2;
    if (value > 1.0)
        value = 1.0;
    else if (value < -1.0)
        value = -1.0;
    return value;
}

void mouse_axes(ALLEGRO_EVENT *event)
{
    log_debug("mouse: axes event, x=%d, y=%d, dx=%d, mx=%d, dy=%d, my=%d", event->mouse.x, event->mouse.y, event->mouse.dx, mx, event->mouse.dy, my);
    if (curtube == 3) {
        mx += event->mouse.dx;
        my += event->mouse.dy;
    }
    else if (mouse_amx) {
        mx += event->mouse.dx * 2;
        my += event->mouse.dy * 2;
    }
    if (mouse_stick) {
        joyaxes[0] = stick_calc(event->mouse.x, winsizex);
        joyaxes[1] = stick_calc(event->mouse.y, winsizey);
    }
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
    if (mouse_stick) {
        int btn = (button & 1) ^ 1;
        log_debug("mouse: stick button %d down", btn);
        joybutton[btn] = 1;
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
    if (mouse_stick)
        joybutton[(button & 1) ^ 1] = 0;
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
