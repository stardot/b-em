/* Part of B-Em by Tom Walker */

#include "b-em.h"

#include <allegro.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#define LOG_DEST_FILE   0x01
#define LOG_DEST_STDERR 0x02
#define LOG_DEST_MSGBOX 0x04

#define LOG_DEBUG_MASK  0x000f
#define LOG_DEBUG_SHIFT 0
#define LOG_INFO_MASK   0x00f0
#define LOG_INFO_SHIFT  4
#define LOG_WARN_MASK   0x0f00
#define LOG_WARN_SHIFT  8
#define LOG_ERROR_MASK  0xf000
#define LOG_ERROR_SHIFT 12
#define LOG_FATAL_MASK  0xf0000
#define LOG_FATAL_SHIFT 16

static unsigned log_options =
    (LOG_DEST_FILE << LOG_DEBUG_SHIFT) |
    (LOG_DEST_FILE << LOG_INFO_SHIFT)  |
    ((LOG_DEST_FILE|LOG_DEST_STDERR) << LOG_WARN_SHIFT)  |
    ((LOG_DEST_FILE|LOG_DEST_MSGBOX) << LOG_ERROR_SHIFT) |
    ((LOG_DEST_FILE|LOG_DEST_MSGBOX) << LOG_FATAL_SHIFT);

static const char log_fn[] = "b-emlog.txt";
FILE *log_fp;

static char   tmstr[20];
static time_t last = 0;

static void log_common(unsigned dest, const char *level, const char *msg, size_t len)
{
    time_t now;

    while (msg[len-1] == '\n')
	len--;
    if ((dest & LOG_DEST_FILE) && log_fp) {
	time(&now);
	if (now != last)
	{
	    strftime(tmstr, sizeof(tmstr), "%d/%m/%Y %H:%M:%S", localtime(&now));
	    last = now;
	}
	fprintf(log_fp, "%s %s ", tmstr, level); 
	fwrite(msg, len, 1, log_fp);
	putc('\n', log_fp);
	fflush(log_fp);
    }
    if (dest & LOG_DEST_STDERR) {
	fwrite(msg, len, 1, stderr);
	putc('\n', stderr);
    }
    if (dest & LOG_DEST_MSGBOX) {
#ifdef WIN32
	log_win_msgbox(level, msg);
#else
	alert(level, msg, "", "&OK", NULL, 'a', 0);
#endif
    }
}

static const char msg_malloc[] = "log_format: out of space - following message truncated";

static void log_format(unsigned mask, unsigned shift, const char *level, const char *fmt, va_list ap)
{
    unsigned opt, dest;
    char   abuf[200], *mbuf;
    size_t len;

    if ((opt = log_options & mask))
    {
	dest = opt >> shift;
	len = vsnprintf(abuf, sizeof abuf, fmt, ap);
	if (len <= sizeof abuf)
	    log_common(dest, level, abuf, len);
	else if ((mbuf = malloc(len + 1))) {
	    vsnprintf(mbuf, len, fmt, ap);
	    log_common(dest, level, mbuf, len);
	    free(mbuf);
	} else {
	    log_common(dest, level, msg_malloc, sizeof msg_malloc);
	    log_common(dest, level, abuf, len);
	}
    }
}

#ifdef _DEBUG

void log_debug(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(LOG_DEBUG_MASK, LOG_DEBUG_SHIFT, "DEBUG", fmt, ap);
    va_end(ap);
}

#endif

void log_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(LOG_INFO_MASK, LOG_INFO_SHIFT, "INFO", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(LOG_WARN_MASK, LOG_WARN_SHIFT, "WARNING", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(LOG_ERROR_MASK, LOG_ERROR_SHIFT, "ERROR", fmt, ap);
    va_end(ap);
}

void log_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(LOG_FATAL_MASK, LOG_FATAL_SHIFT, "FATAL", fmt, ap);
    va_end(ap);
}

void log_open(void)
{
    log_options = get_config_int(NULL, "logging", log_options);
    if ((log_fp = fopen(log_fn, "at")) == NULL)
	log_warn("log_open: unable to open log %s: %s", log_fn, strerror(errno));
    log_debug("log_open: log options=%d", log_options);
}

void log_close(void)
{
    if (log_fp)
	fclose(log_fp);
}
