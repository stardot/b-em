#ifndef __INC_VIDEO_RENDER_H
#define __INC_VIDEO_RENDER_H

#include <allegro5/allegro.h>
/*extern PALETTE pal;*/
extern ALLEGRO_BITMAP *b, *b16, *b16x, *b32, *tb;
#ifdef WIN32
extern BITMAP *vb;
#endif

extern int firstx, firsty, lastx, lasty;
extern int desktop_width, desktop_height;
extern int scr_x_start, scr_x_size, scr_y_start, scr_y_size;
extern int winsizex, winsizey;

extern int fullscreen;
extern int dcol;

extern int vid_linedbl;
extern int vid_interlace, vid_pal;
extern int vid_fskipmax,  vid_scanlines;
extern int vid_fullborders;

extern int videoresize;

extern int vid_savescrshot;
extern char vid_scrshotname[260];

void video_doblit();
void video_enterfullscreen();
void video_leavefullscreen();
void video_clearscreen();

void video_close();

void clearscreen();

#endif
