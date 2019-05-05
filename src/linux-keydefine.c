/*B-em v2.2 by Tom Walker
  Linux keyboard redefinition GUI*/

#if 0
#ifndef WIN32
#include <allegro.h>
#include "b-em.h"
#include "keyboard.h"
#include "model.h"

int keytemp[128];

char *key_names[] =
{
   "",         "A",          "B",          "C",
   "D",          "E",          "F",          "G",
   "H",          "I",          "J",          "K",
   "L",          "M",          "N",          "O",
   "P",          "Q",          "R",          "S",
   "T",          "U",          "V",          "W",
   "X",          "Y",          "Z",          "0",
   "1",          "2",          "3",          "4",
   "5",          "6",          "7",          "8",
   "9",          "0_PAD",      "1_PAD",      "2_PAD",
   "3_PAD",      "4_PAD",      "5_PAD",      "6_PAD",
   "7_PAD",      "8_PAD",      "9_PAD",      "F1",
   "F2",         "F3",         "F4",         "F5",
   "F6",         "F7",         "F8",         "F9",
   "F10",        "F11",        "F12",        "ESC",
   "TILDE",      "MINUS",      "EQUALS",     "BACKSPACE",
   "TAB",        "OPENBRACE",  "CLOSEBRACE", "ENTER",
   "COLON",      "QUOTE",      "BACKSLASH",  "BACKSLASH2",
   "COMMA",      "STOP",       "SLASH",      "SPACE",
   "INSERT",     "DEL",        "HOME",       "END",
   "PGUP",       "PGDN",       "LEFT",       "RIGHT",
   "UP",         "DOWN",       "SLASH_PAD",  "ASTERISK",
   "MINUS_PAD",  "PLUS_PAD",   "DEL_PAD",    "ENTER_PAD",
   "PRTSCR",     "PAUSE",      "ABNT_C1",    "YEN",
   "KANA",       "CONVERT",    "NOCONVERT",  "AT",
   "CIRCUMFLEX", "COLON2",     "KANJI",      "EQUALS_PAD",
   "BACKQUOTE",  "SEMICOLON",  "COMMAND",    "UNKNOWN1",
   "UNKNOWN2",   "UNKNOWN3",   "UNKNOWN4",   "UNKNOWN5",
   "UNKNOWN6",   "UNKNOWN7",   "UNKNOWN8",   "LSHIFT",
   "RSHIFT",     "LCONTROL",   "RCONTROL",   "ALT",
   "ALTGR",      "LWIN",       "RWIN",       "MENU",
   "SCRLOCK",    "NUMLOCK",    "CAPSLOCK",   "MAX"
};

int d_getkey(int msg, DIALOG *d, int cd)
{
        BITMAP *b;
        int x,y;
        int ret = d_button_proc(msg, d, cd);
        int k,k2;
        int c;
        char s[1024],s2[1024],s3[64];
        if (ret==D_EXIT)
        {
                k=(intptr_t)d->dp2;
                x=(SCREEN_W/2)-100;
                y=(SCREEN_H/2)-36;
                b=create_bitmap(200,72);
                blit(screen,b,x,y,0,0,200,72);
                rectfill(screen,x,y,x+199,y+71,makecol(0,0,0));
                rect(screen,x,y,x+199,y+71,makecol(255,255,255));
                if (d->dp3) textprintf_ex(screen,font,x+8,y+8,makecol(255,255,255),makecol(0,0,0),"Redefining %s",(char *)d->dp3);
                else        textprintf_ex(screen,font,x+8,y+8,makecol(255,255,255),makecol(0,0,0),"Redefining %s",(char *)d->dp);
                textprintf_ex(screen,font,x+8,y+24,makecol(255,255,255),makecol(0,0,0),"Assigned to PC key(s) :");

                s[0]=0;
                for (c=0;c<128;c++)
                {
                        if (keylookup[c]==k)
                        {
                                if (s[0]) sprintf(s3,", %s",key_names[c]);
                                else      sprintf(s3,"%s",key_names[c]);
                                sprintf(s2,"%s%s",s,s3);
                                strcpy(s,s2);
                        }
                }

                textprintf_ex(screen,font,x+8,y+40,makecol(255,255,255),makecol(0,0,0),s);
                
                textprintf_ex(screen,font,x+8,y+56,makecol(255,255,255),makecol(0,0,0),"Please press new key...");
getnewkey:
                while (!keypressed());
                k2=readkey()>>8;
                if (k2==KEY_F11 || k2==KEY_F12) goto getnewkey;
                keylookup[k2]=k;
                
                blit(b,screen,0,0,x,y,200,72);
                destroy_bitmap(b);
                while (key[KEY_SPACE]);
                while (keypressed()) readkey();
                return 0;
        }
        return ret;
}

