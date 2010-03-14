/*B-em v2.0 by Tom Walker
  Configuration handling*/

#include <allegro.h>
#include "b-em.h"

char discfns[2][260]={"",""};
char tapefn[260]="";
int tapeloaded=0;
int keylookup[128];
int fullscreen=0;
int sndtape=0;
int keyas=0;
int defaultwriteprot;

void loadconfig()
{
        int c;
        char s[256];
        char *p;
        sprintf(s,"%sb-em.cfg",exedir);
        set_config_file(s);

        p=(char *)get_config_string(NULL,"disc0",NULL);
        if (p) strcpy(discfns[0],p);
        else   discfns[0][0]=0;
        p=(char *)get_config_string(NULL,"disc1",NULL);
        if (p) strcpy(discfns[1],p);
        else   discfns[1][0]=0;
//        p=(char *)get_config_string(NULL,"tape",NULL);
//        if (p) strcpy(tapefn,p);
        /*else   */tapefn[0]=0;

        defaultwriteprot=get_config_int(NULL,"defaultwriteprotect",1);

        curmodel=get_config_int(NULL,"model",3);
        selecttube=get_config_int(NULL,"tube",-1);
        tube6502speed=get_config_int(NULL,"tube6502speed",1);

        sndinternal=get_config_int(NULL,"sndinternal",1);
        sndbeebsid=get_config_int(NULL,"sndbeebsid",1);
        sndddnoise=get_config_int(NULL,"sndddnoise",1);
        sndtape=get_config_int(NULL,"sndtape",0);

        soundfilter=get_config_int(NULL,"soundfilter",1);
        curwave=get_config_int(NULL,"soundwave",0);

        sidmethod=get_config_int(NULL,"sidmethod",0);
        cursid=get_config_int(NULL,"cursid",2);

        ddvol=get_config_int(NULL,"ddvol",2);
        ddtype=get_config_int(NULL,"ddtype",0);

        fullborders=get_config_int(NULL,"fullborders",0);
        c=get_config_int(NULL,"displaymode",3);
        comedyblit=(c==2);
        interlace=(c==1);
        linedbl=(c==3);

        fasttape=get_config_int(NULL,"fasttape",0);

        keyas=get_config_int(NULL,"key_as",0);
        for (c=0;c<128;c++)
        {
                sprintf(s,"key_define_%03i",c);
                keylookup[c]=get_config_int("user_keyboard",s,c);
        }
}

void saveconfig()
{
        int c;
        char s[256];
        sprintf(s,"%sb-em.cfg",exedir);
        set_config_file(s);
        set_config_string(NULL,"disc0",discfns[0]);
        set_config_string(NULL,"disc1",discfns[1]);
//        set_config_string(NULL,"tape",tapefn);

        set_config_int(NULL,"defaultwriteprotect",defaultwriteprot);

        set_config_int(NULL,"model",curmodel);
        set_config_int(NULL,"tube",selecttube);
        set_config_int(NULL,"tube6502speed",tube6502speed);

        set_config_int(NULL,"sndinternal",sndinternal);
        set_config_int(NULL,"sndbeebsid",sndbeebsid);
        set_config_int(NULL,"sndddnoise",sndddnoise);
        set_config_int(NULL,"sndtape",sndtape);

        set_config_int(NULL,"soundfilter",soundfilter);
        set_config_int(NULL,"soundwave",curwave);

        set_config_int(NULL,"sidmethod",sidmethod);
        set_config_int(NULL,"cursid",cursid);

        set_config_int(NULL,"ddvol",ddvol);
        set_config_int(NULL,"ddtype",ddtype);

        set_config_int(NULL,"fullborders",fullborders);
        set_config_int(NULL,"displaymode",comedyblit?2:(interlace?1:(linedbl?3:0)));

        set_config_int(NULL,"fasttape",fasttape);

        set_config_int(NULL,"key_as",keyas);

        for (c=0;c<128;c++)
        {
                sprintf(s,"key_define_%03i",c);
                set_config_int("user_keyboard",s,keylookup[c]);
        }
}
