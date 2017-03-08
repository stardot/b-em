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

#if __GNUC__
#define printflike __attribute__((format (printf, 1, 2)))
#else
#define printflike
#endif

extern void log_open(void);
extern void log_close(void);
extern void bem_error(const char *s);
extern void bem_errorf(const char *fmt, ...) printflike;
extern void bem_warn(const char *s);
extern void bem_warnf(const char *fmt, ...) printflike;
extern void win_log_msgbox(const char *level, const char *s);

// If debugging is enabled a real pair of functions will be available
// to log debug messages.  if debug is disabled we use a static inline
// empty function to make the debug calls disappear but in a way that
// doesn't look syntactically different to the compiler.

#ifdef _DEBUG
extern void bem_debug(const char *s);
extern void bem_debugf(const char *format, ...) printflike;
#else
static inline void bem_debug(const char *s) {}
static inline void bem_debugf(const char *format, ...) printflike;
static inline void bem_debugf(const char *format, ...) {}
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
