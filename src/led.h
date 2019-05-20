#ifndef __INC_LED_H
#define __INC_LED_H

typedef enum
{
    LED_CASSETTE_MOTOR,
    LED_CAPS_LOCK,
    LED_SHIFT_LOCK,
    LED_DRIVE_0,
    LED_DRIVE_1,
    LED_VDFS,
    LED_MAX
} led_name_t;

void led_init(void);
void led_update(led_name_t led_name, bool b);

#endif
