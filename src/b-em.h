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

//#define printf bem_debug

#define VERSION_STR "B-em v-" VERSION

void updatewindowsize(int x, int y);

void setejecttext(int drive, char *fn);

extern void bem_error(char *s);
extern void bem_errorf(const char *fmt, ...);
extern void bem_warn(const char *s);
extern void bem_warnf(const char *fmt, ...);
extern void bem_debug(const char *s);
extern void bem_debugf(const char *format, ...);
extern void debug_open(void);
extern void debug_close(void);

// Remove debugging calls if debug not selected.
#ifndef DEBUG
#define bem_debug(s) {}
#if __STDC_VERSION__ >= 199901L
#define bem_debugf(format, ...) {}
#endif
#define debug_open()  {}
#define debug_close() {}
#endif

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
