/*B-em 0.6 by Tom Walker*/
/*Main loop*/

#include <allegro.h>
#include "b-em.h"

int model;
int uefena;
int soundon;
int logging;
AUDIOSTREAM *as;
volatile int fps;
unsigned char *ram;
int quit=0;
char uefname[260];

int scupdate=0,sndupdate=0;
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
        unsigned char *p;
        printf("B-em v0.61\n");
        load_config();
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
        install_int_ex(update200,MSEC_TO_TIMER(5));
        while (!quit)
        {
                exec6502(312,128);
                copyvols();
                if (logging) logsound();
//                drawscr();
                checkkeys();
                poll_joystick();
                while (!scupdate)
                {
                        yield_timeslice();
                        p++;
                }
                scupdate=0;
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
        allegro_exit();
        save_config();
        dumpregs();
//        printf("%i\n",vidbank);
//        dumpcrtc();
        return 0;
}

END_OF_MAIN();
