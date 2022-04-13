#include "fullscreen.h"
#include <allegro5/allegro_primitives.h>
#include "video_render.h"
#include "gui-allegro.h"
extern ALLEGRO_EVENT_QUEUE *queue;
extern ALLEGRO_DISPLAY *tmp_display;
void enter_fullscreen()
{
}

void leave_fullscreen()
{
    gui_allegro_init(queue, tmp_display);
    video_leavefullscreen();
}
void toggle_fullscreen_menu()
{
    if (fullscreen) {
        fullscreen = 0;
        gui_allegro_init(queue, tmp_display);
        video_leavefullscreen();
    } else {
        fullscreen = 1;
        video_enterfullscreen();
        gui_allegro_destroy(queue, tmp_display);
    }
}

void toggle_fullscreen()
{
    if (fullscreen) {
        fullscreen = 0;
        video_leavefullscreen();
    } else {
        fullscreen = 1;
        video_enterfullscreen();
    }
}
