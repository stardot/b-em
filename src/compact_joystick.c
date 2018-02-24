#include "b-em.h"
#include "compact_joystick.h"

uint8_t compact_joystick_read()
{
/*Master Compact inputs :
        PB4 - right
        PB3 - up
        PB2 - down
        PB1 - left
        PB0 - fire*/
    uint8_t temp = 0xFF;
    ALLEGRO_JOYSTICK *joy;
    ALLEGRO_JOYSTICK_STATE jstate;
    float posn;

    if (al_is_joystick_installed() && al_get_num_joysticks() >= 1) {
        joy = al_get_joystick(0);
        al_get_joystick_state(joy, &jstate);
        posn = jstate.stick[0].axis[0];
        if (posn >=  64)
            temp &= ~0x10;
        else if (posn <= -64)
            temp &= ~0x02;
        posn = jstate.stick[0].axis[0];
        if (posn >=  64)
            temp &= ~0x04;
        else if (posn <= -64)
            temp &= ~0x08;
        if (jstate.button[0])
            temp &= ~0x01;
    }
    return temp;
}
