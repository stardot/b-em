/*B-em 0.6 by Tom Walker*/
/*Main loop*/

#include <allegro.h>
#include "b-em.h"

int uefena;
int soundon;
int logging;
AUDIOSTREAM *as;
volatile int fps;
unsigned char *ram;
int quit=0;
char uefname[260];

int main()
{
        unsigned char *p;
        printf("B-em v0.6\n");
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
        while (!quit)
        {
                exec6502(312,128);
                if (logging) logsound();
//                drawscr();
                checkkeys();
                poll_joystick();
                if (soundon)
                {
                        p=0;
                        while (!p)
                              p=(unsigned char *)get_audio_stream_buffer(as);
                        updatebuffer(p,624);
                        free_audio_stream_buffer(as);
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
                        remaketables();
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
//        dumpregs();
//        printf("%i\n",vidbank);
//        dumpcrtc();
        return 0;
}

END_OF_MAIN();
