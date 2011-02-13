/*B-em v2.1 by Tom Walker
  OpenGL video handling*/
#include <allegro.h>
#include <alleggl.h>
#include "b-em.h"

extern BITMAP *b,*b16,*b16x,*tb;
extern int firstx,firsty,lastx,lasty;

GLuint texture;

extern int winsizex,winsizey;
extern int dcol;

#undef printf

void openglinit()
{
                install_allegro_gl();
                allegro_gl_set(AGL_Z_DEPTH, 8);
                allegro_gl_set(AGL_COLOR_DEPTH, dcol);
                allegro_gl_set(AGL_SUGGEST, AGL_Z_DEPTH | AGL_COLOR_DEPTH);

                openglreinit();
}

void openglreinit()
{
        destroy_bitmap(tb);

        if (fullscreen)
        {
                allegro_gl_set(AGL_COLOR_DEPTH, 16);
                allegro_gl_set(AGL_FULLSCREEN,1);
                allegro_gl_set(AGL_WINDOWED,0);
        }
        else
        {
                allegro_gl_set(AGL_COLOR_DEPTH, dcol);
                allegro_gl_set(AGL_FULLSCREEN,0);
                allegro_gl_set(AGL_WINDOWED,1);
        }
        allegro_gl_set(AGL_RENDERMETHOD, 1);
        allegro_gl_set(AGL_DOUBLEBUFFER, 1);

        allegro_gl_set(AGL_REQUIRE, AGL_COLOR_DEPTH|AGL_FULLSCREEN|AGL_WINDOWED|AGL_DOUBLEBUFFER|AGL_RENDERMETHOD);


        if (winsizex<64) winsizex=64;
        if (winsizey<48) winsizey=48;
                if (fullscreen) set_gfx_mode(GFX_OPENGL_WINDOWED, 800, 600, 0, 0);
                else            set_gfx_mode(GFX_OPENGL_WINDOWED, winsizex, winsizey, 0, 0);

                glDisable(GL_ALPHA_TEST);
                glDisable(GL_BLEND);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_POLYGON_SMOOTH);
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_DITHER);
                glEnable(GL_TEXTURE_2D);
                glClearColor(0.0, 0.0, 0.0, 0.0);

                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(-1.0,1.0,1.0,-1.0,-1.0,1.0);
                set_color_depth(16);
                tb=create_bitmap(1024,1024);
//                printf("Can make texture ? %i\n",allegro_gl_check_texture(tb));
                texture=allegro_gl_make_texture_ex(0,tb,-1);
                destroy_bitmap(tb);
                tb=create_bitmap(832,614);
                set_color_depth(dcol);
}

void blitogl()
{
        float x,y;
        int c;
//        rpclog("Blit OpenGL\n");
        if (!videoresize)
        {
                updatewindowsize(lastx-firstx,(lasty-firsty)<<1);
        }
//        winsizex=512; winsizey=512;
        if (fullscreen) glViewport(0,0,800,600);
        else            glViewport(0,0,winsizex,winsizey);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0,1.0,1.0,-1.0,-1.0,1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D,texture);

        if (comedyblit)
        {
                for (c=firsty;c<lasty;c++) blit(b,tb,firstx,c,0,(c<<1)-(firsty<<1),lastx-firstx,1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0,
                             832,(lasty-firsty)<<1, __allegro_gl_get_bitmap_color_format(tb,0), __allegro_gl_get_bitmap_type(tb,0), tb->line[0]);
                y=(float)(lasty-firsty)/512.0;
        }
        else if (interlace || linedbl)
        {
                blit(b,tb,firstx,firsty<<1,0,0,lastx-firstx,(lasty-firsty)<<1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0,
                             832,(lasty-firsty)<<1, __allegro_gl_get_bitmap_color_format(tb,0), __allegro_gl_get_bitmap_type(tb,0), tb->line[0]);
                y=(float)(lasty-firsty)/512.0;
        }
        else
        {
                blit(b,tb,firstx,firsty,0,0,lastx-firstx,lasty-firsty);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0,
                             832,lasty-firsty, __allegro_gl_get_bitmap_color_format(tb,0), __allegro_gl_get_bitmap_type(tb,0), tb->line[0]);
                y=(float)(lasty-firsty)/1024.0;
        }
        x=(float)(lastx-firstx)/1024.0;

//        glDisable(GL_TEXTURE_2D);
//        glColor3f(1.0f,1.0f,1.0f);
//        rpclog("Size %f %f\n",x,y);
//      x=y=0.1;
        glBegin(GL_QUADS);
                glTexCoord2f(0.0,0.0); glVertex3f(-1, -1, 0);
                glTexCoord2f(x,  0.0); glVertex3f( 1, -1, 0);
                glTexCoord2f(x,  y);   glVertex3f( 1,  1, 0);
                glTexCoord2f(0.0,y);   glVertex3f(-1,  1, 0);
        glEnd();
        allegro_gl_flip();
//      rpclog("Blit OpenGL done\n");
}

