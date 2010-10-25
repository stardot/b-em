/*B-em v2.1 by Tom Walker
  Linux main*/

#ifndef WIN32
#include <allegro.h>
#include <alleggl.h>
#include "b-em.h"

int videoresize,winsizex,winsizey;

int quited=0;
extern int dcol;
extern PALETTE pal;

void waitforready() { }
void resumeready() { }
int windx,windy;
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
                if (opengl) openglreinit();//set_gfx_mode(GFX_OPENGL_WINDOWED, x,y, 0, 0);
                else        set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
//                if (opengl) reinitopengl();
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
//#undef printf
int main(int argc, char *argv[])
{
        int oldf;
        char *p;
        allegro_init();
        get_executable_name(exedir,511);
        p=get_filename(exedir);
        p[0]=0;
        loadconfig();
//        printf("Main\n");
        initbbc(argc,argv);
//        printf("Inited\n");
        while (!quited)
        {
                runbbc();
                if (key[KEY_F11]) entergui();
                if (key[KEY_ALT] && key[KEY_ENTER] && fullscreen && !oldf)
                {
                        fullscreen=0;
                        leavefullscreen();
                }
                else if (key[KEY_ALT] && key[KEY_ENTER] && !fullscreen && !oldf)
                {
                        fullscreen=1;
                        enterfullscreen();
                }
                oldf=key[KEY_ALT] && key[KEY_ENTER];
        }
        closebbc();
}

END_OF_MAIN();
#endif
