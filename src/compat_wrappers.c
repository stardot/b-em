/* B-em */
/*
 * Standard wrappers around system calls,
 */

#include "compat_wrappers.h"

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
