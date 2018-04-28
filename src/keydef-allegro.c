#include "b-em.h"
#include "keyboard.h"
#include "model.h"
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>
#include <limits.h>

typedef enum {
    COL_BLACK,
    COL_GREY,
    COL_RED,
} key_col_t;

typedef struct {
    uint16_t x, y, w, h;
    key_col_t col;
    char cap[7];
    char name[11];
    int keycode;
} key_cap_t;

typedef struct {
    const key_cap_t *captab;
    const key_cap_t *capend;
    uint16_t  disp_x, disp_y;
} key_dlg_t;

typedef enum {
    ST_BBC_KEY,
    ST_PC_KEY,
    ST_DONE
} state_t;

#define BBC_NKEY 74

static const key_cap_t kcaps_bbc[BBC_NKEY] = {
    {  82,  10,  28,  28, COL_RED,   "F0",     "F0",         ALLEGRO_KEY_F1         },
    { 114,  10,  28,  28, COL_RED,   "F1",     "F1",         ALLEGRO_KEY_F2         },
    { 146,  10,  28,  28, COL_RED,   "F2",     "F2",         ALLEGRO_KEY_F3         },
    { 178,  10,  28,  28, COL_RED,   "F3",     "F3",         ALLEGRO_KEY_F4         },
    { 210,  10,  28,  28, COL_RED,   "F4",     "F4",         ALLEGRO_KEY_F5         },
    { 242,  10,  28,  28, COL_RED,   "F5",     "F5",         ALLEGRO_KEY_F6         },
    { 274,  10,  28,  28, COL_RED,   "F6",     "F6",         ALLEGRO_KEY_F7         },
    { 306,  10,  28,  28, COL_RED,   "F7",     "F7",         ALLEGRO_KEY_F8         },
    { 338,  10,  28,  28, COL_RED,   "F8",     "F8",         ALLEGRO_KEY_F9         },
    { 370,  10,  28,  28, COL_RED,   "F9",     "F9",         ALLEGRO_KEY_F10        },
    { 402,  10,  28,  28, COL_BLACK, "BRK",    "Break",      ALLEGRO_KEY_F12        },
    {  10,  42,  28,  28, COL_BLACK, "ESC",    "Escape",     ALLEGRO_KEY_ESCAPE     },
    {  42,  42,  28,  28, COL_BLACK, "1",      "1",          ALLEGRO_KEY_1          },
    {  74,  42,  28,  28, COL_BLACK, "2",      "2",          ALLEGRO_KEY_2          },
    { 106,  42,  28,  28, COL_BLACK, "3",      "3",          ALLEGRO_KEY_3          },
    { 138,  42,  28,  28, COL_BLACK, "4",      "4",          ALLEGRO_KEY_4          },
    { 170,  42,  28,  28, COL_BLACK, "5",      "5",          ALLEGRO_KEY_5          },
    { 202,  42,  28,  28, COL_BLACK, "6",      "6",          ALLEGRO_KEY_6          },
    { 234,  42,  28,  28, COL_BLACK, "7",      "7",          ALLEGRO_KEY_7          },
    { 266,  42,  28,  28, COL_BLACK, "8",      "8",          ALLEGRO_KEY_8          },
    { 298,  42,  28,  28, COL_BLACK, "9",      "9",          ALLEGRO_KEY_9          },
    { 330,  42,  28,  28, COL_BLACK, "0",      "0",          ALLEGRO_KEY_0          },
    { 362,  42,  28,  28, COL_BLACK, "=",      "=",          ALLEGRO_KEY_MINUS      },
    { 394,  42,  28,  28, COL_BLACK, "^",      "^",          ALLEGRO_KEY_EQUALS     },
    { 426,  42,  28,  28, COL_BLACK, "\\",     "\\",         ALLEGRO_KEY_BACKSLASH2 },
    { 458,  42,  28,  28, COL_GREY,  "LFT",    "Left",       ALLEGRO_KEY_LEFT       },
    { 490,  42,  28,  28, COL_GREY,  "RGT",    "Right",      ALLEGRO_KEY_RIGHT      },
    {  10,  74,  44,  28, COL_BLACK, "TAB",    "Tab",        ALLEGRO_KEY_TAB        },
    {  58,  74,  28,  28, COL_BLACK, "Q",      "Q",          ALLEGRO_KEY_Q          },
    {  90,  74,  28,  28, COL_BLACK, "W",      "W",          ALLEGRO_KEY_W          },
    { 122,  74,  28,  28, COL_BLACK, "E",      "E",          ALLEGRO_KEY_E          },
    { 154,  74,  28,  28, COL_BLACK, "R",      "R",          ALLEGRO_KEY_R          },
    { 186,  74,  28,  28, COL_BLACK, "T",      "T",          ALLEGRO_KEY_T          },
    { 218,  74,  28,  28, COL_BLACK, "Y",      "Y",          ALLEGRO_KEY_Y          },
    { 250,  74,  28,  28, COL_BLACK, "U",      "U",          ALLEGRO_KEY_U          },
    { 282,  74,  28,  28, COL_BLACK, "I",      "I",          ALLEGRO_KEY_I          },
    { 314,  74,  28,  28, COL_BLACK, "O",      "O",          ALLEGRO_KEY_O          },
    { 346,  74,  28,  28, COL_BLACK, "P",      "P",          ALLEGRO_KEY_P          },
    { 378,  74,  28,  28, COL_BLACK, "@",      "@",          ALLEGRO_KEY_OPENBRACE  },
    { 410,  74,  28,  28, COL_BLACK, "[",      "[",          ALLEGRO_KEY_CLOSEBRACE },
    { 442,  74,  28,  28, COL_BLACK, "_",      "_",          ALLEGRO_KEY_TILDE      },
    { 474,  74,  28,  28, COL_GREY,  "UP",     "Up",         ALLEGRO_KEY_UP         },
    { 506,  74,  28,  28, COL_GREY,  "DWN",    "Down",       ALLEGRO_KEY_DOWN       },
    {  10, 106,  28,  28, COL_BLACK, "CLK",    "Caps Lock",  ALLEGRO_KEY_CAPSLOCK   },
    {  42, 106,  28,  28, COL_BLACK, "CTL",    "CTRL",       ALLEGRO_KEY_LCTRL      },
    {  74, 106,  28,  28, COL_BLACK, "A",      "A",          ALLEGRO_KEY_A          },
    { 106, 106,  28,  28, COL_BLACK, "S",      "S",          ALLEGRO_KEY_S          },
    { 138, 106,  28,  28, COL_BLACK, "D",      "D",          ALLEGRO_KEY_D          },
    { 170, 106,  28,  28, COL_BLACK, "F",      "F",          ALLEGRO_KEY_F          },
    { 202, 106,  28,  28, COL_BLACK, "G",      "G",          ALLEGRO_KEY_G          },
    { 234, 106,  28,  28, COL_BLACK, "H",      "H",          ALLEGRO_KEY_H          },
    { 266, 106,  28,  28, COL_BLACK, "J",      "J",          ALLEGRO_KEY_J          },
    { 298, 106,  28,  28, COL_BLACK, "K",      "K",          ALLEGRO_KEY_K          },
    { 330, 106,  28,  28, COL_BLACK, "L",      "L",          ALLEGRO_KEY_L          },
    { 362, 106,  28,  28, COL_BLACK, ";",      ";",          ALLEGRO_KEY_SEMICOLON  },
    { 394, 106,  28,  28, COL_BLACK, ":",      ":",          ALLEGRO_KEY_QUOTE      },
    { 426, 106,  28,  28, COL_BLACK, "]",      "]",          ALLEGRO_KEY_BACKSLASH  },
    { 458, 106,  60,  28, COL_BLACK, "RET",    "Return",     ALLEGRO_KEY_ENTER      },
    {  10, 138,  28,  28, COL_BLACK, "SLK",    "Shift Lock", ALLEGRO_KEY_ALT        },
    {  42, 138,  44,  28, COL_BLACK, "SHIFT",  "Shift",      ALLEGRO_KEY_LSHIFT     },
    {  90, 138,  28,  28, COL_BLACK, "Z",      "Z",          ALLEGRO_KEY_Z          },
    { 122, 138,  28,  28, COL_BLACK, "X",      "X",          ALLEGRO_KEY_X          },
    { 154, 138,  28,  28, COL_BLACK, "C",      "C",          ALLEGRO_KEY_C          },
    { 186, 138,  28,  28, COL_BLACK, "V",      "V",          ALLEGRO_KEY_V          },
    { 218, 138,  28,  28, COL_BLACK, "B",      "B",          ALLEGRO_KEY_B          },
    { 250, 138,  28,  28, COL_BLACK, "N",      "N",          ALLEGRO_KEY_N          },
    { 282, 138,  28,  28, COL_BLACK, "M",      "M",          ALLEGRO_KEY_M          },
    { 314, 138,  28,  28, COL_BLACK, ",",      ",",          ALLEGRO_KEY_COMMA      },
    { 346, 138,  28,  28, COL_BLACK, ".",      ".",          ALLEGRO_KEY_FULLSTOP   },
    { 378, 138,  28,  28, COL_BLACK, "/",      "/",          ALLEGRO_KEY_SLASH      },
    { 410, 138,  44,  28, COL_BLACK, "SHIFT",  "Shift",      ALLEGRO_KEY_RSHIFT     },
    { 458, 138,  28,  28, COL_BLACK, "DEL",    "Delete",     ALLEGRO_KEY_DELETE     },
    { 490, 138,  28,  28, COL_GREY,  "CPY",    "Copy",       ALLEGRO_KEY_END        },
    { 122, 170, 256,  28, COL_BLACK, "SPACE",  "Space",      ALLEGRO_KEY_SPACE      }
};

