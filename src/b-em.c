/*B-em 0.7 by Tom Walker*/
/*Main loop*/

#include <allegro.h>
#include "b-em.h"

int fullscreen;
void dumpram();
int model;
int uefena;
int soundon;
int logging;
AUDIOSTREAM *as;
volatile int fps;
unsigned char *ram;
int quit=0;
char uefname[260];

static int scupdate=0;
int sndupdate=0;
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

int main()
{
        unsigned short *p;
//        printf("B-em v0.7\n");
//        atexit(dumpram2);
        load_config();
        loadcmos();
        allegro_init();
        install_keyboard();
        key_led_flag=0;
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
        if (!uefena && model<4) trapos();
        set_window_title("B-em 0.71");
        install_int_ex(update50,MSEC_TO_TIMER(20));
        install_int_ex(update200,BPS_TO_TIMER(65));
        while (!quit)
        {
                exec6502(312,128);
                if (logging) logsound();
//                drawscr();
                checkkeys();
//                poll_joystick();
                if (soundon && !fullscreen)
                {
                        p=0;
                        while (!p)
                              p=(unsigned short *)get_audio_stream_buffer(as);
                        updatebuffer(p,624);
                        free_audio_stream_buffer(as);
                }
                else
                {
                        while (!scupdate)
                        {
                                yield_timeslice();
                                if (soundon && sndupdate)
                                {
                                        p=(unsigned short *)get_audio_stream_buffer(as);
                                        if (p)
                                        {
                                                updatebuffer(p,624);
                                                free_audio_stream_buffer(as);
                                        }
                                        sndupdate=0;
                                }
                        }
                        scupdate=0;
                        if (soundon && sndupdate)
                        {
                                p=(unsigned short *)get_audio_stream_buffer(as);
                                if (p)
                                {
                                        updatebuffer(p,624);
                                        free_audio_stream_buffer(as);
                                }
                                sndupdate=0;
                        }
                }
                if (key[KEY_F12])
                {
                        if (key[KEY_LCONTROL] || key[KEY_RCONTROL])
                        {
                                resetsysvia();
                                resetuservia();
                                memset(ram,0,65536);
                        }
//                        resetacia();
//                        initserial();
//                        reset1770();
//                        reset8271(0);
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
//        dumpram();
        allegro_exit();
        save_config();
        savecmos();
//        savebuffers();
//        dumpregs();
        checkdiscchanged(0);
        checkdiscchanged(1);
//        dumpram2();
//        printf("%i\n",vidbank);
//        dumpcrtc();
        return 0;
}

END_OF_MAIN();
