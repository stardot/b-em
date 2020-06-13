#ifndef __INC_VIDEO_RENDER_H
#define __INC_VIDEO_RENDER_H

extern ALLEGRO_BITMAP *b, *b16, *b32;
extern ALLEGRO_LOCKED_REGION *region;
extern ALLEGRO_COLOR border_col;
extern ALLEGRO_THREAD *video_thread;
extern ALLEGRO_MUTEX *video_mutex;
extern ALLEGRO_COND *video_cond;

#define BORDER_NONE_X_START_GRA 336
#define BORDER_NONE_X_END_GRA   976
#define BORDER_NONE_X_START_TTX 352
#define BORDER_NONE_X_END_TTX   992
#define BORDER_NONE_Y_START_GRA  32
#define BORDER_NONE_Y_END_GRA   288
#define BORDER_NONE_Y_START_TXT  30
#define BORDER_NONE_Y_END_TXT   282

#define BORDER_MED_X_START_GRA  304
#define BORDER_MED_X_END_GRA   1008
#define BORDER_MED_X_START_TTX  320
#define BORDER_MED_X_END_TTX   1024
#define BORDER_MED_Y_START_GRA   22
#define BORDER_MED_Y_END_GRA    298
#define BORDER_MED_Y_START_TXT   20
#define BORDER_MED_Y_END_TXT    292

#define BORDER_FULL_X_START_GRA 240
#define BORDER_FULL_X_END_GRA  1072
#define BORDER_FULL_X_START_TTX 256
#define BORDER_FULL_X_END_TTX  1088
#define BORDER_FULL_Y_START_GRA   6
#define BORDER_FULL_Y_END_GRA   314
#define BORDER_FULL_Y_START_TXT   4
#define BORDER_FULL_Y_END_TXT   308

extern int scr_x_start, scr_x_size, scr_y_start, scr_y_size;
extern int winsizex, winsizey;
extern int crtc_mode, vtotal;

extern int fullscreen;

extern enum vid_disptype {
    VDT_SCALE,
    VDT_INTERLACE,
    VDT_SCANLINES,
    VDT_LINEDOUBLE,
    VDT_NONE,
} vid_dtype_user, vid_dtype_intern;

extern bool vid_pal;
extern int vid_fskipmax, vid_fullborders;
extern bool vid_print_mode;

extern int vid_savescrshot;
extern char vid_scrshotname[260];

void *video_doblit(ALLEGRO_THREAD *thread, void *ptr);
void video_enterfullscreen(void);
void video_leavefullscreen(void);
void video_toggle_fullscreen(void);
void video_set_window_size(bool fudge);
void video_update_window_size(ALLEGRO_EVENT *event);
void video_set_disptype(enum vid_disptype dtype);
void video_set_borders(int borders);

void video_close(void);

void clearscreen(void);

#endif
