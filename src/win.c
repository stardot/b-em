/*B-em v2.1 by Tom Walker
  Windows main*/

#ifdef WIN32
#include <allegro.h>
#include <winalleg.h>
#include <alleggl.h>
#include "resources.h"
#include "b-em.h"

RECT oldclip,arcclip;
int mousecapture=0;
int videoresize=0;

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);

/*  Make the class name into a global variable  */
char szClassName[] = "B-emMainWnd";

HWND ghwnd;
int winsizex=640,winsizey=480;

void updatewindowsize(int x, int y)
{
        RECT r;
        if (x<128) x=128;
        if (y<64)  y=64;
        if (x==winsizex && y==winsizey) return;
        if (!videoresize) { x--; y--; }
        winsizex=x;
        winsizey=y;
        GetWindowRect(ghwnd,&r);
        MoveWindow(ghwnd,r.left,r.top,
                     x+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),
                     y+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,
                     TRUE);
}

int quited=0;
void setquit()
{
        quited=1;
}

HINSTANCE hinstance;

char **argv;
int argc;
char *argbuf;

void processcommandline()
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
         rpclog("Arg %i - %s\n",argc-1,argv[argc-1]);
      }
   }

   argv[argc] = NULL;
//   free(argbuf);
}

void makemodelmenu()
{
/*        HMENU hmenu,hsettingsmenu,hmodelmenu;
        char *s;
        int c=0;
        hmenu=GetMenu(ghwnd);
        hsettingsmenu=GetSubMenu(hmenu,2);
        hmodelmenu=GetSubMenu(hsettingsmenu,0);
        while (1)
        {
                rpclog("Getmodel %i\n",c);
                s=getmodel();
                if (!s) break;
                if (!strcmp(s,"")) break;
                rpclog("Model %s %i\n",s,IDM_MODEL_0+c);
                AppendMenu(hmodelmenu,MF_STRING,IDM_MODEL_0+c,s);
                c++;
        }*/
//        RemoveMenu(hmodelmenu,0,MF_BYPOSITION);
}

void initmenu()
{
        char t[512];
        HMENU hmenu=GetMenu(ghwnd);

        CheckMenuItem(hmenu,IDM_DISC_WPROT_0,(writeprot[0])?MF_CHECKED:MF_UNCHECKED);
        CheckMenuItem(hmenu,IDM_DISC_WPROT_1,(writeprot[1])?MF_CHECKED:MF_UNCHECKED);
        CheckMenuItem(hmenu,IDM_DISC_WPROT_D,(defaultwriteprot)?MF_CHECKED:MF_UNCHECKED);

        CheckMenuItem(hmenu,IDM_TUBE_6502+selecttube,MF_CHECKED);
        CheckMenuItem(hmenu,IDM_MODEL_0+curmodel,MF_CHECKED);

        if (comedyblit)     CheckMenuItem(hmenu,IDM_VIDEO_SCANLINES, MF_CHECKED);
        else if (interlace) CheckMenuItem(hmenu,IDM_VIDEO_INTERLACED,MF_CHECKED);
        else if (linedbl)   CheckMenuItem(hmenu,IDM_VIDEO_SLINEDBL,   MF_CHECKED);
        else                CheckMenuItem(hmenu,IDM_VIDEO_LINEDBL,   MF_CHECKED);

        if (sndinternal) CheckMenuItem(hmenu,IDM_SOUND_INTERNAL,MF_CHECKED);
        if (sndbeebsid)  CheckMenuItem(hmenu,IDM_SOUND_BEEBSID, MF_CHECKED);
        if (sndddnoise)  CheckMenuItem(hmenu,IDM_SOUND_DDNOISE, MF_CHECKED);
        if (sndtape)     CheckMenuItem(hmenu,IDM_SOUND_TAPE,    MF_CHECKED);

        CheckMenuItem(hmenu,IDM_SOUND_FILTER,(soundfilter)?MF_CHECKED:MF_UNCHECKED);
        CheckMenuItem(hmenu,IDM_WAVE_SQUARE+curwave,MF_CHECKED);

        CheckMenuItem(hmenu,IDM_SID_TYPE+cursid,MF_CHECKED);
        CheckMenuItem(hmenu,IDM_SID_INTERP+sidmethod,MF_CHECKED);

        CheckMenuItem(hmenu,IDM_DDT_525+ddtype,MF_CHECKED);
        CheckMenuItem(hmenu,(IDM_DDV_33+ddvol)-1,MF_CHECKED);

        CheckMenuItem(hmenu,IDM_TAPES_NORMAL+fasttape,MF_CHECKED);

        CheckMenuItem(hmenu,IDM_TUBES_4+(tube6502speed-1),MF_CHECKED);

        CheckMenuItem(hmenu,IDM_VIDEO_NOBORDERS+fullborders,MF_CHECKED);

        append_filename(t,exedir,"roms\\tube\\ReCo6502ROM_816",511);
        if (!file_exists(t,FA_ALL,NULL)) EnableMenuItem(hmenu,IDM_TUBE_65816,MF_GRAYED);

        if (keyas)    CheckMenuItem(hmenu,IDM_KEY_AS,    MF_CHECKED);

        CheckMenuItem(hmenu,IDM_IDE_ENABLE,ideenable?MF_CHECKED:MF_UNCHECKED);

        if (opengl)   CheckMenuItem(hmenu,IDM_VIDEO_OPENGL,MF_CHECKED);
        else          CheckMenuItem(hmenu,IDM_VIDEO_DDRAW, MF_CHECKED);

        CheckMenuItem(hmenu,IDM_VIDEO_RESIZE,(videoresize)?MF_CHECKED:MF_UNCHECKED);

        CheckMenuItem(hmenu,IDM_SPD_100,MF_CHECKED);

        if (opengl) EnableMenuItem(hmenu,IDM_VIDEO_FULLSCR,MF_GRAYED);
}

