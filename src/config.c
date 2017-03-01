/*B-em v2.2 by Tom Walker
  Configuration handling*/

#include <allegro.h>
#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "disc.h"
#include "keyboard.h"
#include "mouse.h"
#include "ide.h"
#include "sn76489.h"
#include "sound.h"
#include "tape.h"
#include "tube.h"
#include "video_render.h"

int curmodel;
int fasttape = 0;
int selecttube = -1;
int cursid = 0;
int sidmethod = 0;

void config_load()
{
        int c;
        char s[256];
        char *p;
        sprintf(s, "%sb-em.cfg", exedir);
        //printf("%s\n",s);
        set_config_file(s);
        
        p = (char *)get_config_string(NULL, "disc0", NULL);
        if (p) strcpy(discfns[0], p);
        else   discfns[0][0] = 0;
        p = (char *)get_config_string(NULL, "disc1", NULL);
        if (p) strcpy(discfns[1], p);
        else   discfns[1][0] = 0;
//        p=(char *)get_config_string(NULL,"tape",NULL);
//        if (p) strcpy(tapefn,p);
        /*else   */tape_fn[0] = 0;

        defaultwriteprot = get_config_int(NULL, "defaultwriteprotect", 1);
        
        curmodel        = get_config_int(NULL, "model",         3);
        selecttube      = get_config_int(NULL, "tube",         -1);
        tube_6502_speed = get_config_int(NULL, "tube6502speed", 1);

        sound_internal     = get_config_int(NULL, "sndinternal",   1);
        sound_beebsid      = get_config_int(NULL, "sndbeebsid",    1);
        sound_music5000    = get_config_int(NULL, "sndmusic5000",  0);
        sound_dac          = get_config_int(NULL, "snddac    ",    0);
        sound_ddnoise      = get_config_int(NULL, "sndddnoise",    1);
        sound_tape         = get_config_int(NULL, "sndtape",       0);
        
        sound_filter    = get_config_int(NULL, "soundfilter",   1);
        curwave         = get_config_int(NULL, "soundwave",     0);

        sidmethod       = get_config_int(NULL, "sidmethod",     0);
        cursid          = get_config_int(NULL, "cursid",        2);
        
        ddnoise_vol     = get_config_int(NULL, "ddvol",         2);
        ddnoise_type    = get_config_int(NULL, "ddtype",        0);
        
        vid_fullborders = get_config_int(NULL, "fullborders",   1);
        
        c               = get_config_int(NULL, "displaymode",   3);
        vid_scanlines = (c == 2);
        vid_interlace = (c == 1) || (c == 5);
        vid_linedbl   = (c == 3);
        vid_pal       = (c == 4) || (c == 5);
        videoresize     = get_config_int(NULL, "video_resize",  0);
        
        fasttape        = get_config_int(NULL, "fasttape",      0);

        ide_enable      = get_config_int(NULL, "ideenable",     0);
        
        keyas           = get_config_int(NULL, "key_as",        0);

        mouse_amx       = get_config_int(NULL, "mouse_amx",     0);
        
        for (c = 0; c < 128; c++)
        {
                sprintf(s, "key_define_%03i", c);
                keylookup[c] = get_config_int("user_keyboard", s, c);
        }
}

void config_save()
{
        int c;
        char s[256];
        sprintf(s, "%sb-em.cfg", exedir);
        set_config_file(s);
        set_config_string(NULL, "disc0", discfns[0]);
        set_config_string(NULL, "disc1", discfns[1]);
//        set_config_string(NULL,"tape",tape_fn);
        
        set_config_int(NULL, "defaultwriteprotect", defaultwriteprot);
        
        set_config_int(NULL, "model", curmodel);
        set_config_int(NULL, "tube", selecttube);
        set_config_int(NULL, "tube6502speed", tube_6502_speed);
        
        set_config_int(NULL, "sndinternal", sound_internal);
        set_config_int(NULL, "sndbeebsid",  sound_beebsid);
        set_config_int(NULL, "sndmusic5000",sound_music5000);
        set_config_int(NULL, "snddac",      sound_dac);
        set_config_int(NULL, "sndddnoise",  sound_ddnoise);
        set_config_int(NULL, "sndtape",     sound_tape);
        
        set_config_int(NULL, "soundfilter", sound_filter);
        set_config_int(NULL, "soundwave", curwave);
        
        set_config_int(NULL, "sidmethod", sidmethod);
        set_config_int(NULL, "cursid", cursid);
        
        set_config_int(NULL, "ddvol", ddnoise_vol);
        set_config_int(NULL, "ddtype", ddnoise_type);
        
        set_config_int(NULL, "fullborders", vid_fullborders);
        set_config_int(NULL, "displaymode", (vid_pal && vid_interlace) ? 5 : (vid_scanlines ? 2 : (vid_interlace ? 1 : (vid_linedbl ? 3 : (vid_pal ? 4 : 0)))));
        set_config_int(NULL, "video_resize", videoresize);

        set_config_int(NULL, "fasttape", fasttape);
        
        set_config_int(NULL, "ideenable", ide_enable);
        
        set_config_int(NULL, "key_as", keyas);
        
        set_config_int(NULL, "mouse_amx", mouse_amx);
        
        for (c = 0; c < 128; c++)
        {
                sprintf(s, "key_define_%03i", c);
                set_config_int("user_keyboard", s, keylookup[c]);
        }
}
