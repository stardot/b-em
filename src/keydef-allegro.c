#define _DEBUG
#include "b-em.h"
#include "keyboard.h"
#include "main.h"
#include "model.h"
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>
#include <limits.h>

typedef enum {
    COL_BLACK,
    COL_GREY,
    COL_RED,
    COL_GREEN
} key_col_t;

typedef struct {
    uint16_t x, y, w, h;
    key_col_t col;
    char cap[12];
    char name[12];
    uint8_t keycode;
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

#define BBC_NKEY 78

static const key_cap_t kcaps_bbc[BBC_NKEY] = {
    {  82,  10,  28,  28, COL_RED,   "F0",          "F0",          0x20 },
    { 114,  10,  28,  28, COL_RED,   "F1",          "F1",          0x71 },
    { 146,  10,  28,  28, COL_RED,   "F2",          "F2",          0x72 },
    { 178,  10,  28,  28, COL_RED,   "F3",          "F3",          0x73 },
    { 210,  10,  28,  28, COL_RED,   "F4",          "F4",          0x14 },
    { 242,  10,  28,  28, COL_RED,   "F5",          "F5",          0x74 },
    { 274,  10,  28,  28, COL_RED,   "F6",          "F6",          0x75 },
    { 306,  10,  28,  28, COL_RED,   "F7",          "F7",          0x16 },
    { 338,  10,  28,  28, COL_RED,   "F8",          "F8",          0x76 },
    { 370,  10,  28,  28, COL_RED,   "F9",          "F9",          0x77 },
    { 402,  10,  28,  28, COL_BLACK, "BRK",         "Break",       0xff },
    {  10,  42,  28,  28, COL_BLACK, "ESC",         "Escape",      0x70 },
    {  42,  42,  28,  28, COL_BLACK, "1",           "1",           0x30 },
    {  74,  42,  28,  28, COL_BLACK, "2",           "2",           0x31 },
    { 106,  42,  28,  28, COL_BLACK, "3",           "3",           0x11 },
    { 138,  42,  28,  28, COL_BLACK, "4",           "4",           0x12 },
    { 170,  42,  28,  28, COL_BLACK, "5",           "5",           0x13 },
    { 202,  42,  28,  28, COL_BLACK, "6",           "6",           0x34 },
    { 234,  42,  28,  28, COL_BLACK, "7",           "7",           0x24 },
    { 266,  42,  28,  28, COL_BLACK, "8",           "8",           0x15 },
    { 298,  42,  28,  28, COL_BLACK, "9",           "9",           0x26 },
    { 330,  42,  28,  28, COL_BLACK, "0",           "0",           0x27 },
    { 362,  42,  28,  28, COL_BLACK, "=",           "=",           0x17 },
    { 394,  42,  28,  28, COL_BLACK, "^",           "^",           0x18 },
    { 426,  42,  28,  28, COL_BLACK, "\\",          "\\",          0x78 },
    { 458,  42,  28,  28, COL_GREY,  "LFT",         "Left",        0x19 },
    { 490,  42,  28,  28, COL_GREY,  "RGT",         "Right",       0x79 },
    {  10,  74,  44,  28, COL_BLACK, "TAB",         "Tab",         0x60 },
    {  58,  74,  28,  28, COL_BLACK, "Q",           "Q",           0x10 },
    {  90,  74,  28,  28, COL_BLACK, "W",           "W",           0x21 },
    { 122,  74,  28,  28, COL_BLACK, "E",           "E",           0x22 },
    { 154,  74,  28,  28, COL_BLACK, "R",           "R",           0x33 },
    { 186,  74,  28,  28, COL_BLACK, "T",           "T",           0x23 },
    { 218,  74,  28,  28, COL_BLACK, "Y",           "Y",           0x44 },
    { 250,  74,  28,  28, COL_BLACK, "U",           "U",           0x35 },
    { 282,  74,  28,  28, COL_BLACK, "I",           "I",           0x25 },
    { 314,  74,  28,  28, COL_BLACK, "O",           "O",           0x36 },
    { 346,  74,  28,  28, COL_BLACK, "P",           "P",           0x37 },
    { 378,  74,  28,  28, COL_BLACK, "@",           "@",           0x47 },
    { 410,  74,  28,  28, COL_BLACK, "[",           "[",           0x38 },
    { 442,  74,  28,  28, COL_BLACK, "_",           "_",           0x28 },
    { 474,  74,  28,  28, COL_GREY,  "UP",          "Up",          0x39 },
    { 506,  74,  28,  28, COL_GREY,  "DWN",         "Down",        0x29 },
    {  10, 106,  28,  28, COL_BLACK, "CLK",         "Caps Lock",   0x40 },
    {  42, 106,  28,  28, COL_BLACK, "CTL",         "CTRL",        0x01 },
    {  74, 106,  28,  28, COL_BLACK, "A",           "A",           0x41 },
    { 106, 106,  28,  28, COL_BLACK, "S",           "S",           0x51 },
    { 138, 106,  28,  28, COL_BLACK, "D",           "D",           0x32 },
    { 170, 106,  28,  28, COL_BLACK, "F",           "F",           0x43 },
    { 202, 106,  28,  28, COL_BLACK, "G",           "G",           0x53 },
    { 234, 106,  28,  28, COL_BLACK, "H",           "H",           0x54 },
    { 266, 106,  28,  28, COL_BLACK, "J",           "J",           0x45 },
    { 298, 106,  28,  28, COL_BLACK, "K",           "K",           0x46 },
    { 330, 106,  28,  28, COL_BLACK, "L",           "L",           0x56 },
    { 362, 106,  28,  28, COL_BLACK, ";",           ";",           0x57 },
    { 394, 106,  28,  28, COL_BLACK, ":",           ":",           0x48 },
    { 426, 106,  28,  28, COL_BLACK, "]",           "]",           0x58 },
    { 458, 106,  60,  28, COL_BLACK, "RET",         "Return",      0x49 },
    {  10, 138,  28,  28, COL_BLACK, "SLK",         "Shift Lock",  0x50 },
    {  42, 138,  44,  28, COL_BLACK, "SHIFT",       "Shift",       0x00 },
    {  90, 138,  28,  28, COL_BLACK, "Z",           "Z",           0x61 },
    { 122, 138,  28,  28, COL_BLACK, "X",           "X",           0x42 },
    { 154, 138,  28,  28, COL_BLACK, "C",           "C",           0x52 },
    { 186, 138,  28,  28, COL_BLACK, "V",           "V",           0x63 },
    { 218, 138,  28,  28, COL_BLACK, "B",           "B",           0x64 },
    { 250, 138,  28,  28, COL_BLACK, "N",           "N",           0x55 },
    { 282, 138,  28,  28, COL_BLACK, "M",           "M",           0x65 },
    { 314, 138,  28,  28, COL_BLACK, ",",           ",",           0x66 },
    { 346, 138,  28,  28, COL_BLACK, ".",           ".",           0x67 },
    { 378, 138,  28,  28, COL_BLACK, "/",           "/",           0x68 },
    { 410, 138,  44,  28, COL_BLACK, "SHIFT",       "Shift",       0x00 },
    { 458, 138,  28,  28, COL_BLACK, "DEL",         "Delete",      0x59 },
    { 490, 138,  28,  28, COL_GREY,  "CPY",         "Copy",        0x69 },
    { 122, 170, 256,  28, COL_BLACK, "SPACE",       "Space",       0x62 },

    {  70, 218,  96,  28, COL_GREEN, "Full Speed",  "Full Speed",  0xfe },
    { 170, 218,  96,  28, COL_GREEN, "Pause",       "Pause",       0xfd },
    { 270, 218,  96,  28, COL_GREEN, "Full Screen", "Full Screen", 0xfc },
    { 370, 218,  96,  28, COL_GREEN, "Debug Break", "Debug Break", 0xfb }
};

static const key_dlg_t bbc_kbd_dlg = { kcaps_bbc, kcaps_bbc + BBC_NKEY, 538, 304 };

#define MASTER_NKEY 97

static const key_cap_t kcaps_master[MASTER_NKEY] = {
    {  50,  10,  28,  28, COL_RED,   "F0",          "F0",          0x20 },
    {  82,  10,  28,  28, COL_RED,   "F1",          "F1",          0x71 },
    { 114,  10,  28,  28, COL_RED,   "F2",          "F2",          0x72 },
    { 146,  10,  28,  28, COL_RED,   "F3",          "F3",          0x73 },
    { 178,  10,  28,  28, COL_RED,   "F4",          "F4",          0x14 },
    { 210,  10,  28,  28, COL_RED,   "F5",          "F5",          0x74 },
    { 242,  10,  28,  28, COL_RED,   "F6",          "F6",          0x75 },
    { 274,  10,  28,  28, COL_RED,   "F7",          "F7",          0x16 },
    { 306,  10,  28,  28, COL_RED,   "F8",          "F8",          0x76 },
    { 338,  10,  28,  28, COL_RED,   "F9",          "F9",          0x77 },
    { 370,  10,  28,  28, COL_BLACK, "BRK",         "Break",       0xff },
    {  10,  42,  28,  28, COL_BLACK, "ESC",         "Escape",      0x70 },
    {  42,  42,  28,  28, COL_BLACK, "1",           "1",           0x30 },
    {  74,  42,  28,  28, COL_BLACK, "2",           "2",           0x31 },
    { 106,  42,  28,  28, COL_BLACK, "3",           "3",           0x11 },
    { 138,  42,  28,  28, COL_BLACK, "4",           "4",           0x12 },
    { 170,  42,  28,  28, COL_BLACK, "5",           "5",           0x13 },
    { 202,  42,  28,  28, COL_BLACK, "6",           "6",           0x34 },
    { 234,  42,  28,  28, COL_BLACK, "7",           "7",           0x24 },
    { 266,  42,  28,  28, COL_BLACK, "8",           "8",           0x15 },
    { 298,  42,  28,  28, COL_BLACK, "9",           "9",           0x26 },
    { 330,  42,  28,  28, COL_BLACK, "0",           "0",           0x27 },
    { 362,  42,  28,  28, COL_BLACK, "=",           "=",           0x17 },
    { 394,  42,  28,  28, COL_BLACK, "^",           "^",           0x18 },
    { 426,  42,  28,  28, COL_BLACK, "\\",          "\\",          0x78 },
    { 458,  42,  28,  28, COL_GREY,  "LFT",         "Left",        0x19 },
    { 490,  42,  28,  28, COL_GREY,  "RGT",         "Right",       0x79 },
    {  10,  74,  44,  28, COL_BLACK, "TAB",         "Tab",         0x60 },
    {  58,  74,  28,  28, COL_BLACK, "Q",           "Q",           0x10 },
    {  90,  74,  28,  28, COL_BLACK, "W",           "W",           0x21 },
    { 122,  74,  28,  28, COL_BLACK, "E",           "E",           0x22 },
    { 154,  74,  28,  28, COL_BLACK, "R",           "R",           0x33 },
    { 186,  74,  28,  28, COL_BLACK, "T",           "T",           0x23 },
    { 218,  74,  28,  28, COL_BLACK, "Y",           "Y",           0x44 },
    { 250,  74,  28,  28, COL_BLACK, "U",           "U",           0x35 },
    { 282,  74,  28,  28, COL_BLACK, "I",           "I",           0x25 },
    { 314,  74,  28,  28, COL_BLACK, "O",           "O",           0x36 },
    { 346,  74,  28,  28, COL_BLACK, "P",           "P",           0x37 },
    { 378,  74,  28,  28, COL_BLACK, "@",           "@",           0x47 },
    { 410,  74,  28,  28, COL_BLACK, "[",           "[",           0x38 },
    { 442,  74,  28,  28, COL_BLACK, "_",           "_",           0x28 },
    { 474,  10,  28,  28, COL_GREY,  "UP",          "UP",          0x39 },
    { 474,  74,  28,  28, COL_GREY,  "DWN",         "DOWN",        0x29 },
    {  10, 106,  28,  28, COL_BLACK, "CLK",         "CAPS LOCK",   0x40 },
    {  42, 106,  28,  28, COL_BLACK, "CTL",         "CTRL",        0x01 },
    {  74, 106,  28,  28, COL_BLACK, "A",           "A",           0x41 },
    { 106, 106,  28,  28, COL_BLACK, "S",           "S",           0x51 },
    { 138, 106,  28,  28, COL_BLACK, "D",           "D",           0x32 },
    { 170, 106,  28,  28, COL_BLACK, "F",           "F",           0x43 },
    { 202, 106,  28,  28, COL_BLACK, "G",           "G",           0x53 },
    { 234, 106,  28,  28, COL_BLACK, "H",           "H",           0x54 },
    { 266, 106,  28,  28, COL_BLACK, "J",           "J",           0x45 },
    { 298, 106,  28,  28, COL_BLACK, "K",           "K",           0x46 },
    { 330, 106,  28,  28, COL_BLACK, "L",           "L",           0x56 },
    { 362, 106,  28,  28, COL_BLACK, ";",           ";",           0x57 },
    { 394, 106,  28,  28, COL_BLACK, ":",           ":",           0x48 },
    { 426, 106,  28,  28, COL_BLACK, "]",           "]",           0x58 },
    { 458, 106,  60,  28, COL_BLACK, "RET",         "Return",      0x49 },
    {  10, 138,  28,  28, COL_BLACK, "SLK",         "Shift Lock",  0x50 },
    {  42, 138,  44,  28, COL_BLACK, "SHIFT",       "Shift",       0x00 },
    {  90, 138,  28,  28, COL_BLACK, "Z",           "Z",           0x61 },
    { 122, 138,  28,  28, COL_BLACK, "X",           "X",           0x42 },
    { 154, 138,  28,  28, COL_BLACK, "C",           "C",           0x52 },
    { 186, 138,  28,  28, COL_BLACK, "V",           "V",           0x63 },
    { 218, 138,  28,  28, COL_BLACK, "B",           "B",           0x64 },
    { 250, 138,  28,  28, COL_BLACK, "N",           "N",           0x55 },
    { 282, 138,  28,  28, COL_BLACK, "M",           "M",           0x65 },
    { 314, 138,  28,  28, COL_BLACK, ",",           ",",           0x66 },
    { 346, 138,  28,  28, COL_BLACK, ".",           ".",           0x67 },
    { 378, 138,  28,  28, COL_BLACK, "/",           "/",           0x68 },
    { 410, 138,  44,  28, COL_BLACK, "SHIFT",       "Shift",       0x00 },
    { 458, 138,  28,  28, COL_BLACK, "DEL",         "Delete",      0x59 },
    { 490, 138,  28,  28, COL_GREY,  "CPY",         "Copy",        0x69 },
    { 122, 170, 256,  28, COL_BLACK, "SPACE",       "Space",       0x62 },

    { 538,  10,  28,  28, COL_BLACK, "+",           "+",           0x3a },
    { 570,  10,  28,  28, COL_BLACK, "-",           "-",           0x3b },
    { 602,  10,  28,  28, COL_BLACK, "/",           "/",           0x4a },
    { 634,  10,  28,  28, COL_BLACK, "*",           "*",           0x5b },
    { 538,  42,  28,  28, COL_BLACK, "7",           "7",           0x1b },
    { 570,  42,  28,  28, COL_BLACK, "8",           "8",           0x2a },
    { 602,  42,  28,  28, COL_BLACK, "9",           "9",           0x2b },
    { 634,  42,  28,  28, COL_BLACK, "#",           "#",           0x5a },
    { 538,  74,  28,  28, COL_BLACK, "4",           "4",           0x7a },
    { 570,  74,  28,  28, COL_BLACK, "5",           "5",           0x7b },
    { 602,  74,  28,  28, COL_BLACK, "6",           "6",           0x1a },
    { 634,  74,  28,  28, COL_BLACK, "DEL",         "Delete",      0x4b },
    { 538, 106,  28,  28, COL_BLACK, "1",           "1",           0x6b },
    { 570, 106,  28,  28, COL_BLACK, "2",           "2",           0x7c },
    { 602, 106,  28,  28, COL_BLACK, "3",           "3",           0x6c },
    { 634, 106,  28,  28, COL_BLACK, ",",           ",",           0x5c },
    { 538, 138,  28,  28, COL_BLACK, "0",           "0",           0x6a },
    { 570, 138,  28,  28, COL_BLACK, ".",           ".",           0x4c },
    { 602, 138,  60,  28, COL_BLACK, "RETURN",      "Return",      0x3c },

    { 116, 218,  96,  28, COL_GREEN, "Full Speed",  "Full Speed",  0xfe },
    { 216, 218,  96,  28, COL_GREEN, "Pause",       "Pause",       0xfd },
    { 316, 218,  96,  28, COL_GREEN, "Full Screen", "Full Screen", 0xfc },
    { 416, 218,  96,  28, COL_GREEN, "Debug Break", "Debug Break", 0xfb }
};

static const key_dlg_t master_kbd_dlg = { kcaps_master, kcaps_master + MASTER_NKEY, 676, 304 };

#define BTNS_Y    266
#define BTNS_W     60
#define BTNS_H     28
#define BTN_OK_X  201
#define BTN_CAN_X 267

static ALLEGRO_THREAD *keydef_thread;
static ALLEGRO_FONT *font;
static ALLEGRO_EVENT_SOURCE uevsrc;

static void draw_button(int x, int y, int w, int h, ALLEGRO_COLOR bcol, ALLEGRO_COLOR tcol, const char *text)
{
    if (hiresdisplay) {
        x *= 2;
        y *= 2;
        w *= 2;
        h *= 2;
    }
    al_draw_filled_rectangle(x, y, x+w, y+h, bcol);
    al_draw_text(font, tcol, x+(w/2), y+(h/2)-2, ALLEGRO_ALIGN_CENTRE, text);
}

static void draw_keyboard(const key_dlg_t *key_dlg, int ok_x, int can_x)
{
    ALLEGRO_COLOR black, grey, white, red, brown, navy, green;
    const key_cap_t *kptr;

    black = al_map_rgb(  0,   0,   0);
    grey  = al_map_rgb(127, 127, 127);
    white = al_map_rgb(255, 255, 255);
    brown = al_map_rgb( 64,  32,  32);
    red   = al_map_rgb(255,  64,  64);
    navy  = al_map_rgb( 32,  32,  64);
    green = al_map_rgb(  0,  127,  0);
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
            case COL_GREEN:
                draw_button(kptr->x, kptr->y, kptr->w, kptr->h, green, white, kptr->cap);
                break;
        }
    }
    draw_button(ok_x, BTNS_Y, BTNS_W, BTNS_H, navy, white, "OK");
    draw_button(can_x, BTNS_Y, BTNS_W, BTNS_H, navy, white, "Cancel");
    al_flip_display();
}

