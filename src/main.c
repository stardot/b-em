/*B-em v2.0 by Tom Walker
  Main loop + start/finish code*/

#include <allegro.h>
#ifdef WIN32
#include <winalleg.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b-em.h"
#include "serial.h"

#undef printf

int oldmodel;
int I8271,WD1770,BPLUS,x65c02,MASTER,MODELA,OS01;
int curmodel,curtube,oldmodel;
int samples;
int comedyblit;
int linedbl;
int fasttape=0;
int selecttube=-1;
int cursid=0;
int sidmethod=0;
int sndinternal=1,sndbeebsid=1,sndddnoise=1;

int joybutton[2];

MODEL models[]=
{
        {"BBC A w/OS 0.1",       1,0,0,0,0,0,1,1,"",     "a01",  "",         romsetup_os01,         -1},
        {"BBC B w/OS 0.1",       1,0,0,0,0,0,0,1,"",     "a01",  "",         romsetup_os01,         -1},
        {"BBC A",                1,0,0,0,0,0,1,0,"os",   "a",    "",         NULL,                  -1},
        {"BBC B w/8271 FDC",     1,0,0,0,0,0,0,0,"os",   "b",    "",         NULL,                  -1},
        {"BBC B w/8271+SWRAM",   1,0,0,0,0,1,0,0,"os",   "b",    "",         NULL,                  -1},
        {"BBC B w/1770 FDC",     0,1,0,0,0,1,0,0,"os",   "b1770","",         NULL,                  -1},
        {"BBC B US",             1,0,0,0,0,0,0,0,"usmos","us",   "",         NULL,                  -1},
        {"BBC B German",         1,0,0,0,0,0,0,0,"deos", "us",   "",         NULL,                  -1},
        {"BBC B+ 64K",           0,1,0,1,0,0,0,0,"bpos", "bp",   "",         NULL,                  -1},
        {"BBC B+ 128K",          0,1,0,1,0,0,0,0,"bpos", "bp",   "",         romsetup_bplus128,     -1},
        {"BBC Master 128",       0,1,1,0,1,0,0,0,"","master",    "cmos.bin", romsetup_master128,    -1},
        {"BBC Master 512",       0,1,1,0,1,0,0,0,"","master",    "cmos.bin", romsetup_master128,    3},
        {"BBC Master Turbo",     0,1,1,0,1,0,0,0,"","master",    "cmos.bin", romsetup_master128,    0},
        {"BBC Master Compact",   0,1,1,0,1,0,0,0,"","compact",   "cmosc.bin",romsetup_mastercompact,-1},
        {"ARM Evaluation System",0,1,1,0,1,0,0,0,"","master",    "cmosa.bin",romsetup_master128,    1},
        {0,0,0,0,0,0}
};

int _modelcount=0;
char *getmodel()
{
        return models[_modelcount++].name;
}

typedef struct
{
        char name[32];
        void (*init)();
        void (*reset)();
} TUBE;

TUBE tubes[]=
{
        {"6502",tubeinit6502,tubereset6502},
        {"ARM",tubeinitarm,resetarm},
        {"Z80",tubeinitz80,resetz80},
        {"80186",tubeinitx86,resetx86},
        {"65816",tubeinit65816,reset65816},
        {0,0,0}
};

int frames;

FILE *arclog;
void rpclog(const char *format, ...)
{
   char buf[256];
 return;
        if (!arclog) arclog=fopen("b-emlog.txt","wt");
   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,arclog);
   fflush(arclog);
}

int printsec;
void secint()
{
        printsec=1;
}

int fcount=0;
void int50()
{
        fcount++;
}

char exedir[512];
int debug=0,debugon=0;
int ddnoiseframes=0;

