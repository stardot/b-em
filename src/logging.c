/* Part of B-Em by Tom Walker */

#include <allegro.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#include "b-em.h"

#define LOG_DEST_FILE   0x01
#define LOG_DEST_MSGBOX 0x02

static int		 log_pri = -1;
static const char	*log_fn = "b-em-log.txt";
static FILE		*log_fp;

static const char	*ll_names[] = {
	"INFO",
	"WARN",
	"ERROR",
	"DEBUG"
};


void bem_log(enum log_level ll, const char *m_line, ...)
{
	char		*fmt;
	char		 date[2048];
	time_t		 raw_time;
	struct tm	*ti;
	va_list		 ap;

	if (log_fp == NULL)
		return;

	/* Log-levels are cumulative.  Don't log anything higher than what
	 * we've asked for.
	 */
	if (ll > log_pri)
		return;

#ifndef _DEBUG
	if (ll == LOG_DEBUG)
		return;
#endif

	va_start(ap, m_line);

	if (vasprintf(&fmt, m_line, ap) == -1)
		exit (-1);

	/* Windows doesn't have gettimeofday().  Dance around this by looking
	 * up the localtime().
	 */
	raw_time = time(0);
	time(&raw_time);
	ti = localtime(&raw_time);
	strftime(date, sizeof date, "%d/%m/%Y %H:%M:%S", ti);

	fprintf(log_fp, "%s: [%s]: %s\n", date, ll_names[ll], fmt);
	fflush(log_fp);

	free(fmt);
}

void log_open(void)
{
	char		 log_ll[1024];
	int		 i = 0, pos = 0;

	log_pri = get_config_int(NULL, "logging", log_pri);

	if (log_pri < LOG_INFO || log_pri > LOG_DEBUG)
		return;

	log_close();
	log_fp = x_fopen(log_fn, "at");

	bem_log(LOG_INFO, "log open: %s", log_fn);

	for (i = 0; i <= log_pri; i++)
		pos += snprintf(&log_ll[pos], sizeof(log_ll), "%s,", ll_names[i]);
	log_ll[strlen(log_ll) - 1] = '\0';

	bem_log(LOG_INFO, "log level: %d - includes: [%s]", log_pri, log_ll);
}

void log_close(void)
{
    if (log_fp)
	fclose(log_fp);
}
