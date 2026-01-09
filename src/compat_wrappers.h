/* B-em */

#ifndef __INC_COMPAT_WRAPPERS_H__
#define __INC_COMPAT_WRAPPERS_H__

#include "b-em.h"

#ifdef WIN32
#undef HAVE_STPCPY
#endif

#ifndef HAVE_STPCPY
extern char *stpcpy(char *dest, const char *src);
#endif

#endif
