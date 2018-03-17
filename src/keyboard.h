#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

extern int kbdips;

extern int keylookup[128];
extern int keyas;

extern void key_clear();
extern int key_any_down();
extern void key_check();
extern void key_scan(int row, int col);
extern int key_is_down(void);

#endif
