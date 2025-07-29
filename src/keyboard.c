#include "b-em.h"
#include "main.h"
#include "via.h"
#include "sysvia.h"
#include "keyboard.h"
#include "model.h"
#include "6502.h"
#include "fullscreen.h"
#include <ctype.h>
#include <allegro5/keyboard.h>

/*
 * The BBC micro keyboard is a fairly standard matrix.  On the Model B,
 * column lines are activated by decoding the output of a 74LS163
 * counter with a 4 to 10 line decoder.  Row lines are pulled up with
 * resistors and fed both to an eight input NAND gate to generate an
 * interrupt and to a 74LS251 multiplexer which allows the row lines
 * to be read.
 *
 * The Master adds an extra three columns to the matrix to handle the
 * numeric keymap.  In the diagram below columns 0x00-0x09 are common
 * to the Model B and the Master while 0x0a-0x0c are Master-only.
 *
 * The diagram in the original version of The Advanced User Guide is
 * slighly misleading because the bits as seen by the 74LS251 do not
 * match the rows shown in the diagram.  From a software perspective
 * the keyboard looks like this:
 *
 *       0x00      0x01  0x02  0x03 0x04 0x05 0x06 0x07 0x08 0x09    0x0a   0x0b   0x0c
 * 0x00  Shift     Ctrl  <------- starup up DIP swicthes ------->
 * 0x10  Q         3     4     5    f4   8    f7   =-   ~^   Left    KP 6   KP 7
 * 0x20  f0        W     E     T    7    I    9    0    £    Down    KP 8   KP 9
 * 0x30  1         2     D     R    6    U    O    P    [{   Up      KP +   KP -   KP Return
 * 0x40  CapsLck   A     X     F    Y    J    K    @    :*   Return  KP /   KP Del KP .
 * 0x50  ShiftLck  S     C     G    H    N    L    ;+   ]}   Delete  KP #   KP *   KP ,
 * 0x60  Tab       Z     SPC   V    B    M    <,   >.   /?   Copy    KP 0   KP 1   KP 3
 * 0x70  ESC       f1    f2    f3   f5   f6   f8   f9   \    Right   KP 4   KP 5   KP 2
 *
 * In the two keymaps that follow, one for physical mode and one for
 * hybrid/physical mode, the matrix values are encoded as in the
 * above table, i.e. the upper 4 bits are the row number and the
 * lower 4 bits are the column number.
 */

/* This keymap is used for physical mode.
 *
 * This uses a single rogue value, 0xaa, that cannot correspond
 * to a real key, to indicate that the key event from Allegro
 * should be ignored rather than translating to a BBC micro keypress.
 */

