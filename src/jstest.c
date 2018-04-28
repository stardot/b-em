#include <allegro5/allegro.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void jsenum(int num_js)
{
    ALLEGRO_JOYSTICK *js;
    const char *js_name, *stick_name;
    int js_num, num_btn, btn_num, num_stick, stick_num, num_axes, axis_num;

    for (js_num = 0; js_num < num_js; js_num++) {
        if ((js = al_get_joystick(js_num))) {
            js_name = al_get_joystick_name(js);
            num_btn = al_get_joystick_num_buttons(js);
            num_stick = al_get_joystick_num_sticks(js);
            printf("Joystick #%d, name='%s', %d buttons, %d sicks\n", js_num, js_name, num_btn, num_stick);
            for (btn_num = 0; btn_num < num_btn; btn_num++)
                printf("  Button #%d: name='%s'\n", btn_num, al_get_joystick_button_name(js, btn_num));
            for (stick_num = 0; stick_num < num_stick; stick_num++) {
                stick_name = al_get_joystick_stick_name(js, stick_num);
                num_axes = al_get_joystick_num_axes(js, stick_num);
                printf("  Stick #%d, name='%s', %d axes\n", stick_num, stick_name, num_axes);
                for (axis_num = 0; axis_num < num_axes; axis_num++)
                    printf("    Axis #%d, name='%s'\n", axis_num, al_get_joystick_axis_name(js, stick_num, axis_num));
            }
        }
    }
}

static void jsevents(void)
{
    ALLEGRO_EVENT_QUEUE *queue;
    ALLEGRO_EVENT event;

    queue = al_create_event_queue();
    al_register_event_source(queue, al_get_joystick_event_source());
    for (;;) {
        al_wait_for_event(queue, &event);
        switch(event.type) {
            case ALLEGRO_EVENT_JOYSTICK_AXIS:
                printf("joystick %s, stick #%d, axis%d, value=%.6f\n", al_get_joystick_name(event.joystick.id), event.joystick.stick, event.joystick.axis, event.joystick.pos);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN:
                printf("joystick %s, button #%d down\n", al_get_joystick_name(event.joystick.id), event.joystick.button);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_UP:
                printf("joystick %s, button #%d down\n", al_get_joystick_name(event.joystick.id), event.joystick.button);
                break;
        }
    }
}

int main(int argc, char **argv)
{
    int status, num_js;

    if (al_init()) {
        if (al_install_joystick()) {
            if ((num_js = al_get_num_joysticks())) {
                status = 0;
                jsenum(num_js);
                if (argc == 2 && strcmp(argv[1], "-e") == 0)
                    jsevents();
            }
            else {
                fputs("jsenum: no joysticks found\n", stderr);
                status = 3;
            }
        }
        else {
            fputs("jsenum: unable to install joystick driver\n", stderr);
            status = 2;
        }
    }
    else {
        fputs("jsenum: unable to initialise Allegro\n", stderr);
        status = 2;
    }
    return status;
}
