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
    ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);
	int value;
    double aspect;

    if (al_set_display_flag(al_get_current_display(), ALLEGRO_FULLSCREEN_WINDOW, true)) {
        aspect = (double)desktop_width / (double)desktop_height;
        if (aspect > (4.0 / 3.0)) {
            value = 800 * desktop_height / 600;
            scr_x_start = (desktop_width - value) / 2;
            scr_y_start = 0;
            scr_x_size = value;
            scr_y_size = desktop_height;
            al_set_target_backbuffer(al_get_current_display());
            // fill the gap between the left screen edge and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, black);
            // fill the gap between the BBC image and the right screen edge.
            al_draw_filled_rectangle(scr_x_start + value, 0, desktop_width, desktop_height, black);
        }
        else {
            value = 600 * desktop_width / 800;
            scr_x_start = 0;
            scr_y_start = (desktop_height - value) / 2;
            scr_x_size = desktop_width;
            scr_y_size = value;
            // fill the gap between the top of the screen and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, black);
            // fill the gap between the BBC image and the bottom of the screen.
            al_draw_filled_rectangle(0, scr_y_start + value, desktop_width, desktop_height, black);        
        }
    } else {
        log_error("vidalleg: could not set graphics mode to full-screen");
        fullscreen = 0;
    }
}

void video_set_window_size(void)
{
    switch(vid_fullborders) {
        case 0:
            winsizex = BORDER_NONE_X_SIZE;
            winsizey = BORDER_NONE_Y_SIZE * 2;
            break;
        case 1:
            winsizex = BORDER_MED_X_SIZE;
            winsizey = BORDER_MED_Y_SIZE * 2;
            break;
        case 2:
            winsizex = BORDER_FULL_X_SIZE;
            winsizey = BORDER_FULL_Y_SIZE * 2;
    }
}
    
void video_leavefullscreen()
{
    al_set_display_flag(al_get_current_display(), ALLEGRO_FULLSCREEN_WINDOW, false);
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
        if (fullscreen) {
            firstx = 256;
            lastx  = 256 + 800;
            firsty = 8;
            lasty  = 300;
        }
        else if (vid_fullborders == 1) {
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
        if (videoresize && !fullscreen) {
            fskipcount = 0;
            if (vid_scanlines) {
#ifdef WIN32
                al_set_target_bitmap(b16x);
                for (c = firsty; c < lasty; c++)
                    al_draw_bitmap_region(b, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
                al_set_target_bitmap(vb);
                al_draw_bitmap_region(b16x, 0, firsty << 1lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(vb, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0, 0, winsizex, winsizey, 0);
#else
                al_set_target_bitmap(b16x);
                al_draw_bitmap_region(b, firstx, firsty, lastx - firstx, lasty - firsty, 0, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                for (c = firsty; c < lasty; c++)
                    al_draw_bitmap_region(b16x, 0, c - firsty, lastx - firstx, 1, 0, (c - firsty) << 1, 0);
#endif
            }
#ifdef WIN32
            else if (vid_interlace && vid_pal) {
                pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
                al_set_target_bitmap(vb);
                al_draw_bitmap_region(b32, (firstx * 922) / 832, firsty << 1, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(vb, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
            }
            else if (vid_pal) {
                pal_convert(b, firstx, firsty, lastx, lasty, 1);
                al_set_target_bitmap(vb);
                al_draw_bitmap_region(b32, (firstx * 922) / 832, firsty, ((lastx - firstx) * 922) / 832, lasty - firsty, 0, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(vb, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, winsizex, winsizey, 0);
            }
#endif
            else if (vid_interlace || vid_linedbl) {
#ifdef WIN32
                al_set_target_bitmap(vb);
                al_draw_bitmap_region(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(vb, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0, 0, winsizex, winsizey, 0);
#else
                al_set_target_backbuffer(al_get_current_display());
                al_draw_bitmap_region(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
#endif
            }
            else {
#ifdef WIN32
                al_set_target_bitmap(vb);
                al_draw_bitmap_region(b, firstx, firsty, lastx - firstx, lasty - firsty, 0, 0, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(vb, 0, 0, lastx - firstx, lasty - firsty, 0, 0, winsizex, winsizey, 0);
#else
                al_set_target_bitmap(b16x);
                for (c = (firsty << 1); c < (lasty << 1); c++)
                    al_draw_bitmap_region(b, firstx, c >> 1, lastx - firstx, 1, 0, c, 0);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_bitmap_region(b16x, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, 0, 0, 0);
#endif
            }
        }
        else {
            //if (!fullscreen)
            //    updatewindowsize((lastx - firstx) + 2, ((lasty - firsty) << 1) + 2);
            fskipcount = 0;
            if (vid_scanlines) {
                al_set_target_bitmap(b16x);
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
        }
#ifdef WIN32
        // One would expect that drawing black rectangles into the
        // space that is not having the BBC screen blitted into it
        // could be done once when full screen mode is entered but
        // on Windows this does not seem to work so for Windows only
        // we do it here.  For Linux there is no need to do it all
        // as the space defaults to black anyway.

        if (scr_x_start > 0) {
            // fill the gap between the left screen edge and the BBC image.
            rectfill(screen, 0, 0, scr_x_start, scr_y_size, c);
            // fill the gap between the BBC image and the right screen edge.
            rectfill(screen, scr_x_start + scr_x_size, 0, desktop_width, desktop_height, c);
        }
        else if (scr_y_start > 0) {
            // fill the gap between the top of the screen and the BBC image.
            rectfill(screen, 0, 0, scr_x_size, scr_y_start, c);
            // fill the gap between the BBC image and the bottom of the screen.
            rectfill(screen, 0, scr_y_start + scr_y_size, desktop_width, desktop_height, c);
        }
#endif
        al_flip_display();
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
    endblit();
}
