#include "fullscreen.h"
#include <allegro5/allegro_primitives.h>
#include "video_render.h"
#include "gui-allegro.h"
extern ALLEGRO_EVENT_QUEUE *queue;
extern ALLEGRO_DISPLAY *tmp_display;

void toggle_fullscreen_menu()
{
    if (fullscreen) {
        gui_allegro_init(queue, tmp_display);
        video_leavefullscreen();
        fullscreen = 0;
    } else {
        fullscreen = 1;
        video_enterfullscreen();
        gui_allegro_destroy(queue, tmp_display);
    }
}

void toggle_fullscreen()
{
    if (fullscreen) {
        video_leavefullscreen();
        fullscreen = 0;
    } else {
        fullscreen = 1;
        video_enterfullscreen();
    }
}