static void redef_message(const key_dlg_t *key_dlg, const key_cap_t *kptr, uint8_t *keylookcpy)
{
    int mid_x  = key_dlg->disp_x/2;
    int left_x = mid_x-200;
    int mid_y  = key_dlg->disp_y/2;
    int top_y  = mid_y - 36;
    ALLEGRO_COLOR navy = al_map_rgb( 32,  32,  64);
    ALLEGRO_COLOR white = al_map_rgb(255, 255, 255);
    char s[1024], *p;
    int size, remain, count;

    log_debug("keydef-allegro: BBC key %s (%s), code %d clicked", kptr->cap, kptr->name, kptr->keycode);

    if (hiresdisplay) {
        mid_x  *= 2;
        left_x *= 2;
        mid_y  *= 2;
        top_y  *= 2;
    }
    al_draw_filled_rectangle(left_x, top_y, left_x + 400, top_y + 72, navy);
    snprintf(s, sizeof s, "Redefining %s", kptr->name);
    al_draw_text(font, white, left_x+24, top_y+16, ALLEGRO_ALIGN_LEFT, s);
    size = snprintf(s, sizeof s, "Assigned to PC key(s):");
    p = s + size;
    remain = sizeof s - size;
    count = 0;
    int actcode = 0xff - kptr->keycode;
    if (actcode >= 0) {
        const char *fmt = keyactions[actcode].altstate ? "Alt-%s" : "%s";
        snprintf(p, remain, fmt, al_keycode_to_name(keyactions[actcode].keycode));
    }
    else {
        for (int code = 0; remain > 0 && code < ALLEGRO_KEY_MAX; code++) {
            if (keylookcpy[code] == kptr->keycode) {
                const char *fmt = count == 0 ? " %s" : ", %s";
                size = snprintf(p, remain, fmt, al_keycode_to_name(code));
                p += size;
                remain -= size;
            }
        }
    }
    al_draw_text(font, white, left_x+24, top_y+32, ALLEGRO_ALIGN_LEFT, s);
    al_draw_text(font, white, left_x+24, top_y+48, ALLEGRO_ALIGN_LEFT, "Please press new key...");
    al_flip_display();
}