const uint8_t key_allegro2bbc[ALLEGRO_KEY_MAX] =
{
    0xaa,   // 0
    0x41,   // 1    ALLEGRO_KEY_A
    0x64,   // 2    ALLEGRO_KEY_B
    0x52,   // 3    ALLEGRO_KEY_C
    0x32,   // 4    ALLEGRO_KEY_D
    0x22,   // 5    ALLEGRO_KEY_E
    0x43,   // 6    ALLEGRO_KEY_F
    0x53,   // 7    ALLEGRO_KEY_G
    0x54,   // 8    ALLEGRO_KEY_H
    0x25,   // 9    ALLEGRO_KEY_I
    0x45,   // 10   ALLEGRO_KEY_J
    0x46,   // 11   ALLEGRO_KEY_K
    0x56,   // 12   ALLEGRO_KEY_L
    0x65,   // 13   ALLEGRO_KEY_M
    0x55,   // 14   ALLEGRO_KEY_N
    0x36,   // 15   ALLEGRO_KEY_O
    0x37,   // 16   ALLEGRO_KEY_P
    0x10,   // 17   ALLEGRO_KEY_Q
    0x33,   // 18   ALLEGRO_KEY_R
    0x51,   // 19   ALLEGRO_KEY_S
    0x23,   // 20   ALLEGRO_KEY_T
    0x35,   // 21   ALLEGRO_KEY_U
    0x63,   // 22   ALLEGRO_KEY_V
    0x21,   // 23   ALLEGRO_KEY_W
    0x42,   // 24   ALLEGRO_KEY_X
    0x44,   // 25   ALLEGRO_KEY_Y
    0x61,   // 26   ALLEGRO_KEY_Z
    0x27,   // 27   ALLEGRO_KEY_0
    0x30,   // 28   ALLEGRO_KEY_1
    0x31,   // 29   ALLEGRO_KEY_2
    0x11,   // 30   ALLEGRO_KEY_3
    0x12,   // 31   ALLEGRO_KEY_4
    0x13,   // 32   ALLEGRO_KEY_5
    0x34,   // 33   ALLEGRO_KEY_6
    0x24,   // 34   ALLEGRO_KEY_7
    0x15,   // 35   ALLEGRO_KEY_8
    0x26,   // 36   ALLEGRO_KEY_9
    0x6a,   // 37   ALLEGRO_KEY_PAD_0
    0x6b,   // 38   ALLEGRO_KEY_PAD_1
    0x7c,   // 39   ALLEGRO_KEY_PAD_2
    0x6c,   // 40   ALLEGRO_KEY_PAD_3
    0x7a,   // 41   ALLEGRO_KEY_PAD_4
    0x7b,   // 42   ALLEGRO_KEY_PAD_5
    0x1a,   // 43   ALLEGRO_KEY_PAD_6
    0x1b,   // 44   ALLEGRO_KEY_PAD_7
    0x2a,   // 45   ALLEGRO_KEY_PAD_8
    0x2b,   // 46   ALLEGRO_KEY_PAD_9
    0x20,   // 56   ALLEGRO_KEY_F1
    0x71,   // 47   ALLEGRO_KEY_F2
    0x72,   // 48   ALLEGRO_KEY_F3
    0x73,   // 49   ALLEGRO_KEY_F4
    0x14,   // 50   ALLEGRO_KEY_F5
    0x74,   // 51   ALLEGRO_KEY_F6
    0x75,   // 52   ALLEGRO_KEY_F7
    0x16,   // 53   ALLEGRO_KEY_F8
    0x76,   // 54   ALLEGRO_KEY_F9
    0x77,   // 55   ALLEGRO_KEY_F10
    0x28,   // 57   ALLEGRO_KEY_F11
    0xaa,   // 58   ALLEGRO_KEY_F12
    0x70,   // 59   ALLEGRO_KEY_ESCAPE
    0x28,   // 60   ALLEGRO_KEY_TILDE
    0x17,   // 61   ALLEGRO_KEY_MINUS
    0x18,   // 62   ALLEGRO_KEY_EQUALS
    0x59,   // 63   ALLEGRO_KEY_BACKSPACE
    0x60,   // 64   ALLEGRO_KEY_TAB
    0x47,   // 65   ALLEGRO_KEY_OPENBRACE
    0x38,   // 66   ALLEGRO_KEY_CLOSEBRACE
    0x49,   // 67   ALLEGRO_KEY_ENTER
    0x57,   // 68   ALLEGRO_KEY_SEMICOLON
    0x48,   // 69   ALLEGRO_KEY_QUOTE
    0x58,   // 70   ALLEGRO_KEY_BACKSLASH
    0x78,   // 71   ALLEGRO_KEY_BACKSLASH2
    0x66,   // 72   ALLEGRO_KEY_COMMA
    0x67,   // 73   ALLEGRO_KEY_FULLSTOP
    0x68,   // 74   ALLEGRO_KEY_SLASH
    0x62,   // 75   ALLEGRO_KEY_SPACE
    0xaa,   // 76   ALLEGRO_KEY_INSERT
    0x59,   // 77   ALLEGRO_KEY_DELETE
    0x5c,   // 78   ALLEGRO_KEY_HOME
    0x69,   // 79   ALLEGRO_KEY_END
    0xaa,   // 80   ALLEGRO_KEY_PGUP
    0x4c,   // 81   ALLEGRO_KEY_PGDN
    0x19,   // 82   ALLEGRO_KEY_LEFT
    0x79,   // 83   ALLEGRO_KEY_RIGHT
    0x39,   // 84   ALLEGRO_KEY_UP
    0x29,   // 85   ALLEGRO_KEY_DOWN
    0x4a,   // 86   ALLEGRO_KEY_PAD_SLASH
    0x5b,   // 87   ALLEGRO_KEY_PAD_ASTERISK
    0x3b,   // 88   ALLEGRO_KEY_PAD_MINUS
    0x3a,   // 89   ALLEGRO_KEY_PAD_PLUS
    0x59,   // 90   ALLEGRO_KEY_PAD_DELETE
    0x3c,   // 91   ALLEGRO_KEY_PAD_ENTER
    0x4c,   // 92   ALLEGRO_KEY_PRINTSCREEN
    0xaa,   // 93   ALLEGRO_KEY_PAUSE
    0x4c,   // 94   ALLEGRO_KEY_ABNT_C1
    0xaa,   // 95   ALLEGRO_KEY_YEN
    0xaa,   // 96   ALLEGRO_KEY_KANA
    0xaa,   // 97   ALLEGRO_KEY_CONVERT
    0xaa,   // 98   ALLEGRO_KEY_NOCONVERT
    0x00,   // 99   ALLEGRO_KEY_AT
    0x00,   // 100  ALLEGRO_KEY_CIRCUMFLEX
    0x01,   // 101  ALLEGRO_KEY_COLON2
    0xaa,   // 102  ALLEGRO_KEY_KANJI
    0x50,   // 103  ALLEGRO_KEY_PAD_EQUALS
    0xaa,   // 104  ALLEGRO_KEY_BACKQUOTE
    0x57,   // 105  ALLEGRO_KEY_SEMICOLON2
    0xaa,   // 106  ALLEGRO_KEY_COMMAND
    0x50,   // 107  ALLEGRO_KEY_BACK
    0xaa,   // 108  ALLEGRO_KEY_VOLUME_UP
    0xaa,   // 109  ALLEGRO_KEY_VOLUME_DOWN
    0xaa,   // 110  ALLEGRO_KEY_SEARCH
    0xaa,   // 111  ALLEGRO_KEY_DPAD_CENTER
    0xaa,   // 112  ALLEGRO_KEY_BUTTON_X
    0xaa,   // 113  ALLEGRO_KEY_BUTTON_Y
    0xaa,   // 114  ALLEGRO_KEY_DPAD_UP
    0xaa,   // 115  ALLEGRO_KEY_DPAD_DOWN
    0xaa,   // 116  ALLEGRO_KEY_DPAD_LEFT
    0xaa,   // 117  ALLEGRO_KEY_DPAD_RIGHT
    0xaa,   // 118  ALLEGRO_KEY_SELECT
    0xaa,   // 119  ALLEGRO_KEY_START
    0xaa,   // 120  ALLEGRO_KEY_BUTTON_L1
    0xaa,   // 121  ALLEGRO_KEY_BUTTON_R1
    0xaa,   // 122  ALLEGRO_KEY_BUTTON_L2
    0xaa,   // 123  ALLEGRO_KEY_BUTTON_R2
    0xaa,   // 124  ALLEGRO_KEY_BUTTON_A
    0xaa,   // 125  ALLEGRO_KEY_BUTTON_B
    0xaa,   // 126  ALLEGRO_KEY_THUMBL
    0xaa,   // 127  ALLEGRO_KEY_THUMBR
    0xaa,   // 128
    0xaa,   // 129
    0xaa,   // 130
    0xaa,   // 131
    0xaa,   // 132
    0xaa,   // 133
    0xaa,   // 134
    0xaa,   // 135
    0xaa,   // 136
    0xaa,   // 137
    0xaa,   // 138
    0xaa,   // 139
    0xaa,   // 140
    0xaa,   // 141
    0xaa,   // 142
    0xaa,   // 143
    0xaa,   // 144
    0xaa,   // 145
    0xaa,   // 146
    0xaa,   // 147
    0xaa,   // 148
    0xaa,   // 149
    0xaa,   // 150
    0xaa,   // 151
    0xaa,   // 152
    0xaa,   // 153
    0xaa,   // 154
    0xaa,   // 155
    0xaa,   // 156
    0xaa,   // 157
    0xaa,   // 158
    0xaa,   // 159
    0xaa,   // 160
    0xaa,   // 161
    0xaa,   // 162
    0xaa,   // 163
    0xaa,   // 164
    0xaa,   // 165
    0xaa,   // 166
    0xaa,   // 167
    0xaa,   // 168
    0xaa,   // 169
    0xaa,   // 170
    0xaa,   // 171
    0xaa,   // 172
    0xaa,   // 173
    0xaa,   // 174
    0xaa,   // 175
    0xaa,   // 176
    0xaa,   // 177
    0xaa,   // 178
    0xaa,   // 179
    0xaa,   // 180
    0xaa,   // 181
    0xaa,   // 182
    0xaa,   // 183
    0xaa,   // 184
    0xaa,   // 185
    0xaa,   // 186
    0xaa,   // 187
    0xaa,   // 188
    0xaa,   // 189
    0xaa,   // 190
    0xaa,   // 191
    0xaa,   // 192
    0xaa,   // 193
    0xaa,   // 194
    0xaa,   // 195
    0xaa,   // 196
    0xaa,   // 197
    0xaa,   // 198
    0xaa,   // 199
    0xaa,   // 200
    0xaa,   // 201
    0xaa,   // 202
    0xaa,   // 203
    0xaa,   // 204
    0xaa,   // 205
    0xaa,   // 206
    0xaa,   // 207
    0xaa,   // 208
    0xaa,   // 209
    0xaa,   // 210
    0xaa,   // 211
    0xaa,   // 212
    0xaa,   // 213
    0xaa,   // 214
    0x00,   // 215  ALLEGRO_KEY_LSHIFT
    0x00,   // 216  ALLEGRO_KEY_RSHIFT
    0x01,   // 217  ALLEGRO_KEY_LCTRL
    0x01,   // 218  ALLEGRO_KEY_RCTRL
    0xaa,   // 219  ALLEGRO_KEY_ALT
    0xaa,   // 220  ALLEGRO_KEY_ALTGR
    0xaa,   // 221  ALLEGRO_KEY_LWIN
    0xaa,   // 222  ALLEGRO_KEY_RWIN
    0xaa,   // 223  ALLEGRO_KEY_MENU
    0xaa,   // 224  ALLEGRO_KEY_SCROLLLOCK
    0x71,   // 225  ALLEGRO_KEY_NUMLOCK
    0x40,   // 226  ALLEGRO_KEY_CAPSLOCK
};

