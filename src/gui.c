/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.4a


              All of this code is written by Tom Walker
         You may use SMALL sections from this program (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*GUI*/

#include <stdio.h>
#include <allegro.h>

int ddnoise;
BITMAP *b;
int logging;
FILE *snlog;
int modela;
int frameskip;
int us;
int mono;
int quit;
int soundon;
int scanlinedraw;
char discname[260]={"discs/"};

PALETTE pal;
PALETTE beebpal,monopal;
BITMAP *_mouse_sprite;
BITMAP *mouse;

int disc_proc(int msg, DIALOG *d, int c)
{
      int ret;
      if ((ret=d_button_proc(msg,d,c))==D_CLOSE)
      {
            if (file_select_ex("Choose a disc image",discname,"SSD;DSD;IMG;BBC",320,240))
            {
                   for (c=0;c<strlen(discname);c++)
                   {
                         if (discname[c]=='.')
                         {
                               c++;
                               break;
                         }
                   }
                   if ((discname[c]=='d'||discname[c]=='D')&&(c!=strlen(discname)))
                      load8271dsd(discname,0);
                   else
                      load8271ssd(discname,0);
            }
            return D_REDRAW;

      }
      return ret;
}

int sound_proc(int msg, DIALOG *d, int c)
{
      int ret,sel;
      sel=d->flags&D_SELECTED;
      ret=d_check_proc(msg,d,c);
      if (sel!=(d->flags&D_SELECTED))
      {
             soundon=d->flags&D_SELECTED;
             if (!soundon)
                remove_sound();
             else
                initsnd();
      }
      return ret;
}

int mono_proc(int msg, DIALOG *d, int c)
{
      int ret,sel;
      sel=d->flags&D_SELECTED;
      ret=d_check_proc(msg,d,c);
      if (sel!=(d->flags&D_SELECTED))
      {
             mono=d->flags&D_SELECTED;
             if (mono)
                set_palette(monopal);
             else
                set_palette(beebpal);
      }
      return ret;
}

int line_proc(int msg, DIALOG *d, int c)
{
      int ret,sel;
      sel=d->flags&D_SELECTED;
      ret=d_check_proc(msg,d,c);
      if (sel!=(d->flags&D_SELECTED))
      {
             scanlinedraw=d->flags&D_SELECTED;
      }
      return ret;
}

int model_proc(int msg, DIALOG *d, int c)
{
      int ret,sel;
      sel=d->flags&D_SELECTED;
      ret=d_check_proc(msg,d,c);
      if (sel!=(d->flags&D_SELECTED))
      {
             modela=d->flags&D_SELECTED;
      }
      return ret;
}

/*This stops any buttons being pressed when the GUI is entered - the initial
  focus is passed to it*/
int nothing_proc(int msg, DIALOG *d, int c)
{
      if (msg==MSG_WANTFOCUS)
         return D_WANTFOCUS;
      return D_O_K;
}

int fskip_proc(int msg, DIALOG *d, int c)
{
      int ret;
      char *s=(char *)d->dp;
      ret=d_radio_proc(msg,d,c);
      if (d->flags&D_SELECTED)
         frameskip=s[0]-48;
      return ret;
}

int ntsc_proc(int msg, DIALOG *d, int c)
{
        int ret=d_radio_proc(msg,d,c);
        if (d->flags&D_SELECTED)
           us=0;
        else
           us=1;
        return ret;
}

int pal_proc(int msg, DIALOG *d, int c)
{
        int ret=d_radio_proc(msg,d,c);
        if (d->flags&D_SELECTED)
           us=0;
        else
           us=1;
        return ret;
}

int load_proc(int msg, DIALOG *d, int c)
{
        int ret;
        char shotname[260];
        PALETTE pal;
        if ((ret=d_button_proc(msg,d,c))==D_CLOSE)
        {
                if (file_select_ex("Enter a snapshot name",shotname,"SNP",320,240))
                {
                        loadsnapshot(shotname);
                }
                return D_REDRAW;
        }
        return ret;
}

int save_proc(int msg, DIALOG *d, int c)
{
        int ret;
        char shotname[260];
        PALETTE pal;
        if ((ret=d_button_proc(msg,d,c))==D_CLOSE)
        {
                if (file_select_ex("Enter a snapshot name",shotname,"SNP",320,240))
                {
                        savesnapshot(shotname);
                }
                return D_REDRAW;
        }
        return ret;
}

int shot_proc(int msg, DIALOG *d, int c)
{
        int ret;
        char shotname[260];
        PALETTE pal;
        if ((ret=d_button_proc(msg,d,c))==D_CLOSE)
        {
                if (file_select_ex("Enter a filename",shotname,"PCX;BMP;TGA",320,240))
                {
                        get_palette(pal);
                        save_bitmap(shotname,b,pal);
                }
                return D_REDRAW;
        }
        return ret;
}

int logsn_proc(int msg, DIALOG *d, int c)
{
        char logname[260];
        int ret=d_button_proc(msg,d,c);
        if (ret==D_EXIT)
        {
                if (file_select_ex("Enter a log file name",logname,"SN",320,240))
                {
                        startsnlog(logname);
                }
                return D_REDRAW;
        }
}

int logstop_proc(int msg, DIALOG *d, int c)
{
        int ret=d_button_proc(msg,d,c);
        if (ret==D_EXIT)
        {
                logging=0;
                if (snlog)
                   fclose(snlog);
                return D_O_K;
        }
}

int noises_proc(int msg, DIALOG *d, int c)
{
      int ret,sel;
      sel=d->flags&D_SELECTED;
      ret=d_check_proc(msg,d,c);
      if (sel!=(d->flags&D_SELECTED))
      {
             ddnoise=d->flags&D_SELECTED;
      }
      return ret;
}

DIALOG bemgui[]=
{
      {d_button_proc,0,  0,  64, 32,7,0,0,D_EXIT,0,0,"Return",0,0},
      {d_button_proc,80, 0,  64, 32,7,0,0,D_EXIT,0,0,"Quit",0,0},
      {disc_proc,    0,  48, 128,32,7,0,0,D_EXIT,0,0,"Select disc",0,0},
      {sound_proc,   0,  96, 50, 16,7,0,0,0,     1,0,"Sound",0,0},
      {nothing_proc, 400,300, 0,  0,0,0,0,0,0,0,0,0,0},
      {line_proc,    0,  120,50, 16,7,0,0,0,     1,0,"Line drawing mode",0,0},
      {mono_proc,    0,  144,50, 16,7,0,0,0,     1,0,"Monochrome",0,0},
      {d_text_proc,  184,0,  32, 8, 7,0,0,0,     0,0,"Skip",0,0},
      {fskip_proc,   184,16, 16, 16,7,0,0,0,     0,0,"1 frame",0,0},
      {fskip_proc,   184,32, 16, 16,7,0,0,0,     0,0,"2 frames",0,0},
      {fskip_proc,   184,48, 16, 16,7,0,0,0,     0,0,"3 frames",0,0},
      {fskip_proc,   184,64, 16, 16,7,0,0,0,     0,0,"4 frames",0,0},
      {fskip_proc,   184,80, 16, 16,7,0,0,0,     0,0,"5 frames",0,0},
      {fskip_proc,   184,96, 16, 16,7,0,0,0,     0,0,"6 frames",0,0},
      {fskip_proc,   184,112,16, 16,7,0,0,0,     0,0,"7 frames",0,0},
      {fskip_proc,   184,128,16, 16,7,0,0,0,     0,0,"8 frames",0,0},
      {fskip_proc,   184,144,16, 16,7,0,0,0,     0,0,"9 frames",0,0},
      {d_text_proc,  0,  168,32, 8, 7,0,0,0,     0,0,"TV standard",0,0},
      {d_text_proc,  0,  176,32, 8, 7,0,0,0,     0,0,"(change resets emu)",0,0},
      {ntsc_proc,    0,  192,16, 16,7,0,0,0,     1,0,"NTSC",0,0},
      {pal_proc,     0,  208,16, 16,7,0,0,0,     1,0,"PAL",0,0},
      {model_proc,   0,  232,60, 16,7,0,0,0,     1,0,"Model A",0,0},
      {noises_proc,  0,  256,60, 16,7,0,0,0,     1,0,"Disc drive noise",0,0},
      {load_proc,    272,0,  128,32,7,0,0,D_EXIT,0,0,"Load snapshot",0,0},
      {save_proc,    272,48, 128,32,7,0,0,D_EXIT,0,0,"Save snapshot",0,0},
      {shot_proc,    272,96, 128,32,7,0,0,D_EXIT,0,0,"Save screenshot",0,0},
      {logsn_proc,   272,144,128,32,7,0,0,D_EXIT,0,0,"Start SN log",0,0},
      {logstop_proc, 272,192,128,32,7,0,0,D_EXIT,0,0,"Stop SN log",0,0},
      {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

void rungui()
{
         int x,y;
         int oldus=us;
         int oldmodel=modela;
         if (soundon)
            bemgui[3].flags|=D_SELECTED;
         else
            bemgui[3].flags&=~D_SELECTED;
         if (scanlinedraw)
            bemgui[5].flags|=D_SELECTED;
         else
            bemgui[5].flags&=~D_SELECTED;
         if (mono)
            bemgui[6].flags|=D_SELECTED;
         else
            bemgui[6].flags&=~D_SELECTED;
         if (modela)
            bemgui[21].flags|=D_SELECTED;
         else
            bemgui[21].flags&=~D_SELECTED;
         if (ddnoise)
            bemgui[22].flags|=D_SELECTED;
         else
            bemgui[22].flags&=~D_SELECTED;
         if (us)
         {
                bemgui[19].flags|=D_SELECTED;
                bemgui[20].flags&=~D_SELECTED;
         }
         else
         {
                bemgui[20].flags|=D_SELECTED;
                bemgui[19].flags&=~D_SELECTED;
         }
         bemgui[7+frameskip].flags|=D_SELECTED;
      if (!mouse)
      {
            mouse=create_bitmap(10,16);
            for (y=0;y<16;y++)
            {
                  for (x=0;x<10;x++)
                  {
                        switch (getpixel(_mouse_sprite,x,y))
                        {
                              case 0:
                              putpixel(mouse,x,y,0);
                              break;
                              case 16:
                              putpixel(mouse,x,y,7);
                              break;
                              case 255:
                              putpixel(mouse,x,y,0);
                              break;
                        }
                  }
            }
            set_mouse_sprite(mouse);
      }
      clear_keybuf();
      if (do_dialog(bemgui,4)==1)
         quit=1;
      clear_keybuf();
      for (x=0;x<128;x++)
          key[x]=0;
      clear(screen);
      updateframeskip();
      if (oldus!=us || modela!=oldmodel)
      {
                initmem();
                init6502();
                UVIAReset();
                SVIAReset();
                reset8271();
      }
}
