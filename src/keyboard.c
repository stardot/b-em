#include <allegro.h>
#include "b-em.h"
#include "via.h"
#include "sysvia.h"
#include "keyboard.h"
#include "model.h"

int keylookup[128];
int keyas = 0;

int keycol, keyrow;
int bbckey[16][16];

/*Keyboard*/
#include "scan2bbc.h"

static uint8_t codeconvert[128]=
{
        0,30,48,46,32,18,33,34,    //0
        35,23,36,37,38,50,49,24,   //8
        25,16,19,31,20,22,47,17,   //16
        45,21,44,11,2,3,4,5,       //24
        6,7,8,9,10,100,101,102,       //32
        103,104,105,106,107,108,109,59,   //40
        60,61,62,63,64,65,66,67,   //48
        68,87,88,1,41,12,13,14,    //56
        15,26,27,28,39,40,43,86,   //64
        51,52,53,57,0,83,116,79,     //72
        0,115,75,77,72,80,114,55,      //80
        74,78,83,111,84,0,115,125,   //88
        112,121,123,42,54,29,0,56, //96
        0,39,92,56,70,69,58,0,     //104
        0,0,58,0,0,0,0,0,          //112
        0,0,0,0,0,113,58,0,          //120
};

void key_press(int row, int col)
{
        bbckey[col][row] = 1;
}

void key_release(int row, int col)
{
        bbckey[col][row] = 0;
}

static inline int TranslateKey(int index, int *row, int *col)
{
        unsigned int vkey = scan2bbc[index & 127];
        if (vkey == 0xaa) return -1;
        *col = vkey & 15;
        *row = (vkey >> 4) & 15;
        return *row;
}

static int keys2[128];

void key_clear()
{
        int c;
        int row, col;
        for (c = 0; c < 128; c++)
        {
                keys2[c] = 0;
                if (TranslateKey(codeconvert[keylookup[c]], &row, &col) > 0) key_release(row,col);
        }
}

void key_check()
{
        int c;
        int row,col;
        int rc;
//        if (key[KEY_A]) printf("KEY_A!\n");
        memset(bbckey, 0, sizeof(bbckey));
        for (c = 0; c < 128; c++)
        {
                rc = c;
                if (keyas && c == KEY_A) rc = KEY_CAPSLOCK;
//                if (keyas && c==KEY_S) rc=KEY_LCONTROL;
                if (key[c] && rc != KEY_F11)
                {
//                bem_debugf("%i %i\n",c,rc);
                        if (TranslateKey(codeconvert[keylookup[rc]], &row, &col)>0)
                        {
                                if (key[c])
                                   key_press(row, col);
//                                else
//                                   releasekey(row,col);
                        }
                }
        }
        if (key[keylookup[KEY_RSHIFT]] || key[keylookup[KEY_LSHIFT]] || autoboot)
           key_press(0, 0);
        if (autoboot)
           key_press(3, 2);
        if (key[keylookup[KEY_LCONTROL]] || key[keylookup[KEY_RCONTROL]] || (keyas && key[KEY_S]))
           key_press(0, 1);
        for (c = 0; c < 128; c++)
            keys2[c] = key[c];
        key_update();
}

void key_update()
{
        int c,d;
        if (IC32 & 8)
        {
                for (d = 0; d < ((MASTER) ? 13 : 10); d++)
                {
                        for (c = 1; c < 8; c++)
                        {
                                if (bbckey[d][c])
                                {
                                        sysvia_set_ca2(1);
                                        return;
                                }
                        }
                }
        }
        else
        {
                if (keycol < ((MASTER) ? 13 : 10))
                {
                        for (c = 1; c < 8; c++)
                        {
                                if (bbckey[keycol][c])
                                {
                                        sysvia_set_ca2(1);
                                        return;
                                }
                        }
                }
        }
        sysvia_set_ca2(0);
}

void key_set_DIPS(uint8_t dips)
{
        int c;
        for (c = 9; c >= 2; c--)
        {
                if (dips & 1)
                   key_press(0, c);
                else
                   key_release(0, c);
                dips >>= 1;
        }
}