/* Mapping from Allegro to BBC keycodes for logical and hybrid keyboard
 * modes.  We use two invalid keycodes to indicate special actions:
 * - 0xaa causes us to fake the necessary keypresses to type the ASCII
 *        character we got from the ALLEGRO_EVENT_KEY_CHAR event.
 * - 0xbb causes us to ignore the key.
 * Valid keycodes cause the logical keyboard code to press and release
 * the key for that keycode.

 * Note that not all keys generate an ALLEGRO_EVENT_KEY_CHAR, so some
 * of the entries in this table can never be accessed.
 */

static const uint8_t allegro2bbclogical[ALLEGRO_KEY_MAX] =
{
    0xbb,   // 0
    0x41,   // 1    ALLEGRO_KEY_A
    0x64,   // 2    ALLEGRO_KEY_B
    0x52,   // 3    ALLEGRO_KEY_C
    0x32,   // 4    ALLEGRO_KEY_D
    0x22,   // 5    ALLEGRO_KEY_E
    0x43,   // 6    ALLEGRO_KEY_F
    0x53,   // 7    ALLEGRO_KEY_G
    0x54,   // 8    ALLEGRO_KEY_H
    0x25,   // 9    ALLEGRO_KEY_I
    0x45,   // 10   ALLEGRO_KEY_J
    0x46,   // 11   ALLEGRO_KEY_K
    0x56,   // 12   ALLEGRO_KEY_L
    0x65,   // 13   ALLEGRO_KEY_M
    0x55,   // 14   ALLEGRO_KEY_N
    0x36,   // 15   ALLEGRO_KEY_O
    0x37,   // 16   ALLEGRO_KEY_P
    0x10,   // 17   ALLEGRO_KEY_Q
    0x33,   // 18   ALLEGRO_KEY_R
    0x51,   // 19   ALLEGRO_KEY_S
    0x23,   // 20   ALLEGRO_KEY_T
    0x35,   // 21   ALLEGRO_KEY_U
    0x63,   // 22   ALLEGRO_KEY_V
    0x21,   // 23   ALLEGRO_KEY_W
    0x42,   // 24   ALLEGRO_KEY_X
    0x44,   // 25   ALLEGRO_KEY_Y
    0x61,   // 26   ALLEGRO_KEY_Z
    0xaa,   // 27   ALLEGRO_KEY_0
    0xaa,   // 28   ALLEGRO_KEY_1
    0xaa,   // 29   ALLEGRO_KEY_2
    0xaa,   // 30   ALLEGRO_KEY_3
    0xaa,   // 31   ALLEGRO_KEY_4
    0xaa,   // 32   ALLEGRO_KEY_5
    0xaa,   // 33   ALLEGRO_KEY_6
    0xaa,   // 34   ALLEGRO_KEY_7
    0xaa,   // 35   ALLEGRO_KEY_8
    0xaa,   // 36   ALLEGRO_KEY_9
    0xaa,   // 37   ALLEGRO_KEY_PAD_0
    0xaa,   // 38   ALLEGRO_KEY_PAD_1
    0xaa,   // 39   ALLEGRO_KEY_PAD_2
    0xaa,   // 40   ALLEGRO_KEY_PAD_3
    0xaa,   // 41   ALLEGRO_KEY_PAD_4
    0xaa,   // 42   ALLEGRO_KEY_PAD_5
    0xaa,   // 43   ALLEGRO_KEY_PAD_6
    0xaa,   // 44   ALLEGRO_KEY_PAD_7
    0xaa,   // 45   ALLEGRO_KEY_PAD_8
    0xaa,   // 46   ALLEGRO_KEY_PAD_9
    0x71,   // 56   ALLEGRO_KEY_F1
    0x72,   // 47   ALLEGRO_KEY_F2
    0x73,   // 48   ALLEGRO_KEY_F3
    0x14,   // 49   ALLEGRO_KEY_F4
    0x74,   // 50   ALLEGRO_KEY_F5
    0x75,   // 51   ALLEGRO_KEY_F6
    0x16,   // 52   ALLEGRO_KEY_F7
    0x76,   // 53   ALLEGRO_KEY_F8
    0x77,   // 54   ALLEGRO_KEY_F9
    0x20,   // 55   ALLEGRO_KEY_F10
    0xbb,   // 57   ALLEGRO_KEY_F11
    0xaa,   // 58   ALLEGRO_KEY_F12
    0x70,   // 59   ALLEGRO_KEY_ESCAPE
    0xaa,   // 60   ALLEGRO_KEY_TILDE
    0xaa,   // 61   ALLEGRO_KEY_MINUS
    0xaa,   // 62   ALLEGRO_KEY_EQUALS
    0x59,   // 63   ALLEGRO_KEY_BACKSPACE
    0x60,   // 64   ALLEGRO_KEY_TAB
    0xaa,   // 65   ALLEGRO_KEY_OPENBRACE
    0xaa,   // 66   ALLEGRO_KEY_CLOSEBRACE
    0xaa,   // 67   ALLEGRO_KEY_ENTER
    0xaa,   // 68   ALLEGRO_KEY_SEMICOLON
    0xaa,   // 69   ALLEGRO_KEY_QUOTE
    0xaa,   // 70   ALLEGRO_KEY_BACKSLASH
    0xaa,   // 71   ALLEGRO_KEY_BACKSLASH2
    0xaa,   // 72   ALLEGRO_KEY_COMMA
    0xaa,   // 73   ALLEGRO_KEY_FULLSTOP
    0xaa,   // 74   ALLEGRO_KEY_SLASH
    0x62,   // 75   ALLEGRO_KEY_SPACE
    0xbb,   // 76   ALLEGRO_KEY_INSERT
    0x59,   // 77   ALLEGRO_KEY_DELETE
    0xbb,   // 78   ALLEGRO_KEY_HOME
    0x69,   // 79   ALLEGRO_KEY_END
    0xbb,   // 80   ALLEGRO_KEY_PGUP
    0xbb,   // 81   ALLEGRO_KEY_PGDN
    0x19,   // 82   ALLEGRO_KEY_LEFT
    0x79,   // 83   ALLEGRO_KEY_RIGHT
    0x39,   // 84   ALLEGRO_KEY_UP
    0x29,   // 85   ALLEGRO_KEY_DOWN
    0xaa,   // 86   ALLEGRO_KEY_PAD_SLASH
    0xaa,   // 87   ALLEGRO_KEY_PAD_ASTERISK
    0xaa,   // 88   ALLEGRO_KEY_PAD_MINUS
    0xaa,   // 89   ALLEGRO_KEY_PAD_PLUS
    0x59,   // 90   ALLEGRO_KEY_PAD_DELETE
    0xaa,   // 91   ALLEGRO_KEY_PAD_ENTER
    0xbb,   // 92   ALLEGRO_KEY_PRINTSCREEN
    0xbb,   // 93   ALLEGRO_KEY_PAUSE
    0xbb,   // 94   ALLEGRO_KEY_ABNT_C1
    0xbb,   // 95   ALLEGRO_KEY_YEN
    0xbb,   // 96   ALLEGRO_KEY_KANA
    0xbb,   // 97   ALLEGRO_KEY_CONVERT
    0xbb,   // 98   ALLEGRO_KEY_NOCONVERT
    0xbb,   // 99   ALLEGRO_KEY_AT
    0xbb,   // 100  ALLEGRO_KEY_CIRCUMFLEX
    0xbb,   // 101  ALLEGRO_KEY_COLON2
    0xbb,   // 102  ALLEGRO_KEY_KANJI
    0xbb,   // 103  ALLEGRO_KEY_PAD_EQUALS
    0xbb,   // 104  ALLEGRO_KEY_BACKQUOTE
    0xbb,   // 105  ALLEGRO_KEY_SEMICOLON2
    0xbb,   // 106  ALLEGRO_KEY_COMMAND
    0xbb,   // 107  ALLEGRO_KEY_BACK
    0xbb,   // 108  ALLEGRO_KEY_VOLUME_UP
    0xbb,   // 109  ALLEGRO_KEY_VOLUME_DOWN
    0xbb,   // 110
    0xbb,   // 111
    0xbb,   // 112
    0xbb,   // 113
    0xbb,   // 114
    0xbb,   // 115
    0xbb,   // 116
    0xbb,   // 117
    0xbb,   // 118
    0xbb,   // 119
    0xbb,   // 120
    0xbb,   // 121
    0xbb,   // 122
    0xbb,   // 123
    0xbb,   // 124
    0xbb,   // 125
    0xbb,   // 126
    0xbb,   // 127
    0xbb,   // 128
    0xbb,   // 129
    0xbb,   // 130
    0xbb,   // 131
    0xbb,   // 132
    0xbb,   // 133
    0xbb,   // 134
    0xbb,   // 135
    0xbb,   // 136
    0xbb,   // 137
    0xbb,   // 138
    0xbb,   // 139
    0xbb,   // 140
    0xbb,   // 141
    0xbb,   // 142
    0xbb,   // 143
    0xbb,   // 144
    0xbb,   // 145
    0xbb,   // 146
    0xbb,   // 147
    0xbb,   // 148
    0xbb,   // 149
    0xbb,   // 150
    0xbb,   // 151
    0xbb,   // 152
    0xbb,   // 153
    0xbb,   // 154
    0xbb,   // 155
    0xbb,   // 156
    0xbb,   // 157
    0xbb,   // 158
    0xbb,   // 159
    0xbb,   // 160
    0xbb,   // 161
    0xbb,   // 162
    0xbb,   // 163
    0xbb,   // 164
    0xbb,   // 165
    0xbb,   // 166
    0xbb,   // 167
    0xbb,   // 168
    0xbb,   // 169
    0xbb,   // 170
    0xbb,   // 171
    0xbb,   // 172
    0xbb,   // 173
    0xbb,   // 174
    0xbb,   // 175
    0xbb,   // 176
    0xbb,   // 177
    0xbb,   // 178
    0xbb,   // 179
    0xbb,   // 180
    0xbb,   // 181
    0xbb,   // 182
    0xbb,   // 183
    0xbb,   // 184
    0xbb,   // 185
    0xbb,   // 186
    0xbb,   // 187
    0xbb,   // 188
    0xbb,   // 189
    0xbb,   // 190
    0xbb,   // 191
    0xbb,   // 192
    0xbb,   // 193
    0xbb,   // 194
    0xbb,   // 195
    0xbb,   // 196
    0xbb,   // 197
    0xbb,   // 198
    0xbb,   // 199
    0xbb,   // 200
    0xbb,   // 201
    0xbb,   // 202
    0xbb,   // 203
    0xbb,   // 204
    0xbb,   // 205
    0xbb,   // 206
    0xbb,   // 207
    0xbb,   // 208
    0xbb,   // 209
    0xbb,   // 210
    0xbb,   // 211
    0xbb,   // 212
    0xbb,   // 213
    0xbb,   // 214
    0xbb,   // 215  ALLEGRO_KEY_LSHIFT
    0xbb,   // 216  ALLEGRO_KEY_RSHIFT
    0xbb,   // 217  ALLEGRO_KEY_LCTRL
    0xbb,   // 218  ALLEGRO_KEY_RCTRL
    0xbb,   // 219  ALLEGRO_KEY_ALT
    0xbb,   // 220  ALLEGRO_KEY_ALTGR
    0xbb,   // 221  ALLEGRO_KEY_LWIN
    0xbb,   // 222  ALLEGRO_KEY_RWIN
    0xbb,   // 223  ALLEGRO_KEY_MENU
    0xbb,   // 224  ALLEGRO_KEY_SCROLLLOCK
    0xbb,   // 225  ALLEGRO_KEY_NUMLOCK
    0xbb,   // 226  ALLEGRO_KEY_CAPSLOCK
};

