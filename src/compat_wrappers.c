/* B-em */
/*
 * Standard wrappers around system calls,
 */

#include "compat_wrappers.h"

FILE *
x_fopen(const char *path, const char *mode)
{
	/* Check to see if the path exists.  If it does, return the resultant
	 * FILE * pointer, otherwise bail out.
	 */

	FILE	*fp = NULL;
	char	*err;

	if (path == NULL || path[0] == '\0')
		return (NULL);

	if ((fp = fopen(path, mode)) == NULL) {
		if ((asprintf(&err, "Failed to load '%s' - %s\n", path,
			strerror(errno))) == -1) {

			exit(-1);
		}
		bem_error(err);
		free(err);
		exit(-1);
	}

	return (fp);
}

#ifndef HAVE_ASPRINTF
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

int
asprintf(char **ret, const char *fmt, ...)
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = vasprintf(ret, fmt, ap);
	va_end(ap);

	return (n);
}

int
vasprintf(char **ret, const char *fmt, va_list ap)
{
	int	 n;
	va_list  ap2;

	va_copy(ap2, ap);

	if ((n = vsnprintf(NULL, 0, fmt, ap)) < 0)
		goto error;

	if ((*ret = malloc(n + 1)) == NULL) {
		fprintf(stderr, "malloc failed: %s\n", strerror(errno));
	}

	if ((n = vsnprintf(*ret, n + 1, fmt, ap2)) < 0) {
		free(*ret);
		goto error;
	}
	va_end(ap2);

	return (n);

error:
	va_end(ap2);
	*ret = NULL;
	return (-1);
}
#endif
