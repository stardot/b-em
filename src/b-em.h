/*B-em v2.2 by Tom Walker
  Main header file*/

#ifndef __INCLUDE_B_EM_HEADER__
#define __INCLUDE_B_EM_HEADER__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat_wrappers.h"

#ifdef _MSC_VER

#define inline __inline

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#endif

#include "logging.h"

#define VERSION_STR "B-em v-" VERSION

#ifndef __unused
#define __unused __attribute__ ((__unused__))
#endif

void updatewindowsize(int x, int y);

void setejecttext(int drive, char *fn);

extern char exedir[512];

extern int joybutton[2];

void waitforready();
void resumeready();

void setquit();

void startblit();
void endblit();

extern int autoboot;

void cataddname(char *s);
void showcatalogue();

void redefinekeys();

void changetimerspeed(int i);

extern int mousecapture;

#endif