/* The following table maps from the modified ASCII code used by the
 * BBC micro to the BBC key matrix code for the character key along
 * with any necessary modifiers needed to obtain that character.
 */

#define A2B_SHIFT 0x100
#define A2B_CTRL  0x200

static const uint16_t ascii2bbc[] =
{
    0x47|A2B_CTRL,  // 0x00 NUL (CTRL-@)
    0x41|A2B_CTRL,  // 0x01 SOH (CTRL-A)
    0x64|A2B_CTRL,  // 0x02 STX (CTRL-B)
    0x52|A2B_CTRL,  // 0x03 ETX (CTRL-C)
    0x32|A2B_CTRL,  // 0x04 EOT (CTRL-D)
    0x22|A2B_CTRL,  // 0x05 ENQ (CTRL-E)
    0x43|A2B_CTRL,  // 0x06 ACK (CTRL-F)
    0x53|A2B_CTRL,  // 0x07 BEL (CTRL-G)
    0x54|A2B_CTRL,  // 0x08 BS  (CTRL-H)
    0x60,           // 0x09 HT  (CTRL-I)
    0x49,           // 0x0a LF  (CTRL-J)
    0x46|A2B_CTRL,  // 0x0b VT  (CTRL-K)
    0x56|A2B_CTRL,  // 0x0c FF  (CTRL-L)
    0x49,           // 0x0d CR  (CTRL-M)
    0x55|A2B_CTRL,  // 0x0e SO  (CTRL-N)
    0x36|A2B_CTRL,  // 0x0f SI  (CTRL-O)
    0x37|A2B_CTRL,  // 0x10 DLE (CTRL-P)
    0x10|A2B_CTRL,  // 0x11 DC1 (CTRL-Q, XON)
    0x33|A2B_CTRL,  // 0x12 DC2 (CTRL-R)
    0x51|A2B_CTRL,  // 0x13 DC3 (CTRL-S, XOFF)
    0x23|A2B_CTRL,  // 0x14 DC4 (CTRL-T)
    0x35|A2B_CTRL,  // 0x15 NAK (CTRL-U)
    0x63|A2B_CTRL,  // 0x16 SYN (CTRL-V)
    0x21|A2B_CTRL,  // 0x17 ETB (CTRL-W)
    0x42|A2B_CTRL,  // 0x18 CAN (CTRL-X)
    0x44|A2B_CTRL,  // 0x19 EM  (CTRL-Y)
    0x61|A2B_CTRL,  // 0x1a SUB (CTRL-Z)
    0x38,           // 0x1b ESC (CTRL-[)
    0x48|A2B_CTRL,  // 0x1c FS  (CTRL-\)
    0x58|A2B_CTRL,  // 0x1d GS  (CTRL-])
    0x18|A2B_CTRL,  // 0x1e RS  (CTRL-^)`
    0x28|A2B_CTRL,  // 0x1f US  (CTRL-_)
    0x62,           // 0x20 SPC
    0x30|A2B_SHIFT, // 0x21 !
    0x31|A2B_SHIFT, // 0x22 "
    0x11|A2B_SHIFT, // 0x23 #
    0x12|A2B_SHIFT, // 0x24 $
    0x13|A2B_SHIFT, // 0x25 %
    0x34|A2B_SHIFT, // 0x26 &
    0x24|A2B_SHIFT, // 0x27 '
    0x15|A2B_SHIFT, // 0x28 (
    0x26|A2B_SHIFT, // 0x29 )
    0x48|A2B_SHIFT, // 0x2a *
    0x57|A2B_SHIFT, // 0x2b +
    0x66,           // 0x2c ,
    0x17,           // 0x2d -
    0x67,           // 0x2e .
    0x68,           // 0x2f /
    0x27,           // 0x30 0
    0x30,           // 0x31 1
    0x31,           // 0x32 2
    0x11,           // 0x33 3
    0x12,           // 0x34 4
    0x13,           // 0x35 5
    0x34,           // 0x36 6
    0x24,           // 0x37 7
    0x15,           // 0x38 8
    0x26,           // 0x39 9
    0x48,           // 0x3a :
    0x57,           // 0x3b ;
    0x66|A2B_SHIFT, // 0x3c <
    0x17|A2B_SHIFT, // 0x3D =
    0x67|A2B_SHIFT, // 0x3E >
    0x68|A2B_SHIFT, // 0x3F ?
    0x47,           // 0x40 @
    0x41|A2B_SHIFT, // 0x41 A
    0x64|A2B_SHIFT, // 0x42 B
    0x52|A2B_SHIFT, // 0x43 C
    0x32|A2B_SHIFT, // 0x44 D
    0x22|A2B_SHIFT, // 0x45 E
    0x43|A2B_SHIFT, // 0x46 F
    0x53|A2B_SHIFT, // 0x47 G
    0x54|A2B_SHIFT, // 0x48 H
    0x25|A2B_SHIFT, // 0x49 I
    0x45|A2B_SHIFT, // 0x4a J
    0x46|A2B_SHIFT, // 0x4b K
    0x56|A2B_SHIFT, // 0x4c L
    0x65|A2B_SHIFT, // 0x4d M
    0x55|A2B_SHIFT, // 0x4e N
    0x36|A2B_SHIFT, // 0x4f O
    0x37|A2B_SHIFT, // 0x50 P
    0x10|A2B_SHIFT, // 0x51 Q
    0x33|A2B_SHIFT, // 0x52 R
    0x51|A2B_SHIFT, // 0x53 S
    0x23|A2B_SHIFT, // 0x54 T
    0x35|A2B_SHIFT, // 0x55 U
    0x63|A2B_SHIFT, // 0x56 V
    0x21|A2B_SHIFT, // 0x57 W
    0x42|A2B_SHIFT, // 0x58 X
    0x44|A2B_SHIFT, // 0x59 Y
    0x61|A2B_SHIFT, // 0x5a Z
    0x38,           // 0x5b [
    0x78,           // 0x5c backslash
    0x58,           // 0x5d ]
    0x18,           // 0x5e ^
    0x28,           // 0x5f _
    0x28|A2B_SHIFT, // 0x60 £
    0x41,           // 0x61 a
    0x64,           // 0x62 b
    0x52,           // 0x63 c
    0x32,           // 0x64 d
    0x22,           // 0x65 e
    0x43,           // 0x66 f
    0x53,           // 0x67 g
    0x54,           // 0x68 h
    0x25,           // 0x69 i
    0x45,           // 0x6a j
    0x46,           // 0x6b k
    0x56,           // 0x6c l
    0x65,           // 0x6d m
    0x55,           // 0x6e n
    0x36,           // 0x6f o
    0x37,           // 0x70 p
    0x10,           // 0x71 q
    0x33,           // 0x72 r
    0x51,           // 0x73 s
    0x23,           // 0x74 t
    0x35,           // 0x75 u
    0x63,           // 0x76 v
    0x21,           // 0x77 w
    0x42,           // 0x78 x
    0x44,           // 0x79 y
    0x61,           // 0x7a z
    0x38|A2B_SHIFT, // 0x7b {
    0x78|A2B_SHIFT, // 0x7c |
    0x58|A2B_SHIFT, // 0x7d }
    0x18|A2B_SHIFT, // 0x7e ~
    0x59            // 0x7f DEL
};

