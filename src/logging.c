#define _DEBUG
/* Part of B-Em by Tom Walker */

#include "b-em.h"
#include "config.h"

#include <allegro5/allegro_native_dialog.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#define LOG_DEST_FILE   0x01
#define LOG_DEST_STDERR 0x02
#define LOG_DEST_MSGBOX 0x04

typedef struct {
    uint32_t   mask;
    uint32_t   shift;
    const char *name;
} log_level_t;

static const log_level_t ll_fatal = { 0xf0000, 16, "FATAL"   };
static const log_level_t ll_error = { 0x0f000, 12, "ERROR"   };
static const log_level_t ll_warn  = { 0x00f00,  8, "WARNING" };
static const log_level_t ll_info  = { 0x000f0,  4, "INFO"    };
static const log_level_t ll_debug = { 0x0000f,  0, "DEBUG"   };

static const log_level_t *log_levels[] =
{
    &ll_fatal,
    &ll_error,
    &ll_warn,
    &ll_info,
    &ll_debug,
    NULL
};

static const char log_section[]    = "logging";
static const char log_default_fn[] = "b-emlog";

static unsigned log_options = 0x22222;

static FILE *log_fp;
static char   tmstr[20];
static time_t last = 0;

static void log_msgbox(const char *level, char *msg)
{
    const int max_len = 80;
    char *max_ptr, *new_split, *cur_split;
    ALLEGRO_DISPLAY *display;

    display = al_get_current_display();
    if (strlen(msg) < max_len)
        al_show_native_message_box(display, level, msg, "", NULL, 0);
    else
    {
        max_ptr = msg + max_len;
        cur_split = msg;
        while ((new_split = strchr(cur_split+1, ' ')) && new_split < max_ptr)
            cur_split = new_split;

        if (cur_split > msg)
        {
            *cur_split = '\0';
            al_show_native_message_box(display, level, msg, cur_split+1, NULL, 0);
            *cur_split = ' ';
        }
        else
            al_show_native_message_box(display, level, msg, "", NULL, 0);
    }
}

