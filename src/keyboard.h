#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

extern int kbdips;

extern int keylookup[ALLEGRO_KEY_MAX];
extern bool keyas;

extern int key_map(ALLEGRO_EVENT *event);
extern void key_down(int code);
extern void key_up(int code);

extern void key_clear(void);
extern void key_check(void);
extern void key_paste_start(char *str);
extern void key_paste_poll(void);
extern void key_scan(int row, int col);
extern int key_is_down(void);

#endif