static int keycol, keyrow;
static int bbcmatrix[16][16];
static int hostshift, hostctrl, hostalt;

static void debug_break(void)
{
    if (debug_core || debug_tube)
        debug_step = 1;
}

static void stop_full_speed(void)
{
    main_stop_fullspeed(hostshift);
}

static void do_nothing(void) {}

const struct key_act_const keyact_const[KEY_ACTION_MAX] = {
    { "break",        ALLEGRO_KEY_F12,   false, main_key_break,          do_nothing      },
    { "full-Speed",   ALLEGRO_KEY_PGUP,  false, main_start_fullspeed,    stop_full_speed },
    { "pause",        ALLEGRO_KEY_PGDN,  false, main_key_pause,          do_nothing      },
    { "full-screen1", ALLEGRO_KEY_F11,   false, toggle_fullscreen_menu, do_nothing      },
    { "debug-break",  ALLEGRO_KEY_F10,   false, debug_break,             do_nothing      },
    { "full-screen2", ALLEGRO_KEY_ENTER, true,  toggle_fullscreen_menu, do_nothing      }
};

uint8_t keylookup[ALLEGRO_KEY_MAX];
struct key_act_lookup keyactions[KEY_ACTION_MAX];
bool keyas  = false;
bool keypad = false;
bem_key_mode key_mode = BKM_PHYSICAL;

const char *bem_key_modes[4] = { "Physical", "Hybrid", "Logical", NULL };

typedef enum {
    KP_IDLE,
    KP_NEXT,
    KP_DOWN,
    KP_DELAY
} kp_state_t;

// Extra vkey codes used in logical keyboard mode:
// - VKEY_SHIFT_EVENT|1 indicates the SHIFT key being pushed down
// - VKEY_SHIFT_EVENT indicates the SHIFT key being released
// - VKEY_CTRL_EVENT is like VKEY_SHIFT_EVENT but for the CTRL key
// - VKEY_DOWN is used before an ordinary keycode to indicate that key being
//   pushed down
// - VKEY_UP is used before an ordinary keycode to indicate that key being
//   released
#define VKEY_UP          (0xd0)
#define VKEY_DOWN        (0xd1)
#define VKEY_SHIFT_EVENT (0xe0)
#define VKEY_CTRL_EVENT  (0xf0)

static kp_state_t kp_state = KP_IDLE;
#define KEY_PASTE_BUF_CAPACITY (256)
#define KEY_PASTE_THRESHOLD (128)
#define KEY_PASTE_SEGLEN (KEY_PASTE_THRESHOLD/8)
static unsigned char key_paste_buf[KEY_PASTE_BUF_CAPACITY];
static size_t key_paste_buf_size;
static unsigned char *key_paste_ptr;
static char *key_paste_str, *key_paste_tail;
static size_t key_paste_tail_size;
static uint8_t key_paste_vkey_down;
static bool key_paste_shift;
static bool key_paste_ctrl;
static int last_unichar[ALLEGRO_KEY_MAX];

void key_reset()
{
    for (int c = 0; c < 16; c++)
        for (int r = 0; r < 16; r++)
            bbcmatrix[c][r] = 0;
    sysvia_set_ca2(0);
    key_paste_vkey_down = 0;
    key_paste_shift = false;
    key_paste_ctrl = false;
}

