#ifndef __INC_MAIN_H
#define __INC_MAIN_H

#define NUM_EMU_SPEEDS   10
#define EMU_SPEED_FULL   255
#define EMU_SPEED_PAUSED 254

typedef struct {
    const char *name;
    float timer_interval;
    int fskipmax;
} emu_speed_t;

extern const emu_speed_t emu_speeds[NUM_EMU_SPEEDS];
extern int emuspeed;
extern int framesrun;

extern bool quitting;
extern bool keydefining;

void main_init(int argc, char *argv[]);
void main_softreset(void);
void main_reset(void);
void main_restart(void);
void main_run(void);
void main_close(void);
void main_pause(void);
void main_resume(void);
void main_setspeed(int speed);
void main_setquit(void);
void main_start_fullspeed(void);
void main_stop_fullspeed(bool hostshift);

void main_key_break(void);
void main_key_pause(void);

void main_cleardrawit(void);
void main_setmouse(void);

#endif
