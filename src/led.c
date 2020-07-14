#include "b-em.h"
#include "led.h"
#include "video_render.h"
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>

#define LED_BOX_WIDTH (64)
#define LED_REGION_HEIGHT (12)

int led_ticks = 0;
int last_led_update_at = -10000;

ALLEGRO_BITMAP *led_bitmap;

static ALLEGRO_FONT *font;

typedef struct {
    led_name_t led_name;
    const char *label;
    bool transient;
    int index;
    bool state;
    int turn_off_at;
} led_details_t;

// SFTODO: WE NEED AN LED FOR HARD DRIVE - DO WE WANT ONE FOR SCSI AND ONE FOR
// IDE? IF WE CAN ONLY HAVE ONE TYPE OF HARD DRIVE INSTALLED AT A TIME WE
// PROBABLY DON'T NEED THIS. IF WE CAN HAVE EG MULTIPLE DRIVES OF THE SAME TYPE
// MAYBE THEY SHOULD HAVE A SEPARATE LED EACH, AS WOULD BE THE CASE ON REAL
// HARDWARE.
static led_details_t led_details[] = {
    {LED_CASSETTE_MOTOR, "cassette\nmotor", false, 0, false, 0},
    {LED_CAPS_LOCK, "caps\nlock", false, 1, false, 0},
    {LED_SHIFT_LOCK, "shift\nlock", false, 2, false, 0},
    {LED_DRIVE_0, "drive 0", true, 3, false, 0},
    {LED_DRIVE_1, "drive 1", true, 4, false, 0},
    {LED_HDISK, "hard\ndisc", true, 5, false, 0 },
    {LED_VDFS, "VDFS", true, 6, false, 0} // SFTODO: MIGHT BE NICE TO HIDE VDFS LED IF VDFS DISABLED
};

static void draw_led(const led_details_t *led_details, bool b)
{
    const int x1 = led_details->index * LED_BOX_WIDTH;
    const int y1 = 0;
    const int led_width = 16;
    const int led_height = 4;
    const int led_x1 = x1 + (LED_BOX_WIDTH - led_width) / 2;
    const int led_y1 = y1 + (LED_REGION_HEIGHT - led_height) / 2;
    if (!led_bitmap)
        return; // SFTODO!?
    al_set_target_bitmap(led_bitmap);
    // SFTODO: PERHAPS HAVE A MORE SUBDUED RED SO THE CAPS LOCK LED (WHICH IS ON
    // MOST OF THE TIME) IS NOT SO IN-YOUR-FACE. I WANT THE LED DISPLAY TO BE
    // RELATIVELY UNINTRUSIVE, NOT COMPETE WITH THE EMULATED SCREEN.
    ALLEGRO_COLOR color = al_map_rgb(b ? 255 : 0, 0, 0);
    al_draw_filled_rectangle(led_x1, led_y1, led_x1 + led_width, led_y1 + led_height, color);
}