HANDLE mainthread;

int bemclose=0;
int bempause=0,bemwaiting=0;
int doautoboot=0;

void _mainthread(PVOID pvoid)
{
        HMENU hmenu;
        initbbc(argc,argv);
        while (1)
        {
                if (bempause)
                {
                        bemwaiting=1;
                        Sleep(100);
                }
                else
                {
                        bemwaiting=0;
                        runbbc();
                }
                if (doautoboot)
                {
                        restartbbc();
                        closedisc(0);
                        loaddisc(0,discfns[0]);
                        if (defaultwriteprot) writeprot[0]=1;
                        hmenu=GetMenu(ghwnd);
                        CheckMenuItem(hmenu,IDM_DISC_WPROT_0,(writeprot[0])?MF_CHECKED:MF_UNCHECKED);
                        autoboot=150;
                        doautoboot=0;
                }
        }
}

void waitforready()
{
        bempause=1;
        while (!bemwaiting) Sleep(100);
}

void resumeready()
{
        bempause=0;
}

CRITICAL_SECTION cs;

void startblit()
{
        rpclog("startblit\n");
        EnterCriticalSection(&cs);
}

void endblit()
{
        rpclog("endblit\n");
        LeaveCriticalSection(&cs);
}

void setejecttext(int drive, char *fn)
{
        MENUITEMINFO mi;
        HMENU hmenu;
        char s[128];
        if (fn[0]) sprintf(s,"Eject drive :%i/%i - %s",drive,drive+2,get_filename(fn));
        else       sprintf(s,"Eject drive :%i/%i",drive,drive+2);
        memset(&mi,0,sizeof(MENUITEMINFO));
        mi.cbSize=sizeof(MENUITEMINFO);
        mi.fMask=MIIM_STRING;
        mi.fType=MFT_STRING;
        mi.dwTypeData=s;
        hmenu=GetMenu(ghwnd);
        SetMenuItemInfo(hmenu,IDM_DISC_EJECT_0+drive,0,&mi);
        CheckMenuItem(hmenu,IDM_DISC_WPROT_0+drive,(writeprot[drive])?MF_CHECKED:MF_UNCHECKED);
}

