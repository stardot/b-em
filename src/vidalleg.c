/*B-em v2.1 by Tom Walker
  Allegro video code*/
#include <allegro.h>
#include "b-em.h"

int vidchange=0;

extern BITMAP *b,*b16,*b16x,*tb,*vb;
extern int firstx,firsty,lastx,lasty;
extern int dcol;
extern int collook[8];
extern PALETTE pal;
extern int fskipcount;
extern int winsizex,winsizey;

int savescrshot=0;
BITMAP *scrshotb,*scrshotb2;
char scrshotname[260];

void clearscreen()
{
        set_color_depth(dcol);
        #ifdef WIN32
        if (!opengl) clear(vb);
        #endif
        clear(b16);
        clear(b16x);
        if (!opengl) clear(screen);
        else         clear(tb);
        set_color_depth(8);
        clear_to_color(b,collook[0]);
}

void closevideo()
{
        destroy_bitmap(b16x);
        destroy_bitmap(b16);
        destroy_bitmap(b);
        #ifdef WIN32
        if (!opengl) destroy_bitmap(vb);
        #endif
        if (opengl) remove_allegro_gl();
}

void enterfullscreen()
{
/*      if (opengl)
        {
                rpclog("Enter fullscreen start\n");
                openglreinit();
                rpclog("Enter fullscreen end\n");
                return;
        }*/
        #ifdef WIN32
        destroy_bitmap(vb);
        #endif
        set_color_depth(dcol);
        set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0);
        #ifdef WIN32
        vb=create_video_bitmap(832,614);
        #endif
        set_color_depth(8);
        set_palette(pal);
}
void leavefullscreen()
{
/*      if (opengl)
        {
                openglreinit();
                return;
        }*/
        #ifdef WIN32
        destroy_bitmap(vb);
        #endif
        set_color_depth(dcol);
#ifdef WIN32
        set_gfx_mode(GFX_AUTODETECT_WINDOWED,2048,2048,0,0);
#else
        set_gfx_mode(GFX_AUTODETECT_WINDOWED,640,480,0,0);
#endif
        #ifdef WIN32
        vb=create_video_bitmap(832,614);
        #endif
        set_color_depth(8);
        set_palette(pal);
        updatewindowsize(640,480);
}
int motor;
void doblit()
{
        int c;
//        while (doopenglblit) sleep(1);
//rpclog("Blit\n");
        startblit();
        if (vidchange)
        {
                closevideo();
                opengl=vidchange-1;
                initvideo();
                vidchange=0;
                updatewindow();
        }
/*        if (opengl)
        {
                doopenglblit=1;
                return;
        }*/
        if (savescrshot)
        {
                set_color_depth(dcol);
                scrshotb=create_bitmap(lastx-firstx,(lasty-firsty<<1));
                scrshotb2=create_bitmap(lastx-firstx,lasty-firsty);
                if (interlace || linedbl)
                {
                        blit(b,scrshotb,firstx,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                }
                else
                {
                        blit(b,scrshotb2,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                        stretch_blit(scrshotb2,scrshotb,0,0,lastx-firstx,lasty-firsty,0,0,lastx-firstx,(lasty-firsty)<<1);
                }
                save_bmp(scrshotname,scrshotb,NULL);
                destroy_bitmap(scrshotb2);
                destroy_bitmap(scrshotb);
                set_color_depth(8);
                savescrshot=0;
        }
        fskipcount++;
        if (fskipcount>=((motor && fasttape)?5:fskipmax))
        {
                lasty++;
                if (fullborders==1)
                {
//                        rpclog("%i %i %i %i  ",firstx,lastx,firsty,lasty);
                        c=(lastx+firstx)/2;
                        firstx=c-336;
                        lastx=c+336;
                        c=(lasty+firsty)/2;
                        firsty=c-136;
                        lasty=c+136;
//                        rpclog("  %i %i %i %i\n",firstx,lastx,firsty,lasty);
                }
                else if (fullborders==2)
                {
                        firstx=240;
                        lastx=240+832;
                        firsty=8;
                        lasty=312;
                }
                if (fullscreen)
                {
                        firstx=256;
                        lastx=256+800;
                        firsty=8;
                        lasty=300;
                }
                if (opengl)
                {
                        blitogl();
                }
                else if (videoresize && !fullscreen)
                {
                        fskipcount=0;
                        if (comedyblit)
                        {
                                #ifdef WIN32
                                for (c=firsty;c<lasty;c++) blit(b,b16x,firstx,c,0,c<<1,lastx-firstx,1);
                                blit(b16x,vb,0,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                stretch_blit(vb,screen,0,0,lastx-firstx,(lasty-firsty)<<1,0,0,winsizex,winsizey);
                                #else
                                blit(b,b16x,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                                for (c=firsty;c<lasty;c++) blit(b16x,screen,0,c-firsty,0,(c-firsty)<<1,lastx-firstx,1);
                                #endif
                        }
                        else if (interlace || linedbl)
                        {
                                #ifdef WIN32
                                blit(b,vb,firstx,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                stretch_blit(vb,screen,0,0,lastx-firstx,(lasty-firsty)<<1,0,0,winsizex,winsizey);
                                #else
                                blit(b,screen,firstx,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                #endif
                        }
                        else
                        {
                                #ifdef WIN32
                                blit(b,vb,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                                stretch_blit(vb,screen,0,0,lastx-firstx,lasty-firsty,0,0,winsizex,winsizey);
                                #else
                                for (c=(firsty<<1);c<(lasty<<1);c++) blit(b,b16x,firstx,c>>1,0,c,lastx-firstx,1);
                                blit(b16x,screen,0,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                #endif
                        }
                }
                else
                {
                        if (!fullscreen) updatewindowsize((lastx-firstx)+2,((lasty-firsty)<<1)+2);
                        fskipcount=0;
                        if (comedyblit)
                        {
                                #ifdef WIN32
                                for (c=firsty;c<lasty;c++) blit(b,b16x,firstx,c,0,c<<1,lastx-firstx,1);
                                blit(b16x,screen,0,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                #else
                                blit(b,b16x,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                                for (c=firsty;c<lasty;c++) blit(b16x,screen,0,c-firsty,0,(c-firsty)<<1,lastx-firstx,1);
                                #endif
                        }
                        else if (interlace || linedbl)
                        {
                                blit(b,screen,firstx,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                        }
                        else
                        {
                                #ifdef WIN32
                                blit(b,vb,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                                stretch_blit(vb,screen,0,0,lastx-firstx,lasty-firsty,0,0,lastx-firstx,(lasty-firsty)<<1);
                                #else
                                for (c=(firsty<<1);c<(lasty<<1);c++) blit(b,b16x,firstx,c>>1,0,c,lastx-firstx,1);
                                blit(b16x,screen,0,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                                #endif
                        }
                }
//                textprintf(screen,font,0,0,makecol(255,255,255),"%08X",uefpos());
        }
        firstx=firsty=65535;
        lastx=lasty=0;

        endblit();
//rpclog("Blit over\n");
}
