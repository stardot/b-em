/*B-em v2.2 by Tom Walker
  Windows main*/

#ifdef WIN32
#include <allegro.h>
#include <winalleg.h>
#include <process.h>
#include "resources.h"

#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "ide.h"
#include "keyboard.h"
#include "main.h"
#include "model.h"
#include "mouse.h"
#include "savestate.h"
#include "sid_b-em.h"
#include "sound.h"
#include "sn76489.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video_render.h"
#include "win.h"

char tempname[1000];

RECT oldclip, newclip;
int mousecapture = 0;
int videoresize  = 0;

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);

/*  Make the class name into a global variable  */
char szClassName[] = "B-emMainWnd";

HWND ghwnd;
int winsizex = 640, winsizey = 480;

void updatewindowsize(int x, int y)
{
        RECT r;
        if (x < 128) x = 128;
        if (y < 64)  y = 64;
        if (x == winsizex && y == winsizey) return;
	if (!videoresize) { x--; y--; }
        winsizex = x;
        winsizey = y;
        GetWindowRect(ghwnd, &r);
        MoveWindow(ghwnd, r.left, r.top,
                     x + (GetSystemMetrics(SM_CXFIXEDFRAME) * 2),
                     y + (GetSystemMetrics(SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 1,
                     TRUE);
}

static int quited = 0;
void setquit()
{
        quited = 1;
}

HINSTANCE hinstance;

static char **argv;
static int argc;
static char *argbuf;

static void processcommandline()
{
        char *cmdline;
        int argc_max;
        int i, q;

        /* can't use parameter because it doesn't include the executable name */
        cmdline = GetCommandLine();
        i = strlen(cmdline) + 1;
        argbuf = malloc(i);
        memcpy(argbuf, cmdline, i);

        argc = 0;
        argc_max = 64;
        argv = malloc(sizeof(char *) * argc_max);
        if (!argv) {
           free(argbuf);
           return;
        }

        i = 0;

        /* parse commandline into argc/argv format */
        while (argbuf[i]) {
           while ((argbuf[i]) && (uisspace(argbuf[i])))
	i++;

      if (argbuf[i]) {
	 if ((argbuf[i] == '\'') || (argbuf[i] == '"')) {
	    q = argbuf[i++];
	    if (!argbuf[i])
	       break;
	 }
	 else
	    q = 0;

	 argv[argc++] = &argbuf[i];

         if (argc >= argc_max) {
            argc_max += 64;
            argv = realloc(argv, sizeof(char *) * argc_max);
            if (!argv) {
               free(argbuf);
               return;
            }
         }

	 while ((argbuf[i]) && ((q) ? (argbuf[i] != q) : (!uisspace(argbuf[i]))))
	    i++;

         if (argbuf[i]) {
            argbuf[i] = 0;
            i++;
         }
         bem_debugf("Arg %i - %s\n",argc-1,argv[argc-1]);
      }
   }

   argv[argc] = NULL;
//   free(argbuf);
}

static void initmenu()
{
        char t[512];
        HMENU hmenu = GetMenu(ghwnd);
        
        CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0])     ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_DISC_WPROT_1, (writeprot[1])     ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_DISC_WPROT_D, (defaultwriteprot) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_DISC_VDFS_ENABLE, (vdfs_enabled) ? MF_CHECKED : MF_UNCHECKED);

        CheckMenuItem(hmenu, IDM_TUBE_6502 + selecttube, MF_CHECKED);
        CheckMenuItem(hmenu, IDM_MODEL_0   + curmodel,   MF_CHECKED);

        if (vid_scanlines)      CheckMenuItem(hmenu, IDM_VIDEO_SCANLINES,  MF_CHECKED);
        else if (vid_interlace) CheckMenuItem(hmenu, IDM_VIDEO_INTERLACED, MF_CHECKED);
        else if (vid_linedbl)   CheckMenuItem(hmenu, IDM_VIDEO_SLINEDBL,   MF_CHECKED);
        else if (vid_pal)       CheckMenuItem(hmenu, IDM_VIDEO_PAL,        MF_CHECKED);
        else                    CheckMenuItem(hmenu, IDM_VIDEO_LINEDBL,    MF_CHECKED);

        if (sound_internal) CheckMenuItem(hmenu, IDM_SOUND_INTERNAL, MF_CHECKED);
        if (sound_beebsid)  CheckMenuItem(hmenu, IDM_SOUND_BEEBSID,  MF_CHECKED);
        if (sound_music5000) CheckMenuItem(hmenu, IDM_SOUND_MUSIC5000, MF_CHECKED);
        if (sound_ddnoise)  CheckMenuItem(hmenu, IDM_SOUND_DDNOISE,  MF_CHECKED);
        if (sound_tape)     CheckMenuItem(hmenu, IDM_SOUND_TAPE,     MF_CHECKED);
        
        CheckMenuItem(hmenu, IDM_SOUND_FILTER, (sound_filter) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_WAVE_SQUARE + curwave, MF_CHECKED);

        CheckMenuItem(hmenu, IDM_SID_TYPE   + cursid,    MF_CHECKED);
        CheckMenuItem(hmenu, IDM_SID_INTERP + sidmethod, MF_CHECKED);

        CheckMenuItem(hmenu, IDM_DDT_525 + ddnoise_type,     MF_CHECKED);
        CheckMenuItem(hmenu, (IDM_DDV_33 + ddnoise_vol) - 1, MF_CHECKED);
        
        CheckMenuItem(hmenu, IDM_TAPES_NORMAL + fasttape, MF_CHECKED);
        
        CheckMenuItem(hmenu, IDM_TUBES_4 + (tube_6502_speed - 1), MF_CHECKED);
        
        CheckMenuItem(hmenu, IDM_VIDEO_NOBORDERS + vid_fullborders, MF_CHECKED);
        
        append_filename(t, exedir, "roms\\tube\\ReCo6502ROM_816", 511);
        if (!file_exists(t, FA_ALL, NULL)) EnableMenuItem(hmenu, IDM_TUBE_65816, MF_GRAYED);
        
        if (keyas)     CheckMenuItem(hmenu, IDM_KEY_AS, MF_CHECKED);
        
        if (mouse_amx) CheckMenuItem(hmenu, IDM_MOUSE_AMX, MF_CHECKED);

        CheckMenuItem(hmenu, IDM_IDE_ENABLE, ide_enable ? MF_CHECKED : MF_UNCHECKED);
        
        CheckMenuItem(hmenu, IDM_VIDEO_RESIZE, (videoresize) ? MF_CHECKED : MF_UNCHECKED);

        CheckMenuItem(hmenu, IDM_SPD_100, MF_CHECKED);
}

