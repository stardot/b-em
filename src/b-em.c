/*B-em 0.7 by Tom Walker*/
/*Main loop*/

#include <allegro.h>
#include "b-em.h"

int sndupdatehappened;
unsigned char *farsndbuf;
unsigned char osram[0x2000],swram[0x1000];
void dumpram2();
int model;
int uefena;
int soundon;
int logging;
AUDIOSTREAM *as;
volatile int fps;
unsigned char *ram;
int quit=0;
char uefname[260];

int scupdate=0,sndupdate=1;
static void update50()
{
        scupdate=1;
}
END_OF_FUNCTION(update50);
static void update200()
{
        sndupdate=1;
}
END_OF_FUNCTION(update200);

inline void trysoundupdate3()
{
        unsigned char *p;
        if (!soundon) return;
        p=(unsigned char *)get_audio_stream_buffer(as);
//        printf("Trying sound update %08X\n",p);
        if (p)
        {
                updatebuffer(p,624);
//                free_audio_stream_buffer(as);
                sndupdate=0;
        }
}

int main()
{
        unsigned short *p;
        printf("B-em v0.7\n");
//        atexit(dumpram2);
        load_config();
        loadcmos();
        allegro_init();
        install_keyboard();
        install_timer();
        install_mouse();
        initmem();
        loadroms();
        reset6502();
        resetsysvia();
        resetuservia();
        resetcrtc();
        resetacia();
        initserial();
        initvideo();
        initsnd();
        initadc();
        reset8271(1);
        reset1770();
        loaddiscsamps();
        openuef(uefname);
        if (!uefena) trapos();
        install_int_ex(update50,MSEC_TO_TIMER(20));
        install_int_ex(update200,BPS_TO_TIMER(60));
//        dumpinitram();
        while (!quit)
        {
                exec6502(312,128);
                copyvols();
/*                if (sndupdatehappened)
                {
                        sndupdatehappened=0;
                        updatebuffer(farsndbuf,624);
                }*/
                if (logging) logsound();
//                drawscr();
                checkkeys();
                poll_joystick();
                while (!scupdate)
                {
                        if (sndupdate) trysoundupdate3();
                        yield_timeslice();
//                        p++;
                }
                scupdate=0;
                if (key[KEY_F12])
                {
                        if (key[KEY_LCONTROL] || key[KEY_RCONTROL])
                        {
                                resetsysvia();
                                resetuservia();
                                memset(ram,0,65536);
                                memset(osram,0,sizeof(osram));
                                memset(swram,0,sizeof(swram));
                        }
//                        resetacia();
//                        initserial();
                        reset1770();
                        reset8271(0);
                        if (model<2) remaketablesa();
                        else         remaketables();
                        reset6502();
                }
                if (key[KEY_F11])
                {
                        while (key[KEY_F11]) yield_timeslice();
                        entergui();
                }
                if (mouse_b&2)
                {
                        while (mouse_b&2) yield_timeslice();
                        entergui();
                }
        }
        allegro_exit();
        save_config();
        savecmos();
//        dumpregs();
        checkdiscchanged(0);
        checkdiscchanged(1);
//        dumpram2();
//        printf("%i\n",vidbank);
//        dumpcrtc();
        return 0;
}

END_OF_MAIN();
