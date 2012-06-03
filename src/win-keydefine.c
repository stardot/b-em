/*B-em v2.2 by Tom Walker
  Windows key redefinition*/

#ifdef WIN32
#include <allegro.h>
#include <winalleg.h>
#include "resources.h"

#include "b-em.h"
#include "keyboard.h"
#include "model.h"
#include "win.h"


char szClasskey[] = "B-emKeyWnd";

HWND khwnd=NULL;
LRESULT CALLBACK KeyWindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
int keywind=0,windcurkey;

void getkey(HWND parent)
{
        MSG messages;
        WNDCLASSEX wincl;        /* Data structure for the windowclass */
        if (!GetClassInfoEx(hinstance,szClasskey,&wincl))
        {
                /* The Window structure */
                wincl.hInstance = hinstance;
                wincl.lpszClassName = szClasskey;
                wincl.lpfnWndProc = KeyWindowProcedure;      /* This function is called by windows */
                wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
                wincl.cbSize = sizeof (WNDCLASSEX);

                /* Use default icon and mouse-pointer */
                wincl.hIcon = LoadIcon(hinstance, "allegro_icon");
                wincl.hIconSm = LoadIcon(hinstance, "allegro_icon");
                wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
                wincl.lpszMenuName = NULL;                 /* No menu */
                wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
                wincl.cbWndExtra = 0;                      /* structure or the window instance */
                /* Use Windows's default color as the background of the window */
                wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

                /* Register the window class, and if it fails quit the program */
                if (!RegisterClassEx (&wincl))
                   return;
        }

        if (khwnd) SendMessage(khwnd, WM_CLOSE, 0, 0);

        /* The class is registered, let's create the window*/
        khwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           szClasskey,         /* Classname */
           "Press key to define", /* Title Text */
           WS_OVERLAPPEDWINDOW&~WS_SIZEBOX, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           240,
           80,
           parent,        /* The window is a child-window to desktop */
           NULL,
           hinstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );
        ShowWindow(khwnd, SW_SHOW);
        keywind = 0;
        while (!keywind)
        {
                if (PeekMessage(&messages, NULL, 0, 0, PM_REMOVE))
                {
                        if (messages.message == WM_QUIT) keywind = 1;
                        TranslateMessage(&messages);
                        DispatchMessage(&messages);
                }
                else
                   Sleep(10);
        }
        khwnd = NULL;
}

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
HWND hwndCtrl;
unsigned char hw_to_mycode[256] = {
   /* 0x00 */ 0, KEY_ESC, KEY_1, KEY_2,
   /* 0x04 */ KEY_3, KEY_4, KEY_5, KEY_6,
   /* 0x08 */ KEY_7, KEY_8, KEY_9, KEY_0,
   /* 0x0C */ KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB,
   /* 0x10 */ KEY_Q, KEY_W, KEY_E, KEY_R,
   /* 0x14 */ KEY_T, KEY_Y, KEY_U, KEY_I,
   /* 0x18 */ KEY_O, KEY_P, KEY_OPENBRACE, KEY_CLOSEBRACE,
   /* 0x1C */ KEY_ENTER, KEY_LCONTROL, KEY_A, KEY_S,
   /* 0x20 */ KEY_D, KEY_F, KEY_G, KEY_H,
   /* 0x24 */ KEY_J, KEY_K, KEY_L, KEY_SEMICOLON,
   /* 0x28 */ KEY_QUOTE, KEY_TILDE, KEY_LSHIFT, KEY_BACKSLASH,
   /* 0x2C */ KEY_Z, KEY_X, KEY_C, KEY_V,
   /* 0x30 */ KEY_B, KEY_N, KEY_M, KEY_COMMA,
   /* 0x34 */ KEY_STOP, KEY_SLASH, KEY_RSHIFT, KEY_ASTERISK,
   /* 0x38 */ KEY_ALT, KEY_SPACE, KEY_CAPSLOCK, KEY_F1,
   /* 0x3C */ KEY_F2, KEY_F3, KEY_F4, KEY_F5,
   /* 0x40 */ KEY_F6, KEY_F7, KEY_F8, KEY_F9,
   /* 0x44 */ KEY_F10, KEY_NUMLOCK, KEY_SCRLOCK, KEY_7_PAD,
   /* 0x48 */ KEY_8_PAD, KEY_9_PAD, KEY_MINUS_PAD, KEY_4_PAD,
   /* 0x4C */ KEY_5_PAD, KEY_6_PAD, KEY_PLUS_PAD, KEY_1_PAD,
   /* 0x50 */ KEY_2_PAD, KEY_3_PAD, KEY_0_PAD, KEY_DEL_PAD,
   /* 0x54 */ KEY_PRTSCR, 0, KEY_BACKSLASH2, KEY_F11,
   /* 0x58 */ KEY_F12, 0, 0, KEY_LWIN,
   /* 0x5C */ KEY_RWIN, KEY_MENU, 0, 0,
   /* 0x60 */ 0, 0, 0, 0,
   /* 0x64 */ 0, 0, 0, 0,
   /* 0x68 */ 0, 0, 0, 0,
   /* 0x6C */ 0, 0, 0, 0,
   /* 0x70 */ KEY_KANA, 0, 0, KEY_ABNT_C1,
   /* 0x74 */ 0, 0, 0, 0,
   /* 0x78 */ 0, KEY_CONVERT, 0, KEY_NOCONVERT,
   /* 0x7C */ 0, KEY_YEN, 0, 0,
   /* 0x80 */ 0, 0, 0, 0,
   /* 0x84 */ 0, 0, 0, 0,
   /* 0x88 */ 0, 0, 0, 0,
   /* 0x8C */ 0, 0, 0, 0,
   /* 0x90 */ 0, KEY_AT, KEY_COLON2, 0,
   /* 0x94 */ KEY_KANJI, 0, 0, 0,
   /* 0x98 */ 0, 0, 0, 0,
   /* 0x9C */ KEY_ENTER_PAD, KEY_RCONTROL, 0, 0,
   /* 0xA0 */ 0, 0, 0, 0,
   /* 0xA4 */ 0, 0, 0, 0,
   /* 0xA8 */ 0, 0, 0, 0,
   /* 0xAC */ 0, 0, 0, 0,
   /* 0xB0 */ 0, 0, 0, 0,
   /* 0xB4 */ 0, KEY_SLASH_PAD, 0, KEY_PRTSCR,
   /* 0xB8 */ KEY_ALTGR, 0, 0, 0,
   /* 0xBC */ 0, 0, 0, 0,
   /* 0xC0 */ 0, 0, 0, 0,
   /* 0xC4 */ 0, KEY_PAUSE, 0, KEY_HOME,
   /* 0xC8 */ KEY_UP, KEY_PGUP, 0, KEY_LEFT,
   /* 0xCC */ 0, KEY_RIGHT, 0, KEY_END,
   /* 0xD0 */ KEY_DOWN, KEY_PGDN, KEY_INSERT, KEY_DEL,
   /* 0xD4 */ 0, 0, 0, 0,
   /* 0xD8 */ 0, 0, 0, KEY_LWIN,
   /* 0xDC */ KEY_RWIN, KEY_MENU, 0, 0,
   /* 0xE0 */ 0, 0, 0, 0,
   /* 0xE4 */ 0, 0, 0, 0,
   /* 0xE8 */ 0, 0, 0, 0,
   /* 0xEC */ 0, 0, 0, 0,
   /* 0xF0 */ 0, 0, 0, 0,
   /* 0xF4 */ 0, 0, 0, 0,
   /* 0xF8 */ 0, 0, 0, 0,
   /* 0xFC */ 0, 0, 0, 0
};
LRESULT CALLBACK KeyWindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        char s[1024], s2[1024], s3[64];
        int c;
        switch (message)
        {
                case WM_CREATE:
                sprintf(s, "Redefining %s", key_names[windcurkey]);
                hwndCtrl = CreateWindow( "STATIC", s, WS_CHILD | SS_SIMPLE | WS_VISIBLE, 4, 4, 240-10, 16, hwnd, NULL, hinstance, NULL );
                hwndCtrl = CreateWindow( "STATIC", "Assigned to PC key(s): ", WS_CHILD | SS_SIMPLE | WS_VISIBLE, 4, 20, 240-10, 16, hwnd, NULL, hinstance, NULL );
                s[0] = 0;
                for (c = 0; c < 128; c++)
                {
                        if (keylookup[c] == windcurkey)
                        {
                                if (s[0]) sprintf(s3, ", %s", key_names[c]);
                                else      sprintf(s3, "%s",   key_names[c]);
                                sprintf(s2, "%s%s", s, s3);
                                strcpy(s, s2);
                        }
                }
                hwndCtrl = CreateWindow( "STATIC", s, WS_CHILD | SS_SIMPLE | WS_VISIBLE, 4, 36, 240-10, 16, hwnd, NULL, hinstance, NULL );
                break;

                case WM_SYSKEYUP:
                case WM_KEYUP:
                if (LOWORD(wParam) != 255)
                {
                        c = MapVirtualKey(LOWORD(wParam), 0);
                        c = hw_to_mycode[c];
                        keylookup[c] = windcurkey;
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        break;
                }
                break;

                case WM_DESTROY:
                PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                break;

                default:
                return DefWindowProc (hwnd, message, wParam, lParam);
        }
        return 0;
}

BOOL CALLBACK redefinedlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        switch (message)
        {
                case WM_INITDIALOG:
                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        case IDCANCEL:
                        EndDialog(hdlg, 0);
                        return TRUE;
                }
                if (LOWORD(wParam) >= Button1 && LOWORD(wParam) <= (Button1 + 128) && !khwnd)
                {
                        windcurkey = LOWORD(wParam) - Button1;
                        getkey(hdlg);
//                        DialogBox(hinstance,TEXT("KeyDlg"),hdlg,keydlgproc);
                }
                break;
        }
        return FALSE;
}

void redefinekeys()
{
        if (MASTER) DialogBox(hinstance, TEXT("RedefineM"), ghwnd, redefinedlgproc);
        else        DialogBox(hinstance, TEXT("Redefine"),  ghwnd, redefinedlgproc);
}
#endif