void key_init(void)
{
    memcpy(keylookup, key_allegro2bbc, sizeof(key_allegro2bbc));
    for (int i =0; i < KEY_ACTION_MAX; i++) {
        keyactions[i].keycode = keyact_const[i].keycode;
        keyactions[i].altstate = keyact_const[i].altstate;
    }
    key_reset();
}

void key_lost_focus(void)
{
    key_reset();
    hostshift = false;
    hostctrl = false;
    hostalt = false;
}

static void key_update()
{
    int maxcol = (MASTER) ? 13 : 10;
    if (IC32 & 8) {
        /* autoscan mode */
        for (int col = 0; col < maxcol; col++) {
            for (int row = 1; row < 8; row++) {
                if (bbcmatrix[col][row]) {
                    sysvia_set_ca2(1);
                    return;
                }
            }
        }
    }
    else {
        /* scan specific key mode */
        if (keycol < maxcol) {
            for (int row = 1; row < 8; row++) {
                if (bbcmatrix[keycol][row]) {
                    sysvia_set_ca2(1);
                    return;
                }
            }
        }
    }
    sysvia_set_ca2(0);
}

static const int map_keypad[] = {
    /* ALLEGRO_KEY_PAD_0 */ ALLEGRO_KEY_INSERT,
    /* ALLEGRO_KEY_PAD_1 */ ALLEGRO_KEY_END,
    /* ALLEGRO_KEY_PAD_2 */ ALLEGRO_KEY_DOWN,
    /* ALLEGRO_KEY_PAD_3 */ ALLEGRO_KEY_PGDN,
    /* ALLEGRO_KEY_PAD_4 */ ALLEGRO_KEY_LEFT,
    /* ALLEGRO_KEY_PAD_5 */ ALLEGRO_KEY_PAD_5,
    /* ALLEGRO_KEY_PAD_6 */ ALLEGRO_KEY_RIGHT,
    /* ALLEGRO_KEY_PAD_7 */ ALLEGRO_KEY_HOME,
    /* ALLEGRO_KEY_PAD_8 */ ALLEGRO_KEY_UP,
    /* ALLEGRO_KEY_PAD_9 */ ALLEGRO_KEY_PGUP
};

static void key_paste_add_vkey(uint8_t vkey1, uint8_t vkey2)
{
    log_debug("keyboard: key_paste_add_vkey, vkey1=&%02x, vkey2=&%02x", vkey1, vkey2);

    size_t new_size = key_paste_buf_size + 1 + ((vkey2 != 0xaa) ? 1 : 0);
    if (new_size >= KEY_PASTE_BUF_CAPACITY) {
        assert(key_paste_ptr);
        if (key_paste_ptr > key_paste_buf) {
            log_debug("keyboard: key_paste_add_vkey moving down");
            size_t vkeys_left = key_paste_buf_size - (key_paste_ptr - key_paste_buf);
            memmove(key_paste_buf, key_paste_ptr, vkeys_left);
            key_paste_ptr = key_paste_buf;
            key_paste_buf_size = vkeys_left;
        }
        new_size = key_paste_buf_size + 1 + ((vkey2 != 0xaa) ? 1 : 0);
        if (new_size >= KEY_PASTE_BUF_CAPACITY) {
            // We really don't want this case to happen; it could cause annoying
            // side effects like keys being stuck down because the VKEY_DOWN
            // event made it into the buffer but the corresponding VKEY_UP event
            // didn't. This is why KEY_PASTE_BUF_CAPACITY is significantly
            // larger than KEY_PASTE_THRESHOLD.
            log_warn("keyboard: out of memory adding key to paste, key discarded");
            return;
        }
    }

    // If the number of pending events gets too large, stop inserting key down
    // events. We allow key up events in to avoid keys being stuck down
    // unintentionally. This makes the undesirable out of memory case above
    // unlikely, and has the more practical benefit that the user can't type
    // ahead so much that there's a significant amount of activity after they
    // actually stop typing.

    size_t vkeys_left = key_paste_ptr ? (key_paste_buf_size - (key_paste_ptr - key_paste_buf)) : 0;
    if ((vkey1 == VKEY_DOWN) && (vkeys_left >= KEY_PASTE_THRESHOLD)) {
        log_debug("keyboard: discarding key down as buffer over threshold");
        return;
    }

    if (kp_state == KP_IDLE) {
        key_paste_ptr = key_paste_buf;
        kp_state = KP_NEXT;
    }

    key_paste_buf[key_paste_buf_size++] = vkey1;
    if (vkey2 != 0xaa)
        key_paste_buf[key_paste_buf_size++] = vkey2;
}

static void key_paste_add_vkey_up(uint8_t vkey)
{
    assert((vkey != 0x00) && (vkey != 0x01)); // not SHIFT or CTRL
    assert(vkey == key_paste_vkey_down);
    key_paste_add_vkey(VKEY_UP, vkey);
    key_paste_vkey_down = 0;
}

static void key_paste_add_vkey_down(uint8_t vkey)
{
    assert((vkey != 0x00) && (vkey != 0x01)); // not SHIFT or CTRL
    assert(key_paste_vkey_down == 0);
    key_paste_add_vkey(VKEY_DOWN, vkey);
    key_paste_vkey_down = vkey;
}

static bool key_is_down_logical(uint8_t vkey)
{
    assert(vkey != 0);
    return (key_paste_vkey_down != 0) && (key_paste_vkey_down == vkey);
}

static void key_paste_add_combo(uint8_t vkey, bool shift, bool ctrl)
{
    log_debug("keyboard: key_paste_add_combo vkey=&%02x, shift=%d, ctrl=%d", vkey, shift, ctrl);

    // The following code won't work if we have a value other than 0 or 1; force
    // this in case we're using "typedef int bool".
    shift = !!shift;
    ctrl = !!ctrl;

    if (shift != key_paste_shift) {
        key_paste_add_vkey(VKEY_SHIFT_EVENT | shift, 0xaa);
        key_paste_shift = shift;
    }

    if (ctrl != key_paste_ctrl) {
        key_paste_add_vkey(VKEY_CTRL_EVENT | ctrl, 0xaa);
        key_paste_ctrl = ctrl;
    }

    if (vkey != 0xaa)
        key_paste_add_vkey_down(vkey);
}

static char *key_paste_chars(char *ptr, size_t len)
{
    while (len--) {
        int ch = *(const unsigned char *)ptr++;
        if (ch != 96) {     // Unicode backtick
            if (ch == 163)  // Unicode pound sign.
                ch = 96;    // Translate to back-tick
            if (ch < sizeof(ascii2bbc)) {
                uint16_t bbc_keys = ascii2bbc[ch];
                uint_least8_t vkey = bbc_keys & 0xff;
                key_paste_add_combo(vkey, bbc_keys & A2B_SHIFT, bbc_keys & A2B_CTRL);
                key_paste_add_vkey_up(vkey);
                log_debug("keyboard: key_paste_chars, new key_paste_buf_size=%lu", (unsigned long)key_paste_buf_size);
            }
        }
    }
    return ptr;
}

void key_paste_start(char *str)
{
    if (str) {
        size_t len = strlen(str);
        if (len) {
            if (len <= KEY_PASTE_SEGLEN) {
                log_debug("keyboard: key_paste_start, len=%ld, under threshold", (unsigned long)len);
                key_paste_chars(str, len);
                al_free(str);
            }
            else {
                log_debug("keyboard: key_paste_start, len=%ld, over threshold", (unsigned long)len);
                key_paste_tail = key_paste_chars(str, KEY_PASTE_SEGLEN);
                key_paste_str = str;
                key_paste_tail_size = len - KEY_PASTE_SEGLEN;
            }
        }
    }
}

