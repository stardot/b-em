/*B-em v2.2 by Tom Walker
  Main header file*/

#ifndef __INCLUDE_B_EM_HEADER__
#define __INCLUDE_B_EM_HEADER__

#include <allegro5/allegro.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat_wrappers.h"

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

#ifdef _MSC_VER

#define inline __inline

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#endif

#include "logging.h"

#define VERSION_STR "B-em v-" VERSION

extern ALLEGRO_PATH *find_dat_file(ALLEGRO_PATH *dir, const char *name, const char *ext);
extern ALLEGRO_PATH *find_cfg_file(const char *name, const char *ext);
extern ALLEGRO_PATH *find_cfg_dest(const char *name, const char *ext);

extern int joybutton[2];
extern float joyaxes[4];

void setquit();

void cataddname(char *s);

#ifdef WIN32

#include <windows.h>

#endif

extern int autoboot;

void redefinekeys(void);

void changetimerspeed(int i);

extern int mousecapture;

#endif
