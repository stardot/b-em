/*B-em v2.2 by Tom Walker
  Allegro video code*/
#include <allegro5/allegro_primitives.h>
#include "b-em.h"
#include "pal.h"
#include "serial.h"
#include "tape.h"
#include "video.h"
#include "video_render.h"

bool vid_interlace, vid_linedbl, vid_pal, vid_scanlines;
int vid_fskipmax = 3;
int vid_fullborders = 1;

static int fskipcount;

int vid_savescrshot = 0;
char vid_scrshotname[260];

static ALLEGRO_BITMAP *scrshotb, *scrshotb2;

int vid_clear = 0;

int winsizex, winsizey;
int scr_x_start, scr_x_size, scr_y_start, scr_y_size;

bool vid_print_mode = false;

void video_clearscreen()
{
    ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);

    al_set_target_bitmap(b16);
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
    al_destroy_bitmap(b16);
    al_destroy_bitmap(b);
}

#ifdef WIN32
static const int y_fudge = 0;
#else
static const int y_fudge = 19;
#endif

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
            scr_x_size = BORDER_NONE_X_END_GRA - BORDER_NONE_X_START_GRA;
            scr_y_size = (BORDER_NONE_Y_END_GRA - BORDER_NONE_Y_START_GRA) * 2;
            break;
        case 1:
            scr_x_size = BORDER_MED_X_END_GRA - BORDER_MED_X_START_GRA;
            scr_y_size = (BORDER_MED_Y_END_GRA - BORDER_MED_Y_START_GRA) * 2;
            break;
        case 2:
            scr_x_size = BORDER_FULL_X_END_GRA - BORDER_FULL_X_START_GRA;
            scr_y_size = (BORDER_FULL_Y_END_GRA - BORDER_FULL_Y_START_GRA) * 2;
    }
    winsizex = scr_x_size;
    winsizey = scr_y_size + y_fudge;
    log_debug("vidalleg: video_set_window_size, scr_x_size=%d, scr_y_size=%d, fudgedy=%d", scr_x_size, scr_y_size, winsizey);
}

void video_set_borders(int borders)
{
    vid_fullborders = borders;
    video_set_window_size();
    al_resize_display(al_get_current_display(), winsizex, winsizey);
}

void video_update_window_size(ALLEGRO_EVENT *event)
{
    if (!fullscreen) {
        scr_x_start = 0;
        scr_x_size = winsizex = event->display.width;
        scr_y_start = 0;
        scr_y_size = winsizey = event->display.height;
        log_debug("vidalleg: video_update_window_size, scr_x_size=%d, scr_y_size=%d", scr_x_size, scr_y_size);
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
    if (dw > sw+10 || dh > sh+10)
        al_draw_scaled_bitmap(src, sx, sy, sw, sh, dx, dy, dw, dh, 0);
    else
        al_draw_bitmap_region(src, sx, sy, sw, sh, dx, dy, 0);
}

void video_doblit(bool non_ttx, uint8_t vtotal)
{
    int c, xsize, ysize;
    ALLEGRO_COLOR black;

    if (vid_savescrshot) {
        vid_savescrshot--;
        if (!vid_savescrshot) {
            xsize = lastx - firstx;
            ysize = lasty - firsty + 1;
            scrshotb  = al_create_bitmap(xsize, ysize << 1);
            if (vid_interlace || vid_linedbl) {
                al_set_target_bitmap(scrshotb);
                al_draw_bitmap_region(b, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                al_save_bitmap(vid_scrshotname, scrshotb);
            }
            else {
                scrshotb2 = al_create_bitmap(lastx - firstx,  lasty-firsty);
                al_set_target_bitmap(scrshotb2);
                al_draw_bitmap_region(b, firstx, firsty, xsize, ysize, 0, 0, 0);
                al_set_target_bitmap(scrshotb);
                al_draw_scaled_bitmap(scrshotb2, 0, 0, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                al_save_bitmap(vid_scrshotname, scrshotb);
                al_destroy_bitmap(scrshotb2);
            }
            al_destroy_bitmap(scrshotb);
        }
    }

    fskipcount++;
    if (fskipcount >= ((motor && fasttape) ? 5 : vid_fskipmax)) {
        lasty++;
        if (vid_fullborders == 0) {
            if (non_ttx) {
                firstx = BORDER_NONE_X_START_GRA;
                lastx  = BORDER_NONE_X_END_GRA;
            }
            else {
                firstx = BORDER_NONE_X_START_TTX;
                lastx  = BORDER_NONE_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_NONE_Y_START_GRA;
                lasty  = BORDER_NONE_Y_END_GRA;
            }
            else {
                firsty = BORDER_NONE_Y_START_TXT;
                lasty  = BORDER_NONE_Y_END_TXT;
            }
        }
        else if (vid_fullborders == 1) {
            if (non_ttx) {
                firstx = BORDER_MED_X_START_GRA;
                lastx  = BORDER_MED_X_END_GRA;
            }
            else {
                firstx = BORDER_MED_X_START_TTX;
                lastx  = BORDER_MED_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_MED_Y_START_GRA;
                lasty  = BORDER_MED_Y_END_GRA;
            }
            else {
                firsty = BORDER_MED_Y_START_TXT;
                lasty  = BORDER_MED_Y_END_TXT;
            }
        }
        else if (vid_fullborders == 2) {
            if (non_ttx) {
                firstx = BORDER_FULL_X_START_GRA;
                lastx  = BORDER_FULL_X_END_GRA;
            }
            else {
                firstx = BORDER_FULL_X_START_TTX;
                lastx  = BORDER_FULL_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_FULL_Y_START_GRA;
                lasty  = BORDER_FULL_Y_END_GRA;
            }
            else {
                firsty = BORDER_FULL_Y_START_TXT;
                lasty  = BORDER_FULL_Y_END_TXT;
            }
        }
        fskipcount = 0;
        if (vid_scanlines) {
            al_unlock_bitmap(b);
            al_set_target_bitmap(b16);
            al_clear_to_color(al_map_rgb(0, 0,0));
            for (c = firsty; c < lasty; c++)
                al_draw_bitmap_region(b, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
            upscale_only(b16, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
            region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
        }
        else if (vid_interlace && vid_pal) {
            pal_convert(firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
            al_set_target_backbuffer(al_get_current_display());
            upscale_only(b32, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
        }
        else if (vid_pal) {
            pal_convert(firstx, firsty, lastx, lasty, 1);
            al_set_target_backbuffer(al_get_current_display());
            upscale_only(b32, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
        }
        else {
            al_unlock_bitmap(b);
            if (vid_interlace || vid_linedbl)
                upscale_only(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
            else
                upscale_only(b, firstx, firsty, lastx - firstx, lasty - firsty, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
            region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
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
        al_flip_display();
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
}