DIALOG bemdefinegui[]=
{
        {d_box_proc, 0, 0, 538,  256, 15,15,0,0,     0,0,0, NULL,NULL},

        {d_button_proc, 205,218,60,28, 15,15,0,D_CLOSE, 0,0,"OK", NULL,NULL},
        {d_button_proc, 271,218,60,28, 15,15,0,D_CLOSE, 0,0,"Cancel", NULL,NULL},

        {d_getkey, 82,10,28,28, 15,15,0,D_EXIT, 0,0,"F0",  (void *)KEY_F1,NULL},
        {d_getkey, 114,10,28,28, 15,15,0,D_EXIT, 0,0,"F1", (void *)KEY_F2,NULL},
        {d_getkey, 146,10,28,28, 15,15,0,D_EXIT, 0,0,"F2", (void *)KEY_F3,NULL},
        {d_getkey, 178,10,28,28, 15,15,0,D_EXIT, 0,0,"F3", (void *)KEY_F4,NULL},
        {d_getkey, 210,10,28,28, 15,15,0,D_EXIT, 0,0,"F4", (void *)KEY_F5,NULL},
        {d_getkey, 242,10,28,28, 15,15,0,D_EXIT, 0,0,"F5", (void *)KEY_F6,NULL},
        {d_getkey, 274,10,28,28, 15,15,0,D_EXIT, 0,0,"F6", (void *)KEY_F7,NULL},
        {d_getkey, 306,10,28,28, 15,15,0,D_EXIT, 0,0,"F7", (void *)KEY_F8,NULL},
        {d_getkey, 338,10,28,28, 15,15,0,D_EXIT, 0,0,"F8", (void *)KEY_F9,NULL},
        {d_getkey, 370,10,28,28, 15,15,0,D_EXIT, 0,0,"F9", (void *)KEY_F10,NULL},
        {d_getkey, 10,42,28,28, 15,15,0,D_EXIT, 0,0,"ESC", (void *)KEY_ESC,NULL},
        {d_getkey, 42,42,28,28, 15,15,0,D_EXIT, 0,0,"1", (void *)KEY_1,NULL},
        {d_getkey, 74,42,28,28, 15,15,0,D_EXIT, 0,0,"2", (void *)KEY_2,NULL},
        {d_getkey, 106,42,28,28, 15,15,0,D_EXIT, 0,0,"3", (void *)KEY_3,NULL},
        {d_getkey, 138,42,28,28, 15,15,0,D_EXIT, 0,0,"4", (void *)KEY_4,NULL},
        {d_getkey, 170,42,28,28, 15,15,0,D_EXIT, 0,0,"5", (void *)KEY_5,NULL},
        {d_getkey, 202,42,28,28, 15,15,0,D_EXIT, 0,0,"6", (void *)KEY_6,NULL},
        {d_getkey, 234,42,28,28, 15,15,0,D_EXIT, 0,0,"7", (void *)KEY_7,NULL},
        {d_getkey, 266,42,28,28, 15,15,0,D_EXIT, 0,0,"8", (void *)KEY_8,NULL},
        {d_getkey, 298,42,28,28, 15,15,0,D_EXIT, 0,0,"9", (void *)KEY_9,NULL},
        {d_getkey, 330,42,28,28, 15,15,0,D_EXIT, 0,0,"0", (void *)KEY_0,NULL},
        {d_getkey, 362,42,28,28, 15,15,0,D_EXIT, 0,0,"=", (void *)KEY_MINUS,NULL},
        {d_getkey, 394,42,28,28, 15,15,0,D_EXIT, 0,0,"^", (void *)KEY_EQUALS,NULL},
        {d_getkey, 426,42,28,28, 15,15,0,D_EXIT, 0,0,"\\", (void *)KEY_BACKSLASH2,NULL},
        {d_getkey, 458,42,28,28, 15,15,0,D_EXIT, 0,0,"LFT", (void *)KEY_LEFT,"LEFT"},//25
        {d_getkey, 490,42,28,28, 15,15,0,D_EXIT, 0,0,"RGT", (void *)KEY_RIGHT,"RIGHT"},//26
        {d_getkey, 10,74,44,28, 15,15,0,D_EXIT, 0,0,"TAB", (void *)KEY_TAB,NULL},
        {d_getkey, 58,74,28,28, 15,15,0,D_EXIT, 0,0,"Q", (void *)KEY_Q,NULL},
        {d_getkey, 90,74,28,28, 15,15,0,D_EXIT, 0,0,"W", (void *)KEY_W,NULL},
        {d_getkey, 122,74,28,28, 15,15,0,D_EXIT, 0,0,"E", (void *)KEY_E,NULL},
        {d_getkey, 154,74,28,28, 15,15,0,D_EXIT, 0,0,"R", (void *)KEY_R,NULL},
        {d_getkey, 186,74,28,28, 15,15,0,D_EXIT, 0,0,"T", (void *)KEY_T,NULL},
        {d_getkey, 218,74,28,28, 15,15,0,D_EXIT, 0,0,"Y", (void *)KEY_Y,NULL},
        {d_getkey, 250,74,28,28, 15,15,0,D_EXIT, 0,0,"U", (void *)KEY_U,NULL},
        {d_getkey, 282,74,28,28, 15,15,0,D_EXIT, 0,0,"I", (void *)KEY_I,NULL},
        {d_getkey, 314,74,28,28, 15,15,0,D_EXIT, 0,0,"O", (void *)KEY_O,NULL},
        {d_getkey, 346,74,28,28, 15,15,0,D_EXIT, 0,0,"P", (void *)KEY_P,NULL},
        {d_getkey, 378,74,28,28, 15,15,0,D_EXIT, 0,0,"@", (void *)KEY_OPENBRACE,NULL},
        {d_getkey, 410,74,28,28, 15,15,0,D_EXIT, 0,0,"[", (void *)KEY_CLOSEBRACE,NULL},
        {d_getkey, 442,74,28,28, 15,15,0,D_EXIT, 0,0,"_", (void *)KEY_TILDE,NULL},
        {d_getkey, 474,74,28,28, 15,15,0,D_EXIT, 0,0,"UP", (void *)KEY_UP,NULL},//41
        {d_getkey, 506,74,28,28, 15,15,0,D_EXIT, 0,0,"DWN", (void *)KEY_DOWN,"DOWN"},//42
        {d_getkey, 10,106,28,28, 15,15,0,D_EXIT, 0,0,"CLK", (void *)KEY_CAPSLOCK,"CAPS LOCK"},
        {d_getkey, 42,106,28,28, 15,15,0,D_EXIT, 0,0,"CTL", (void *)KEY_LCONTROL,"CTRL"},
        {d_getkey, 74,106,28,28, 15,15,0,D_EXIT, 0,0,"A", (void *)KEY_A,NULL},
        {d_getkey, 106,106,28,28, 15,15,0,D_EXIT, 0,0,"S", (void *)KEY_S,NULL},
        {d_getkey, 138,106,28,28, 15,15,0,D_EXIT, 0,0,"D", (void *)KEY_D,NULL},
        {d_getkey, 170,106,28,28, 15,15,0,D_EXIT, 0,0,"F", (void *)KEY_F,NULL},
        {d_getkey, 202,106,28,28, 15,15,0,D_EXIT, 0,0,"G", (void *)KEY_G,NULL},
        {d_getkey, 234,106,28,28, 15,15,0,D_EXIT, 0,0,"H", (void *)KEY_H,NULL},
        {d_getkey, 266,106,28,28, 15,15,0,D_EXIT, 0,0,"J", (void *)KEY_J,NULL},
        {d_getkey, 298,106,28,28, 15,15,0,D_EXIT, 0,0,"K", (void *)KEY_K,NULL},
        {d_getkey, 330,106,28,28, 15,15,0,D_EXIT, 0,0,"L", (void *)KEY_L,NULL},
        {d_getkey, 362,106,28,28, 15,15,0,D_EXIT, 0,0,";", (void *)KEY_SEMICOLON,NULL},
        {d_getkey, 394,106,28,28, 15,15,0,D_EXIT, 0,0,":", (void *)KEY_QUOTE,NULL},
        {d_getkey, 426,106,28,28, 15,15,0,D_EXIT, 0,0,"]", (void *)KEY_BACKSLASH,NULL},
        {d_getkey, 458,106,60,28, 15,15,0,D_EXIT, 0,0,"RET", (void *)KEY_ENTER,"RETURN"},
        {d_getkey, 10,138,28,28, 15,15,0,D_EXIT, 0,0,"SLK", (void *)KEY_ALT,"SHIFT LOCK"},
        {d_getkey, 42,138,44,28, 15,15,0,D_EXIT, 0,0,"SHIFT", (void *)KEY_LSHIFT,NULL},
        {d_getkey, 90,138,28,28, 15,15,0,D_EXIT, 0,0,"Z", (void *)KEY_Z,NULL},
        {d_getkey, 122,138,28,28, 15,15,0,D_EXIT, 0,0,"X", (void *)KEY_X,NULL},
        {d_getkey, 154,138,28,28, 15,15,0,D_EXIT, 0,0,"C", (void *)KEY_C,NULL},
        {d_getkey, 186,138,28,28, 15,15,0,D_EXIT, 0,0,"V", (void *)KEY_V,NULL},
        {d_getkey, 218,138,28,28, 15,15,0,D_EXIT, 0,0,"B", (void *)KEY_B,NULL},
        {d_getkey, 250,138,28,28, 15,15,0,D_EXIT, 0,0,"N", (void *)KEY_N,NULL},
        {d_getkey, 282,138,28,28, 15,15,0,D_EXIT, 0,0,"M", (void *)KEY_M,NULL},
        {d_getkey, 314,138,28,28, 15,15,0,D_EXIT, 0,0,",", (void *)KEY_COMMA,NULL},
        {d_getkey, 346,138,28,28, 15,15,0,D_EXIT, 0,0,".", (void *)KEY_STOP,NULL},
        {d_getkey, 378,138,28,28, 15,15,0,D_EXIT, 0,0,"/", (void *)KEY_SLASH,NULL},
        {d_getkey, 410,138,44,28, 15,15,0,D_EXIT, 0,0,"SHIFT", (void *)KEY_RSHIFT,NULL},
        {d_getkey, 458,138,28,28, 15,15,0,D_EXIT, 0,0,"DEL", (void *)KEY_DEL,"DELETE"},
        {d_getkey, 490,138,28,28, 15,15,0,D_EXIT, 0,0,"CPY", (void *)KEY_END,"COPY"},//72
        {d_getkey, 122,170,256,28, 15,15,0,D_EXIT, 0,0,"SPACE", (void *)KEY_SPACE,NULL},
        
        {d_yield_proc},
        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

DIALOG bemdefineguim[]=
{
        {d_box_proc, 0, 0, 676,  256, 15,15,0,0,     0,0,0, NULL,NULL},

        {d_button_proc, 205,218,60,28, 15,15,0,D_CLOSE, 0,0,"OK", NULL,NULL},
        {d_button_proc, 271,218,60,28, 15,15,0,D_CLOSE, 0,0,"Cancel", NULL,NULL},

        {d_getkey, 50,10,28,28, 15,15,0,D_EXIT, 0,0,"F0", (void *)KEY_F1,NULL},
        {d_getkey, 82,10,28,28, 15,15,0,D_EXIT, 0,0,"F1", (void *)KEY_F2,NULL},
        {d_getkey, 114,10,28,28, 15,15,0,D_EXIT, 0,0,"F2", (void *)KEY_F3,NULL},
        {d_getkey, 146,10,28,28, 15,15,0,D_EXIT, 0,0,"F3", (void *)KEY_F4,NULL},
        {d_getkey, 178,10,28,28, 15,15,0,D_EXIT, 0,0,"F4", (void *)KEY_F5,NULL},
        {d_getkey, 210,10,28,28, 15,15,0,D_EXIT, 0,0,"F5", (void *)KEY_F6,NULL},
        {d_getkey, 242,10,28,28, 15,15,0,D_EXIT, 0,0,"F6", (void *)KEY_F7,NULL},
        {d_getkey, 274,10,28,28, 15,15,0,D_EXIT, 0,0,"F7", (void *)KEY_F8,NULL},
        {d_getkey, 306,10,28,28, 15,15,0,D_EXIT, 0,0,"F8", (void *)KEY_F9,NULL},
        {d_getkey, 338,10,28,28, 15,15,0,D_EXIT, 0,0,"F9", (void *)KEY_F10,NULL},
        {d_getkey, 10,42,28,28, 15,15,0,D_EXIT, 0,0,"ESC", (void *)KEY_ESC,NULL},
        {d_getkey, 42,42,28,28, 15,15,0,D_EXIT, 0,0,"1", (void *)KEY_1,NULL},
        {d_getkey, 74,42,28,28, 15,15,0,D_EXIT, 0,0,"2", (void *)KEY_2,NULL},
        {d_getkey, 106,42,28,28, 15,15,0,D_EXIT, 0,0,"3", (void *)KEY_3,NULL},
        {d_getkey, 138,42,28,28, 15,15,0,D_EXIT, 0,0,"4", (void *)KEY_4,NULL},
        {d_getkey, 170,42,28,28, 15,15,0,D_EXIT, 0,0,"5", (void *)KEY_5,NULL},
        {d_getkey, 202,42,28,28, 15,15,0,D_EXIT, 0,0,"6", (void *)KEY_6,NULL},
        {d_getkey, 234,42,28,28, 15,15,0,D_EXIT, 0,0,"7", (void *)KEY_7,NULL},
        {d_getkey, 266,42,28,28, 15,15,0,D_EXIT, 0,0,"8", (void *)KEY_8,NULL},
        {d_getkey, 298,42,28,28, 15,15,0,D_EXIT, 0,0,"9", (void *)KEY_9,NULL},
        {d_getkey, 330,42,28,28, 15,15,0,D_EXIT, 0,0,"0", (void *)KEY_0,NULL},
        {d_getkey, 362,42,28,28, 15,15,0,D_EXIT, 0,0,"=", (void *)KEY_MINUS,NULL},
        {d_getkey, 394,42,28,28, 15,15,0,D_EXIT, 0,0,"^", (void *)KEY_EQUALS,NULL},
        {d_getkey, 426,42,28,28, 15,15,0,D_EXIT, 0,0,"\\", (void *)KEY_BACKSLASH2,NULL},
        {d_getkey, 458,42,28,28, 15,15,0,D_EXIT, 0,0,"LFT", (void *)KEY_LEFT,"LEFT"},//25
        {d_getkey, 490,42,28,28, 15,15,0,D_EXIT, 0,0,"RGT", (void *)KEY_RIGHT,"RIGHT"},//26
        {d_getkey, 10,74,44,28, 15,15,0,D_EXIT, 0,0,"TAB", (void *)KEY_TAB,NULL},
        {d_getkey, 58,74,28,28, 15,15,0,D_EXIT, 0,0,"Q", (void *)KEY_Q,NULL},
        {d_getkey, 90,74,28,28, 15,15,0,D_EXIT, 0,0,"W", (void *)KEY_W,NULL},
        {d_getkey, 122,74,28,28, 15,15,0,D_EXIT, 0,0,"E", (void *)KEY_E,NULL},
        {d_getkey, 154,74,28,28, 15,15,0,D_EXIT, 0,0,"R", (void *)KEY_R,NULL},
        {d_getkey, 186,74,28,28, 15,15,0,D_EXIT, 0,0,"T", (void *)KEY_T,NULL},
        {d_getkey, 218,74,28,28, 15,15,0,D_EXIT, 0,0,"Y", (void *)KEY_Y,NULL},
        {d_getkey, 250,74,28,28, 15,15,0,D_EXIT, 0,0,"U", (void *)KEY_U,NULL},
        {d_getkey, 282,74,28,28, 15,15,0,D_EXIT, 0,0,"I", (void *)KEY_I,NULL},
        {d_getkey, 314,74,28,28, 15,15,0,D_EXIT, 0,0,"O", (void *)KEY_O,NULL},
        {d_getkey, 346,74,28,28, 15,15,0,D_EXIT, 0,0,"P", (void *)KEY_P,NULL},
        {d_getkey, 378,74,28,28, 15,15,0,D_EXIT, 0,0,"@", (void *)KEY_OPENBRACE,NULL},
        {d_getkey, 410,74,28,28, 15,15,0,D_EXIT, 0,0,"[", (void *)KEY_CLOSEBRACE,NULL},
        {d_getkey, 442,74,28,28, 15,15,0,D_EXIT, 0,0,"_", (void *)KEY_TILDE,NULL},
        {d_getkey, 474,10,28,28, 15,15,0,D_EXIT, 0,0,"UP", (void *)KEY_UP,NULL},//41
        {d_getkey, 474,74,28,28, 15,15,0,D_EXIT, 0,0,"DWN", (void *)KEY_DOWN,"DOWN"},//42
        {d_getkey, 10,106,28,28, 15,15,0,D_EXIT, 0,0,"CLK", (void *)KEY_CAPSLOCK,"CAPS LOCK"},
        {d_getkey, 42,106,28,28, 15,15,0,D_EXIT, 0,0,"CTL", (void *)KEY_LCONTROL,"CTRL"},
        {d_getkey, 74,106,28,28, 15,15,0,D_EXIT, 0,0,"A", (void *)KEY_A,NULL},
        {d_getkey, 106,106,28,28, 15,15,0,D_EXIT, 0,0,"S", (void *)KEY_S,NULL},
        {d_getkey, 138,106,28,28, 15,15,0,D_EXIT, 0,0,"D", (void *)KEY_D,NULL},
        {d_getkey, 170,106,28,28, 15,15,0,D_EXIT, 0,0,"F", (void *)KEY_F,NULL},
        {d_getkey, 202,106,28,28, 15,15,0,D_EXIT, 0,0,"G", (void *)KEY_G,NULL},
        {d_getkey, 234,106,28,28, 15,15,0,D_EXIT, 0,0,"H", (void *)KEY_H,NULL},
        {d_getkey, 266,106,28,28, 15,15,0,D_EXIT, 0,0,"J", (void *)KEY_J,NULL},
        {d_getkey, 298,106,28,28, 15,15,0,D_EXIT, 0,0,"K", (void *)KEY_K,NULL},
        {d_getkey, 330,106,28,28, 15,15,0,D_EXIT, 0,0,"L", (void *)KEY_L,NULL},
        {d_getkey, 362,106,28,28, 15,15,0,D_EXIT, 0,0,";", (void *)KEY_SEMICOLON,NULL},
        {d_getkey, 394,106,28,28, 15,15,0,D_EXIT, 0,0,":", (void *)KEY_QUOTE,NULL},
        {d_getkey, 426,106,28,28, 15,15,0,D_EXIT, 0,0,"]", (void *)KEY_BACKSLASH,NULL},
        {d_getkey, 458,106,60,28, 15,15,0,D_EXIT, 0,0,"RET", (void *)KEY_ENTER,"RETURN"},
        {d_getkey, 10,138,28,28, 15,15,0,D_EXIT, 0,0,"SLK", (void *)KEY_ALT,"SHIFT LOCK"},
        {d_getkey, 42,138,44,28, 15,15,0,D_EXIT, 0,0,"SHIFT", (void *)KEY_LSHIFT,NULL},
        {d_getkey, 90,138,28,28, 15,15,0,D_EXIT, 0,0,"Z", (void *)KEY_Z,NULL},
        {d_getkey, 122,138,28,28, 15,15,0,D_EXIT, 0,0,"X", (void *)KEY_X,NULL},
        {d_getkey, 154,138,28,28, 15,15,0,D_EXIT, 0,0,"C", (void *)KEY_C,NULL},
        {d_getkey, 186,138,28,28, 15,15,0,D_EXIT, 0,0,"V", (void *)KEY_V,NULL},
        {d_getkey, 218,138,28,28, 15,15,0,D_EXIT, 0,0,"B", (void *)KEY_B,NULL},
        {d_getkey, 250,138,28,28, 15,15,0,D_EXIT, 0,0,"N", (void *)KEY_N,NULL},
        {d_getkey, 282,138,28,28, 15,15,0,D_EXIT, 0,0,"M", (void *)KEY_M,NULL},
        {d_getkey, 314,138,28,28, 15,15,0,D_EXIT, 0,0,",", (void *)KEY_COMMA,NULL},
        {d_getkey, 346,138,28,28, 15,15,0,D_EXIT, 0,0,".", (void *)KEY_STOP,NULL},
        {d_getkey, 378,138,28,28, 15,15,0,D_EXIT, 0,0,"/", (void *)KEY_SLASH,NULL},
        {d_getkey, 410,138,44,28, 15,15,0,D_EXIT, 0,0,"SHIFT", (void *)KEY_RSHIFT,NULL},
        {d_getkey, 458,138,28,28, 15,15,0,D_EXIT, 0,0,"DEL", (void *)KEY_DEL,"DELETE"},
        {d_getkey, 490,138,28,28, 15,15,0,D_EXIT, 0,0,"CPY", (void *)KEY_END,"COPY"},//72
        {d_getkey, 122,170,256,28, 15,15,0,D_EXIT, 0,0,"SPACE", (void *)KEY_SPACE,NULL},

        {d_getkey, 538,10,28,28, 15,15,0,D_EXIT, 0,0,"+", (void *)KEY_PLUS_PAD, NULL},
        {d_getkey, 570,10,28,28, 15,15,0,D_EXIT, 0,0,"-", (void *)KEY_MINUS_PAD, NULL},
        {d_getkey, 602,10,28,28, 15,15,0,D_EXIT, 0,0,"/", (void *)KEY_SLASH_PAD, NULL},
        {d_getkey, 634,10,28,28, 15,15,0,D_EXIT, 0,0,"*", (void *)KEY_ASTERISK, NULL},
        {d_getkey, 538,42,28,28, 15,15,0,D_EXIT, 0,0,"7", (void *)KEY_7_PAD, NULL},
        {d_getkey, 570,42,28,28, 15,15,0,D_EXIT, 0,0,"8", (void *)KEY_8_PAD, NULL},
        {d_getkey, 602,42,28,28, 15,15,0,D_EXIT, 0,0,"9", (void *)KEY_9_PAD, NULL},
        {d_getkey, 634,42,28,28, 15,15,0,D_EXIT, 0,0,"#", (void *)KEY_DOWN, NULL},
        {d_getkey, 538,74,28,28, 15,15,0,D_EXIT, 0,0,"4", (void *)KEY_4_PAD, NULL},
        {d_getkey, 570,74,28,28, 15,15,0,D_EXIT, 0,0,"5", (void *)KEY_5_PAD, NULL},
        {d_getkey, 602,74,28,28, 15,15,0,D_EXIT, 0,0,"6", (void *)KEY_6_PAD, NULL},
        {d_getkey, 634,74,28,28, 15,15,0,D_EXIT, 0,0,"DEL", (void *)KEY_DEL_PAD, "DELETE"},
        {d_getkey, 538,106,28,28, 15,15,0,D_EXIT, 0,0,"1", (void *)KEY_1_PAD, NULL},
        {d_getkey, 570,106,28,28, 15,15,0,D_EXIT, 0,0,"2", (void *)KEY_2_PAD, NULL},
        {d_getkey, 602,106,28,28, 15,15,0,D_EXIT, 0,0,"3", (void *)KEY_3_PAD, NULL},
        {d_getkey, 634,106,28,28, 15,15,0,D_EXIT, 0,0,",", (void *)KEY_HOME, NULL},
        {d_getkey, 538,138,28,28, 15,15,0,D_EXIT, 0,0,"0", (void *)KEY_0_PAD, NULL},
        {d_getkey, 570,138,28,28, 15,15,0,D_EXIT, 0,0,".", (void *)KEY_PGDN, NULL},
        {d_getkey, 602,138,60,28, 15,15,0,D_EXIT, 0,0,"RETURN", (void *)KEY_ENTER_PAD, NULL},

        {d_yield_proc},
        {0,0,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL}
};

int gui_keydefine()
{
        DIALOG_PLAYER *dp;
        BITMAP *b;
        DIALOG *d=MASTER?bemdefineguim:bemdefinegui;
        int x=0,y;
        while (d[x].proc)
        {
                d[x].x+=(SCREEN_W/2)-(d[0].w/2);
                d[x].y+=(SCREEN_H/2)-(d[0].h/2);
                d[x].fg=0xFFFFFF;
                if (x>=1 && x<=10) d[x].bg=makecol(127,0,0);
                if (x==25 || x==26 || x==41 || x==42 || x==72) d[x].bg=makecol(127,127,127);
                x++;
        }
        for (x=0;x<128;x++) keytemp[x]=keylookup[x];
        x=(SCREEN_W/2)-(d[0].w/2);
        y=(SCREEN_H/2)-(d[0].h/2);
        b=create_bitmap(d[0].w,d[0].h);
        blit(screen,b,x,y,0,0,d[0].w,d[0].h);
        dp=init_dialog(d,0);
        do
            x=update_dialog(dp);
        while (x && !key[KEY_F11] && !(mouse_b&2) && !key[KEY_ESC]);
        shutdown_dialog(dp);
        if (x==1)
        {
                for (x=0;x<128;x++) keylookup[x]=keytemp[x];
        }
        x=(SCREEN_W/2)-(d[0].w/2);
        y=(SCREEN_H/2)-(d[0].h/2);
        blit(b,screen,0,0,x,y,d[0].w,d[0].h);
        x=0;
        while (d[x].proc)
        {
                d[x].x-=(d[0].w/2)-(538/2);
                d[x].y-=(d[0].h/2)-(256/2);
                x++;
        }
        return D_CLOSE;
}
#endif
#endif