static const key_dlg_t bbc_kbd_dlg = { kcaps_bbc, kcaps_bbc + BBC_NKEY, 538, 256 };

#define MASTER_NKEY 93

static const key_cap_t kcaps_master[MASTER_NKEY] = {
    {  50,  10,  28,  28, COL_RED,   "F0",     "F0",         ALLEGRO_KEY_F1         },
    {  82,  10,  28,  28, COL_RED,   "F1",     "F1",         ALLEGRO_KEY_F2         },
    { 114,  10,  28,  28, COL_RED,   "F2",     "F2",         ALLEGRO_KEY_F3         },
    { 146,  10,  28,  28, COL_RED,   "F3",     "F3",         ALLEGRO_KEY_F4         },
    { 178,  10,  28,  28, COL_RED,   "F4",     "F4",         ALLEGRO_KEY_F5         },
    { 210,  10,  28,  28, COL_RED,   "F5",     "F5",         ALLEGRO_KEY_F6         },
    { 242,  10,  28,  28, COL_RED,   "F6",     "F6",         ALLEGRO_KEY_F7         },
    { 274,  10,  28,  28, COL_RED,   "F7",     "F7",         ALLEGRO_KEY_F8         },
    { 306,  10,  28,  28, COL_RED,   "F8",     "F8",         ALLEGRO_KEY_F9         },
    { 338,  10,  28,  28, COL_RED,   "F9",     "F9",         ALLEGRO_KEY_F10        },
    { 370,  10,  28,  28, COL_BLACK, "BRK",    "Break",      ALLEGRO_KEY_F12        },
    {  10,  42,  28,  28, COL_BLACK, "ESC",    "Escape",     ALLEGRO_KEY_ESCAPE     },
    {  42,  42,  28,  28, COL_BLACK, "1",      "1",          ALLEGRO_KEY_1          },
    {  74,  42,  28,  28, COL_BLACK, "2",      "2",          ALLEGRO_KEY_2          },
    { 106,  42,  28,  28, COL_BLACK, "3",      "3",          ALLEGRO_KEY_3          },
    { 138,  42,  28,  28, COL_BLACK, "4",      "4",          ALLEGRO_KEY_4          },
    { 170,  42,  28,  28, COL_BLACK, "5",      "5",          ALLEGRO_KEY_5          },
    { 202,  42,  28,  28, COL_BLACK, "6",      "6",          ALLEGRO_KEY_6          },
    { 234,  42,  28,  28, COL_BLACK, "7",      "7",          ALLEGRO_KEY_7          },
    { 266,  42,  28,  28, COL_BLACK, "8",      "8",          ALLEGRO_KEY_8          },
    { 298,  42,  28,  28, COL_BLACK, "9",      "9",          ALLEGRO_KEY_9          },
    { 330,  42,  28,  28, COL_BLACK, "0",      "0",          ALLEGRO_KEY_0          },
    { 362,  42,  28,  28, COL_BLACK, "=",      "=",          ALLEGRO_KEY_MINUS      },
    { 394,  42,  28,  28, COL_BLACK, "^",      "^",          ALLEGRO_KEY_EQUALS     },
    { 426,  42,  28,  28, COL_BLACK, "\\",     "\\",         ALLEGRO_KEY_BACKSLASH2 },
    { 458,  42,  28,  28, COL_GREY,  "LFT",    "Left",       ALLEGRO_KEY_LEFT       },
    { 490,  42,  28,  28, COL_GREY,  "RGT",    "Right",      ALLEGRO_KEY_RIGHT      },
    {  10,  74,  44,  28, COL_BLACK, "TAB",    "Tab",        ALLEGRO_KEY_TAB        },
    {  58,  74,  28,  28, COL_BLACK, "Q",      "Q",          ALLEGRO_KEY_Q          },
    {  90,  74,  28,  28, COL_BLACK, "W",      "W",          ALLEGRO_KEY_W          },
    { 122,  74,  28,  28, COL_BLACK, "E",      "E",          ALLEGRO_KEY_E          },
    { 154,  74,  28,  28, COL_BLACK, "R",      "R",          ALLEGRO_KEY_R          },
    { 186,  74,  28,  28, COL_BLACK, "T",      "T",          ALLEGRO_KEY_T          },
    { 218,  74,  28,  28, COL_BLACK, "Y",      "Y",          ALLEGRO_KEY_Y          },
    { 250,  74,  28,  28, COL_BLACK, "U",      "U",          ALLEGRO_KEY_U          },
    { 282,  74,  28,  28, COL_BLACK, "I",      "I",          ALLEGRO_KEY_I          },
    { 314,  74,  28,  28, COL_BLACK, "O",      "O",          ALLEGRO_KEY_O          },
    { 346,  74,  28,  28, COL_BLACK, "P",      "P",          ALLEGRO_KEY_P          },
    { 378,  74,  28,  28, COL_BLACK, "@",      "@",          ALLEGRO_KEY_OPENBRACE  },
    { 410,  74,  28,  28, COL_BLACK, "[",      "[",          ALLEGRO_KEY_CLOSEBRACE },
    { 442,  74,  28,  28, COL_BLACK, "_",      "_",          ALLEGRO_KEY_TILDE      },
    { 474,  10,  28,  28, COL_GREY,  "UP",     "UP",         ALLEGRO_KEY_UP         },
    { 474,  74,  28,  28, COL_GREY,  "DWN",    "DOWN",       ALLEGRO_KEY_DOWN       },
    {  10, 106,  28,  28, COL_BLACK, "CLK",    "CAPS LOCK",  ALLEGRO_KEY_CAPSLOCK   },
    {  42, 106,  28,  28, COL_BLACK, "CTL",    "CTRL",       ALLEGRO_KEY_LCTRL      },
    {  74, 106,  28,  28, COL_BLACK, "A",      "A",          ALLEGRO_KEY_A          },
    { 106, 106,  28,  28, COL_BLACK, "S",      "S",          ALLEGRO_KEY_S          },
    { 138, 106,  28,  28, COL_BLACK, "D",      "D",          ALLEGRO_KEY_D          },
    { 170, 106,  28,  28, COL_BLACK, "F",      "F",          ALLEGRO_KEY_F          },
    { 202, 106,  28,  28, COL_BLACK, "G",      "G",          ALLEGRO_KEY_G          },
    { 234, 106,  28,  28, COL_BLACK, "H",      "H",          ALLEGRO_KEY_H          },
    { 266, 106,  28,  28, COL_BLACK, "J",      "J",          ALLEGRO_KEY_J          },
    { 298, 106,  28,  28, COL_BLACK, "K",      "K",          ALLEGRO_KEY_K          },
    { 330, 106,  28,  28, COL_BLACK, "L",      "L",          ALLEGRO_KEY_L          },
    { 362, 106,  28,  28, COL_BLACK, ";",      ";",          ALLEGRO_KEY_SEMICOLON  },
    { 394, 106,  28,  28, COL_BLACK, ":",      ":",          ALLEGRO_KEY_QUOTE      },
    { 426, 106,  28,  28, COL_BLACK, "]",      "]",          ALLEGRO_KEY_BACKSLASH  },
    { 458, 106,  60,  28, COL_BLACK, "RET",    "Return",     ALLEGRO_KEY_ENTER      },
    {  10, 138,  28,  28, COL_BLACK, "SLK",    "Shift Lock", ALLEGRO_KEY_ALT        },
    {  42, 138,  44,  28, COL_BLACK, "SHIFT",  "Shift",      ALLEGRO_KEY_LSHIFT     },
    {  90, 138,  28,  28, COL_BLACK, "Z",      "Z",          ALLEGRO_KEY_Z          },
    { 122, 138,  28,  28, COL_BLACK, "X",      "X",          ALLEGRO_KEY_X          },
    { 154, 138,  28,  28, COL_BLACK, "C",      "C",          ALLEGRO_KEY_C          },
    { 186, 138,  28,  28, COL_BLACK, "V",      "V",          ALLEGRO_KEY_V          },
    { 218, 138,  28,  28, COL_BLACK, "B",      "B",          ALLEGRO_KEY_B          },
    { 250, 138,  28,  28, COL_BLACK, "N",      "N",          ALLEGRO_KEY_N          },
    { 282, 138,  28,  28, COL_BLACK, "M",      "M",          ALLEGRO_KEY_M          },
    { 314, 138,  28,  28, COL_BLACK, ",",      ",",          ALLEGRO_KEY_COMMA      },
    { 346, 138,  28,  28, COL_BLACK, ".",      ".",          ALLEGRO_KEY_FULLSTOP   },
    { 378, 138,  28,  28, COL_BLACK, "/",      "/",          ALLEGRO_KEY_SLASH      },
    { 410, 138,  44,  28, COL_BLACK, "SHIFT",  "Shift",      ALLEGRO_KEY_RSHIFT     },
    { 458, 138,  28,  28, COL_BLACK, "DEL",    "Delete",     ALLEGRO_KEY_DELETE     },
    { 490, 138,  28,  28, COL_GREY,  "CPY",    "Copy",       ALLEGRO_KEY_END        },
    { 122, 170, 256,  28, COL_BLACK, "SPACE",  "Space",      ALLEGRO_KEY_SPACE,     },

    { 538,  10,  28,  28, COL_BLACK, "+",      "+",          ALLEGRO_KEY_PAD_PLUS,  },
    { 570,  10,  28,  28, COL_BLACK, "-",      "-",          ALLEGRO_KEY_PAD_MINUS, },
    { 602,  10,  28,  28, COL_BLACK, "/",      "/",          ALLEGRO_KEY_PAD_SLASH, },
    { 634,  10,  28,  28, COL_BLACK, "*",      "*",          ALLEGRO_KEY_PAD_ASTERISK, },
    { 538,  42,  28,  28, COL_BLACK, "7",      "7",          ALLEGRO_KEY_PAD_7,     },
    { 570,  42,  28,  28, COL_BLACK, "8",      "8",          ALLEGRO_KEY_PAD_8,     },
    { 602,  42,  28,  28, COL_BLACK, "9",      "9",          ALLEGRO_KEY_PAD_9,     },
    { 634,  42,  28,  28, COL_BLACK, "#",      "#",          ALLEGRO_KEY_DOWN,      },
    { 538,  74,  28,  28, COL_BLACK, "4",      "4",          ALLEGRO_KEY_PAD_4,     },
    { 570,  74,  28,  28, COL_BLACK, "5",      "5",          ALLEGRO_KEY_PAD_5,     },
    { 602,  74,  28,  28, COL_BLACK, "6",      "6",          ALLEGRO_KEY_PAD_6,     },
    { 634,  74,  28,  28, COL_BLACK, "DEL",    "Delete",     ALLEGRO_KEY_PAD_DELETE },
    { 538, 106,  28,  28, COL_BLACK, "1",      "1",          ALLEGRO_KEY_PAD_1,     },
    { 570, 106,  28,  28, COL_BLACK, "2",      "2",          ALLEGRO_KEY_PAD_2,     },
    { 602, 106,  28,  28, COL_BLACK, "3",      "3",          ALLEGRO_KEY_PAD_3,     },
    { 634, 106,  28,  28, COL_BLACK, ",",      ",",          ALLEGRO_KEY_HOME,      },
    { 538, 138,  28,  28, COL_BLACK, "0",      "0",          ALLEGRO_KEY_PAD_0,     },
    { 570, 138,  28,  28, COL_BLACK, ".",      ".",          ALLEGRO_KEY_PGDN,      },
    { 602, 138,  60,  28, COL_BLACK, "RETURN", "Return",     ALLEGRO_KEY_PAD_ENTER  }
};

