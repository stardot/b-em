/* Part of B-Em by Tom Walker */

#include "b-em.h"

#include <errno.h>
#include <stdarg.h>

#ifdef DEBUG
static const char debug_fn[] = "b-emlog.txt";
FILE *debug_fp;

void bem_debug(const char *s)
{
        if (debug_fp)
        {
                fputs(s, debug_fp);
                fflush(debug_fp);
        }
}

void bem_debugf(const char *fmt, ...)
{
        va_list ap;

        if (debug_fp)
        {
                va_start(ap, fmt);
                vfprintf(debug_fp, fmt, ap);
                va_end(ap);
                fflush(debug_fp);
        }
}

#endif

void bem_errorf(const char *fmt, ...)
{
        char buf[256];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);

        bem_error(buf);
        bem_debug(buf);
}

void bem_warn(const char *s)
{
        fputs(s, stderr);
        fputc('\n', stderr);
}

void bem_warnf(const char *fmt, ...)
{
        char buf[256];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);

        bem_warn(buf);
        bem_debug(buf);
}

#ifdef DEBUG

void debug_open()
{
        if ((debug_fp = fopen(debug_fn, "wt")) == NULL)
                bem_warnf("unable to open debug log %s: %s", strerror(errno));
}

void debug_close(void)
{
        if (debug_fp)
                fclose(debug_fp);
}

#else

#undef bem_debug
#undef bem_debugf
#undef debug_open
#undef debug_close
void bem_debug(const char *s) {}
void bem_debugf(const char *format, ...) {}
void debug_open() {}
void debug_close() {}

#endif
