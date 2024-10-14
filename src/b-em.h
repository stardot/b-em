/*B-em v2.2 by Tom Walker
  Main header file*/

#ifndef __INCLUDE_B_EM_HEADER__
#define __INCLUDE_B_EM_HEADER__

#include <allegro5/allegro.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "compat_wrappers.h"

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

#ifdef _MSC_VER

#define inline __inline

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#define fflush_unlocked _fflush_nolock
#define getc_unlocked   _getc_nolock
#define putc_unlocked   _putc_nolock
#define fread_unlocked  _fread_nolock
#define fwrite_unlocked _fwrite_nolock

#else

#ifdef WIN32
#define fflush_unlocked fflush
#define getc_unlocked   getc
#define putc_unlocked   putc
#define fread_unlocked  fread
#define fwrite_unlocked fwrite
#else
#ifdef __APPLE__
#define fread_unlocked fread
#define fwrite_unlocked fwrite
#endif
#endif

#endif

#include "logging.h"

#define VERSION_STR "B-em v-" VERSION

extern ALLEGRO_PATH *find_dat_file(ALLEGRO_PATH *dir, const char *name, const char *ext);
extern ALLEGRO_PATH *find_cfg_file(const char *name, const char *ext);
extern ALLEGRO_PATH *find_cfg_dest(const char *name, const char *ext);
extern bool is_relative_filename(const char *fn);

extern int joybutton[4];
extern float joyaxes[4];
extern bool tricky_sega_adapter;

void setquit(void);

void cataddname(char *s);

#ifdef WIN32

#include <windows.h>

#endif

extern int autoboot;

void redefinekeys(void);

void changetimerspeed(int i);

extern int mousecapture;

#endif