static bool mouse_within(ALLEGRO_EVENT *event, int x, int y, int w, int h)
{
    if (hiresdisplay) {
        x *= 2;
        y *= 2;
        w *= 2;
        h *= 2;
    }
    return event->mouse.x >= x && event->mouse.x <= x+w && event->mouse.y >= y && event->mouse.y <= y+h;
}

static void *keydef_thread_proc(ALLEGRO_THREAD *thread, void *tdata)
{
    log_debug("keydef-allegro: key define thread started");
    if (!font) {
        al_init_font_addon();
        font = al_create_builtin_font();
        if (!font)
            return NULL;
    }

    const key_dlg_t *key_dlg = MASTER ? &master_kbd_dlg : &bbc_kbd_dlg;
    unsigned disp_x = key_dlg->disp_x;
    unsigned disp_y = key_dlg->disp_y;
    if (hiresdisplay) {
        disp_x *= 2;
        disp_y *= 2;
    }
    ALLEGRO_DISPLAY *display = al_create_display(disp_x, disp_y);
    if (display) {
        ALLEGRO_EVENT_QUEUE *queue = al_create_event_queue();
        if (queue) {
            state_t state = ST_BBC_KEY;
            uint8_t keylookcpy[ALLEGRO_KEY_MAX];
            struct key_act_lookup keyactioncpy[KEY_ACTION_MAX];
            const key_cap_t *kptr = NULL;
            bool alt_down = false;
            int mid_x = key_dlg->disp_x/2;
            int ok_x = mid_x-3-BTNS_W;
            int can_x = mid_x+3;
            al_init_user_event_source(&uevsrc);
            al_register_event_source(queue, &uevsrc);
            al_register_event_source(queue, al_get_display_event_source(display));
            al_register_event_source(queue, al_get_mouse_event_source());
            al_register_event_source(queue, al_get_keyboard_event_source());
            memcpy(keylookcpy, keylookup, ALLEGRO_KEY_MAX);
            memcpy(keyactioncpy, keyactions, KEY_ACTION_MAX * sizeof(struct key_act_lookup));
            draw_keyboard(key_dlg, ok_x, can_x);
            while (state != ST_DONE) {
                ALLEGRO_EVENT event;
                al_wait_for_event(queue, &event);
                switch(event.type) {
                    case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                        if (mouse_within(&event, ok_x, BTNS_Y, BTNS_W, BTNS_H)) {
                            // Ok button clicked.
                            memcpy(keylookup, keylookcpy, ALLEGRO_KEY_MAX);
                            memcpy(keyactions, keyactioncpy, KEY_ACTION_MAX * sizeof(struct key_act_lookup));
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
                        memcpy(keylookup, keylookcpy, ALLEGRO_KEY_MAX);
                        memcpy(keyactions, keyactioncpy, KEY_ACTION_MAX * sizeof(struct key_act_lookup));
                        state = ST_DONE;
                        break;
                    case ALLEGRO_EVENT_KEY_DOWN:
                        if (event.keyboard.keycode == ALLEGRO_KEY_ALT || event.keyboard.keycode == ALLEGRO_KEY_ALTGR) {
                            log_debug("keydef-allegro: alt down");
                            alt_down = true;
                        }
                        break;
                    case ALLEGRO_EVENT_KEY_CHAR:
                        if (state == ST_PC_KEY) {
                            int keycode = key_map_keypad(&event);
                            int actcode = kptr->keycode;
                            if (actcode & 0x80) {
                                actcode = 0xff - actcode;
                                log_debug("keydef-allegro: mapping allegro code %d:%s to action#%d:%s, alt=%d", keycode, al_keycode_to_name(keycode), actcode, kptr->name, alt_down);
                                keyactioncpy[actcode].keycode = keycode;
                                keyactioncpy[actcode].altstate = alt_down;
                            }
                            else {
                                log_debug("keydef-allegro: mapping allegro code %d:%s to BBC code %02x", keycode, al_keycode_to_name(keycode), actcode);
                                keylookcpy[event.keyboard.keycode] = actcode;
                            }
                            state = ST_BBC_KEY;
                            draw_keyboard(key_dlg, ok_x, can_x);
                        }
                        break;
                    case ALLEGRO_EVENT_KEY_UP:
                        if (event.keyboard.keycode == ALLEGRO_KEY_ALT || event.keyboard.keycode == ALLEGRO_KEY_ALTGR) {
                            log_debug("keydef-allegro: alt up");
                            alt_down = false;
                        }
                }
            }
            al_destroy_event_queue(queue);
            keydef_thread = NULL;
        } else
            log_error("keydef-allegro: unable to create event queue");
        al_destroy_display(display);
    } else
        log_error("keydef-allegro: unable to create display");
    keydefining = false;
    log_debug("keydef-allegro: key define thread finished");
    return NULL;
}

void gui_keydefine_open(void)
{
    if (!keydef_thread) {
        if ((keydef_thread = al_create_thread(keydef_thread_proc, NULL))) {
            keydefining = true;
            al_start_thread(keydef_thread);
        }
    }
}

void gui_keydefine_close(void)
{
    if (keydef_thread) {
        ALLEGRO_EVENT event;

        event.type = ALLEGRO_EVENT_DISPLAY_CLOSE;
        al_emit_user_event(&uevsrc, &event, NULL);
        al_join_thread(keydef_thread, NULL);
        keydef_thread = NULL;
    }
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
