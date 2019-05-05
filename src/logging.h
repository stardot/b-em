#ifndef __INCLUDE_LOGGING_HEADER__
#define __INCLUDE_LOGGING_HEADER__

#if __GNUC__
#define printflike __attribute__((format (printf, 1, 2)))
#else
#define printflike
#endif

extern void log_open(void);
extern void log_close(void);
extern void log_fatal(const char *fmt, ...) printflike;
extern void log_error(const char *fmt, ...) printflike;
extern void log_warn(const char *fmt, ...) printflike;
extern void log_info(const char *fmt, ...) printflike;

// If the debugging compilation option is enabled a real function will
// be available to log debug messages.  If the debugging compilation
// optionis disabled we use a static inline empty function to make the
// debug calls disappear but in a way that does not generate warnings
// about unused variables etc.

#ifdef _DEBUG
extern void log_debug(const char *format, ...) printflike;
#else
static inline void log_debug(const char *format, ...) printflike;
static inline void log_debug(const char *format, ...) {}
#endif

#endif