void initbbc(int argc, char *argv[])
{
        char t[512],t2[512];
        int c;
        int tapenext=0,discnext=0;
        char *p;

        printf("B-em v2.0a\n");

//      comedyblit=0;
        fskipmax=1;

        initalmain(argc,argv);
        allegro_init();


        get_executable_name(exedir,511);
        p=get_filename(exedir);
        p[0]=0;
//        printf("At dir %s\n",exedir);

        loadconfig();

        append_filename(t,exedir,"roms\\tube\\ReCo6502ROM_816",511);
        if (!file_exists(t,FA_ALL,NULL) && selecttube==4) selecttube=-1;

        curtube=selecttube;
        if (models[curmodel].tube!=-1)     curtube=models[curmodel].tube;

        if (curtube!=-1) tubes[curtube].init();
        resettube();
        if (curtube!=-1) tubes[curtube].reset();
        else             tubeexec=NULL;

        disc_reset();
        ssd_reset();
        adf_reset();
        fdi_reset();
//        loaddisc(1,"BBCMaster512-Disc2-GemApplications.adf");
//        loaddisc(0,"dosplus.adl");
//        loaddisc(0,"buzz.ssd");
//        loaddisc(0,"d:/emulators/beebem381/swift/repton/repton.ssd");
//        loaddisc(1,"D:/RETRO SOFTWARE/Repton - The Lost Realms/ReptonTLR-Full-Final.ssd");
//        loaddisc(0,"d:/emulators/beebem381/swift/menu/menu/menux.dsd");
//        loaddisc(0,"exile.fdi");
//        loaddisc(1,"stuff.ssd");
        for (c=1;c<argc;c++)
        {
//                printf("%i : %s\n",c,argv[c]);
/*                if (!strcasecmp(argv[c],"-1770"))
                {
                        I8271=0;
                        WD1770=1;
                }
                else*/
#ifndef WIN32
                if (!strcasecmp(argv[c],"--help"))
                {
                        printf("B-em v2.0 command line options :\n\n");
                        printf("-mx             - start as model x (see readme.txt for models)\n");
                        printf("-tx             - start with tube x (see readme.txt for tubes)\n");
                        printf("-disc disc.ssd  - load disc.ssd into drives :0/:2\n");
                        printf("-disc1 disc.ssd - load disc.ssd into drives :1/:3\n");
                        printf("-tape tape.uef  - load tape.uef\n");
                        printf("-fasttape       - set tape speed to fast\n");
                        printf("-s              - scanlines display mode\n");
                        printf("-i              - interlace display mode\n");
                        printf("-debug          - start debugger\n");
                        exit(-1);
                }
                else
#endif
                if (!strcasecmp(argv[c],"-tape"))
                {
                        tapenext=2;
                }
                else if (!strcasecmp(argv[c],"-disc"))
                {
                        discnext=1;
                }
                else if (!strcasecmp(argv[c],"-disc1"))
                {
                        discnext=2;
                }
                else if (argv[c][0]=='-' && (argv[c][1]=='m' || argv[c][1]=='M'))
                {
                        sscanf(&argv[c][2],"%i",&curmodel);
                }
                else if (argv[c][0]=='-' && (argv[c][1]=='t' || argv[c][1]=='T'))
                {
                        sscanf(&argv[c][2],"%i",&curtube);
                }
                else if (!strcasecmp(argv[c],"-fasttape"))
                {
                        fasttape=1;
                }
                else if (argv[c][0]=='-' && (argv[c][1]=='f' || argv[c][1]=='F'))
                {
                        sscanf(&argv[c][2],"%i",&fskipmax);
                        if (fskipmax<1) fskipmax=1;
                        if (fskipmax>9) fskipmax=9;
                }
                else if (argv[c][0]=='-' && (argv[c][1]=='s' || argv[c][1]=='S'))
                {
                        comedyblit=1;
                }
                else if (!strcasecmp(argv[c],"-debug"))
                {
                        debug=debugon=1;
                }
                else if (argv[c][0]=='-' && (argv[c][1]=='i' || argv[c][1]=='I'))
                {
                        interlace=1;
                }
                else if (tapenext)
                   strcpy(tapefn,argv[c]);
                else if (discnext)
                {
                        strcpy(discfns[discnext-1],argv[c]);
                        discnext=0;
                }
                if (tapenext) tapenext--;
        }


        initvideo();
        makemode7chars();
        install_keyboard();
        install_timer();
//        install_mouse();
        initmem();
        loaddiscsamps();
        maketapenoise();

//        openuef("Nightshade (Ultimate) (B) (Tape) [side-imprint-back].hq.uef");
//        openuef("Cosmic Battlezones (US Gold-Ultimate) (B) (Tape) [side-lab].hq.uef");
        printf("Starting emulation as %s\n",models[curmodel].name);
        I8271=models[curmodel].I8271;
        WD1770=models[curmodel].WD1770;
        BPLUS=models[curmodel].bplus;
        x65c02=models[curmodel].x65c02;
        MASTER=models[curmodel].master;
        MODELA=models[curmodel].modela;
        OS01=models[curmodel].os01;
        getcwd(t,511);
        append_filename(t2,exedir,"roms",511);
        chdir(t2);
        if (models[curmodel].romsetup) models[curmodel].romsetup();
        loadroms(models[curmodel]);
        if (curtube!=-1) tubes[curtube].init();
        resettube();
        chdir(t);
        loadcmos(models[curmodel]);
        if (models[curmodel].swram) fillswram();
//        trapos();
        reset6502();
        resetsysvia();
        resetuservia();
        initserial();
        resetacia();
        reset1770();
        reset8271();
//      resetcrtc();
        initsound();
        inital();
        initadc();
//        initsid();

        initresid();
        setsidtype(sidmethod, cursid);
//                tubeinit6502();
//                tubeinitarm();
//                tubeinitz80();
//                tubeinitx86();

        startdebug();

        install_int_ex(secint,MSEC_TO_TIMER(1000));
        install_int_ex(int50,MSEC_TO_TIMER(20));

        set_display_switch_mode(SWITCH_BACKGROUND);
#ifdef WIN32
                timeBeginPeriod(1);
#endif
        oldmodel=curmodel;

        if (curtube==3) install_mouse();

//printf("Disc 0 : %s\n",discfns[0]);
//printf("Disc 1 : %s\n",discfns[1]);
//printf("Tape   : %s\n",tapefn);
        loaddisc(0,discfns[0]);
        loaddisc(1,discfns[1]);
        loadtape(tapefn);
        if (defaultwriteprot) writeprot[0]=writeprot[1]=1;
}