void updatewindowtitle()
{
        if (curtube==3)
        {
                if (!mousecapture) set_window_title("B-em v2.1a - click to capture mouse");
                else               set_window_title("B-em v2.1a - CTRL-END to release mouse");
        }
        else
           set_window_title("B-em v2.1a");
}

extern int doopenglblit;

void bem_error(char *s)
{
        MessageBox(ghwnd,s,"B-em error",MB_OK|MB_ICONEXCLAMATION);
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
        int oldf;
        char *p;

        for (c=0;c<128;c++) keylookup[c]=c;

        processcommandline();

        hinstance=hThisInstance;
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
           "B-em v2.1a", /* Title Text */
           WS_OVERLAPPEDWINDOW/*&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX*/, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           640+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),                 /* The programs width */
           480+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           LoadMenu(hThisInstance,TEXT("MainMenu")),                /* No menu */
           hThisInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );

        ghwnd=hwnd;

        win_set_window(hwnd);

        allegro_init();

        get_executable_name(exedir,511);
        p=get_filename(exedir);
        p[0]=0;

        loadconfig();

        makemodelmenu();

        InitializeCriticalSection(&cs);

        /* Make the window visible on the screen */
        if (!opengl) ShowWindow (hwnd, nFunsterStil);

        initmenu();

        mainthread=(HANDLE)_beginthread(_mainthread,0,NULL);

        updatewindowtitle();


        /* Run the message loop. It will run until GetMessage() returns 0 */
        while (!quited)
        {
//                runbbc();
                if (PeekMessage(&messages,NULL,0,0,PM_REMOVE))
                {
                        if (messages.message==WM_QUIT)
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
                        mousecapture=0;
                        updatewindowtitle();
                }
                if (key[KEY_ALT] && key[KEY_ENTER] && fullscreen && !oldf && !opengl)
                {
                        EnterCriticalSection(&cs);
                        fullscreen=0;
                        leavefullscreen();
                        LeaveCriticalSection(&cs);
                }
                else if (key[KEY_ALT] && key[KEY_ENTER] && !fullscreen && !oldf && !opengl)
                {
                        EnterCriticalSection(&cs);
                        fullscreen=1;
                        enterfullscreen();
                        LeaveCriticalSection(&cs);
                }
                oldf=key[KEY_ALT] && key[KEY_ENTER];
        }

        EnterCriticalSection(&cs);
        TerminateThread(mainthread,0);
        killdebug();
        closebbc();
        DeleteCriticalSection(&cs);

        return messages.wParam;
}

char openfilestring[260];
int getfile(HWND hwnd, char *f, char *fn)
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

        // Display the Open dialog box.

        if (GetOpenFileName(&ofn))
        {
                LeaveCriticalSection(&cs);
                strcpy(fn,openfilestring);
                return 0;
        }
        LeaveCriticalSection(&cs);
        return 1;
}
int getsfile(HWND hwnd, char *f, char *fn, char *de)
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
        ofn.lpstrDefExt=de;

        // Display the Open dialog box.

        if (GetSaveFileName(&ofn))
        {
                LeaveCriticalSection(&cs);
                strcpy(fn,openfilestring);
                return 0;
        }
        LeaveCriticalSection(&cs);
        return 1;
}

void removewindow()
{
        SendMessage(ghwnd,WM_USER,0,0);
}
void updatewindow()
{
        SendMessage(ghwnd,WM_USER,0,0);
}

extern unsigned char hw_to_mycode[256];