static HANDLE mainthread;

static int bempause = 0, bemwaiting = 0;
static int doautoboot = 0;

void _mainthread(PVOID pvoid)
{
        HMENU hmenu;
        main_init(argc, argv);

//Carlo Concari: show correct disc protected status at startup
       	hmenu=GetMenu(ghwnd);
        CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0]) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_DISC_WPROT_1, (writeprot[1]) ? MF_CHECKED : MF_UNCHECKED);

        while (1)
        {
                if (bempause)
                {
                        bemwaiting = 1;
                        Sleep(100);
                }
                else
                {
                        bemwaiting = 0;
                        main_run();
                }
                if (doautoboot)
                {
                        main_reset();
                        disc_close(0);
                        disc_load(0, discfns[0]);
                        if (defaultwriteprot) writeprot[0] = 1;
                	hmenu = GetMenu(ghwnd);
                        CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0]) ? MF_CHECKED : MF_UNCHECKED);
                        autoboot = 150;
                        doautoboot = 0;
                }
        }
}

void waitforready()
{
        bempause = 1;
        while (!bemwaiting) Sleep(100);
}

void resumeready()
{
        bempause = 0;
}

CRITICAL_SECTION cs;

void startblit()
{
//        bem_debug("startblit\n");
        EnterCriticalSection(&cs);
}

void endblit()
{
//        bem_debug("endblit\n");
        LeaveCriticalSection(&cs);
}

void setejecttext(int drive, char *fn)
{
	MENUITEMINFO mi;
	HMENU hmenu;
	char s[128];
	if (fn[0]) sprintf(s,"Eject drive :%i/%i - %s", drive, drive + 2, get_filename(fn));
	else       sprintf(s,"Eject drive :%i/%i", drive, drive + 2);
	memset(&mi, 0, sizeof(MENUITEMINFO));
	mi.cbSize = sizeof(MENUITEMINFO);
	mi.fMask = MIIM_STRING;
	mi.fType = MFT_STRING;
	mi.dwTypeData = s;
	hmenu = GetMenu(ghwnd);
	SetMenuItemInfo(hmenu, IDM_DISC_EJECT_0 + drive, 0, &mi);
	CheckMenuItem(hmenu, IDM_DISC_WPROT_0 + drive, (writeprot[drive]) ? MF_CHECKED : MF_UNCHECKED);
}

void updatewindowtitle()
{
        if (curtube == 3 || mouse_amx)
        {
                if (!mousecapture) set_window_title(VERSION_STR " - click to capture mouse");
                else               set_window_title(VERSION_STR " - CTRL-END to release mouse");
        }
        else
           set_window_title(VERSION_STR);
}