static void draw_led_full(const led_details_t *led_details, bool b)
{
    const ALLEGRO_COLOR label_colour = al_map_rgb(128, 128, 128);
    const int text_region_height = LED_BOX_HEIGHT - LED_REGION_HEIGHT;
    const int x1 = led_details->index * LED_BOX_WIDTH;
    const int y1 = 0;
    const char *label = led_details->label;
    if (!led_bitmap)
        return; // SFTODO!?
    al_set_target_bitmap(led_bitmap);
#if 1
    // SFTODO: THIS BOX IS MAINLY TO SHOW THE AREA OF EACH LED FOR DEBUGGING; I
    // WILL PROBABLY JUST HAVE A PLAIN BLACK BACKGROUND (SO NOTHING TO DRAW
    // HERE) IN FINAL VSN
    al_draw_filled_rectangle(x1, y1, x1 + LED_BOX_WIDTH - 1, y1 + LED_BOX_HEIGHT - 1, al_map_rgb(64,64,64));
#endif
    draw_led(led_details, b);
    const char *label_newline = strchr(label, '\n');
    if (!label_newline) {
        const int text_height = al_get_font_ascent(font);
        const int text_y1 = y1 + LED_REGION_HEIGHT + (text_region_height - text_height) / 2;
        //al_draw_text(font, al_map_rgb(255, 255, 255), x1 + LED_BOX_WIDTH / 2, y1 + LED_REGION_HEIGHT + text_region_height / 2, ALLEGRO_ALIGN_CENTRE, label);
        al_draw_text(font, label_colour, x1 + LED_BOX_WIDTH / 2, text_y1, ALLEGRO_ALIGN_CENTRE, label);
    }
    else {
        char *label1 = malloc(label_newline - label + 1);
        memcpy(label1, label, label_newline - label);
        label1[label_newline - label] = '\0';
        const char *label2 = label_newline + 1;
        const int text_height = al_get_font_line_height(font);
        const int line_space = 2;
        const int text_y1 = y1 + LED_REGION_HEIGHT + (text_region_height - 2 * text_height - line_space) / 2;
        al_draw_text(font, label_colour, x1 + LED_BOX_WIDTH / 2, text_y1, ALLEGRO_ALIGN_CENTRE, label1);
        al_draw_text(font, label_colour, x1 + LED_BOX_WIDTH / 2, text_y1 + text_height + line_space, ALLEGRO_ALIGN_CENTRE, label2);
    }
}

void led_init()
{
    const int led_count = sizeof(led_details) / sizeof(led_details[0]);
    led_bitmap = al_create_bitmap(led_count * LED_BOX_WIDTH, LED_BOX_HEIGHT);
    al_set_target_bitmap(led_bitmap);
    al_clear_to_color(al_map_rgb(0, 0, 64)); // sFTODO!?
    al_init_primitives_addon();
    al_init_font_addon();
    if ((font = al_create_builtin_font())) {
        for (int i = 0; i < led_count; i++) {
            draw_led_full(&led_details[i], false);
            led_details[i].state = false;
        }
    }
    else
        vid_ledlocation = -1;
}

extern int framesrun; // SFTODO BIT OF A HACK
void led_update(led_name_t led_name, bool b, int ticks)
{
    if (vid_ledlocation > 0) {
        // SFTODO: INEFFICIENT!
        for (int i = 0; i < sizeof(led_details)/sizeof(led_details[0]); i++) {
            if (led_details[i].led_name == led_name) {
                draw_led(&led_details[i], b);
                if (b != led_details[i].state) {
                    last_led_update_at = framesrun;
                    led_details[i].state = b;
                }
                if (!b || (ticks == 0))
                    led_details[i].turn_off_at = 0;
                else {
                    led_details[i].turn_off_at = framesrun + ticks;
                    if ((led_ticks == 0) || (ticks < led_ticks))
                        led_ticks = ticks;
                }
                return;
            }
        }
    }
}

void led_timer_fired(void)
{
    if (vid_ledlocation > 0) {
        for (int i = 0; i < sizeof(led_details)/sizeof(led_details[0]); i++) {
            if (led_details[i].turn_off_at != 0) {
                if (framesrun >= led_details[i].turn_off_at) {
                    // SFTODO: FACTOR OUT THE NEXT 6ISH LINES OF CODE - COMMON WITH led_update()
                    if (led_details[i].state != false) {
                        last_led_update_at = framesrun;
                        led_details[i].state = false;
                    }
                    draw_led(&led_details[i], false);
                    led_details[i].turn_off_at = 0;
                    // SFTODO? last_led_update_at = framesrun;
                }
                else {
                    int ticks = led_details[i].turn_off_at - framesrun;
                    assert(ticks > 0); // SFTODO TEMP
                    if ((led_ticks == 0) || (ticks < led_ticks))
                        led_ticks = ticks;
                }
            }
        }
    }
}

bool led_any_transient_led_on(void)
{
    for (int i = 0; i < sizeof(led_details)/sizeof(led_details[0]); i++)
        if (led_details[i].transient && led_details[i].state)
            return true;
    return false;
}
