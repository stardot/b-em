#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

extern int kbdips;

#define KEY_ACTION_MAX 6

struct key_act_const {
    const char *name;
    int keycode;
    bool altstate;
    void (*downfunc)(void);
    void (*upfunc)(void);
};

struct key_act_lookup {
    int keycode;
    bool altstate;
};

extern const struct key_act_const keyact_const[KEY_ACTION_MAX];
extern struct key_act_lookup keyactions[KEY_ACTION_MAX];

extern const uint8_t key_allegro2bbc[ALLEGRO_KEY_MAX];
extern uint8_t keylookup[ALLEGRO_KEY_MAX];
extern bool keyas;
extern bool keylogical;
extern bool keypad;

extern int key_map_keypad(const ALLEGRO_EVENT *event);
extern void key_down_event(const ALLEGRO_EVENT *event);
extern void key_char_event(const ALLEGRO_EVENT *event);
extern void key_up_event(const ALLEGRO_EVENT *event);
extern void key_lost_focus(void);

extern void key_down(uint8_t code);
extern void key_up(uint8_t code);

extern void key_init(void);
extern void key_reset(void);
extern void key_check(void);
extern void key_paste_poll(void);
extern void key_scan(int row, int col);
extern bool key_is_down(void);
extern bool key_any_down(void);
extern bool key_code_down(int code);

#endif