static const key_dlg_t master_kbd_dlg = { kcaps_master, kcaps_master + MASTER_NKEY, 676, 256 };

static const char *allegro_std_key_names[] =
{
    "A",               "B",               "C",               "D",
    "E",               "F",               "G",               "H",
    "I",               "J",               "K",               "L",
    "M",               "N",               "O",               "P",
    "Q",               "R",               "S",               "T",
    "U",               "V",               "W",               "X",
    "Y",               "Z",               "0",               "1",
    "2",               "3",               "4",               "5",
    "6",               "7",               "8",               "9",
    "Keypad 0",        "Keypad 1",        "Keypad 2",        "Keypad 3",
    "Keypad 4",        "Keypad 5",        "Keypad 6",        "Keypad 7",
    "Keypad 8",        "Keypad 9",        "F1",              "F2",
    "F3",              "F4",              "F5",              "F6",
    "F7",              "F8",              "F9",              "F10",
    "F11",             "F12",             "Escape",          "Tilde",
    "Minus",           "Equals",          "Backspace",       "Tab",
    "Open brace",      "Close brace",     "Enter",           "Semicolon",
    "Quote",           "Backslash",       "Backslash 2",     "Comma",
    "Full stop",       "Slash",           "Space",           "Insert",
    "Delete",          "Home",            "End",             "Page Up",
    "Page Down",       "Left",            "Right",           "Up",
    "Down",            "Keypad Slash",    "Keypad Asterisk", "Keypad Minus",
    "Keypad Plus",     "Keypad Delete",   "Keypad Enter",    "Print Screen",
    "Pause",           "ABNT_C1",         "Yen",             "Kana",
    "Convert",         "No Convert",      "AT",              "Circumflex",
    "Colon 2"          "Kanji",           "Keypad Equals",   "Back quote",
    "Semicolon 2",     "Command",         "Back",            "Volume Up",
    "Volume Down",     "Search",          "Dpad Centre",     "Button X",
    "Button Y",        "Dpad Up",         "Dpad Down",       "Dpad Left",
    "Dpad Right",      "Select",          "Start",           "Button L1",
    "Button R1",       "Button L2",       "Button R2",       "Button A",
    "Button B",        "Thumb L",         "Thumb R",         "Unknown"
};

