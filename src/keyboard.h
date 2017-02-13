#ifndef __INC_KEYBOARD_H
#define __INC_KEYBOARD_H

extern int keycol,keyrow;
extern int bbckey[16][16];

extern int keylookup[128];
extern int keyas;

void key_press(int row, int col);
void key_release(int row, int col);
void key_clear();
void key_check();
void key_update();

#endif