static void key_paste_stop(void)
{
    al_free(key_paste_str);
    key_paste_str = NULL;
}

static void key_paste_nextseg(void)
{
    size_t len = key_paste_tail_size;
    if (len <= KEY_PASTE_SEGLEN) {
        log_debug("keyboard: key_paste_nextseg, len=%ld, under threshold", (unsigned long)len);
        key_paste_chars(key_paste_tail, len);
        key_paste_stop();
    }
    else {
        log_debug("keyboard: key_paste_nextseg, len=%ld, over threshold", (unsigned long)len);
        key_paste_tail = key_paste_chars(key_paste_tail, KEY_PASTE_SEGLEN);
        key_paste_tail_size = len - KEY_PASTE_SEGLEN;
    }
}

// Called on (derived) host key down and host key up events in logical keyboard
// mode; unichar is only meaningful on key down events (state==1).

static void set_key_logical(int keycode, int unichar, int state)
{
    static uint8_t keycode_to_vkey_map[ALLEGRO_KEY_MAX];

    log_debug("keyboard: set_key_logical keycode=%d, unichar=%d, state=%d", keycode, unichar, state);

    uint8_t vkey = (key_mode == BKM_LOGICAL && unichar > ' ') ? 0xaa : allegro2bbclogical[keycode];
    if (vkey == 0xbb) // ignore the key
        return;

    if (state) { // host key pressed
        bool shift = hostshift;
        bool ctrl = hostctrl;

        if (vkey == 0xaa) {
            // type the ASCII character corresponding to the key
            if (unichar == 96) // unicode backtick
                return;
            if (unichar == 163) // unicode pound currency symbol
                unichar = 96; // Acorn pound currency symbol
            // Ignore keys we can't map and make a note to ignore them when
            // they're released as well.
            if (unichar >= (sizeof(ascii2bbc) / sizeof(ascii2bbc[0]))) {
                keycode_to_vkey_map[keycode] = 0;
                return;
            }
            uint16_t bbc_keys = ascii2bbc[unichar];
            vkey = bbc_keys & 0xff;
            shift = bbc_keys & A2B_SHIFT;
            ctrl = bbc_keys & A2B_CTRL;
            keycode_to_vkey_map[keycode] = vkey;
        }

        // If a key other than 'vkey' is pressed already, release it. This
        // avoids having more keys pressed down than the OS can make sense of
        // and is harmless because the key we're about to press will/should
        // "take over" as the currently active key anyway. If 'vkey' is already
        // pressed we leave it alone, so we don't interfere with any ongoing OS
        // auto-repeat. (This shows up if you hold down 'A' and intermittently
        // press and release SHIFT, for example.)

        if (key_is_down_logical(vkey))
            vkey = 0xaa;
        else if (key_paste_vkey_down != 0)
            key_paste_add_vkey_up(key_paste_vkey_down);

        // Now press 'vkey' (if it's not the no-op value 0xaa). We also set the
        // SHIFT and CTRL keys to the relevant state; we do this after releasing
        // any existing key above (so we don't accidentally cause the OS to
        // interpret the existing key with the new SHIFT/CTRL state) and before
        // pressing the new key.

        key_paste_add_combo(vkey, shift, ctrl);
    }
    else { // host key released
        if (vkey == 0xaa) {
            // If we translated the ASCII character into an emulated keypress
            // when the host key was pressed, we need to release the same key
            // now. If we ignored the key when it was pressed, we need to ignore
            // it now.
            vkey = keycode_to_vkey_map[keycode];
            if (vkey == 0)
                return;
            keycode_to_vkey_map[keycode] = 0;
        }

        if (key_is_down_logical(vkey))
            key_paste_add_vkey_up(vkey);

        // We don't need to do anything to the emulated machine's SHIFT/CTRL
        // state now; no emulated keys (except maybe SHIFT/CTRL themselves) are
        // down so they have no effect at the moment.
        // set_logical_shift_ctrl_if_idle() will be called to update them
        // later.
    }
}

// Set the emulated machine's SHIFT/CTRL state to match the host's state,
// provided the host has no keys held down (if it does, KEY_CHAR events will
// cause any necessary SHIFT/CTRL state to be passed through to the emulated
// machine at the appropriate point) and there are no keyboard events waiting
// to be passed through to the emulated machine (if there are, the current
// state of the host's keyboard is irrelevant; this function will be called
// again to pick up the then-current state when the pending keyboard events
// have all been processed).

static void set_logical_shift_ctrl_if_idle()
{
    if ((kp_state == KP_IDLE) && (key_paste_vkey_down == 0))
        key_paste_add_combo(0xaa, hostshift, hostctrl);
}

void key_down(uint8_t bbckey)
{
    if (bbckey != 0xaa) {
        log_debug("keyboard: BBC key down %02x", bbckey);
        bbcmatrix[bbckey & 15][bbckey >> 4] = true;
        key_update();
    }
}

void key_down_event(const ALLEGRO_EVENT *event)
{
    int keycode = event->keyboard.keycode;
    log_debug("keyboard: key down event, keycode=%d:%s, modifiers=%04X", keycode, al_keycode_to_name(keycode), event->keyboard.modifiers);
    if (keycode == ALLEGRO_KEY_ALT || keycode == ALLEGRO_KEY_ALTGR)
        hostalt = true;
    else if (keycode == ALLEGRO_KEY_CAPSLOCK)
            key_down(keylookup[keycode]);
    else if (keycode == ALLEGRO_KEY_ESCAPE && key_paste_str)
        key_paste_stop();
    else {
        bool shiftctrl = false;
        if (keycode == ALLEGRO_KEY_LSHIFT || keycode == ALLEGRO_KEY_RSHIFT) {
            hostshift = true;
            shiftctrl = true;
        }
        else if (keycode == ALLEGRO_KEY_LCTRL || keycode == ALLEGRO_KEY_RCTRL) {
            hostctrl = true;
            shiftctrl = true;
        }
        if (shiftctrl) {
            if (key_mode != BKM_PHYSICAL)
                set_logical_shift_ctrl_if_idle();
            else
                key_down(keylookup[keycode]);
        }
    }
}

static int map_keypad_intern(int keycode, int unichar)
{
    if (keypad || key_mode != BKM_PHYSICAL) {
        if (keycode >= ALLEGRO_KEY_PAD_0 && keycode <= ALLEGRO_KEY_PAD_9 && (unichar < '0' || unichar > '9')) {
            int newcode = map_keypad[keycode-ALLEGRO_KEY_PAD_0];
            log_debug("keyboard: mapping keypad key %d:%s to %d:%s", keycode, al_keycode_to_name(keycode), newcode, al_keycode_to_name(newcode));
            return newcode;
        }
        else if (keycode == ALLEGRO_KEY_PAD_DELETE && unichar == '.') {
            int newcode = ALLEGRO_KEY_FULLSTOP;
            log_debug("keyboard: mapping keypad key %d:%s to %d:%s", keycode, al_keycode_to_name(keycode), newcode, al_keycode_to_name(newcode));
            return newcode;
        }
    }
    return keycode;
}

