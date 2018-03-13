/*B-em v2.2 by Tom Walker
  Allegro video code*/
#include <allegro5/allegro_primitives.h>
#include "b-em.h"
#ifdef WIN32
#include "pal.h"
#endif
#include "serial.h"
#include "tape.h"
#include "video.h"
#include "video_render.h"

int vid_interlace, vid_linedbl, vid_pal, vid_scanlines;
int vid_fskipmax = 3;
int vid_fullborders = 1;

static int fskipcount;

int vid_savescrshot = 0;
char vid_scrshotname[260];

static ALLEGRO_BITMAP *scrshotb, *scrshotb2;

int vid_clear = 0;

int scr_x_start, scr_x_size, scr_y_start, scr_y_size;

void video_clearscreen()
{
    ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);

#ifdef WIN32
    al_set_target_bitmap(vb);
    al_clear_to_color(black);
#endif
    al_set_target_bitmap(b16);
    al_clear_to_color(black);
    al_set_target_bitmap(b16x);
    al_clear_to_color(black);
    al_set_target_bitmap(b32);
    al_clear_to_color(black);
    al_set_target_backbuffer(al_get_current_display());
    al_clear_to_color(black);
    al_set_target_bitmap(b);
    al_clear_to_color(black);
}

void video_close()
{
    al_destroy_bitmap(b32);
    al_destroy_bitmap(b16x);
    al_destroy_bitmap(b16);
    al_destroy_bitmap(b);
#ifdef WIN32
    al_destroy_bitmap(vb);
#endif
}

void video_enterfullscreen()
{
    ALLEGRO_DISPLAY *display;
    ALLEGRO_COLOR black;
	int value;
    double aspect;

    display = al_get_current_display();
    if (al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, true)) {
        black = al_map_rgb(0, 0, 0);
        winsizex = al_get_display_width(display);
        winsizey = al_get_display_height(display);
        aspect = (double)winsizex / (double)winsizey;
        if (aspect > (4.0 / 3.0)) {
            value = 4 * winsizey / 3;
            scr_x_start = (winsizex - value) / 2;
            scr_y_start = 0;
            scr_x_size = value;
            scr_y_size = winsizey;
            al_set_target_backbuffer(display);
            // fill the gap between the left screen edge and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, black);
            // fill the gap between the BBC image and the right screen edge.
            al_draw_filled_rectangle(scr_x_start + value, 0, winsizex, winsizey, black);
        }
        else {
            value = 3 * winsizex / 4;
            scr_x_start = 0;
            scr_y_start = (winsizey - value) / 2;
            scr_x_size = winsizex;
            scr_y_size = value;
            // fill the gap between the top of the screen and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, black);
            // fill the gap between the BBC image and the bottom of the screen.
            al_draw_filled_rectangle(0, scr_y_start + value, winsizex, winsizey, black);        
        }
    } else {
        log_error("vidalleg: could not set graphics mode to full-screen");
        fullscreen = 0;
    }
}

void video_set_window_size(void)
{
    scr_x_start = 0;
    scr_y_start = 0;

    switch(vid_fullborders) {
        case 0:
            scr_x_size = winsizex = BORDER_NONE_X_SIZE;
            scr_y_size = winsizey = BORDER_NONE_Y_SIZE * 2;
            break;
        case 1:
            scr_x_size = winsizex = BORDER_MED_X_SIZE;
            scr_y_size = winsizey = BORDER_MED_Y_SIZE * 2;
            break;
        case 2:
            scr_x_size = winsizex = BORDER_FULL_X_SIZE;
            scr_y_size = winsizey = BORDER_FULL_Y_SIZE * 2;
    }
}

void video_update_window_size(ALLEGRO_EVENT *event)
{
    if (!fullscreen) {
        scr_x_start = 0;
        scr_x_size = winsizex = event->display.width;
        scr_y_start = 0;
        scr_y_size = winsizey = event->display.height;
    }
    al_acknowledge_resize(event->display.source);
}

void video_leavefullscreen(void)
{
    ALLEGRO_DISPLAY *display;

    display = al_get_current_display();
    al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, false);
    scr_x_start = 0;
    scr_x_size = winsizex = al_get_display_width(display);
    scr_y_start = 0;
    scr_y_size = winsizey = al_get_display_height(display);
}

void video_toggle_fullscreen(void)
{
    if (fullscreen) {
        fullscreen = 0;
        video_leavefullscreen();
    } else {
        fullscreen = 1;
        video_enterfullscreen();
    }
}

static inline void upscale_only(ALLEGRO_BITMAP *src, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
    al_set_target_backbuffer(al_get_current_display());
    if (dw > sw || dh > sh)
        al_draw_scaled_bitmap(src, sx, sy, sw, sh, dx, dy, dw, dh, 0);
    else
        al_draw_bitmap_region(src, sx, sy, sw, sh, dx, dy, 0);
}

