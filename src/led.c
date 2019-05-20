#include "b-em.h"
#include "led.h"
#include "video_render.h"
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>

static ALLEGRO_FONT *font;

static const char *leds[] = {
    "Drive 0",
    "Drive 1",
    "VDFS",
    "cassette\nmotor",
};

void led_init()
{
    al_init_primitives_addon();
    al_init_font_addon();
    font = al_create_builtin_font();
    assert(font); // SFTODO: ERROR/DISABLE LEDS?? IF FONT IS NULL - ASSERT IS TEMP HACK
    // SFTODO;
}

static void draw_led(int i, bool b)
{
    const ALLEGRO_COLOR label_colour = al_map_rgb(128, 128, 128);
    const int box_width = 64;
    const int box_height = 32;
    const int led_width = 16;
    const int led_height = 4;
    const int led_region_height = 12;
    const int text_region_height = box_height - led_region_height;
    const int x1 = i * box_width;
    const int y1 = 0;
    const int led_x1 = x1 + (box_width - led_width) / 2;
    const int led_y1 = y1 + (led_region_height - led_height) / 2;
    const char *label = leds[i];
    al_set_target_bitmap(led);
    al_draw_filled_rectangle(x1, y1, x1 + box_width - 1, y1 + box_height - 1, al_map_rgb(64,64,64));
    ALLEGRO_COLOR color_red = al_map_rgb(b ? 255 : 0, 0, 0);
    al_draw_filled_rectangle(led_x1, led_y1, led_x1 + led_width, led_y1 + led_height, color_red);
    const char *label_newline = strchr(label, '\n');
    if (!label_newline) {
        const int text_width = al_get_text_width(font, label);
        const int text_height = al_get_font_ascent(font);
        const int text_y1 = y1 + led_region_height + (text_region_height - text_height) / 2;
        //al_draw_text(font, al_map_rgb(255, 255, 255), x1 + box_width / 2, y1 + led_region_height + text_region_height / 2, ALLEGRO_ALIGN_CENTRE, label);
        al_draw_text(font, label_colour, x1 + box_width / 2, text_y1, ALLEGRO_ALIGN_CENTRE, label);
    }
    else {
        char *label1 = malloc(label_newline - label + 1);
        memcpy(label1, label, label_newline - label);
        label1[label_newline - label] = '\0';
        const char *label2 = label_newline + 1;
        const int text_height = al_get_font_line_height(font);
        const int line_space = 2;
        const int text_y1 = y1 + led_region_height + (text_region_height - 2 * text_height - line_space) / 2;
        al_draw_text(font, label_colour, x1 + box_width / 2, text_y1, ALLEGRO_ALIGN_CENTRE, label1);
        al_draw_text(font, label_colour, x1 + box_width / 2, text_y1 + text_height + line_space, ALLEGRO_ALIGN_CENTRE, label2);
    }
    //al_draw_line(1.0, 5.0, 160.0, 2.0, color_red, 1.0);
}

void led_update(bool b)
{
    al_clear_to_color(al_map_rgb(0, 0, 0));
    draw_led(0, b);
    draw_led(1, b);
    draw_led(2, b);
    draw_led(3, b);
    // SFTODO!
}