int key_map_keypad(const ALLEGRO_EVENT *event)
{
    return map_keypad_intern(event->keyboard.keycode, event->keyboard.unichar);
}

void key_char_event(const ALLEGRO_EVENT *event)
{
    int keycode = event->keyboard.keycode;
    int unichar = event->keyboard.unichar;
    log_debug("keyboard: key char event, keycode=%d:%s, unichar=%d", keycode, al_keycode_to_name(keycode), unichar);
    if ((!event->keyboard.repeat || unichar != last_unichar[keycode]) && keycode < ALLEGRO_KEY_MAX) {
        last_unichar[keycode] = unichar;
        keycode = map_keypad_intern(keycode, unichar);
        if (keycode == ALLEGRO_KEY_A && keyas && key_mode == BKM_PHYSICAL)
            keycode = ALLEGRO_KEY_CAPSLOCK;
        else if (keycode == ALLEGRO_KEY_S && keyas && key_mode == BKM_PHYSICAL)
            keycode = ALLEGRO_KEY_LCTRL;
        for (int act = 0; act < KEY_ACTION_MAX; act++) {
            log_debug("keyboard: checking key action %d:%s codes %d<>%d, alt %d<>%d", act, keyact_const[act].name, keycode, keyactions[act].keycode, hostalt, keyactions[act].altstate);
            if (keycode == keyactions[act].keycode && keyactions[act].altstate == hostalt) {
                keyact_const[act].downfunc();
                return;
            }
        }
        if (key_mode != BKM_PHYSICAL)
            set_key_logical(keycode, unichar, true);
        else
            key_down(keylookup[keycode]);
    }
}

void key_up(uint8_t bbckey)
{
    if (bbckey != 0xaa) {
        log_debug("keyboard: BBC key up %02x", bbckey);
        bbcmatrix[bbckey & 15][bbckey >> 4] = false;
        key_update();
    }
}

void key_up_event(const ALLEGRO_EVENT *event)
{
    int keycode = event->keyboard.keycode;
    log_debug("keyboard: key up event, keycode=%d:%s, modifiers=%04X", keycode, al_keycode_to_name(keycode), event->keyboard.modifiers);
    if (keycode < ALLEGRO_KEY_MAX) {
        if (keycode == ALLEGRO_KEY_ALT || keycode == ALLEGRO_KEY_ALTGR)
            hostalt = false;
        else if (keycode == ALLEGRO_KEY_CAPSLOCK)
            key_up(keylookup[keycode]);
        else {
            int unichar = last_unichar[keycode];
            bool shiftctrl = false;
            if (keycode == ALLEGRO_KEY_LSHIFT || keycode == ALLEGRO_KEY_RSHIFT) {
                hostshift = false;
                shiftctrl = true;
            }
            else if (keycode == ALLEGRO_KEY_LCTRL || keycode == ALLEGRO_KEY_RCTRL) {
                hostctrl = false;
                shiftctrl = true;
            }
            else if (keycode == ALLEGRO_KEY_A && keyas && key_mode == BKM_PHYSICAL)
                keycode = ALLEGRO_KEY_CAPSLOCK;
            else if (keycode == ALLEGRO_KEY_S && keyas && key_mode == BKM_PHYSICAL)
                keycode = ALLEGRO_KEY_LCTRL;
            if (shiftctrl && key_mode != BKM_PHYSICAL)
                set_logical_shift_ctrl_if_idle();
            keycode = map_keypad_intern(keycode, unichar);
            for (int act = 0; act < KEY_ACTION_MAX; act++) {
                log_debug("keyboard: checking key action %d:%s codes %d<>%d, alt %d<>%d", act, keyact_const[act].name, keycode, keyactions[act].keycode, hostalt, keyactions[act].altstate);
                if (keycode == keyactions[act].keycode && keyactions[act].altstate == hostalt) {
                    keyact_const[act].upfunc();
                    return;
                }
            }
            if (key_mode != BKM_PHYSICAL)
                set_key_logical(keycode, unichar, false);
            else
                key_up(keylookup[keycode]);
        }
    }
}

static int key_paste_count;

void key_paste_poll(void)
{
    if (OS01 && key_paste_count++ & 1)
        return;

    uint8_t vkey;
    //log_debug("keyboard: key_paste_poll kp_state=%d", kp_state);

    switch(kp_state) {
        case KP_IDLE:
            break;

        case KP_NEXT:
            if ((key_paste_ptr - key_paste_buf) < key_paste_buf_size) {
                vkey = *key_paste_ptr++;
                int col = 1;
                log_debug("keyboard: key_paste_poll next vkey=&%02x", vkey);
                switch (vkey) {
                    case VKEY_SHIFT_EVENT:
                    case VKEY_SHIFT_EVENT|1:
                        col = 0;
                    case VKEY_CTRL_EVENT:
                    case VKEY_CTRL_EVENT|1:
                        bbcmatrix[col][0] = vkey & 1;
                        key_update();
                        kp_state = KP_NEXT;
                        break;

                    case VKEY_DOWN:
                        kp_state = KP_DOWN;
                        break;

                    case VKEY_UP:
                        vkey = *key_paste_ptr++;
                        log_debug("keyboard: key_paste_poll up vkey=&%02x", vkey);
                        bbcmatrix[vkey & 15][vkey >> 4] = 0;
                        key_update();
                        kp_state = KP_NEXT;
                        break;

                    default:
                        break;
                }
            }
            else {
                key_paste_buf_size = 0;
                key_paste_ptr = 0;
                kp_state = KP_IDLE;

                if (key_paste_str)
                    key_paste_nextseg();
                else {
                    // Now we've finished, make the emulated machine's SHIFT/CTRL
                    // state agree with the host's state. This doesn't cause an
                    // infinite loop because this is a no-op if the state already
                    // matches.
                    set_logical_shift_ctrl_if_idle();
                }
            }
            break;

        case KP_DOWN:
            vkey = *key_paste_ptr++;
            log_debug("keyboard: key_paste_poll down vkey=&%02x", vkey);
            bbcmatrix[vkey & 15][vkey >> 4] = 1;
            key_update();
            kp_state = KP_DELAY;
            break;

        case KP_DELAY:
            kp_state = KP_NEXT;
            break;
    }
}

void key_scan(int row, int col) {
    keyrow = row;
    keycol = col;
    key_update();
}

bool key_is_down(void) {
    if (keyrow == 0) {
        if (keycol == 0 && autoboot > 0) {
            log_debug("keyboard: return shift is down (autoboot=%d), pc=%04X", autoboot, pc);
            return true;
        }
        else if (keycol >= 2 && keycol <= 9)
            return kbdips & (1 << (9 - keycol));
    }
    return bbcmatrix[keycol][keyrow];
}

bool key_any_down(void)
{
    if (key_mode != BKM_PHYSICAL) {
        for (int c = 0; c < 16; c++)
            for (int r = 1; r < 16; r++)
                if (bbcmatrix[c][r])
                    return true;
        return false;
    }
    else
        return key_paste_vkey_down != 0;
}

bool key_code_down(int code)
{
    if (code < ALLEGRO_KEY_MAX) {
        code = key_allegro2bbc[code];
        assert((code != 0x00) && (code != 0x01)); // not SHIFT or CTRL
        if (key_mode != BKM_PHYSICAL)
            return bbcmatrix[code & 0x0f][code >> 4];
        else
            return (key_paste_vkey_down != 0) && (key_paste_vkey_down == code);
    }
    return false;
}