void restartbbc()
{
        char t[512],t2[512];
        if (curtube==3) remove_mouse();
        loadcmos(models[oldmodel]);
        oldmodel=curmodel;
        waitforready();
        I8271=models[curmodel].I8271;
        WD1770=models[curmodel].WD1770;
        BPLUS=models[curmodel].bplus;
        x65c02=models[curmodel].x65c02;
        MASTER=models[curmodel].master;
        MODELA=models[curmodel].modela;
        OS01=models[curmodel].os01;
        curtube=selecttube;
        if (models[curmodel].tube!=-1)     curtube=models[curmodel].tube;
        resetmem();
        getcwd(t,511);
        append_filename(t2,exedir,"roms",511);
        chdir(t2);
        if (models[curmodel].romsetup) models[curmodel].romsetup();
        loadroms(models[curmodel]);
        if (curtube!=-1) tubes[curtube].init();
        resettube();
        chdir(t);
        loadcmos(models[curmodel]);
        if (models[curmodel].swram) fillswram();

        memset(ram,0,131072);
        reset6502();
        reset8271();
        reset1770();
        if (curtube!=-1) tubes[curtube].reset();
        else             tubeexec=NULL;
        resettube();
        resetsysvia();
        resetuservia();
        resetsid();
        resumeready();
        if (curtube==3) install_mouse();
}

int resetting=0;
int framesrun=0;

void runbbc()
{
        int c,d;

                if ((fcount>0 || key[KEY_PGUP] || (motor && fasttape)))
                {
                        fcount--;
                        framesrun++;
                        if (key[KEY_PGUP] || (motor && fasttape)) fcount=0;
                        if (x65c02) exec65c02();
                        else        exec6502();
                        ddnoiseframes++;
                        if (ddnoiseframes>=5)
                        {
                                ddnoiseframes=0;
                                mixddnoise();
                        }
                        frames++;
                        checkkeys();
                        poll_joystick();
                        for (c=0;c<2;c++)
                        {
                                joybutton[c]=0;
                                for (d=0;d<joy[c].num_buttons;d++)
                                {
                                        if (joy[c].button[d].b) joybutton[c]=1;
                                }
                        }
                        if (key[KEY_F10] && debugon) debug=1;
                        if (key[KEY_F12] && !resetting)
                        {
                                reset6502();
                                reset8271();
                                reset1770();
                                resetsid();

//                                tubereset6502();
//                                resetarm();
//                                resetz80();
//                                resetx86();
                                if (curtube!=-1) tubes[curtube].reset();
                                resettube();

//                                if (key[KEY_LCONTROL] || key[KEY_RCONTROL])
//                                {
//                                        resetsysvia();
//                                        memset(ram,0,65536);
//                                }
                        }
                        resetting=key[KEY_F12];
//                        domouse();
                }
                else
                {
                        framesrun=0;
                        rest(1);
                }
                if (framesrun>10) fcount=0;
                if (printsec)
                {
                        rpclog("%i fps %i samples %04X %i\n",frames,samples,pc,fdctime);
                        frames=printsec=samples=0;
                }
}

void closebbc()
{
#ifdef WIN32
                timeEndPeriod(1);
#endif
//rpclog("Dump regs\n");
//        dumpregs();
//        rpclog("Dump ram\n");
//        x86dumpregs();
//        tubedumpregs();
//        dumpram();

        saveconfig();
        savecmos(models[curmodel]);

        closemem();
        closeuef();
        closecsw();
        closetube();
        closearm();
        closex86();
        close65816();
        closedisc(0);
        closedisc(1);
        closeddnoise();
        closetapenoise();

//        rpclog("closeal\n");
//        printf("We're quitting now!\n"); fflush(stdout);
        closeal();
//        rpclog("closevideo\n");
//        printf("OpenAL gone!\n"); fflush(stdout);
        closevideo();
//        rpclog("allegro_exit\n");
//        printf("Video gone!\n"); fflush(stdout);
//        allegro_exit();
//        rpclog("Done!\n");
//        printf("Allegro gone\n"); fflush(stdout);
//dumpregs65816();
}
