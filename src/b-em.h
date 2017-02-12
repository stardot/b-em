/*B-em v2.2 by Tom Walker
  Main header file*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER

#define inline __inline

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#endif

//#define printf rpclog

void updatewindowsize(int x, int y);

void setejecttext(int drive, char *fn);

void rpclog(const char *format, ...);

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

void bem_error(char *s);

void changetimerspeed(int i);

extern int mousecapture;
