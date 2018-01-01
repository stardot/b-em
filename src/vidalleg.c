/*B-em v2.2 by Tom Walker
  Allegro video code*/
#include <allegro.h>
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

static BITMAP *scrshotb, *scrshotb2;

int vid_clear = 0;

int scr_x_start, scr_x_size, scr_y_start, scr_y_size;

void video_clearscreen()
{
    set_color_depth(dcol);
#ifdef WIN32
    clear(vb);
#endif
    clear(b16);
    clear(b16x);
    clear(b32);
    clear(screen);
    set_color_depth(32);
    clear_to_color(b, 0);
}

void video_close()
{
    destroy_bitmap(b32);
    destroy_bitmap(b16x);
    destroy_bitmap(b16);
    destroy_bitmap(b);
#ifdef WIN32
    destroy_bitmap(vb);
#endif
}

void video_enterfullscreen()
{
	int alt = 0, value;
	int gfx = GFX_AUTODETECT_FULLSCREEN;
    double aspect;

#ifdef WIN32
    destroy_bitmap(vb);
#endif
    set_color_depth(dcol);

	/*
	 * Try and set fullscreen mode here.  Under Unix, the colour depth
	 * will vary, and it's likely the reason why fullscreen failed is that
	 * we need to select an alternative.  8bpp doesn't have an
	 * alternative, but other colour depths do, so we should use those.
	 */
    if (set_gfx_mode(gfx, desktop_width, desktop_height, 0, 0) != 0) {
		switch (dcol) {
		case 8:
			alt = 0;
			break;
		case 15:
			alt = 16;
			break;
		case 16:
			alt = 15;
			break;
		case 24:
			alt = 32;
			break;
		case 32:
			alt = 24;
			break;
		default:
			alt = 16;
			break;
		}

		if (alt != 0) {
			/* Try to set the alt colour depth and gfx mode. */
			set_color_depth(alt);
			if (set_gfx_mode(gfx, desktop_width, desktop_height, 0, 0) != 0) {
				log_error("Couldn't set GFX mode fullscreen");
				exit (-1);
			}
		}
	}
    log_debug("video: got x=%d, y=%d", SCREEN_W, SCREEN_H);

    aspect = (double)desktop_width / (double)desktop_height;
    if (aspect > (4.0 / 3.0)) {
        value = 800 * desktop_height / 600;
        scr_x_start = (desktop_width - value) / 2;
        scr_y_start = 0;
        scr_x_size = value;
        scr_y_size = desktop_height;
    }
    else {
        value = 600 * desktop_width / 800;
        scr_x_start = 0;
        scr_y_start = (desktop_height - value) / 2;
        scr_x_size = desktop_width;
        scr_y_size = 600;
    }

#ifdef WIN32
    vb=create_video_bitmap(924, 614);
#endif
    set_color_depth(32);
}

void video_leavefullscreen()
{
#ifdef WIN32
    destroy_bitmap(vb);
#endif
    set_color_depth(dcol);
#ifdef WIN32
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 2048, 2048, 0, 0);
    vb=create_video_bitmap(924, 614);
#else
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
#endif
    scr_x_start = 0;
    scr_y_start = 0;
    scr_x_size = SCREEN_W;
    scr_y_size = SCREEN_H;
    set_color_depth(32);
    updatewindowsize(640, 480);
}

static inline void upscale_only(BITMAP *src, BITMAP *dst, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (dw > sw || dh > sh)
        stretch_blit(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
    else
        blit(src, dst, sx, sy, dx, dy, sw, sh);
}

void video_doblit()
{
    int c;

    startblit();

    if (vid_savescrshot) {
        vid_savescrshot--;
        if (!vid_savescrshot) {
            set_color_depth(dcol);
            scrshotb  = create_bitmap(lastx - firstx, (lasty-firsty) << 1);
            scrshotb2 = create_bitmap(lastx - firstx,  lasty-firsty);
            if (vid_interlace || vid_linedbl)
                blit(b, scrshotb, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
            else {
                blit(b, scrshotb2, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                stretch_blit(scrshotb2, scrshotb, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx,(lasty - firsty) << 1);
            }
            save_bmp(vid_scrshotname, scrshotb, NULL);
            destroy_bitmap(scrshotb2);
            destroy_bitmap(scrshotb);
            set_color_depth(32);
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
            firstx = 320;
            lastx  = 1024;
            firsty = 24;
            lasty  = 296;
        }
        else if (vid_fullborders == 2) {
            firstx = 240;
            lastx  = 240 + 832;
            firsty = 8;
            lasty  = 312;
        }
        if (videoresize && !fullscreen) {
            fskipcount = 0;
            if (vid_scanlines) {
#ifdef WIN32
                for (c = firsty; c < lasty; c++) blit(b, b16x, firstx, c, 0, c << 1, lastx - firstx, 1);
                blit(b16x, vb, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                stretch_blit(vb, screen, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
#else
                blit(b, b16x, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                for (c = firsty; c < lasty; c++) blit(b16x, screen, 0, c - firsty, 0, (c - firsty) << 1, lastx - firstx, 1);
#endif
            }
#ifdef WIN32
            else if (vid_interlace && vid_pal) {
                pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
                blit(b32, vb, (firstx * 922) / 832, firsty << 1, 0,0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1);
                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
            }
            else if (vid_pal) {
                pal_convert(b, firstx, firsty, lastx, lasty, 1);
                blit(b32, vb, (firstx * 922) / 832, firsty, 0,0, ((lastx - firstx) * 922) / 832, lasty - firsty);
                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, winsizex, winsizey);
            }
#endif
            else if (vid_interlace || vid_linedbl) {
#ifdef WIN32
                blit(b, vb, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                stretch_blit(vb, screen, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
#else
                blit(b, screen, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
#endif
            }
            else {
#ifdef WIN32
                blit(b, vb, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                stretch_blit(vb, screen, 0, 0, lastx - firstx, lasty - firsty, 0, 0, winsizex, winsizey);
#else
                for (c = (firsty << 1); c < (lasty << 1); c++) blit(b, b16x, firstx, c >> 1, 0, c, lastx - firstx, 1);
                blit(b16x, screen, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
#endif
            }
        }
        else {
            if (!fullscreen) updatewindowsize((lastx - firstx) + 2, ((lasty - firsty) << 1) + 2);
            fskipcount = 0;
            if (vid_scanlines) {
                for (c = firsty; c < lasty; c++)
                    blit(b, b16x, firstx, c, 0, c << 1, lastx - firstx, 1);
                upscale_only(b16x, screen, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
            }
#ifdef WIN32
            else if (vid_interlace && vid_pal) {
                pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
                blit(b32, vb, (firstx * 922) / 832, firsty << 1, 0,0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1);
                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, (lastx - firstx), (lasty - firsty) << 1);
            }
            else if (vid_pal) {
                pal_convert(b, firstx, firsty, lastx, lasty, 1);
                blit(b32, vb, (firstx * 922) / 832, firsty, 0,0, ((lastx - firstx) * 922) / 832, lasty - firsty);
                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, (lastx - firstx), (lasty - firsty) << 1);
            }
#endif
            else if (vid_interlace || vid_linedbl)
                upscale_only(b, screen, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
            else {
#ifdef WIN32
                blit(b, vb, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                stretch_blit(vb, screen, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx, (lasty - firsty) << 1);
#else
                for (c = (firsty << 1); c < (lasty << 1); c++) blit(b, b16x, firstx, c >> 1, 0, c, lastx - firstx, 1);
                blit(b16x, screen, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
#endif
            }
        }
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
    endblit();
}
