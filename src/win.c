/*B-em v2.2 by Tom Walker
  Windows main*/

#ifdef WIN32
#include <process.h>
#include <windows.h>
#include <shlobj.h>

#include "b-em.h"

#include "config.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "ide.h"
#include "keyboard.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "mouse.h"
#include "savestate.h"
#include "sid_b-em.h"
#include "scsi.h"
#include "sound.h"
#include "sn76489.h"
#include "tape.h"
#include "tube.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"
#include "win.h"
#include "win-romconfig.h"

char exedir[MAX_PATH];
char tempname[MAX_PATH];

RECT oldclip, newclip;
int mousecapture = 0;
int videoresize  = 0;

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);

/*  Make the class name into a global variable  */
char szClassName[] = "B-emMainWnd";

HWND ghwnd;
int winsizex = 640, winsizey = 480;

static int quited = 0;
void setquit()
{
        quited = 1;
}

HINSTANCE hinstance;

CRITICAL_SECTION cs;

void startblit()
{
//        log_debug("startblit\n");
        EnterCriticalSection(&cs);
}

void endblit()
{
//        log_debug("endblit\n");
        LeaveCriticalSection(&cs);
}

static BOOL win_file_exists(const char *szPath) {
    DWORD dwAttrib = GetFileAttributes(szPath);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
            !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

int find_dat_file(char *path, size_t psize, const char *subdir, const char *name, const char *ext) {
    ALLEGRO_PATH *respath = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    const char *cpath = al_path_cstr(respath, ALLEGRO_NATIVE_PATH_SEP);
    snprintf(path, psize, "%s/%s/%s.%s", cpath, subdir, name, ext);
    return !win_file_exists(path);
}

int find_cfg_file(char *path, size_t psize, const char *name, const char *ext) {
    ALLEGRO_PATH *setpath = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH);
    const char *cpath = al_path_cstr(setpath, ALLEGRO_NATIVE_PATH_SEP);
    snprintf(path, psize, "%s/%s.%s", cpath, name, ext);
    return !win_file_exists(path);
}

int find_cfg_dest(char *path, size_t psize, const char *name, const char *ext) {
    ALLEGRO_PATH *setpath = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH);
    const char *cpath = al_path_cstr(setpath, ALLEGRO_NATIVE_PATH_SEP);
    snprintf(path, psize, "%s/%s.%s", cpath, name, ext);
    return 0;
}

static char openfilestring[MAX_PATH];

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
extern unsigned char hw_to_mycode[256];

int timerspeeds[] = {5, 12, 25, 38, 50, 75, 100, 150, 200, 250};
int frameskips[]  = {0, 0,  0,  0,  0,  0,  1,   2,   3,   4};

#endif