static void log_common(unsigned dest, const char *level, char *msg, size_t len)
{
    time_t now;

    while (msg[len-1] == '\n')
        len--;
    if ((dest & LOG_DEST_FILE) && log_fp) {
        time(&now);
        if (now != last) {
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
    if (dest & LOG_DEST_MSGBOX)
        log_msgbox(level, msg);
}

static char msg_malloc[] = "log_format: out of space - following message truncated";

static void log_format(const log_level_t *ll, const char *fmt, va_list ap)
{
    unsigned opt, dest;
    va_list apc;
    char   abuf[200], *mbuf;
    size_t len;

    if ((opt = log_options & ll->mask)) {
        dest = opt >> ll->shift;
        va_copy(apc, ap);
        len = vsnprintf(abuf, sizeof abuf, fmt, ap);
        if (len < sizeof abuf)
            log_common(dest, ll->name, abuf, len);
        else if ((mbuf = malloc(len + 1))) {
            vsnprintf(mbuf, len+1, fmt, apc);
            log_common(dest, ll->name, mbuf, len);
            free(mbuf);
        } else {
            log_common(dest, ll->name, msg_malloc, sizeof msg_malloc);
            log_common(dest, ll->name, abuf, len);
        }
    }
}

#ifdef _DEBUG

void log_debug(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(&ll_debug, fmt, ap);
    va_end(ap);
}

static char dmp_malloc[] = "log_dump: out of space, data dump omitted";
static const char xdigs[] = "0123456789ABCDEF";

void log_dump(const char *prefix, uint8_t *data, size_t size)
{
    unsigned opt = log_options & ll_debug.mask;
    if (opt) {
        unsigned dest = opt >> ll_debug.shift;
        size_t pfxlen = strlen(prefix);
        size_t totlen = pfxlen + 64;
        char buf[100], *buffer = buf, *hexbase, *ascbase;
        if (totlen > sizeof(buf)) {
            buffer = malloc(totlen);
            if (!buffer) {
                log_common(dest, ll_debug.name, dmp_malloc, sizeof dmp_malloc);
                return;
            }
        }
        memcpy(buffer, prefix, pfxlen);
        hexbase = buffer + pfxlen;
        ascbase = hexbase + 48;
        while (size >= 16) {
            char *hexptr = hexbase;
            char *ascptr = ascbase;
            for (int i = 0; i < 16; i++) {
                uint8_t byte = *data++;
                *hexptr++ = xdigs[byte >> 4];
                *hexptr++ = xdigs[byte & 0x0f];
                *hexptr++ = ' ';
                if (byte < 0x20 || byte > 0x7e)
                    byte = '.';
                *ascptr++ = byte;
            }
            log_common(dest, ll_debug.name, buffer, totlen);
            size -= 16;
        }
        if (size > 0) {
            char *hexptr = hexbase;
            char *ascptr = ascbase;
            size_t pad = 16 - size;
            do {
                uint8_t byte = *data++;
                *hexptr++ = xdigs[byte >> 4];
                *hexptr++ = xdigs[byte & 0x0f];
                *hexptr++ = ' ';
                if (byte < 0x20 || byte > 0x7e)
                    byte = '.';
                *ascptr++ = byte;
            } while (--size);
            do {
                *hexptr++ = '*';
                *hexptr++ = '*';
                *hexptr++ = ' ';
            } while (--pad);
            log_common(dest, ll_debug.name, buffer, ascptr - buffer);
        }
        if (buffer != buf)
            free(buffer);
    }
}

void log_bitfield(const char *fmt, unsigned value, const char **names)
{
    unsigned opt = log_options & ll_debug.mask;
    if (opt) {
        char buf[128];
        char *ptr = buf;
        char *end = buf + sizeof(buf);
        const char *name;
        bool comma = false;
        for (int i = 0; (name = names[i]); ++i) {
            if (value & 1) {
                size_t nlen = strlen(name);
                char *nptr = ptr + 1 + nlen;
                if (ptr >= end) {
                    log_debug("log: bitfield truncated");
                    break;
                }
                if (comma)
                    *ptr++ = ',';
                memcpy(ptr, name, nlen);
                ptr = nptr;
            }
            value >>= 1;
        }
        *ptr = 0;
        log_debug(fmt, buf);
    }
}

#endif

void log_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(&ll_info, fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(&ll_warn, fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(&ll_error, fmt, ap);
    va_end(ap);
}

void log_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_format(&ll_fatal, fmt, ap);
    va_end(ap);
}

static int contains(const char *haystack, const char *needle)
{
    size_t needle_len;

    needle_len = strlen(needle);
    haystack--;
    do {
        while (*++haystack == ' ')
            ;
        if (strncasecmp(haystack, needle, needle_len) == 0)
            return 1;
        haystack = strchr(haystack, ',');
    } while (haystack);
    return 0;
}

static void log_open_file(void) {
    const char *log_fn;
    ALLEGRO_PATH *path = NULL;
    int append;

    log_fn = get_config_string(log_section, "log_filename", NULL);
    if (!log_fn) {
        if ((path = find_cfg_dest(log_default_fn, ".txt")))
            log_fn = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        else
            log_warn("log_open: unable to find suitable destination for log file");
    }
    if (log_fn) {
        append = get_config_int(log_section, "append", 1);
        if ((log_fp = fopen(log_fn, append ? "at" : "wt")) == NULL)
            log_warn("log_open: unable to open log %s: %s", log_fn, strerror(errno));
    }
    if (path)
        al_destroy_path(path);
}

void log_open(void)
{
    const char *to_file, *to_stderr, *to_msgbox;
    unsigned new_opt;
    const log_level_t **llp, *ll;
    int open_file;

    to_file = get_config_string(log_section, "to_file", "FATAL,ERROR,WARNING,INFO,DEBUG");
    to_stderr = get_config_string(log_section, "to_stderr", "FATAL,ERROR,WARNING");
    to_msgbox = get_config_string(log_section, "to_msgbox", "FATAL,ERROR");
    new_opt = 0;
    open_file = 0;
    for (llp = log_levels; (ll = *llp++); ) {
        if (contains(to_file, ll->name)) {
            new_opt |= (LOG_DEST_FILE << ll->shift);
            open_file = 1;
        }
        if (contains(to_stderr, ll->name))
            new_opt |= (LOG_DEST_STDERR << ll->shift);
        if (contains(to_msgbox, ll->name))
            new_opt |= (LOG_DEST_MSGBOX << ll->shift);
    }
    log_options = new_opt;
    if (open_file)
        log_open_file();
    log_debug("log_open: log options=%x", log_options);
}

void log_close(void)
{
    if (log_fp)
        fclose(log_fp);
}