void video_doblit()
{
    int c;
    ALLEGRO_COLOR black;

    startblit();

    if (vid_savescrshot) {
        vid_savescrshot--;
        if (!vid_savescrshot) {
            scrshotb  = al_create_bitmap(lastx - firstx, (lasty-firsty) << 1);
            if (vid_interlace || vid_linedbl) {
                al_set_target_bitmap(scrshotb);
                al_draw_bitmap_region(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
                al_save_bitmap(vid_scrshotname, scrshotb);
            }
            else {
                scrshotb2 = al_create_bitmap(lastx - firstx,  lasty-firsty);
                al_set_target_bitmap(scrshotb2);
                al_draw_bitmap_region(b, firstx, firsty, lastx - firstx, lasty - firsty, 0, 0, 0);
                al_set_target_bitmap(scrshotb);
                al_draw_scaled_bitmap(scrshotb2, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx,(lasty - firsty) << 1, 0);
                al_save_bitmap(vid_scrshotname, scrshotb);
                al_destroy_bitmap(scrshotb2);
            }
            al_destroy_bitmap(scrshotb);
        }
    }

    fskipcount++;
    if (fskipcount >= ((motor && fasttape) ? 5 : vid_fskipmax)) {
        lasty++;
        if (vid_fullborders == 1) {
            firstx = BORDER_MED_X_START;
            lastx  = BORDER_MED_X_START + BORDER_MED_X_SIZE;
            firsty = BORDER_MED_Y_START;
            lasty  = BORDER_MED_Y_START + BORDER_MED_Y_SIZE;
        }
        else if (vid_fullborders == 2) {
            firstx = BORDER_FULL_X_START;
            lastx  = BORDER_FULL_X_START + BORDER_FULL_X_SIZE;
            firsty = BORDER_FULL_Y_START;
            lasty  = BORDER_FULL_Y_START + BORDER_FULL_Y_SIZE;
        }

        fskipcount = 0;
        if (vid_scanlines) {
            al_set_target_bitmap(b16x);
            al_clear_to_color(al_map_rgb(0, 0,0));
            for (c = firsty; c < lasty; c++)
                al_draw_bitmap_region(b, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
            upscale_only(b16x, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
        }
#ifdef WIN32
        else if (vid_interlace && vid_pal) {
            pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
            al_set_target_bitmap(vb);
            al_draw_bitmap_region(b32, (firstx * 922) / 832, firsty << 1, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, 0);
            al_set_target_backbuffer(al_get_current_display());
            al_draw_scaled_bitmap(vb, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, (lastx - firstx), (lasty - firsty) << 1, 0);
        }
        else if (vid_pal) {
            pal_convert(b, firstx, firsty, lastx, lasty, 1);
            al_set_target_bitmap(vb);
            al_draw_bitmap_region(b32, (firstx * 922) / 832, firsty, ((lastx - firstx) * 922) / 832, lasty - firsty, 0, 0, 0);
            al_set_target_backbuffer(al_get_current_display());
            al_draw_scaled_bitmap(vb, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, (lastx - firstx), (lasty - firsty) << 1, 0);
        }
#endif
        else if (vid_interlace || vid_linedbl)
            upscale_only(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
        else {
#ifdef WIN32
            al_set_target_bitmap(vb);
            al_draw_bitmap_region(b, firstx, firsty, lastx - firstx, lasty - firsty, 0, 0, 0);
            al_set_target_backbuffer(al_get_current_display());
            al_draw_scaled_bitmap(vb, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0);
#else
            al_set_target_bitmap(b16x);
            for (c = (firsty << 1); c < (lasty << 1); c++)
                al_draw_bitmap_region(b, firstx, c >> 1, lastx - firstx, 1, 0, c, 0);
            al_set_target_backbuffer(al_get_current_display());
            al_draw_bitmap_region(b16x, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
#endif
        }

        if (scr_x_start > 0) {
            black = al_map_rgb(0, 0, 0);
            // fill the gap between the left screen edge and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, black);
            // fill the gap between the BBC image and the right screen edge.
            al_draw_filled_rectangle(scr_x_start + scr_x_size, 0, winsizex, winsizey, black);
        }
        else if (scr_y_start > 0) {
            black = al_map_rgb(0, 0, 0);
            // fill the gap between the top of the screen and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, black);
            // fill the gap between the BBC image and the bottom of the screen.
            al_draw_filled_rectangle(0, scr_y_start + scr_y_size, winsizex, winsizey, black);        
        }
/*#endif*/
        al_flip_display();
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
    endblit();
}
