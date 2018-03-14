/*B-em v2.2 by Tom Walker
  Windows key redefinition*/

#ifdef WIN32
#include <windows.h>
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
   /* 0x00 */ 0, ALLEGRO_KEY_ESCAPE, ALLEGRO_KEY_1, ALLEGRO_KEY_2,
   /* 0x04 */ ALLEGRO_KEY_3, ALLEGRO_KEY_4, ALLEGRO_KEY_5, ALLEGRO_KEY_6,
   /* 0x08 */ ALLEGRO_KEY_7, ALLEGRO_KEY_8, ALLEGRO_KEY_9, ALLEGRO_KEY_0,
   /* 0x0C */ ALLEGRO_KEY_MINUS, ALLEGRO_KEY_EQUALS, ALLEGRO_KEY_BACKSPACE, ALLEGRO_KEY_TAB,
   /* 0x10 */ ALLEGRO_KEY_Q, ALLEGRO_KEY_W, ALLEGRO_KEY_E, ALLEGRO_KEY_R,
   /* 0x14 */ ALLEGRO_KEY_T, ALLEGRO_KEY_Y, ALLEGRO_KEY_U, ALLEGRO_KEY_I,
   /* 0x18 */ ALLEGRO_KEY_O, ALLEGRO_KEY_P, ALLEGRO_KEY_OPENBRACE, ALLEGRO_KEY_CLOSEBRACE,
   /* 0x1C */ ALLEGRO_KEY_ENTER, ALLEGRO_KEY_LCTRL, ALLEGRO_KEY_A, ALLEGRO_KEY_S,
   /* 0x20 */ ALLEGRO_KEY_D, ALLEGRO_KEY_F, ALLEGRO_KEY_G, ALLEGRO_KEY_H,
   /* 0x24 */ ALLEGRO_KEY_J, ALLEGRO_KEY_K, ALLEGRO_KEY_L, ALLEGRO_KEY_SEMICOLON,
   /* 0x28 */ ALLEGRO_KEY_QUOTE, ALLEGRO_KEY_TILDE, ALLEGRO_KEY_LSHIFT, ALLEGRO_KEY_BACKSLASH,
   /* 0x2C */ ALLEGRO_KEY_Z, ALLEGRO_KEY_X, ALLEGRO_KEY_C, ALLEGRO_KEY_V,
   /* 0x30 */ ALLEGRO_KEY_B, ALLEGRO_KEY_N, ALLEGRO_KEY_M, ALLEGRO_KEY_COMMA,
   /* 0x34 */ ALLEGRO_KEY_FULLSTOP, ALLEGRO_KEY_SLASH, ALLEGRO_KEY_RSHIFT, ALLEGRO_KEY_PAD_ASTERISK,
   /* 0x38 */ ALLEGRO_KEY_ALT, ALLEGRO_KEY_SPACE, ALLEGRO_KEY_CAPSLOCK, ALLEGRO_KEY_F1,
   /* 0x3C */ ALLEGRO_KEY_F2, ALLEGRO_KEY_F3, ALLEGRO_KEY_F4, ALLEGRO_KEY_F5,
   /* 0x40 */ ALLEGRO_KEY_F6, ALLEGRO_KEY_F7, ALLEGRO_KEY_F8, ALLEGRO_KEY_F9,
   /* 0x44 */ ALLEGRO_KEY_F10, ALLEGRO_KEY_NUMLOCK, ALLEGRO_KEY_SCROLLLOCK, ALLEGRO_KEY_PAD_7,
   /* 0x48 */ ALLEGRO_KEY_PAD_8, ALLEGRO_KEY_PAD_9, ALLEGRO_KEY_PAD_MINUS, ALLEGRO_KEY_PAD_4,
   /* 0x4C */ ALLEGRO_KEY_PAD_5, ALLEGRO_KEY_PAD_6, ALLEGRO_KEY_PAD_PLUS, ALLEGRO_KEY_PAD_1,
   /* 0x50 */ ALLEGRO_KEY_PAD_4, ALLEGRO_KEY_PAD_3, ALLEGRO_KEY_PAD_0, ALLEGRO_KEY_PAD_DELETE,
   /* 0x54 */ ALLEGRO_KEY_PRINTSCREEN, 0, ALLEGRO_KEY_BACKSLASH2, ALLEGRO_KEY_F11,
   /* 0x58 */ ALLEGRO_KEY_F12, 0, 0, ALLEGRO_KEY_LWIN,
   /* 0x5C */ ALLEGRO_KEY_RWIN, ALLEGRO_KEY_MENU, 0, 0,
   /* 0x60 */ 0, 0, 0, 0,
   /* 0x64 */ 0, 0, 0, 0,
   /* 0x68 */ 0, 0, 0, 0,
   /* 0x6C */ 0, 0, 0, 0,
   /* 0x70 */ ALLEGRO_KEY_KANA, 0, 0, ALLEGRO_KEY_ABNT_C1,
   /* 0x74 */ 0, 0, 0, 0,
   /* 0x78 */ 0, ALLEGRO_KEY_CONVERT, 0, ALLEGRO_KEY_NOCONVERT,
   /* 0x7C */ 0, ALLEGRO_KEY_YEN, 0, 0,
   /* 0x80 */ 0, 0, 0, 0,
   /* 0x84 */ 0, 0, 0, 0,
   /* 0x88 */ 0, 0, 0, 0,
   /* 0x8C */ 0, 0, 0, 0,
   /* 0x90 */ 0, ALLEGRO_KEY_AT, ALLEGRO_KEY_COLON2, 0,
   /* 0x94 */ ALLEGRO_KEY_KANJI, 0, 0, 0,
   /* 0x98 */ 0, 0, 0, 0,
   /* 0x9C */ ALLEGRO_KEY_PAD_ENTER, ALLEGRO_KEY_RCTRL, 0, 0,
   /* 0xA0 */ 0, 0, 0, 0,
   /* 0xA4 */ 0, 0, 0, 0,
   /* 0xA8 */ 0, 0, 0, 0,
   /* 0xAC */ 0, 0, 0, 0,
   /* 0xB0 */ 0, 0, 0, 0,
   /* 0xB4 */ 0, ALLEGRO_KEY_PAD_SLASH, 0, ALLEGRO_KEY_PRINTSCREEN,
   /* 0xB8 */ ALLEGRO_KEY_ALTGR, 0, 0, 0,
   /* 0xBC */ 0, 0, 0, 0,
   /* 0xC0 */ 0, 0, 0, 0,
   /* 0xC4 */ 0, ALLEGRO_KEY_PAUSE, 0, ALLEGRO_KEY_HOME,
   /* 0xC8 */ ALLEGRO_KEY_UP, ALLEGRO_KEY_PGUP, 0, ALLEGRO_KEY_LEFT,
   /* 0xCC */ 0, ALLEGRO_KEY_RIGHT, 0, ALLEGRO_KEY_END,
   /* 0xD0 */ ALLEGRO_KEY_DOWN, ALLEGRO_KEY_PGDN, ALLEGRO_KEY_INSERT, ALLEGRO_KEY_DELETE,
   /* 0xD4 */ 0, 0, 0, 0,
   /* 0xD8 */ 0, 0, 0, ALLEGRO_KEY_LWIN,
   /* 0xDC */ ALLEGRO_KEY_RWIN, ALLEGRO_KEY_MENU, 0, 0,
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
