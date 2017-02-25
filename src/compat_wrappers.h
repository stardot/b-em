/* B-em */

#ifndef __INC_COMPAT_WRAPPERS_H__
#define __INC_COMPAT_WRAPPERS_H__

#include <string.h>
#include <errno.h>

#include "b-em.h"

FILE *x_fopen(const char *, const char *);

#ifndef HAVE_ASPRINTF
int asprintf(char **, const char *, ...);
#endif

#ifndef HAVE_TDESTROY
void tdestroy(void *, void (*)(void *));
#endif

#ifndef HAVE_STPCPY
extern char *stpcpy(char *dest, const char *src);
#endif

#endif