static const char *allegro_mod_key_names[] =
{
    "Left Shift",      "Right Shift",     "Left Ctrl",       "Right Ctrl",
    "Alt",             "Alt Gr",          "Left Windows",    "Right Windows",
    "Menu",            "Scroll lock",     "Num Lock",        "Caps Lock"
};

#define BTNS_Y    218
#define BTNS_W     60
#define BTNS_H     28
#define BTN_OK_X  201
#define BTN_CAN_X 267

static ALLEGRO_FONT *font;
static ALLEGRO_EVENT_SOURCE uevsrc;

static void draw_button(int x, int y, int w, int h, ALLEGRO_COLOR bcol, ALLEGRO_COLOR tcol, const char *text)
{
    al_draw_filled_rectangle(x, y, x+w, y+h, bcol);
    al_draw_text(font, tcol, x+(w/2), y+(h/2)-2, ALLEGRO_ALIGN_CENTRE, text);
}

static void draw_keyboard(const key_dlg_t *key_dlg, int ok_x, int can_x)
{
    ALLEGRO_COLOR black, grey, white, red, brown, navy;
    const key_cap_t *kptr;

    black = al_map_rgb(  0,   0,   0);
    grey  = al_map_rgb(127, 127, 127);
    white = al_map_rgb(255, 255, 255);
    brown = al_map_rgb( 64,  32,  32);
    red   = al_map_rgb(255,  64,  64);
    navy  = al_map_rgb( 32,  32,  64);
    al_clear_to_color(brown);
    for (kptr = key_dlg->captab; kptr < key_dlg->capend; kptr++) {
        switch(kptr->col) {
            case COL_RED:
                draw_button(kptr->x, kptr->y, kptr->w, kptr->h, red, white, kptr->cap);
                break;
            case COL_GREY:
                draw_button(kptr->x, kptr->y, kptr->w, kptr->h, grey, white, kptr->cap);
                break;
            case COL_BLACK:
                draw_button(kptr->x, kptr->y, kptr->w, kptr->h, black, white, kptr->cap);
                break;
        }
    }
    draw_button(ok_x, BTNS_Y, BTNS_W, BTNS_H, navy, white, "OK");
    draw_button(can_x, BTNS_Y, BTNS_W, BTNS_H, navy, white, "Cancel");
    al_flip_display();
}

