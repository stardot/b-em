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

extern int find_dat_file(char *path, size_t psize, const char *subdir, const char *name, const char *ext);
extern int find_cfg_file(char *path, size_t psize, const char *name, const char *ext);
extern int find_cfg_dest(char *path, size_t psize, const char *name, const char *ext);

void updatewindowsize(int x, int y);

void setejecttext(int drive, const char *fn);

extern int joybutton[2];

void setquit();

#ifdef WIN32

void startblit();
void endblit();

void cataddname(char *s);
void showcatalogue();

#else

static inline void startblit(void) {};
static inline void endblit(void) {};
static inline void cataddname(char *s) {};
static inline void showcatalogue() {};

#endif

extern int autoboot;


void redefinekeys();

void changetimerspeed(int i);

extern int mousecapture;

#endif
