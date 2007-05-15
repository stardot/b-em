/*B-em 1.3 by Tom Walker*/
/*Fullscreen GUI*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"

int autoboot;
int tube;
int framenum;
int soundbuflen;
char exname[512];
int updatewindow=0;
int fullscreen;
int hires;
int soundfilter;
AUDIOSTREAM *as;
char uefname[260]="test.uef";
int uefena=0;
int soundon;
int curwave;
int blurred,mono;
int ddnoise;
unsigned char *ram;
int model;
char discname[2][260];
int quit;
int fasttape;

void save_config()
{
        set_config_string(NULL,"disc0",discname[0]);
        set_config_string(NULL,"disc1",discname[1]);
        set_config_string(NULL,"uef",uefname);
        set_config_int(NULL,"model",model);
        set_config_int(NULL,"sound_enable",soundon);
        set_config_int(NULL,"current_waveform",curwave);
        set_config_int(NULL,"disc_noise",ddnoise);
        set_config_int(NULL,"blur_filter",blurred);
        set_config_int(NULL,"monochrome",mono);
        set_config_int(NULL,"uef_enable",uefena);
        set_config_int(NULL,"sound_filter",soundfilter);
        set_config_int(NULL,"resolution",hires);
        set_config_int(NULL,"fullscreen",fullscreen);
        set_config_int(NULL,"sound_buffer_length",soundbuflen);
        set_config_int(NULL,"tube",tube);
        set_config_int(NULL,"tube_type",tubetype);
        set_config_int(NULL,"fast_tape",fasttape);
        set_config_int(NULL,"tube_speed",tubespeed);
}

void load_config()
{
        char *p;
        char fn[512];
        p=get_filename(exname);
        *p=0;
        append_filename(fn,exname,"b-em.cfg",511);
        set_config_file(fn);
        p=(char *)get_config_string(NULL,"disc0",NULL);
        if (p)
        {
                memcpy(discname[0],p,strlen(p));
                discname[0][strlen(p)]=0;
        }
        else
           discname[0][0]=0;
        p=(char *)get_config_string(NULL,"disc1",NULL);
        if (p)
        {
                memcpy(discname[1],p,strlen(p));
                discname[1][strlen(p)]=0;
        }
        else
           discname[1][0]=0;
        p=(char *)get_config_string(NULL,"uef",NULL);
        if (p)
        {
                memcpy(uefname,p,strlen(p));
                uefname[strlen(p)]=0;
        }
        else
           uefname[0]=0;
        model=get_config_int(NULL,"model",1);
        tube=get_config_int(NULL,"tube",0);
        tubetype=get_config_int(NULL,"tube_type",3);
        soundon=get_config_int(NULL,"sound_enable",1);
        curwave=get_config_int(NULL,"current_waveform",0);
        ddnoise=get_config_int(NULL,"disc_noise",1);
        blurred=get_config_int(NULL,"blur_filter",0);
        mono=get_config_int(NULL,"monochrome",0);
        uefena=get_config_int(NULL,"uef_enable",1);
        soundfilter=get_config_int(NULL,"sound_filter",3);
        hires=get_config_int(NULL,"resolution",0);
        fullscreen=get_config_int(NULL,"fullscreen",0);
//        soundbuflen=625;//get_config_int(NULL,"sound_buffer_length",3120);
        fasttape=get_config_int(NULL,"fast_tape",1);
        tubespeed=get_config_int(NULL,"tube_speed",1);
//        rpclog("Load config - curwave %i\n",curwave);
}

int gui_exit()
{
        quit=1;
        return D_CLOSE;
}

int gui_return()
{
        return D_CLOSE;
}

int gui_loadstate()
{
        char tempname[260];
        int ret;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        tempname[0]=0;
        ret=file_select_ex("Please choose a file name",tempname,"SNP",260,xsize,ysize);
        if (ret)
        {
                loadstate(tempname);
        }
        return D_EXIT;
}

int gui_savestate()
{
        char tempname[260];
        int ret;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        tempname[0]=0;
        ret=file_select_ex("Please choose a file name",tempname,"SNP",260,xsize,ysize);
        if (ret)
        {
                savestate(tempname);
        }
        return D_EXIT;
}

MENU filemenu[]=
{
        {"&Return",gui_return,NULL,NULL,NULL},
        {"&Load state",gui_loadstate,NULL,NULL,NULL},
        {"&Save state",gui_savestate,NULL,NULL,NULL},
        {"&Exit",gui_exit,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

int gui_autostart()
{
        char tempname[260];
        int ret,c;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        memcpy(tempname,discname[0],260);
        ret=file_select_ex("Please choose a disc image",tempname,"SSD;DSD;IMG;ADF;ADL;FDI",260,xsize,ysize);
        if (ret)
        {
                checkdiscchanged(0);
                memcpy(discname[0],tempname,260);
                for (c=0;c<strlen(discname[0]);c++)
                {
                        if (discname[0][c]=='.')
                        {
                                c++;
                                break;
                        }
                }
                if ((discname[0][c]=='d'||discname[0][c]=='D')&&(c!=strlen(discname[0])))
                   load8271dsd(discname[0],0);
                else if ((discname[0][c]=='f'||discname[0][c]=='F')&&(c!=strlen(discname[0])))
                   load8271fdi(discname[0],0);
                else if ((discname[0][c]=='a'||discname[0][c]=='A')&&(c!=strlen(discname[0])))
                   load1770adfs(discname[0],0);
                else if (c!=strlen(discname[0]))
                   load8271ssd(discname[0],0);
                autoboot=50;
                resetsysvia();
                memset(ram,0,65536);
                resetarm();
                resetuservia();
                reset1770s();
                reset8271s();
                if (model<1) remaketablesa();
                else         remaketables();
                reset6502();
        }
        return D_EXIT;
}

int gui_changedisc0()
{
        char tempname[260];
        int ret,c;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        memcpy(tempname,discname[0],260);
        ret=file_select_ex("Please choose a disc image",tempname,"SSD;DSD;IMG;ADF;ADL;FDI",260,xsize,ysize);
        if (ret)
        {
                checkdiscchanged(0);
                memcpy(discname[0],tempname,260);
                for (c=0;c<strlen(discname[0]);c++)
                {
                        if (discname[0][c]=='.')
                        {
                                c++;
                                break;
                        }
                }
                if ((discname[0][c]=='d'||discname[0][c]=='D')&&(c!=strlen(discname[0])))
                   load8271dsd(discname[0],0);
                else if ((discname[0][c]=='f'||discname[0][c]=='F')&&(c!=strlen(discname[0])))
                   load8271fdi(discname[0],0);
                else if ((discname[0][c]=='a'||discname[0][c]=='A')&&(c!=strlen(discname[0])))
                   load1770adfs(discname[0],0);
                else if (c!=strlen(discname[0]))
                   load8271ssd(discname[0],0);
        }
        return D_EXIT;
}

int gui_changedisc1()
{
        char tempname[260];
        int ret,c;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        memcpy(tempname,discname[1],260);
        ret=file_select_ex("Please choose a disc image",tempname,"SSD;DSD;IMG;ADF;ADL;FDI",260,xsize,ysize);
        if (ret)
        {
                checkdiscchanged(1);
                memcpy(discname[1],tempname,260);
                for (c=0;c<strlen(discname[1]);c++)
                {
                        if (discname[1][c]=='.')
                        {
                                c++;
                                break;
                        }
                }
                if ((discname[1][c]=='d'||discname[1][c]=='D')&&(c!=strlen(discname[1])))
                   load8271dsd(discname[1],1);
                else if ((discname[0][c]=='f'||discname[0][c]=='F')&&(c!=strlen(discname[0])))
                   load8271fdi(discname[1],1);
                else if ((discname[1][c]=='a'||discname[1][c]=='A')&&(c!=strlen(discname[1])))
                   load1770adfs(discname[1],1);
                else if (c!=strlen(discname[1]))
                   load8271ssd(discname[1],1);
        }
        return D_EXIT;
}

MENU discmenu[5];

int ddsounds()
{
        ddnoise^=1;
        if (ddnoise) discmenu[2].flags=D_SELECTED;
        else         discmenu[2].flags=0;
        return D_EXIT;
}

MENU discmenu[]=
{
        {"&Autostart drive &0/2",gui_autostart,NULL,NULL,NULL},
        {"Load drive &0/2",gui_changedisc0,NULL,NULL,NULL},
        {"Load drive &1/3",gui_changedisc1,NULL,NULL,NULL},
        {"&Disc sounds",ddsounds,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

MENU tapemenu[5];

int changetape()
{
        char tempname[260];
        int ret,c;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        memcpy(tempname,uefname,260);
        ret=file_select_ex("Please choose a tape image",tempname,"UEF;CSW",260,xsize,ysize);
        if (ret)
        {
                memcpy(uefname,tempname,260);
                for (c=(strlen(uefname)-1);c>0;c--)
                {
                        if (uefname[c]=='.')
                        {
                                c++;
                                break;
                        }
                }
                if (uefname[c]=='c' || uefname[c]=='C') opencsw(uefname);
                else                                    openuef(uefname);
        }
        return D_EXIT;
}

int rewnd=0;

int rewindtape()
{
        rewnd=1;
        return D_EXIT;
}

int tapeena()
{
        uefena^=1;
        if (uefena) tapemenu[2].flags=D_SELECTED;
        else        tapemenu[2].flags=0;
        if (uefena) loadroms();
        else if (model<3) trapos();
        return D_EXIT;
}

int tapefast()
{
        fasttape^=1;
        return D_EXIT;
}

MENU tapemenu[]=
{
        {"&Change tape",changetape,NULL,NULL,NULL},
        {"&Rewind tape",rewindtape,NULL,NULL,NULL},
        {"&Tape enable",tapeena,NULL,NULL,NULL},
        {"&Fast tape",tapefast,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

MENU modelmenu[12];

int mpala()
{
        int c;
        model=0;
        remaketablesa();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,32768);
        tube=0;
        return D_EXIT;
}

int mpalb()
{
        int c;
        model=1;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,32768);
        tube=0;
        return D_EXIT;
}

int mpalbsw()
{
        int c;
        model=2;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,32768);
        tube=0;
        return D_EXIT;
}

int mpalb1770()
{
        int c;
        model=3;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,32768);
        tube=0;
        return D_EXIT;
}

int mntscb()
{
        int c;
        model=4;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,32768);
        tube=0;
        return D_EXIT;
}

int mpalbp()
{
        int c;
        model=5;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,65536);
        tube=0;
        return D_EXIT;
}

int mpalbp96()
{
        int c;
        model=6;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,65536);
        tube=0;
        return D_EXIT;
}

int mpalbp128()
{
        int c;
        model=7;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,65536);
        tube=0;
        return D_EXIT;
}

int mpalm128()
{
        int c;
        model=8;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,65536);
        tube=0;
        return D_EXIT;
}

int mpalmc()
{
        int c;
        model=9;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        memset(ram,0,65536);
        tube=0;
        return D_EXIT;
}

int mpalm128arm()
{
        int c;
        model=8;
        remaketables();
        loadroms();
        reset6502();
        reset8271(0);
        reset1770();
        resetsysvia();
        resetuservia();
        resetarm();
        memset(ram,0,65536);
        tube=1;
        return D_EXIT;
}

MENU modelmenu[]=
{
        {"Model A",mpala,NULL,NULL,NULL},
        {"Model B",mpalb,NULL,NULL,NULL},
        {"Model B w/SWRAM",mpalbsw,NULL,NULL,NULL},
        {"Model B w/1770",mpalb1770,NULL,NULL,NULL},
        {"NTSC Model B",mntscb,NULL,NULL,NULL},
        {"Model B+",mpalbp,NULL,NULL,NULL},
        {"Model B+96K",mpalbp96,NULL,NULL,NULL},
        {"Model B+128K",mpalbp128,NULL,NULL,NULL},
        {"Master 128",mpalm128,NULL,NULL,NULL},
        {"Master Compact",mpalmc,NULL,NULL,NULL},
        {"ARM Evaluation System",mpalm128arm,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};


MENU videomenu[5];

int mlow()
{
        hires=0;
        return D_EXIT;
}

int mhigh()
{
        hires=3;
        return D_EXIT;
}

int mhighs()
{
        hires=1;
        return D_EXIT;
}

int m2x()
{
        hires=2;
        return D_EXIT;
}

MENU modemenu[5]=
{
        {"&400x300",mlow,NULL,NULL,NULL},
        {"&800x600",mhigh,NULL,NULL,NULL},
        {"&800x600 with scanlines",mhighs,NULL,NULL,NULL},
        {"&800x600 with 2xSaI",m2x,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

int blurfilter()
{
        blurred^=1;
        if (blurred) videomenu[2].flags=D_SELECTED;
        else         videomenu[2].flags=0;
        return D_EXIT;
}

int monochrome()
{
        mono^=1;
        if (mono) videomenu[3].flags=D_SELECTED;
        else      videomenu[3].flags=0;
        updatepalette();
        return D_EXIT;
}

int vidwindow()
{
        fullscreen=0;
        updatewindow=1;
        return D_EXIT;
}

MENU videomenu[]=
{
        {"&Video mode",NULL,modemenu,NULL,NULL},
        {"&Windowed",vidwindow,NULL,NULL,NULL},
        {"&Blur filter",blurfilter,NULL,NULL,NULL},
        {"&Monochrome",monochrome,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

MENU wavemenu[6];

int msquare()
{
        wavemenu[0].flags=D_SELECTED;
        wavemenu[4].flags=wavemenu[1].flags=wavemenu[2].flags=wavemenu[3].flags=0;
        curwave=0;
        return D_EXIT;
}

int msaw()
{
        wavemenu[1].flags=D_SELECTED;
        wavemenu[4].flags=wavemenu[0].flags=wavemenu[2].flags=wavemenu[3].flags=0;
        curwave=1;
        return D_EXIT;
}

int msine()
{
        wavemenu[2].flags=D_SELECTED;
        wavemenu[4].flags=wavemenu[1].flags=wavemenu[0].flags=wavemenu[3].flags=0;
        curwave=2;
        return D_EXIT;
}

int mtri()
{
        wavemenu[3].flags=D_SELECTED;
        wavemenu[4].flags=wavemenu[1].flags=wavemenu[2].flags=wavemenu[0].flags=0;
        curwave=3;
        return D_EXIT;
}

int msid()
{
        wavemenu[4].flags=D_SELECTED;
        wavemenu[3].flags=wavemenu[1].flags=wavemenu[2].flags=wavemenu[0].flags=0;
        curwave=4;
        return D_EXIT;
}

MENU wavemenu[]=
{
        {"&Square",msquare,NULL,NULL,NULL},
        {"S&awtooth",msaw,NULL,NULL,NULL},
        {"S&ine",msine,NULL,NULL,NULL},
        {"&Triangle",mtri,NULL,NULL,NULL},
        {"SI&D",msid,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

MENU sbufmenu[3];

int m80()
{
        sbufmenu[0].flags=D_SELECTED;
        sbufmenu[1].flags=0;
        soundbuflen=2496;
        framenum=0;
        return D_EXIT;
}

int m100()
{
        sbufmenu[1].flags=D_SELECTED;
        sbufmenu[0].flags=0;
        soundbuflen=3120;
        framenum=0;
        return D_EXIT;
}

MENU sbufmenu[]=
{
        {"&80ms",m80,NULL,NULL,NULL},
        {"&100ms",m100,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL},
};

MENU soundmenu[8];

int soundena()
{
        soundon^=1;
        if (soundon) soundmenu[0].flags=D_SELECTED;
        else         soundmenu[0].flags=0;
        return D_EXIT;
}

int lowpass()
{
        soundfilter^=1;
        if (soundfilter&1) soundmenu[1].flags=D_SELECTED;
        else               soundmenu[1].flags=0;
        return D_EXIT;
}

int highpass()
{
        soundfilter^=2;
        if (soundfilter&2) soundmenu[2].flags=D_SELECTED;
        else               soundmenu[2].flags=0;
        return D_EXIT;
}

int startlog()
{
        char tempname[260]="";
        int ret;
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        ret=file_select_ex("Please enter a file name",tempname,"VGM",260,xsize,ysize);
        if (ret)
           startsnlog(tempname);
        return D_EXIT;
}

int stoplog()
{
        stopsnlog();
        return D_EXIT;
}

MENU soundmenu[]=
{
        {"Sound &enable",soundena,NULL,NULL,NULL},
        {"&Low pass filter",lowpass,NULL,NULL,NULL},
        {"&High pass filter",highpass,NULL,NULL,NULL},
        {"&Waveform",NULL,wavemenu,NULL,NULL},
        {"&Sound buffer",NULL,sbufmenu,NULL,NULL},
        {"&Start VGM log",startlog,NULL,NULL,NULL},
        {"S&top VGM log",stoplog,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

int calib1()
{
        char *msg;
        char s[128];
        while (joy[0].flags&JOYFLAG_CALIBRATE)
        {
                msg=(char *)calibrate_joystick_name(0);
                sprintf(s,"%s, and press OK\n",msg);
                alert(NULL,s,NULL,"OK",NULL,0,0);
                if (calibrate_joystick(0))
                {
                        alert(NULL,"Calibration error",NULL,"OK",NULL,0,0);
                        return D_EXIT;
                }
        }
        return D_EXIT;
}

int calib2()
{
        char *msg;
        char s[128];
        while (joy[1].flags&JOYFLAG_CALIBRATE)
        {
                msg=(char *)calibrate_joystick_name(1);
                sprintf(s,"%s, and press OK\n",msg);
                alert(NULL,s,NULL,"OK",NULL,0,0);
                if (calibrate_joystick(1))
                {
                        alert(NULL,"Calibration error",NULL,"OK",NULL,0,0);
                        return D_EXIT;
                }
        }
        return D_EXIT;
}

int mscrshot()
{
        char fn[260];
        int xsize=(hires)?768:384,ysize=(hires)?384:192;
        if (file_select_ex("Enter a file name",fn,"BMP;PCX;TGA;LBM",260,xsize,ysize))
        {
                restorepal();
                scrshot(fn);
                fadepal();
        }
        return D_EXIT;
}

MENU joymenu[]=
{
        {"Calibrate joystick &1",calib1,NULL,NULL,NULL},
        {"Calibrate joystick &2",calib2,NULL,NULL,NULL},
        {"&Save screenshot",mscrshot,NULL,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

MENU mainmenu[]=
{
        {"&File",NULL,filemenu,NULL,NULL},
        {"&Model",NULL,modelmenu,NULL,NULL},
        {"&Disc",NULL,discmenu,NULL,NULL},
        {"&Tape",NULL,tapemenu,NULL,NULL},
        {"&Video",NULL,videomenu,NULL,NULL},
        {"&Sound",NULL,soundmenu,NULL,NULL},
        {"&Misc",NULL,joymenu,NULL,NULL},
        {NULL,NULL,NULL,NULL,NULL}
};

DIALOG bemgui[]=
{
      {d_ctext_proc, 200, 260, 0,  0, 15,0,0,0,     0,0,"B-em v1.3"},
      {d_menu_proc,  0,   0,   0,  0, 15,0,0,0,     0,0,mainmenu},
          {d_yield_proc},
      {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

BITMAP *mouse,*_mouse_sprite;

void entergui()
{
        int x,y;
        int oldhires=hires;
        int oldfs=fullscreen;
        DIALOG_PLAYER *dp;
        fadepal();
        if (fasttape) tapemenu[3].flags=D_SELECTED;
        else          tapemenu[3].flags=0;
        if (uefena) tapemenu[2].flags=D_SELECTED;
        else        tapemenu[2].flags=0;
        if (blurred) videomenu[2].flags=D_SELECTED;
        else         videomenu[2].flags=0;
        if (mono) videomenu[3].flags=D_SELECTED;
        else      videomenu[3].flags=0;
        if (ddnoise) discmenu[2].flags=D_SELECTED;
        else         discmenu[2].flags=0;
        if (soundon) soundmenu[0].flags=D_SELECTED;
        else         soundmenu[0].flags=0;
        if (soundfilter&1) soundmenu[1].flags=D_SELECTED;
        else               soundmenu[1].flags=0;
        if (soundfilter&2) soundmenu[2].flags=D_SELECTED;
        else               soundmenu[2].flags=0;
        modelmenu[0].flags=modelmenu[1].flags=modelmenu[2].flags=modelmenu[3].flags=modelmenu[4].flags=modelmenu[5].flags=modelmenu[6].flags=modelmenu[7].flags=modelmenu[8].flags=modelmenu[9].flags=0;
        if (tube && model==7) modelmenu[9].flags=D_SELECTED;
        else                  modelmenu[model].flags=D_SELECTED;
        modemenu[0].flags=modemenu[1].flags=modemenu[2].flags=modemenu[3].flags=0;
        switch (hires)
        {
                case 0: modemenu[0].flags=D_SELECTED; break;
                case 3: modemenu[1].flags=D_SELECTED; break;
                case 1: modemenu[2].flags=D_SELECTED; break;
                case 2: modemenu[3].flags=D_SELECTED; break;
        }
        wavemenu[0].flags=wavemenu[1].flags=wavemenu[2].flags=wavemenu[3].flags=wavemenu[4].flags=0;
        wavemenu[curwave].flags=D_SELECTED;
        set_mouse_sprite(NULL);
        gui_fg_color=makecol(255,255,255); gui_bg_color=makecol(0,0,0);
        bemgui[0].fg=makecol(255,255,255);
        clear_keybuf();
//        if (soundon) soundoff();
        x=1;
        if (hires) { bemgui[0].x=400; bemgui[0].y=560; }
        else       { bemgui[0].x=200; bemgui[0].y=260; }
        if (hires) set_mouse_range(0,0,799,599);
        else       set_mouse_range(0,0,399,299);
        dp=init_dialog(bemgui,0);
        show_mouse(screen);
        while (x && !key[KEY_F11] && !(mouse_b&2) && !key[KEY_ESC])
        {
                x=update_dialog(dp);
        }
        show_mouse(NULL);
        shutdown_dialog(dp);
        while ((mouse_b&2) || key[KEY_F11] || key[KEY_ESC]) yield_timeslice();
//        if (soundon) resetsound();
        framenum=0;
        clear_keybuf();
        restorepal();
        if (rewnd)
        {
                rewindit();
                rewnd=0;
        }
        if (!fullscreen) recreatemenu();
        if ((hires!=oldhires) || (fullscreen!=oldfs)) updategfxmode();
        clear(screen);
        if (!fullscreen) remove_mouse();
}
