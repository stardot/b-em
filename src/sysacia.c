#define _DEBUG
/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "tape.h"
#include <poll.h>

int sysacia_tapespeed=0;
static int sysacia_pty = -1;
static int poll_count = 0;

static void sysvia_set_params(ACIA *acia, uint8_t val) {
    switch (val & 3) {
        case 1: sysacia_tapespeed=0; break;
        case 2: sysacia_tapespeed=1; break;
    }
}

static void sysvia_open_pty(void)
{
    int master = posix_openpt(O_RDWR);
    if (master >= 0) {
        log_debug("sysacia: pty master open, fd=%d", master);
        if (!grantpt(master)) {
            log_debug("sysacia: pty grant done");
            if (!unlockpt(master)) {
                log_debug("sysacia: pty unlocked");
                const char *pname = ptsname(master);
                if (pname) {
                    log_debug("sysacia; slave pty is %s", pname);
                    pid_t pid = fork();
                    if (pid == 0) {
                        /* Child process */
                        setsid();
                        int slave = open(pname, O_RDWR);
                        if (slave) {
                            log_debug("sysacia: slave pty fd=%d", slave);
                            if (slave != 0)
                                dup2(slave, 0);
                            if (slave != 1)
                                dup2(slave, 1);
                            if (slave != 2)
                                dup2(slave, 2);
                            if (slave > 2)
                                close(slave);
                            const char *shell = getenv("SHELL");
                            if (!shell)
                                shell = "/bin/sh";
                            putenv("TERM=ansi");
                            execl(shell, shell, NULL);
                            log_error("sysacia: unable to exec %s: %m", shell);
                        }
                    }
                    else if (pid == -1)
                        log_error("sysvia: unable to fork: %m");
                    else {
                        /* Parent process */
                        sysacia_pty = master;
                        return;
                    }
                }
                else
                    log_error("sysacia: unable to get pty name: %m");
            }
            else
                log_error("sysacia: pty unlock failed: %m");
        }
        else
            log_error("sysacia: pty grant failed: %m");
        close(master);
    }
    else
        log_error("sysacia: pty master open failed: %m");
}

static void sysacia_tx_hook(ACIA *acia, uint8_t data)
{
    if (sysacia_pty < 0)
        sysvia_open_pty();
    if (sysacia_pty >= 0) {
        struct pollfd pfd;
        pfd.fd = sysacia_pty;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 0) > 0) {
        log_debug("sysacia: writing character %02X (%c) to pty master", data, (data >= ' ' && data <= '~') ? data : '.');
        write(sysacia_pty, &data, 1);
    }
    else
            log_debug("sysacia: discarding character %02X (%c) as device not ready", data, (data >= ' ' && data <= '~') ? data : '.');
    }
    else
        putchar(data);
}

static void sysacia_poll(ACIA *acia)
{
    if (sysacia_pty >= 0 && !(acia->status_reg & 0x01)) {
        if (++poll_count >= 20) {
            struct pollfd pfd;
            pfd.fd = sysacia_pty;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 0) > 0) {
                uint8_t val;
                if (read(sysacia_pty, &val, 1) == 1) {
                    log_debug("sysacia: read character %02X (%c) to pty master", val, (val >= ' ' && val <= '~') ? val : '.');
                    acia_receive(acia, val);
                }
                poll_count = 0;
            }
        }
    }
}

static void sysacia_tx_end(ACIA *acia) {
    fflush(stdout);
}

ACIA sysacia = {
    .set_params = sysvia_set_params,
    .rx_hook    = tape_receive,
    .tx_hook    = sysacia_tx_hook,
    .tx_end     = sysacia_tx_end,
    .poll_hook  = sysacia_poll
};
