#include <allegro.h>
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
	uint8_t         temp = 0xFF;
	if (joy[0].stick[0].axis[0].pos >= 64)
		temp &= ~0x10;
	if (joy[0].stick[0].axis[0].pos <= -64)
		temp &= ~0x02;
	if (joy[0].stick[0].axis[1].pos >= 64)
		temp &= ~0x04;
	if (joy[0].stick[0].axis[1].pos <= -64)
		temp &= ~0x08;
	if (joybutton[0])
		temp &= ~0x01;
	return temp;
}
