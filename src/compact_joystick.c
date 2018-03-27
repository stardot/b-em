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
    float posn;

    posn = joyaxes[0];
    if (posn > 0.5)
        temp &= ~0x10;
    else if (posn < -0.5)
        temp &= ~0x02;
    posn = joyaxes[1];
    if (posn > 0.5)
        temp &= ~0x04;
    else if (posn < -0.5)
        temp &= ~0x08;
    if (joybutton[0])
        temp &= ~0x01;
    return temp;
}
