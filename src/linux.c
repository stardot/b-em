/*B-em v2.2 by Tom Walker
  Linux main*/

#ifndef WIN32
#include <allegro.h>
#include "b-em.h"
#include "config.h"
#include "linux-gui.h"
#include "main.h"
#include "video_render.h"

int winsizex, winsizey;
int videoresize = 0;

int mousecapture = 0;
int quited = 0;

void waitforready() { }
void resumeready() { }
int windx, windy;
void updatewindowsize(int x, int y)
{
        x=(x+3)&~3; y=(y+3)&~3;
        if (x<128) x=128;
        if (y<64)  y=64;
        if (windx!=x || windy!=y)
        {
//                printf("Update window size %i,%i\n",x,y);
                windx=winsizex=x; windy=winsizey=y;
                set_color_depth(dcol);
                set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                set_color_depth(8);
                set_palette(pal);
        }
}

void startblit()
{
}

void endblit()
{
}

void cataddname(char *s)
{
}

void updatewindow()
{
}

void bem_error(char *s)
{
        allegro_message(s);
}

void setquit()
{
        quited=1;
}
//#undef printf
int main(int argc, char *argv[])
{
        int oldf = 0;
        char *p;

        if (allegro_init())
        {
                printf("Failed to initialise Allegro!");
                exit(-1);
        }

        set_close_button_callback(setquit);

        set_window_title(B_EM_VERSION);

        get_executable_name(exedir, 511);
        p = get_filename(exedir);
        p[0] = 0;
        config_load();
//        printf("Main\n");
        main_init(argc, argv);
//        printf("Inited\n");
        while (!quited)
        {
                main_run();
                if (key[KEY_F11]) gui_enter();
                if (key[KEY_ALT] && key[KEY_ENTER] && fullscreen && !oldf)
                {
                        fullscreen = 0;
                        video_leavefullscreen();
                }
                else if (key[KEY_ALT] && key[KEY_ENTER] && !fullscreen && !oldf)
                {
                        fullscreen = 1;
                        video_enterfullscreen();
                }
                oldf = key[KEY_ALT] && key[KEY_ENTER];
        }
        main_close();
        return 0;
}

END_OF_MAIN()
//no semicolon after END_OF_MAIN
#endif
