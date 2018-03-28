#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

extern int kbdips;

extern int keylookup[ALLEGRO_KEY_MAX];
extern int keyas;

extern void key_down(ALLEGRO_EVENT *event);
extern void key_up(ALLEGRO_EVENT *event);

extern void key_clear(void);
extern void key_check(void);
extern void key_paste_start(char *str);
extern void key_paste_poll(void);
extern void key_scan(int row, int col);
extern int key_is_down(void);

#endif
