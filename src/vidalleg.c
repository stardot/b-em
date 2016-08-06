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
        set_color_depth(8);
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
        #ifdef WIN32
        destroy_bitmap(vb);
        #endif
        set_color_depth(dcol);
        set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 800, 600, 0, 0);
        #ifdef WIN32
        vb=create_video_bitmap(924, 614);
        #endif
        set_color_depth(8);
        set_palette(pal);
}
void video_leavefullscreen()
{
        #ifdef WIN32
        destroy_bitmap(vb);
        #endif
        set_color_depth(dcol);
#ifdef WIN32
        set_gfx_mode(GFX_AUTODETECT_WINDOWED, 2048, 2048, 0, 0);
#else
        set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
#endif
        #ifdef WIN32
        vb=create_video_bitmap(924, 614);
        #endif
        set_color_depth(8);
        set_palette(pal);
        updatewindowsize(640, 480);
}

void video_doblit()
{
        int c;
//        printf("%03i %03i %03i %03i\n",firstx,lastx,firsty,lasty);
//bem_debug("Blit\n");

        startblit();

//        printf("Blit\n");
        if (vid_savescrshot)
        {
                vid_savescrshot--;
                if (!vid_savescrshot)
                {
                        set_color_depth(dcol);
                        scrshotb  = create_bitmap(lastx - firstx, (lasty-firsty) << 1);
                        scrshotb2 = create_bitmap(lastx - firstx,  lasty-firsty);
                        if (vid_interlace || vid_linedbl)
                        {
                                blit(b, scrshotb, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                        }
                        else
                        {
                                blit(b, scrshotb2, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                                stretch_blit(scrshotb2, scrshotb, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx,(lasty - firsty) << 1);
                        }
                        save_bmp(vid_scrshotname, scrshotb, NULL);
                        destroy_bitmap(scrshotb2);
                        destroy_bitmap(scrshotb);
                        set_color_depth(8);
                }
        }

        fskipcount++;
        if (fskipcount >= ((motor && fasttape) ? 5 : vid_fskipmax))
        {
//                bem_debug("Blit start\n");
                lasty++;
                if (vid_fullborders == 1)
                {
//                        bem_debugf("%i %i %i %i  ",firstx,lastx,firsty,lasty);
/*                        c = (lastx + firstx) / 2;
                        firstx = c - 336;
                        lastx  = c + 336;
                        c = (lasty + firsty) / 2;
                        firsty = c - 136;
                        lasty  = c + 136;*/
//                        bem_debugf("  %i %i %i %i\n",firstx,lastx,firsty,lasty);

                        firstx = 320;
                        lastx  = 992;
                        firsty = 24;
                        lasty  = 296;

                }
                else if (vid_fullborders == 2)
                {
                        firstx = 240;
                        lastx  = 240 + 832;
                        firsty = 8;
                        lasty  = 312;
                }
                if (fullscreen)
                {
                        firstx = 256;
                        lastx  = 256 + 800;
                        firsty = 8;
                        lasty  = 300;
                }
                if (videoresize && !fullscreen)
                {
                        fskipcount = 0;
                        if (vid_scanlines)
                        {
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
                        else if (vid_interlace && vid_pal)
                        {
                                pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
                                blit(b32, vb, (firstx * 922) / 832, firsty << 1, 0,0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1);
                                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
                        }
                        else if (vid_pal)
                        {
                                pal_convert(b, firstx, firsty, lastx, lasty, 1);
                                blit(b32, vb, (firstx * 922) / 832, firsty, 0,0, ((lastx - firstx) * 922) / 832, lasty - firsty);
                                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, winsizex, winsizey);
                        }
                        #endif
                        else if (vid_interlace || vid_linedbl)
                        {
                                #ifdef WIN32
                                blit(b, vb, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                stretch_blit(vb, screen, 0, 0, lastx - firstx, (lasty - firsty) << 1, 0, 0, winsizex, winsizey);
                                #else
                                blit(b, screen, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                #endif
                        }
                        else
                        {
                                #ifdef WIN32
                                blit(b, vb, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                                stretch_blit(vb, screen, 0, 0, lastx - firstx, lasty - firsty, 0, 0, winsizex, winsizey);
                                #else
                                for (c = (firsty << 1); c < (lasty << 1); c++) blit(b, b16x, firstx, c >> 1, 0, c, lastx - firstx, 1);
                                blit(b16x, screen, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                #endif
                        }
                }
                else
                {
                        if (!fullscreen) updatewindowsize((lastx - firstx) + 2, ((lasty - firsty) << 1) + 2);
                        fskipcount = 0;
                        if (vid_scanlines)
                        {
                                #ifdef WIN32
                                for (c = firsty; c < lasty; c++) blit(b, b16x, firstx, c, 0, c << 1, lastx - firstx, 1);
                                blit(b16x, screen, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                #else
                                blit(b, b16x, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                                for (c = firsty; c < lasty; c++) blit(b16x, screen, 0, c - firsty, 0, (c - firsty) << 1, lastx - firstx, 1);
                                #endif
                        }
                        #ifdef WIN32
                        else if (vid_interlace && vid_pal)
                        {
                                pal_convert(b, firstx, (firsty << 1) + (interlline ? 1 : 0), lastx, (lasty << 1) + (interlline ? 1 : 0), 2);
                                blit(b32, vb, (firstx * 922) / 832, firsty << 1, 0,0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1);
                                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, (lasty - firsty) << 1, 0, 0, (lastx - firstx), (lasty - firsty) << 1);
                        }
                        else if (vid_pal)
                        {
                                pal_convert(b, firstx, firsty, lastx, lasty, 1);
                                blit(b32, vb, (firstx * 922) / 832, firsty, 0,0, ((lastx - firstx) * 922) / 832, lasty - firsty);
                                stretch_blit(vb, screen, 0, 0, ((lastx - firstx) * 922) / 832, lasty-firsty, 0, 0, (lastx - firstx), (lasty - firsty) << 1);
                        }
                        #endif
                        else if (vid_interlace || vid_linedbl)
                        {
                                //bem_debugf("Blit %i,%i  %i,%i\n", firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1);
                                blit(b, screen, firstx, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                        }
                        else
                        {
                                #ifdef WIN32
                                blit(b, vb, firstx, firsty, 0, 0, lastx - firstx, lasty - firsty);
                                stretch_blit(vb, screen, 0, 0, lastx - firstx, lasty - firsty, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                #else
                                for (c = (firsty << 1); c < (lasty << 1); c++) blit(b, b16x, firstx, c >> 1, 0, c, lastx - firstx, 1);
                                blit(b16x, screen, 0, firsty << 1, 0, 0, lastx - firstx, (lasty - firsty) << 1);
                                #endif
                        }
                        if (fullscreen)
                           rectfill(screen, 0, 584, 799, 599, 0);
                }
//                bem_debug("Blit end\n");
//                textprintf(screen,font,0,0,makecol(255,255,255),"%08X",uefpos());
        }
        firstx = firsty = 65535;
        lastx  = lasty  = 0;
        endblit();
}
