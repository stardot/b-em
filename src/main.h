#ifndef __INC_MAIN_H
#define __INC_MAIN_H

#define EMU_SPEED_FULL   255
#define EMU_SPEED_PAUSED 254

/* TOHv3: exit codes */
#define SHUTDOWN_OK              0
/* main_init() calls exit(1) on e.g. bad command-line args,
   or allegro init failure, so code 1 is reserved: */
#define SHUTDOWN_STARTUP_FAILURE 1
/* sh seems to return 2 on syntax error?? so reserve that too.
   In fact let's just start at 10. */
#define SHUTDOWN_TAPE_ERROR      10 /* if shutdown-on-tape-error enabled    */
#define SHUTDOWN_TAPE_EOF        11 /* if shutdown-on-tape-finished enabled */
#define SHUTDOWN_BREAKPOINT_0    12 /* Codes 12-19 are reserved for breakpoints 0-7 */
#define SHUTDOWN_BREAKPOINT_1    13
#define SHUTDOWN_BREAKPOINT_2    14
#define SHUTDOWN_BREAKPOINT_3    15
#define SHUTDOWN_BREAKPOINT_4    16
#define SHUTDOWN_BREAKPOINT_5    17
#define SHUTDOWN_BREAKPOINT_6    18
#define SHUTDOWN_BREAKPOINT_7    19
#define SHUTDOWN_BREAKPOINT_X    20 /* for breakpoints >=8; no specificity, one generic code */
#define SHUTDOWN_EXPIRED         21 /* -expire option shut down emulator */
#define SHUTDOWN_FOPEN           22 /* TOHv4.1: uniquely identify file-not-found */

typedef struct {
    const char *name;
    float multiplier;
    int fskipmax;
} emu_speed_t;

extern const emu_speed_t *emu_speeds;
extern int num_emu_speeds;
extern int emuspeed;
extern int framesrun;

extern bool quitting;
extern bool keydefining;
extern bool autopause;
extern bool autoskip;
extern bool skipover;
extern unsigned hiresdisplay;

/* TOHv3: although C exit code is an int, Unix shells don't safely allow
   you to use values > 125, so this is limited to a signed 8-bit value >:( */
extern int8_t shutdown_exit_code;
void main_init(int argc, char *argv[]);
void main_softreset(void);
void main_reset(void);
void main_restart(void);
void main_run(void);
void main_close(void);
void main_pause(const char *why);
void main_resume(void);
void main_setspeed(int speed);
void main_start_fullspeed(void);
void main_stop_fullspeed(bool hostshift);

void main_key_break(void);
void main_key_pause(void);

void main_cleardrawit(void);
void main_setmouse(void);

void set_quit(void);
void set_shutdown_exit_code (uint8_t c); /* TOHv3 */

#endif