static const char *allegro_key_name(int keycode)
{
    if (keycode >= 1 && keycode <= ALLEGRO_KEY_UNKNOWN)
        return allegro_std_key_names[keycode-1];
    if (keycode >= ALLEGRO_KEY_MODIFIERS && keycode <= ALLEGRO_KEY_MAX)
        return allegro_mod_key_names[keycode-ALLEGRO_KEY_MODIFIERS];
    return "Unknown";
}

static void redef_message(const key_dlg_t *key_dlg, const key_cap_t *kptr, int *keylookcpy)
{
    int mid_x  = key_dlg->disp_x/2;
    int left_x = mid_x-200;
    int mid_y  = key_dlg->disp_y/2;
    int top_y  = mid_y - 36;
    ALLEGRO_COLOR navy = al_map_rgb( 32,  32,  64);
    ALLEGRO_COLOR white = al_map_rgb(255, 255, 255);
    char s[1024], *p;
    int size, remain, code, count;
    const char *fmt;

    al_draw_filled_rectangle(left_x, top_y, left_x + 400, top_y + 72, navy);
    snprintf(s, sizeof s, "Redefining %s", kptr->name);
    al_draw_text(font, white, left_x+24, top_y+16, ALLEGRO_ALIGN_LEFT, s);
    size = snprintf(s, sizeof s, "Assigned to PC key(s):");
    p = s + size;
    remain = sizeof s - size;
    count = 0;
    for (code = 0; remain > 0 && code < ALLEGRO_KEY_MAX; code++) {
        if (keylookcpy[code] == kptr->keycode) {
            fmt = count == 0 ? " %s" : ", %s";
            size = snprintf(p, remain, fmt, allegro_key_name(code));
            p += size;
            remain -= size;
        }
    }
    al_draw_text(font, white, left_x+24, top_y+32, ALLEGRO_ALIGN_LEFT, s);
    al_draw_text(font, white, left_x+24, top_y+48, ALLEGRO_ALIGN_LEFT, "Please press new key...");
    al_flip_display();
}

