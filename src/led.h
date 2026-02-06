#ifndef __INC_LED_H
#define __INC_LED_H

#define LED_BOX_HEIGHT (32)

extern int led_ticks;
extern int last_led_update_at;
extern ALLEGRO_BITMAP *led_bitmap;

typedef enum
{
    LED_CASSETTE_MOTOR,
    LED_CAPS_LOCK,
    LED_SHIFT_LOCK,
    LED_DRIVE_0,
    LED_DRIVE_1,
    LED_HARD_DISK_0,
    LED_HARD_DISK_1,
    LED_HARD_DISK_2,
    LED_HARD_DISK_3,
    LED_VDFS,
    LED_MAX
} led_name_t;

typedef enum {
    LED_LOC_UNDEFINED = -1,
    LED_LOC_NONE,
    LED_LOC_OVERLAPPED,
    LED_LOC_SEPARATE
} led_location_t;

typedef enum {
    LED_VIS_CHANGED,
    LED_VIS_TRANSIENT,
    LED_VIS_ALWAYS
} led_visibility_t;

void led_init(void);
void led_close(void);
void led_update(led_name_t led_name, bool b, int ticks);
void led_timer_fired(void);
bool led_any_transient_led_on(void);

#endif
