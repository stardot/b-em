#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

#include <allegro5/allegro.h>

extern int kbdips;

extern int keylookup[ALLEGRO_KEY_MAX];
extern int keyas;

extern void key_clear();
extern void key_check();
extern void key_scan(int row, int col);
extern int key_is_down(void);

#endif