int timerspeeds[]={5,12,25,38,50,75,100,150,200,250};
int frameskips[] ={0,0, 0, 0, 0, 0, 1,  2,  3,  4};
int emuspeed=4;

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HMENU hmenu;
        RECT rect;
        int c;
        LRESULT r;

        switch (message)
        {
                case WM_COMMAND:
                hmenu=GetMenu(hwnd);
                switch (LOWORD(wParam))
                {
                        case IDM_FILE_RESET:
                        EnterCriticalSection(&cs);
                        restartbbc();
                        LeaveCriticalSection(&cs);
                        break;

                        case IDM_FILE_LSTATE:
                        EnterCriticalSection(&cs);
                        if (!getfile(hwnd,"Save State (*.SNP)\0*.SNP\0All files (*.*)\0*.*\0\0",ssname))
                           loadstate();
                        cleardrawit();
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_FILE_SSTATE:
                        EnterCriticalSection(&cs);
                        if (curtube!=-1)
                        {
                                bem_error("Second processor save states not supported yet.");
                        }
                        else
                        {
                                if (!getsfile(hwnd,"Save State (*.SNP)\0*.SNP\0All files (*.*)\0*.*\0\0",ssname,"SNP"))
                                   savestate();
                        }
                        cleardrawit();
                        LeaveCriticalSection(&cs);
                        break;

                        case IDM_FILE_EXIT:
                        PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                        break;


                        case IDM_DISC_AUTOBOOT:
                        if (!getfile(hwnd,"Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0",discfns[0]))
                        {
                                doautoboot=1;
                        }
                        break;
                        case IDM_DISC_LOAD_0:
                        if (!getfile(hwnd,"Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0",discfns[0]))
                        {
                                closedisc(0);
                                loaddisc(0,discfns[0]);
                                if (defaultwriteprot) writeprot[0]=1;
                                CheckMenuItem(hmenu,IDM_DISC_WPROT_0,(writeprot[0])?MF_CHECKED:MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_LOAD_1:
                        if (!getfile(hwnd,"Disc image (*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI)\0*.SSD;*.DSD;*.IMG;*.ADF;*.ADL;*.FDI\0All files (*.*)\0*.*\0",discfns[1]))
                        {
                                closedisc(1);
                                loaddisc(1,discfns[1]);
                                if (defaultwriteprot) writeprot[1]=1;
                                CheckMenuItem(hmenu,IDM_DISC_WPROT_1,(writeprot[1])?MF_CHECKED:MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_EJECT_0:
                        closedisc(0);
                        discfns[0][0]=0;
                        setejecttext(0,"");
                        break;
                        case IDM_DISC_EJECT_1:
                        closedisc(1);
                        discfns[1][0]=0;
                        setejecttext(1,"");
                        break;
                        case IDM_DISC_NEW_0:
                        if (!getsfile(hwnd,"Disc image (*.SSD;*.DSD;*.ADF;*.ADL)\0*.SSD;*.DSD;*.ADF;*.ADL\0All files (*.*)\0*.*\0",discfns[0],"SSD"))
                        {
                                closedisc(0);
                                newdisc(0,discfns[0]);
                                if (defaultwriteprot) writeprot[0]=1;
                                CheckMenuItem(hmenu,IDM_DISC_WPROT_0,(writeprot[0])?MF_CHECKED:MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_NEW_1:
                        if (!getsfile(hwnd,"Disc image (*.SSD;*.DSD;*.ADF;*.ADL)\0*.SSD;*.DSD;*.ADF;*.ADL\0All files (*.*)\0*.*\0",discfns[1],"SSD"))
                        {
                                closedisc(1);
                                newdisc(1,discfns[1]);
                                if (defaultwriteprot) writeprot[1]=1;
                                CheckMenuItem(hmenu,IDM_DISC_WPROT_1,(writeprot[1])?MF_CHECKED:MF_UNCHECKED);
                        }
                        break;
                        case IDM_DISC_WPROT_0:
                        writeprot[0]=!writeprot[0];
                        if (fwriteprot[0]) writeprot[0]=1;
                        CheckMenuItem(hmenu,IDM_DISC_WPROT_0,(writeprot[0])?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_DISC_WPROT_1:
                        writeprot[1]=!writeprot[1];
                        if (fwriteprot[1]) writeprot[1]=1;
                        CheckMenuItem(hmenu,IDM_DISC_WPROT_1,(writeprot[1])?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_DISC_WPROT_D:
                        defaultwriteprot=!defaultwriteprot;
                        CheckMenuItem(hmenu,IDM_DISC_WPROT_D,(defaultwriteprot)?MF_CHECKED:MF_UNCHECKED);
                        break;

                        case IDM_TAPE_LOAD:
                        if (!getfile(hwnd,"Tape image (*.UEF;*.CSW)\0*.UEF;*.CSW\0All files (*.*)\0*.*\0",tapefn))
                        {
                                closeuef();
                                closecsw();
                                loadtape(tapefn);
                                tapeloaded=1;
                        }
                        break;
                        case IDM_TAPE_EJECT:
                        closeuef();
                        closecsw();
                        tapeloaded=0;
                        break;

                        case IDM_TAPE_REWIND:
                        closeuef();
                        closecsw();
                        loadtape(tapefn);
                        break;
                        case IDM_TAPE_CAT:
                        showcatalogue(hinstance,ghwnd);
                        break;

                        case IDM_TAPES_NORMAL: case IDM_TAPES_FAST:
                        fasttape=LOWORD(wParam)-IDM_TAPES_NORMAL;
                        CheckMenuItem(hmenu,IDM_TAPES_NORMAL, MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TAPES_FAST,MF_UNCHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        break;

                        case IDM_VIDEO_NOBORDERS: case IDM_VIDEO_MBORDERS: case IDM_VIDEO_FBORDERS:
                        CheckMenuItem(hmenu,IDM_VIDEO_NOBORDERS+fullborders,MF_UNCHECKED);
                        fullborders=LOWORD(wParam)-IDM_VIDEO_NOBORDERS;
                        CheckMenuItem(hmenu,IDM_VIDEO_NOBORDERS+fullborders,MF_CHECKED);
                        break;
                        case IDM_VIDEO_FULLSCR:
                        fullscreen=1;
                        EnterCriticalSection(&cs);
                        enterfullscreen();
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_VIDEO_RESIZE:
                        videoresize=!videoresize;
                        CheckMenuItem(hmenu,IDM_VIDEO_RESIZE,(videoresize)?MF_CHECKED:MF_UNCHECKED);
                        if (videoresize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
                        else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);
                        GetWindowRect(hwnd,&rect);
                        SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                        break;
                        case IDM_VIDEO_DDRAW:
                        if (opengl) vidchange=1;
                        CheckMenuItem(hmenu,IDM_VIDEO_OPENGL,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_VIDEO_DDRAW, MF_CHECKED);
                        EnableMenuItem(hmenu,IDM_VIDEO_FULLSCR,MF_ENABLED);
                        break;
                        case IDM_VIDEO_OPENGL:
                        if (!opengl) vidchange=2;
                        CheckMenuItem(hmenu,IDM_VIDEO_OPENGL,MF_CHECKED);
                        CheckMenuItem(hmenu,IDM_VIDEO_DDRAW, MF_UNCHECKED);
                        EnableMenuItem(hmenu,IDM_VIDEO_FULLSCR,MF_GRAYED);
                        break;

                        case IDM_VIDEO_SLINEDBL: case IDM_VIDEO_LINEDBL: case IDM_VIDEO_SCANLINES: case IDM_VIDEO_INTERLACED:
                        CheckMenuItem(hmenu,IDM_VIDEO_SLINEDBL,  MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_VIDEO_LINEDBL,   MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_VIDEO_SCANLINES, MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_VIDEO_INTERLACED,MF_UNCHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        comedyblit=interlace=linedbl=0;
                        if (LOWORD(wParam)==IDM_VIDEO_INTERLACED) interlace=1;
                        if (LOWORD(wParam)==IDM_VIDEO_SCANLINES)  comedyblit=1;
                        if (LOWORD(wParam)==IDM_VIDEO_SLINEDBL)   linedbl=1;
                        clearscreen();
                        break;
                        case IDM_TUBE_NONE: case IDM_TUBE_6502: case IDM_TUBE_Z80: case IDM_TUBE_65816: case IDM_TUBE_32016:
                        CheckMenuItem(hmenu,IDM_TUBE_NONE,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBE_6502,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBE_65816,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBE_Z80, MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBE_32016,MF_UNCHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        selecttube=LOWORD(wParam)-IDM_TUBE_6502;
                        restartbbc();
                        break;
                        case IDM_TUBES_4: case IDM_TUBES_8: case IDM_TUBES_16: case IDM_TUBES_32: case IDM_TUBES_64:
                        CheckMenuItem(hmenu,IDM_TUBES_4,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBES_8,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBES_16,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBES_32,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_TUBES_64,MF_UNCHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        tube6502speed=(LOWORD(wParam)-IDM_TUBES_4)+1;
                        updatetubespeed();
                        break;

                        case IDM_SOUND_INTERNAL:
                        sndinternal=!sndinternal;
                        CheckMenuItem(hmenu,IDM_SOUND_INTERNAL,(sndinternal)?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_SOUND_BEEBSID:
                        sndbeebsid=!sndbeebsid;
                        CheckMenuItem(hmenu,IDM_SOUND_BEEBSID,(sndbeebsid)?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_SOUND_DDNOISE:
                        sndddnoise=!sndddnoise;
                        CheckMenuItem(hmenu,IDM_SOUND_DDNOISE,(sndddnoise)?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_SOUND_TAPE:
                        sndtape=!sndtape;
                        CheckMenuItem(hmenu,IDM_SOUND_TAPE,(sndtape)?MF_CHECKED:MF_UNCHECKED);
                        break;
                        case IDM_SOUND_FILTER:
                        soundfilter=!soundfilter;
                        CheckMenuItem(hmenu,IDM_SOUND_FILTER,(soundfilter)?MF_CHECKED:MF_UNCHECKED);
                        break;

                        case IDM_WAVE_SQUARE: case IDM_WAVE_SAW: case IDM_WAVE_SINE: case IDM_WAVE_TRI: case IDM_WAVE_SID:
                        CheckMenuItem(hmenu,IDM_WAVE_SQUARE+curwave,MF_UNCHECKED);
                        curwave=LOWORD(wParam)-IDM_WAVE_SQUARE;
                        CheckMenuItem(hmenu,IDM_WAVE_SQUARE+curwave,MF_CHECKED);
                        break;

                        case IDM_SID_INTERP: case IDM_SID_RESAMP:
                        CheckMenuItem(hmenu,IDM_SID_INTERP,MF_UNCHECKED);
                        CheckMenuItem(hmenu,IDM_SID_RESAMP,MF_UNCHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        sidmethod=LOWORD(wParam)-IDM_SID_INTERP;
                        setsidtype(sidmethod, cursid);
                        break;

                        case IDM_DDV_33: case IDM_DDV_66: case IDM_DDV_100:
                        CheckMenuItem(hmenu,(IDM_DDV_33+ddvol)-1,MF_UNCHECKED);
                        ddvol=(LOWORD(wParam)-IDM_DDV_33)+1;
                        CheckMenuItem(hmenu,(IDM_DDV_33+ddvol)-1,MF_CHECKED);
                        break;

                        case IDM_DDT_525: case IDM_DDT_35:
                        CheckMenuItem(hmenu,IDM_DDT_525+ddtype,MF_UNCHECKED);
                        ddtype=LOWORD(wParam)-IDM_DDT_525;
                        CheckMenuItem(hmenu,IDM_DDT_525+ddtype,MF_CHECKED);
                        closeddnoise();
                        loaddiscsamps();
                        break;

                        case IDM_DEBUGGER:
                        EnterCriticalSection(&cs);
                        rest(200);
                        if (!debugon)
                        {
                                debug=debugon=1;
                                startdebug();
//                                EnableMenuItem(hmenu,IDM_BREAK,MF_ENABLED);
                        }
                        else
                        {
                                debug^=1;
                                enddebug();
//                                EnableMenuItem(hmenu,IDM_BREAK,MF_GRAYED);
                        }
                        CheckMenuItem(hmenu,IDM_DEBUGGER,(debug)?MF_CHECKED:MF_UNCHECKED);
                        LeaveCriticalSection(&cs);
                        break;
                        case IDM_BREAK:
                        debug=1;
                        break;

                        case IDM_SCRSHOT:
                        if (!getsfile(hwnd,"Bitmap file (*.BMP)\0*.BMP\0All files (*.*)\0*.*\0",scrshotname,"BMP"))
                        {
                                savescrshot=1;
                        }
                        break;

                        case IDM_KEY_REDEFINE:
                        redefinekeys();
                        break;

                        case IDM_KEY_AS:
                        keyas=!keyas;
                        CheckMenuItem(hmenu,IDM_KEY_AS,(keyas)?MF_CHECKED:MF_UNCHECKED);
                        break;

                        case IDM_IDE_ENABLE:
                        EnterCriticalSection(&cs);
                        CheckMenuItem(hmenu,IDM_IDE_ENABLE,(!ideenable)?MF_CHECKED:MF_UNCHECKED);
                        ideenable=!ideenable;
                        restartbbc();
                        LeaveCriticalSection(&cs);
                        break;

                        case IDM_SPD_10: case IDM_SPD_25: case IDM_SPD_50: case IDM_SPD_75: case IDM_SPD_100:
                        case IDM_SPD_150: case IDM_SPD_200: case IDM_SPD_300: case IDM_SPD_400: case IDM_SPD_500:
                        CheckMenuItem(hmenu,IDM_SPD_10+emuspeed,MF_UNCHECKED);
                        emuspeed=curmodel=LOWORD(wParam)-IDM_SPD_10;
                        changetimerspeed(timerspeeds[emuspeed]);
                        fskipmax=frameskips[emuspeed];
                        CheckMenuItem(hmenu,IDM_SPD_10+emuspeed,MF_CHECKED);
                        break;
                }
                if (LOWORD(wParam)>=IDM_MODEL_0 && LOWORD(wParam)<(IDM_MODEL_0+50))
                {
                        CheckMenuItem(hmenu,IDM_MODEL_0+curmodel,MF_UNCHECKED);
                        oldmodel=curmodel;
                        curmodel=LOWORD(wParam)-IDM_MODEL_0;
                        CheckMenuItem(hmenu,IDM_MODEL_0+curmodel,MF_CHECKED);
                        restartbbc();
                        updatewindowtitle();
                }
                if (LOWORD(wParam)>=IDM_SID_TYPE && LOWORD(wParam)<(IDM_SID_TYPE+100))
                {
                        CheckMenuItem(hmenu,IDM_SID_TYPE+cursid,MF_UNCHECKED);
                        cursid=LOWORD(wParam)-IDM_SID_TYPE;
                        CheckMenuItem(hmenu,IDM_SID_TYPE+cursid,MF_CHECKED);
                        setsidtype(sidmethod, cursid);
                }
                return 0;

                case WM_USER:
                if (videoresize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
                else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);
                GetWindowRect(hwnd,&rect);
                SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                break;

                case WM_USER+1:
                if (videoresize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW&~WS_VISIBLE);
                else             SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)&~WS_VISIBLE);
                GetWindowRect(hwnd,&rect);
                SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                break;

                case WM_DESTROY:
                PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                break;

                case WM_KILLFOCUS:
                rpclog("KillFocus\n");
//                infocus=0;
//                spdcount=0;
                if (mousecapture)
                {
                        ClipCursor(&oldclip);
                        mousecapture=0;
                        updatewindowtitle();
                }
                break;

                case WM_LBUTTONUP:
                if (!mousecapture && curtube==3)
                {
                        GetClipCursor(&oldclip);
                        GetWindowRect(hwnd,&arcclip);
                        arcclip.left+=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        arcclip.right-=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        arcclip.top+=GetSystemMetrics(SM_CXFIXEDFRAME)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+10;
                        arcclip.bottom-=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        ClipCursor(&arcclip);
                        mousecapture=1;
                        updatewindowtitle();
                }
                break;

                case WM_ENTERMENULOOP:
                rpclog("EnterMenuLoop\n");
                bempause=1;
                //EnterCriticalSection(&cs);
                break;
                case WM_EXITMENULOOP:
                rpclog("ExitMenuLoop\n");
                bempause=0;
                clearkeys();
                for (c=0;c<128;c++) key[c]=0;
                //LeaveCriticalSection(&cs);
                break;

                case WM_SETFOCUS:
                rpclog("SetFocus\n");
                clearkeys();
                for (c=0;c<128;c++) key[c]=0;
                bempause=0;
                break;

                case WM_SIZE:
                winsizex=lParam&0xFFFF;
                winsizey=lParam>>16;
                break;

                case WM_SYSKEYDOWN:
                case WM_KEYDOWN:
                if (LOWORD(wParam)!=255)
                {
                        //rpclog("Key %04X %04X\n",LOWORD(wParam),VK_LEFT);
                        c=MapVirtualKey(LOWORD(wParam),0);
                        c=hw_to_mycode[c];
//                        rpclog("MVK %i %i %i\n",c,hw_to_mycode[c],KEY_PGUP);
                        if (LOWORD(wParam)==VK_LEFT)   c=KEY_LEFT;
                        if (LOWORD(wParam)==VK_RIGHT)  c=KEY_RIGHT;
                        if (LOWORD(wParam)==VK_UP)     c=KEY_UP;
                        if (LOWORD(wParam)==VK_DOWN)   c=KEY_DOWN;
                        if (LOWORD(wParam)==VK_HOME)   c=KEY_HOME;
                        if (LOWORD(wParam)==VK_END)    c=KEY_END;
                        if (LOWORD(wParam)==VK_INSERT) c=KEY_INSERT;
                        if (LOWORD(wParam)==VK_DELETE) c=KEY_DEL;
                        if (LOWORD(wParam)==VK_PRIOR)  c=KEY_PGUP;
                        if (LOWORD(wParam)==VK_NEXT)   c=KEY_PGDN;
                        //rpclog("MVK2 %i %i %i\n",c,hw_to_mycode[c],KEY_PGUP);
                        key[c]=1;
                }
                break;
                case WM_SYSKEYUP:
                case WM_KEYUP:
                if (LOWORD(wParam)!=255)
                {
//                        rpclog("Key %04X %04X\n",LOWORD(wParam),VK_LEFT);
                        c=MapVirtualKey(LOWORD(wParam),0);
                        c=hw_to_mycode[c];
                        if (LOWORD(wParam)==VK_LEFT)   c=KEY_LEFT;
                        if (LOWORD(wParam)==VK_RIGHT)  c=KEY_RIGHT;
                        if (LOWORD(wParam)==VK_UP)     c=KEY_UP;
                        if (LOWORD(wParam)==VK_DOWN)   c=KEY_DOWN;
                        if (LOWORD(wParam)==VK_HOME)   c=KEY_HOME;
                        if (LOWORD(wParam)==VK_END)    c=KEY_END;
                        if (LOWORD(wParam)==VK_INSERT) c=KEY_INSERT;
                        if (LOWORD(wParam)==VK_DELETE) c=KEY_DEL;
                        if (LOWORD(wParam)==VK_PRIOR)  c=KEY_PGUP;
                        if (LOWORD(wParam)==VK_NEXT)   c=KEY_PGDN;
//                        rpclog("MVK %i\n",c);
                        key[c]=0;
                }
                break;

                case WM_CREATE:

//        initbbc(argc,argv);

//        free(argv);

//                        if (opengl) initgl(hwnd);
//                mainthread=(HANDLE)_beginthread(_mainthread,0,NULL);
                break;

                default:
                r=DefWindowProc (hwnd, message, wParam, lParam);
                return r;
        }
        return 0;
}
#endif
