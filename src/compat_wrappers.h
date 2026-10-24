/* B-em */

#ifndef __INC_COMPAT_WRAPPERS_H__
#define __INC_COMPAT_WRAPPERS_H__

#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

#ifdef _MSC_VER

#define inline __inline

#define pclose _pclose
#define popen  _popen

#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#define alloca _alloca

#define flockfile       _lock_file
#define funlockfile     _unlock_file
#define getc_unlocked   _getc_nolock
#define putc_unlocked   _putc_nolock
#define fread_unlocked  _fread_nolock
#define fwrite_unlocked _fwrite_nolock
#define fflush_unlocked _fflush_nolock

#else
#ifdef WIN32

#define flockfile       _lock_file
#define funlockfile     _unlock_file
#define getc_unlocked   _getc_nolock
#define putc_unlocked   _putc_nolock
#if __MSVCRT_VERSION__ >= 0x800
#define fread_unlocked  _fread_nolock
#define fwrite_unlocked _fwrite_nolock
#define fflush_unlocked _fflush_nolock
#else
#define fread_unlocked  fread
#define fwrite_unlocked fwrite
#define fflush_unlocked fflush
#endif

#else
#ifdef __APPLE__

#define fread_unlocked  fread
#define fwrite_unlocked fwrite
#define fflush_unlocked fflush

#else
#ifdef __OpenBSD__

#define fread_unlocked  fread
#define fwrite_unlocked fwrite
#define fflush_unlocked fflush

#endif
#endif
#endif
#endif

// check if we are not using configure
#ifndef PACKAGE_VERSION
#ifdef _MSC_VER

#undef HAVE_STPCPY
#undef HAVE_FTRUNCATE
#undef HAVE_UNISTD_H
#define HAVE_STDINT_H 1

#else
#ifdef WIN32

#undef HAVE_STPCPY
#define HAVE_FTRUNCATE 1
#define HAVE_UNISTD_H 1
#define HAVE_STDINT_H 1

#endif
#endif
#endif

#ifndef HAVE_STPCPY
extern char *stpcpy(char *dest, const char *src);
#endif

#ifndef HAVE_FTRUNCATE
extern int ftruncate(int fd, off_t length);
#endif

#endif
