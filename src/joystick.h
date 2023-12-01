#ifndef BEM_JOYSTICK_INC
#define BEM_JOYSTICK_INC

typedef struct {
    const char *sect;
    const char *name;
} joymap_t;

extern joymap_t *joymaps;
extern int joymap_count;
extern int joymap_index[2];

extern int joystick_count;
extern int joystick_index[2];
extern const char ** joystick_names;

void joystick_init(ALLEGRO_EVENT_QUEUE *queue);
void joystick_axis(ALLEGRO_EVENT *event);
void joystick_button_down(ALLEGRO_EVENT *event);
void joystick_button_up(ALLEGRO_EVENT *event);
void remap_joystick(int joystick);
void change_joystick(int joystick, int index);
void joystick_rescan_sticks();

#endif
