#ifndef __INC_MAIN_H
#define __INC_MAIN_H

#define EMU_SPEED_FULL   255
#define EMU_SPEED_PAUSED 254

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

void main_init(int argc, char *argv[]);
void main_softreset(void);
void main_reset(void);
void main_restart(void);
void main_run(void);
void main_close(void);
void main_pause(const char *why);
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