static bool mouse_within(ALLEGRO_EVENT *event, int x, int y, int w, int h)
{
    return event->mouse.x >= x && event->mouse.x <= x+w && event->mouse.y >= y && event->mouse.y <= y+h;
}

static void *keydef_thread(ALLEGRO_THREAD *thread, void *tdata)
{
    const key_dlg_t *key_dlg;
    ALLEGRO_DISPLAY *display;
    ALLEGRO_EVENT_QUEUE *queue;
    ALLEGRO_EVENT event;
    state_t state;
    int keylookcpy[ALLEGRO_KEY_MAX];
    const key_cap_t *kptr;
    int mid_x, ok_x, can_x;

    if (!font) {
        al_init_primitives_addon();
        al_init_font_addon();
        font = al_create_builtin_font();
        if (!font)
            return NULL;
    }

    key_dlg = MASTER ? &master_kbd_dlg : &bbc_kbd_dlg;
    if ((display = al_create_display(key_dlg->disp_x, key_dlg->disp_y))) {
        if ((queue = al_create_event_queue())) {
            al_init_user_event_source(&uevsrc);
            al_register_event_source(queue, &uevsrc);
            al_register_event_source(queue, al_get_display_event_source(display));
            al_register_event_source(queue, al_get_mouse_event_source());
            al_register_event_source(queue, al_get_keyboard_event_source());
            mid_x = key_dlg->disp_x/2;
            ok_x = mid_x-3-BTNS_W;
            can_x = mid_x+3;
            state = ST_BBC_KEY;
            memcpy(keylookcpy, keylookup, ALLEGRO_KEY_MAX * sizeof(int));
            draw_keyboard(key_dlg, ok_x, can_x);
            while (state != ST_DONE) {
                al_wait_for_event(queue, &event);
                switch(event.type) {
                    case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                        if (mouse_within(&event, ok_x, BTNS_Y, BTNS_W, BTNS_H)) {
                            // Ok button clicked.
                            memcpy(keylookup, keylookcpy, ALLEGRO_KEY_MAX * sizeof(int));
                            state = ST_DONE;
                        }
                        else if (mouse_within(&event, can_x, BTNS_Y, BTNS_W, BTNS_H)) {
                            // Cancel button clicked.
                            if (state == ST_PC_KEY) {
                                state = ST_BBC_KEY;
                                draw_keyboard(key_dlg, ok_x, can_x);
                            }
                            else
                                state = ST_DONE;
                        }
                        else if (state == ST_BBC_KEY) {
                            // Search the keyboard buttons.
                            for (kptr = key_dlg->captab; kptr < key_dlg->capend; kptr++) {
                                if (mouse_within(&event, kptr->x, kptr->y, kptr->w, kptr->h)) {
                                    redef_message(key_dlg, kptr, keylookcpy);
                                    state = ST_PC_KEY;
                                    break;
                                }
                            }
                        }
                        break;
                    case ALLEGRO_EVENT_DISPLAY_CLOSE:
                        memcpy(keylookup, keylookcpy, ALLEGRO_KEY_MAX * sizeof(int));
                        state = ST_DONE;
                        break;
                    case ALLEGRO_EVENT_KEY_DOWN:
                        if (state == ST_PC_KEY) {
                            keylookcpy[event.keyboard.keycode] = kptr->keycode;
                            state = ST_BBC_KEY;
                            draw_keyboard(key_dlg, ok_x, can_x);
                        }
                }
            }
            al_destroy_event_queue(queue);
        } else
            log_error("keydef-allegro: unable to create event queue");
        al_destroy_display(display);
    } else
        log_error("keydef-allegro: unable to create display");
    return NULL;
}

void gui_keydefine_open(void)
{
    ALLEGRO_THREAD *thread;

    if ((thread = al_create_thread(keydef_thread, NULL)))
        al_start_thread(thread);
}

void gui_keydefine_close(void)
{
    ALLEGRO_EVENT event;

    event.type = ALLEGRO_EVENT_DISPLAY_CLOSE;
    al_emit_user_event(&uevsrc, &event, NULL);
}

int keydef_lookup_name(const char *name)
{
    const key_cap_t *ptr = kcaps_master;
    const key_cap_t *end = ptr + MASTER_NKEY;

    while (ptr < end) {
        if (strcasecmp(name, ptr->cap) == 0 || strcasecmp(name, ptr->name) == 0)
            return ptr->keycode;
        ptr++;
    }
    return 0;
}
