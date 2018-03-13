#ifndef __INC_VIDEO_RENDER_H
#define __INC_VIDEO_RENDER_H

/*extern PALETTE pal;*/
extern ALLEGRO_BITMAP *b, *b16, *b16x, *b32, *tb;
#ifdef WIN32
extern BITMAP *vb;
#endif

#define BORDER_NONE_X_START 336
#define BORDER_NONE_X_SIZE  640
#define BORDER_NONE_Y_START  30
#define BORDER_NONE_Y_SIZE  252

#define BORDER_MED_X_START  320
#define BORDER_MED_X_SIZE   704
#define BORDER_MED_Y_START   24
#define BORDER_MED_Y_SIZE   272

#define BORDER_FULL_X_START 240
#define BORDER_FULL_X_SIZE  832
#define BORDER_FULL_Y_START   8
#define BORDER_FULL_Y_SIZE  304

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

void video_doblit(void);
void video_enterfullscreen(void);
void video_leavefullscreen(void);
void video_toggle_fullscreen(void);
void video_clearscreen(void);
void video_set_window_size(void);
void video_update_window_size(ALLEGRO_EVENT *event);

void video_close(void);

void clearscreen(void);

#endif
