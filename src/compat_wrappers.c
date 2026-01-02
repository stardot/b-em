/* B-em */
/*
 * Standard wrappers around system calls,
 */

#include "compat_wrappers.h"

#include <sys/types.h>

#ifndef HAVE_STPCPY
char *stpcpy(char *dest, const char *src)
{
    int c;

    while ((c = *src++))
        *dest++ = c;
    *dest = 0;
    return dest;
}
#endif

#ifndef HAVE_FTRUNCATE
#ifdef WIN32
#include <io.h>

int ftruncate(int fd, off_t length) {
    return _chsize(fd, length);
}
#endif
#endif