void bem_error(char *s)
{
        MessageBox(ghwnd, s, "B-em error", MB_OK | MB_ICONEXCLAMATION);
}

int WINAPI WinMain (HINSTANCE hThisInstance,
                    HINSTANCE hPrevInstance,
                    LPSTR lpszArgument,
                    int nFunsterStil)

{
        HWND hwnd;               /* This is the handle for our window */
        MSG messages;            /* Here messages to the application are saved */
        WNDCLASSEX wincl;        /* Data structure for the windowclass */
        int c;
        int oldf = 0;
        char *p;
        
        for (c = 0; c < 128; c++) keylookup[c] = c;
        
        processcommandline();

        hinstance = hThisInstance;
        /* The Window structure */
        wincl.hInstance = hThisInstance;
        wincl.lpszClassName = szClassName;
        wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
        wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
        wincl.cbSize = sizeof (WNDCLASSEX);

        /* Use default icon and mouse-pointer */
        wincl.hIcon = LoadIcon(hThisInstance, "allegro_icon");
        wincl.hIconSm = LoadIcon(hThisInstance, "allegro_icon");
        wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
        wincl.lpszMenuName = NULL;                 /* No menu */
        wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
        wincl.cbWndExtra = 0;                      /* structure or the window instance */
        /* Use Windows's default color as the background of the window */
        wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

        /* Register the window class, and if it fails quit the program */
        if (!RegisterClassEx (&wincl))
           return 0;

        /* The class is registered, let's create the program*/
        hwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           szClassName,         /* Classname */
           VERSION_STR,         /* Title Text */
           WS_OVERLAPPEDWINDOW/*&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX*/, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           640 + (GetSystemMetrics(SM_CXFIXEDFRAME) * 2),                 /* The programs width */
           480 + (GetSystemMetrics(SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 1,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           LoadMenu(hThisInstance, TEXT("MainMenu")),                /* No menu */
           hThisInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );

        ghwnd = hwnd;
        
        win_set_window(hwnd);

        allegro_init();

        get_executable_name(exedir, 511);
        p = get_filename(exedir);
        p[0] = 0;

        config_load();

        InitializeCriticalSection(&cs);
        
        /* Make the window visible on the screen */
        ShowWindow (hwnd, nFunsterStil);
        
        initmenu();
        
        mainthread = (HANDLE)_beginthread(_mainthread, 0, NULL);
        
        updatewindowtitle();
        

        /* Run the message loop. It will run until GetMessage() returns 0 */
        while (!quited)
        {
//                runbbc();
                if (PeekMessage(&messages, NULL, 0, 0, PM_REMOVE))
                {
                        if (messages.message == WM_QUIT)
                        {
                                quited=1;
                        }
                        TranslateMessage(&messages);
                        DispatchMessage(&messages);
                }
                else
                   Sleep(10);
                if ((key[KEY_LCONTROL] || key[KEY_RCONTROL]) && key[KEY_END] && mousecapture)
                {
                        ClipCursor(&oldclip);
                        mousecapture = 0;
                        updatewindowtitle();
                }
                if (key[KEY_ALT] && key[KEY_ENTER] && fullscreen && !oldf)
                {
                        EnterCriticalSection(&cs);
                        fullscreen = 0;
                        video_leavefullscreen();
                        LeaveCriticalSection(&cs);
                }
                else if (key[KEY_ALT] && key[KEY_ENTER] && !fullscreen && !oldf)
                {
                        EnterCriticalSection(&cs);
                        fullscreen = 1;
                        video_enterfullscreen();
                        LeaveCriticalSection(&cs);
                }
                oldf = key[KEY_ALT] && key[KEY_ENTER];
        }
        
        EnterCriticalSection(&cs);
        TerminateThread(mainthread, 0);
        main_close();
        DeleteCriticalSection(&cs);
        
        return messages.wParam;
}

static char openfilestring[260];
static int getfile(HWND hwnd, char *f, char *fn)
{
        OPENFILENAME ofn;       // common dialog box structure
        EnterCriticalSection(&cs);

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = openfilestring;
        //
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
        // use the contents of szFile to initialize itself.
        //
//        ofn.lpstrFile[0] = '\0';
        strcpy(ofn.lpstrFile, fn);
        ofn.nMaxFile = sizeof(openfilestring);
        ofn.lpstrFilter = f;//"All\0*.*\0Text\0*.TXT\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        // Display the Open dialog box.

        if (GetOpenFileName(&ofn))
        {
                LeaveCriticalSection(&cs);
                strcpy(fn, openfilestring);
                return 0;
        }
        LeaveCriticalSection(&cs);
        return 1;
}
static int getsfile(HWND hwnd, char *f, char *fn, char *de)
{
        OPENFILENAME ofn;       // common dialog box structure
        EnterCriticalSection(&cs);

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = openfilestring;
        //
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
        // use the contents of szFile to initialize itself.
        //
//        ofn.lpstrFile[0] = '\0';
        strcpy(ofn.lpstrFile,fn);
        ofn.nMaxFile = sizeof(openfilestring);
        ofn.lpstrFilter = f;//"All\0*.*\0Text\0*.TXT\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        ofn.lpstrDefExt = de;

        // Display the Open dialog box.

        if (GetSaveFileName(&ofn))
        {
                LeaveCriticalSection(&cs);
                strcpy(fn, openfilestring);
                return 0;
        }
        LeaveCriticalSection(&cs);
        return 1;
}

extern unsigned char hw_to_mycode[256];

int timerspeeds[] = {5, 12, 25, 38, 50, 75, 100, 150, 200, 250};
int frameskips[]  = {0, 0,  0,  0,  0,  0,  1,   2,   3,   4};
int emuspeed = 4;

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HMENU hmenu;
        RECT rect;
        int c;
	LRESULT r;
        
        switch (message)
        {
                case WM_COMMAND:
                hmenu = GetMenu(hwnd);
                switch (LOWORD(wParam))
                {
                        case IDM_FILE_RESET:
                        EnterCriticalSection(&cs);
                        main_reset();
                        LeaveCriticalSection(&cs);
                        break;

                        case IDM_FILE_LSTATE:
                        EnterCriticalSection(&cs);
                        if (!getfile(hwnd, "Save State (*.SNP)\0*.SNP\0All files (*.*)\0*.*\0\0", savestate_name))
                        {
                                CheckMenuItem(hmenu, IDM_TUBE_6502 + selecttube, MF_UNCHECKED);
                                CheckMenuItem(hmenu, IDM_MODEL_0 + curmodel, MF_UNCHECKED);
                                savestate_load();
                                CheckMenuItem(hmenu, IDM_TUBE_6502 + selecttube, MF_CHECKED);
                                CheckMenuItem(hmenu, IDM_MODEL_0 + curmodel, MF_CHECKED);
                        }
                        main_cleardrawit();
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_FILE_SSTATE:
                        EnterCriticalSection(&cs);
                        if (curtube != -1)
                        {
                                bem_error("Second processor save states not supported yet.");
                        }
                        else
                        {
                                if (!getsfile(hwnd, "Save State (*.SNP)\0*.SNP\0All files (*.*)\0*.*\0\0", savestate_name, "SNP"))
                                   savestate_save();
                        }
                        main_cleardrawit();
                        LeaveCriticalSection(&cs);
                        break;

                        case IDM_FILE_EXIT:
                        PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                        break;


                        case IDM_DISC_AUTOBOOT:
                        if (!getfile(hwnd, "Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0", discfns[0]))
                        {
                                doautoboot = 1;
                        }
                        break;
                        case IDM_DISC_LOAD_0:
                        if (!getfile(hwnd, "Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0", discfns[0]))
                        {
                                disc_close(0);
                                disc_load(0, discfns[0]);
                                if (defaultwriteprot) writeprot[0] = 1;
                                CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0]) ? MF_CHECKED : MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_LOAD_1:
                        if (!getfile(hwnd, "Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0", discfns[1]))
                        {
                                disc_close(1);
                                disc_load(1, discfns[1]);
                                if (defaultwriteprot) writeprot[1] = 1;
                                CheckMenuItem(hmenu, IDM_DISC_WPROT_1, (writeprot[1]) ? MF_CHECKED : MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_EJECT_0:
                        disc_close(0);
                        discfns[0][0] = 0;
                        setejecttext(0, "");
                        break;
                        case IDM_DISC_EJECT_1:
                        disc_close(1);
                        discfns[1][0] = 0;
                        setejecttext(1, "");
                        break;
                        case IDM_DISC_NEW_0:
                        if (!getsfile(hwnd, "Disc image (*.SSD;*.DSD;*.ADF;*.ADL)\0*.SSD;*.DSD;*.ADF;*.ADL\0All files (*.*)\0*.*\0", discfns[0], "SSD"))
                        {
                                disc_close(0);
                                disc_new(0, discfns[0]);
                                if (defaultwriteprot) writeprot[0] = 1;
                                CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0]) ? MF_CHECKED : MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_NEW_1:
                        if (!getsfile(hwnd, "Disc image (*.SSD;*.DSD;*.ADF;*.ADL)\0*.SSD;*.DSD;*.ADF;*.ADL\0All files (*.*)\0*.*\0", discfns[1], "SSD"))
                        {
                                disc_close(1);
                                disc_new(1, discfns[1]);
                                if (defaultwriteprot) writeprot[1] = 1;
                                CheckMenuItem(hmenu, IDM_DISC_WPROT_1, (writeprot[1]) ? MF_CHECKED : MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_WPROT_0:
                        writeprot[0] = !writeprot[0];
                        if (fwriteprot[0]) writeprot[0] = 1;
                        CheckMenuItem(hmenu, IDM_DISC_WPROT_0, (writeprot[0]) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_DISC_WPROT_1:
                        writeprot[1] = !writeprot[1];
                        if (fwriteprot[1]) writeprot[1] = 1;
                        CheckMenuItem(hmenu, IDM_DISC_WPROT_1, (writeprot[1]) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_DISC_WPROT_D:
                        defaultwriteprot = !defaultwriteprot;
                        CheckMenuItem(hmenu, IDM_DISC_WPROT_D, (defaultwriteprot) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_DISC_VDFS_ENABLE:
                        vdfs_enabled = !vdfs_enabled;
                        CheckMenuItem(hmenu, IDM_DISC_VDFS_ENABLE, (vdfs_enabled) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_DISC_VDFS_ROOT:
                        // TODO: for some reason setting tempname to vdfs_get_root() stops the dialog coming up
                        // strcpy(tempname, vdfs_get_root());
                        strcpy(tempname, "vdfs_get_root");
                        // TODO: getfile() uses GetOpenFileName() which cannot select a folder
                        if (!getfile(hwnd, "VDFS Root", tempname))
                        {
                                vdfs_set_root(tempname);
                        }
                        break;
                        case IDM_TAPE_LOAD:
                        if (!getfile(hwnd, "Tape image (*.UEF;*.CSW)\0*.UEF;*.CSW\0All files (*.*)\0*.*\0", tape_fn))
                        {
                                tape_close();
                                tape_load(tape_fn);
                                tape_loaded = 1;
                        }
                        break;
                        case IDM_TAPE_EJECT:
                        tape_close();
                        tape_loaded = 0;
                        break;
                        
                        case IDM_TAPE_REWIND:
                        tape_close();
                        tape_load(tape_fn);
                        break;
                        case IDM_TAPE_CAT:
                        showcatalogue(hinstance, ghwnd);
                        break;
                        
                        case IDM_TAPES_NORMAL: case IDM_TAPES_FAST:
                        fasttape = LOWORD(wParam) - IDM_TAPES_NORMAL;
                        CheckMenuItem(hmenu, IDM_TAPES_NORMAL, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TAPES_FAST,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, LOWORD(wParam),   MF_CHECKED);
                        break;

                        case IDM_VIDEO_NOBORDERS: case IDM_VIDEO_MBORDERS: case IDM_VIDEO_FBORDERS:
                        CheckMenuItem(hmenu, IDM_VIDEO_NOBORDERS + vid_fullborders, MF_UNCHECKED);
                        vid_fullborders = LOWORD(wParam) - IDM_VIDEO_NOBORDERS;
                        CheckMenuItem(hmenu, IDM_VIDEO_NOBORDERS + vid_fullborders, MF_CHECKED);
                        break;
                        case IDM_VIDEO_FULLSCR:
                        fullscreen = 1;
                        EnterCriticalSection(&cs);
                        video_enterfullscreen();
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_VIDEO_RESIZE:
                        videoresize = !videoresize;
                        CheckMenuItem(hmenu, IDM_VIDEO_RESIZE, (videoresize) ? MF_CHECKED : MF_UNCHECKED);
                        if (videoresize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                        else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE);
                        GetWindowRect(hwnd, &rect);
                        SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                        break;
                        
                        case IDM_VIDEO_SLINEDBL: case IDM_VIDEO_LINEDBL: case IDM_VIDEO_SCANLINES: case IDM_VIDEO_INTERLACED: case IDM_VIDEO_PAL: case IDM_VIDEO_PALI:
                        CheckMenuItem(hmenu, IDM_VIDEO_SLINEDBL,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_VIDEO_LINEDBL,    MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_VIDEO_SCANLINES,  MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_VIDEO_INTERLACED, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_VIDEO_PAL,        MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_VIDEO_PALI,       MF_UNCHECKED);
                        CheckMenuItem(hmenu, LOWORD(wParam), MF_CHECKED);
                        vid_scanlines = vid_interlace = vid_linedbl = vid_pal = 0;
                        if (LOWORD(wParam) == IDM_VIDEO_INTERLACED) vid_interlace = 1;
                        if (LOWORD(wParam) == IDM_VIDEO_SCANLINES)  vid_scanlines = 1;
                        if (LOWORD(wParam) == IDM_VIDEO_SLINEDBL)   vid_linedbl = 1;
                        if (LOWORD(wParam) == IDM_VIDEO_PAL)        vid_pal = 1;
                        if (LOWORD(wParam) == IDM_VIDEO_PALI)       vid_interlace = vid_pal = 1;
                        video_clearscreen();
                        break;
                        case IDM_TUBE_NONE: case IDM_TUBE_6502: case IDM_TUBE_Z80: case IDM_TUBE_65816: case IDM_TUBE_32016:
                        CheckMenuItem(hmenu, IDM_TUBE_NONE,  MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBE_6502,  MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBE_65816, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBE_Z80,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBE_32016, MF_UNCHECKED);
                        CheckMenuItem(hmenu, LOWORD(wParam), MF_CHECKED);
                        selecttube = LOWORD(wParam) - IDM_TUBE_6502;
                        main_restart();
                        break;
                        case IDM_TUBES_4: case IDM_TUBES_8: case IDM_TUBES_16: case IDM_TUBES_32: case IDM_TUBES_64:
                        CheckMenuItem(hmenu, IDM_TUBES_4,    MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBES_8,    MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBES_16,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBES_32,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_TUBES_64,   MF_UNCHECKED);
                        CheckMenuItem(hmenu, LOWORD(wParam), MF_CHECKED);
                        tube_6502_speed = (LOWORD(wParam) - IDM_TUBES_4) + 1;
                        tube_updatespeed();
                        break;
                        
                        case IDM_SOUND_INTERNAL:
                        sound_internal = !sound_internal;
                        CheckMenuItem(hmenu, IDM_SOUND_INTERNAL, (sound_internal) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_BEEBSID:
                        sound_beebsid = !sound_beebsid;
                        CheckMenuItem(hmenu, IDM_SOUND_BEEBSID, (sound_beebsid) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_MUSIC5000:
                        sound_music5000 = !sound_music5000;
                        CheckMenuItem(hmenu, IDM_SOUND_MUSIC5000, (sound_music5000) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_DAC:
                        sound_dac = !sound_dac;
                        CheckMenuItem(hmenu, IDM_SOUND_DAC, (sound_dac) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_DDNOISE:
                        sound_ddnoise = !sound_ddnoise;
                        CheckMenuItem(hmenu, IDM_SOUND_DDNOISE, (sound_ddnoise) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_TAPE:
                        sound_tape = !sound_tape;
                        CheckMenuItem(hmenu, IDM_SOUND_TAPE, (sound_tape) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        case IDM_SOUND_FILTER:
                        sound_filter = !sound_filter;
                        CheckMenuItem(hmenu, IDM_SOUND_FILTER, (sound_filter) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        
                        case IDM_WAVE_SQUARE: case IDM_WAVE_SAW: case IDM_WAVE_SINE: case IDM_WAVE_TRI: case IDM_WAVE_SID:
                        CheckMenuItem(hmenu, IDM_WAVE_SQUARE + curwave, MF_UNCHECKED);
                        curwave = LOWORD(wParam) - IDM_WAVE_SQUARE;
                        CheckMenuItem(hmenu, IDM_WAVE_SQUARE + curwave, MF_CHECKED);
                        break;

                        case IDM_SID_INTERP: case IDM_SID_RESAMP:
                        CheckMenuItem(hmenu, IDM_SID_INTERP, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_SID_RESAMP, MF_UNCHECKED);
                        CheckMenuItem(hmenu, LOWORD(wParam), MF_CHECKED);
                        sidmethod = LOWORD(wParam) - IDM_SID_INTERP;
                        sid_settype(sidmethod, cursid);
                        break;
                        
                        case IDM_DDV_33: case IDM_DDV_66: case IDM_DDV_100:
                        CheckMenuItem(hmenu, (IDM_DDV_33 + ddnoise_vol) - 1, MF_UNCHECKED);
                        ddnoise_vol = (LOWORD(wParam) - IDM_DDV_33) + 1;
                        CheckMenuItem(hmenu, (IDM_DDV_33 + ddnoise_vol) - 1, MF_CHECKED);
                        break;

                        case IDM_DDT_525: case IDM_DDT_35:
                        CheckMenuItem(hmenu, IDM_DDT_525 + ddnoise_type, MF_UNCHECKED);
                        ddnoise_type = LOWORD(wParam) - IDM_DDT_525;
                        CheckMenuItem(hmenu, IDM_DDT_525 + ddnoise_type, MF_CHECKED);
                        ddnoise_close();
                        ddnoise_init();
                        break;

                        case IDM_DEBUGGER:
                        EnterCriticalSection(&cs);
                        rest(200);
                        if (!debugon)
                        {
                                debug = debugon = 1;
                                debug_start();
//                                EnableMenuItem(hmenu,IDM_BREAK,MF_ENABLED);
                        }
                        else
                        {
                                debug ^= 1;
                                debug_end();
//                                EnableMenuItem(hmenu,IDM_BREAK,MF_GRAYED);
                        }
                        CheckMenuItem(hmenu, IDM_DEBUGGER, (debug) ? MF_CHECKED: MF_UNCHECKED);
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_BREAK:
                        debug = 1;
                        break;
                        
                        case IDM_SCRSHOT:
                        if (!getsfile(hwnd, "Bitmap file (*.BMP)\0*.BMP\0All files (*.*)\0*.*\0", vid_scrshotname, "BMP"))
                        {
                                vid_savescrshot = 1;
                        }
                        break;

                        case IDM_KEY_REDEFINE:
                        redefinekeys();
                        break;

                        case IDM_KEY_AS:
                        keyas = !keyas;
                        CheckMenuItem(hmenu, IDM_KEY_AS, (keyas) ? MF_CHECKED : MF_UNCHECKED);
                        break;
                        
                        case IDM_MOUSE_AMX:
                        mouse_amx = !mouse_amx;
                        CheckMenuItem(hmenu, IDM_MOUSE_AMX, (mouse_amx) ? MF_CHECKED : MF_UNCHECKED);
                        main_setmouse();
                        updatewindowtitle();
                        break;

                        case IDM_IDE_ENABLE:
                        EnterCriticalSection(&cs);
                        CheckMenuItem(hmenu, IDM_IDE_ENABLE, (!ide_enable) ? MF_CHECKED : MF_UNCHECKED);
                        ide_enable = !ide_enable;
                        main_restart();
                        LeaveCriticalSection(&cs);
                        break;
                        
                        case IDM_SPD_10: case IDM_SPD_25: case IDM_SPD_50: case IDM_SPD_75: case IDM_SPD_100:
                        case IDM_SPD_150: case IDM_SPD_200: case IDM_SPD_300: case IDM_SPD_400: case IDM_SPD_500:
                        CheckMenuItem(hmenu, IDM_SPD_10 + emuspeed, MF_UNCHECKED);
                        emuspeed = curmodel = LOWORD(wParam) - IDM_SPD_10;
                        changetimerspeed(timerspeeds[emuspeed]);
                        vid_fskipmax = frameskips[emuspeed];
                        CheckMenuItem(hmenu, IDM_SPD_10 + emuspeed, MF_CHECKED);
                        break;
                }
                if (LOWORD(wParam) >= IDM_MODEL_0 && LOWORD(wParam) < (IDM_MODEL_0 + 50))
                {
                        CheckMenuItem(hmenu, IDM_MODEL_0 + curmodel, MF_UNCHECKED);
                        oldmodel = curmodel;
                        curmodel = LOWORD(wParam) - IDM_MODEL_0;
                        CheckMenuItem(hmenu, IDM_MODEL_0 + curmodel, MF_CHECKED);
                        main_restart();
                        updatewindowtitle();
                }
                if (LOWORD(wParam) >= IDM_SID_TYPE && LOWORD(wParam) < (IDM_SID_TYPE + 100))
                {
                        CheckMenuItem(hmenu, IDM_SID_TYPE + cursid, MF_UNCHECKED);
                        cursid = LOWORD(wParam) - IDM_SID_TYPE;
                        CheckMenuItem(hmenu, IDM_SID_TYPE + cursid, MF_CHECKED);
                        sid_settype(sidmethod, cursid);
                }
                return 0;
                
                case WM_USER:
                if (videoresize) SetWindowLong(hwnd, GWL_STYLE,  WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE);
                GetWindowRect(hwnd, &rect);
                SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                break;
                
                case WM_USER+1:
                if (videoresize) SetWindowLong(hwnd, GWL_STYLE,  WS_OVERLAPPEDWINDOW & ~WS_VISIBLE);
                else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) & ~WS_VISIBLE);
                GetWindowRect(hwnd,&rect);
                SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                break;

                case WM_DESTROY:
                PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                break;
                
                case WM_KILLFOCUS:
//              bem_debug("KillFocus\n");
//                infocus=0;
//                spdcount=0;
                if (mousecapture)
                {
                        ClipCursor(&oldclip);
                        mousecapture = 0;
                        updatewindowtitle();
                }
                break;

                case WM_LBUTTONUP:
                if (!mousecapture && (curtube == 3 || mouse_amx))
                {
                        GetClipCursor(&oldclip);
                        GetWindowRect(hwnd, &newclip);
                        newclip.left   += GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        newclip.right  -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        newclip.top    += GetSystemMetrics(SM_CXFIXEDFRAME) + GetSystemMetrics(SM_CYMENUSIZE) + GetSystemMetrics(SM_CYCAPTION) + 10;
                        newclip.bottom -= GetSystemMetrics(SM_CXFIXEDFRAME) + 10;
                        ClipCursor(&newclip);
                        mousecapture = 1;
                        updatewindowtitle();
                }
                break;
                
                case WM_ENTERMENULOOP:
//              bem_debug("EnterMenuLoop\n");
                bempause = 1;
                //EnterCriticalSection(&cs);
                break;
                case WM_EXITMENULOOP:
//              bem_debug("ExitMenuLoop\n");
                bempause = 0;
                key_clear();
                for (c = 0; c < 128; c++) key[c] = 0;
                //LeaveCriticalSection(&cs);
                break;

                case WM_SETFOCUS:
//              bem_debug("SetFocus\n");
                key_clear();
                for (c = 0; c < 128; c++) key[c] = 0;
		bempause = 0;
		break;
                
                case WM_SIZE:
                winsizex = lParam & 0xFFFF;
                winsizey = lParam >> 16;
                break;

        	case WM_SYSKEYDOWN:
        	case WM_KEYDOWN:
                if (LOWORD(wParam) != 255)
                {
                        //bem_debugf("Key %04X %04X\n",LOWORD(wParam),VK_LEFT);
                        c = MapVirtualKey(LOWORD(wParam),0);
                        c = hw_to_mycode[c];
//                        bem_debugf("MVK %i %i %i\n",c,hw_to_mycode[c],KEY_PGUP);
                        if (LOWORD(wParam) == VK_LEFT)   c = KEY_LEFT;
                        if (LOWORD(wParam) == VK_RIGHT)  c = KEY_RIGHT;
                        if (LOWORD(wParam) == VK_UP)     c = KEY_UP;
                        if (LOWORD(wParam) == VK_DOWN)   c = KEY_DOWN;
                        if (LOWORD(wParam) == VK_HOME)   c = KEY_HOME;
                        if (LOWORD(wParam) == VK_END)    c = KEY_END;
                        if (LOWORD(wParam) == VK_INSERT) c = KEY_INSERT;
                        if (LOWORD(wParam) == VK_DELETE) c = KEY_DEL;
                        if (LOWORD(wParam) == VK_PRIOR)  c = KEY_PGUP;
                        if (LOWORD(wParam) == VK_NEXT)   c = KEY_PGDN;
                        //bem_debugf("MVK2 %i %i %i\n",c,hw_to_mycode[c],KEY_PGUP);
                        key[c]=1;
                }
                break;
        	case WM_SYSKEYUP:
        	case WM_KEYUP:
                if (LOWORD(wParam) != 255)
                {
//                        bem_debugf("Key %04X %04X\n",LOWORD(wParam),VK_LEFT);
                        c = MapVirtualKey(LOWORD(wParam), 0);
                        c = hw_to_mycode[c];
                        if (LOWORD(wParam) == VK_LEFT)   c = KEY_LEFT;
                        if (LOWORD(wParam) == VK_RIGHT)  c = KEY_RIGHT;
                        if (LOWORD(wParam) == VK_UP)     c = KEY_UP;
                        if (LOWORD(wParam) == VK_DOWN)   c = KEY_DOWN;
                        if (LOWORD(wParam) == VK_HOME)   c = KEY_HOME;
                        if (LOWORD(wParam) == VK_END)    c = KEY_END;
                        if (LOWORD(wParam) == VK_INSERT) c = KEY_INSERT;
                        if (LOWORD(wParam) == VK_DELETE) c = KEY_DEL;
                        if (LOWORD(wParam) == VK_PRIOR)  c = KEY_PGUP;
                        if (LOWORD(wParam) == VK_NEXT)   c = KEY_PGDN;
//                        bem_debugf("MVK %i\n",c);
                        key[c] = 0;
                }
                break;

                case WM_CREATE:

//        initbbc(argc,argv);

//        free(argv);

//                mainthread=(HANDLE)_beginthread(_mainthread,0,NULL);
                break;

                default:
                r = DefWindowProc (hwnd, message, wParam, lParam);
		return r;
        }
        return 0;
}
#endif
