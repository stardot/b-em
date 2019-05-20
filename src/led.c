#include "b-em.h"
#include "led.h"
#include "video_render.h"
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>

void led_init()
{
    // SFTODO;
}

void led_update(bool b)
{
    ALLEGRO_COLOR color_red = al_map_rgb(b ? 255 : 0, 0, 0);
    al_init_primitives_addon();
    al_set_target_bitmap(led);
    al_clear_to_color(al_map_rgb(0, 0, 0));
    al_draw_line(1.0, 5.0, 160.0, 2.0, color_red, 1.0);
    // SFTODO!
}

