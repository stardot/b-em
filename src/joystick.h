#ifndef BEM_JOYSTICK_INC
#define BEM_JOYSTICK_INC

typedef struct {
    const char *sect;
    const char *name;
} joymap_t;

extern joymap_t *joymaps;
extern int joymap_count;
extern int joymap_num;

void joystick_init(ALLEGRO_EVENT_QUEUE *queue);
void joystick_change_joymap(int mapno);
void joystick_axis(ALLEGRO_EVENT *event);
void joystick_button_down(ALLEGRO_EVENT *event);
void joystick_button_up(ALLEGRO_EVENT *event);

#endif
