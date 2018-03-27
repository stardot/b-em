/*B-em v2.2 by Tom Walker
  Debugger*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#include "cpu_debug.h"
#include "debugger.h"
#include "b-em.h"
#include "main.h"
#include "model.h"
#include "6502.h"

#include <allegro5/allegro_primitives.h>

#define NUM_BREAKPOINTS 8

int debug_core = 0;
int debug_tube = 0;
int debug_step = 0;
int indebug = 0;
extern int fcount;
static int vrefresh = 1;
static FILE *trace_fp = NULL;

static void close_trace()
{
    if (trace_fp) {
        fputs("Trace finished due to emulator quit\n", trace_fp);
        fclose(trace_fp);
    }
}

static ALLEGRO_THREAD  *mem_thread;

static void *mem_thread_proc(ALLEGRO_THREAD *thread, void *data)
{
    ALLEGRO_DISPLAY *mem_disp;
    ALLEGRO_BITMAP *bitmap;
    ALLEGRO_LOCKED_REGION *region;
    int row, col, addr, cnt, red, grn, blu;

    log_debug("debugger: memory view thread started");
    al_set_new_window_title("B-Em Memory View");
    if ((mem_disp = al_create_display(256, 256))) {
        al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);
        if ((bitmap = al_create_bitmap(256, 256))) {
            while (!quitting && !al_get_thread_should_stop(thread)) {
                al_rest(0.02);
                al_set_target_bitmap(bitmap);
                if ((region = al_lock_bitmap(bitmap, ALLEGRO_LOCK_WRITEONLY, ALLEGRO_PIXEL_FORMAT_ANY))) {
                    addr = 0;
                    for (row = 0; row < 256; row++) {
                        for (col = 0; col < 256; col++) {
                            red = grn = blu = 0;
                            if ((cnt = writec[addr])) {
                                red = cnt * 8;
                                writec[addr] = cnt - 1;
                            }
                            if ((cnt = readc[addr])) {
                                grn = cnt * 8;
                                readc[addr] = cnt - 1;
                            }
                            if ((cnt = fetchc[addr])) {
                                blu = cnt * 8;
                                fetchc[addr] = cnt - 1;
                            }
                            al_put_pixel(col, row, al_map_rgb(red, grn, blu));
                            addr++;
                        }
                    }
                    al_unlock_bitmap(bitmap);
                    al_set_target_backbuffer(mem_disp);
                    al_draw_bitmap(bitmap, 0.0, 0.0, 0);
                    al_flip_display();
                }
            }
            al_destroy_bitmap(bitmap);
        }
        al_destroy_display(mem_disp);
    }
    return NULL;
}

static void debug_memview_open(void)
{
    if (!mem_thread) {
        if ((mem_thread = al_create_thread(mem_thread_proc, NULL))) {
            log_debug("debugger: memory view thread created");
            al_start_thread(mem_thread);
        }
        else
            log_error("debugger: failed to create memory view thread");
    }
}

static void debug_memview_close(void)
{
    if (mem_thread) {
        al_join_thread(mem_thread, NULL);
        mem_thread = NULL;
    }
}

#ifdef WIN32
#include <windows.h>
#include <wingdi.h>
#include <process.h>

static int debug_cons = 0;
static HANDLE cinf, consf;

static inline void debug_in(char *buf, size_t bufsize)
{
    int c;
    DWORD len;

    if ((c = ReadConsole(cinf, buf, bufsize, &len, NULL))) {
        buf[len] = 0;
        log_debug("debugger: read console, len=%d, s=%s", (int)len, buf);
    } else
        log_error("debugger: unable to read from console: %lu", GetLastError());
}

static void debug_out(const char *s, size_t len)
{
    WriteConsole(consf, s, len, NULL, NULL);
}

static void debug_outf(const char *fmt, ...)
{
    va_list ap;
    char s[256];
    size_t len;

    va_start(ap, fmt);
    len = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    WriteConsole(consf, s, len, NULL, NULL);
}

LRESULT CALLBACK DebugWindowProcedure (HWND, UINT, WPARAM, LPARAM);
static HINSTANCE hinst;

BOOL CtrlHandler(DWORD fdwCtrlType)
{
    main_setquit();
    return TRUE;
}

static void debug_cons_open(void)
{
    int c;

    if (debug_cons++ == 0)
    {
        hinst = GetModuleHandle(NULL);
        if ((c = AllocConsole())) {
            SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
            consf = GetStdHandle(STD_OUTPUT_HANDLE);
            cinf  = GetStdHandle(STD_INPUT_HANDLE);
        } else {
            log_fatal("debugger: unable to allocate console: %lu", GetLastError());
            exit(1);
        }
    }
}

static void debug_cons_close(void)
{
    if (--debug_cons == 0)
        FreeConsole();
}

#else

static inline void debug_in(char *buf, size_t bufsize)
{
    fgets(buf, bufsize, stdin);
}

static void debug_out(const char *s, size_t len)
{
    fwrite(s, len, 1, stdout);
    fflush(stdout);
}

static void debug_outf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static inline void debug_cons_open(void) {}

static inline void debug_cons_close(void) {}

#endif

#include <errno.h>
#include "6502.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "sn76489.h"
#include "model.h"

void debug_kill()
{
    close_trace();
    debug_memview_close();
    debug_cons_close();
}

static void enable_core_debug(void)
{
    debug_cons_open();
    debug_memview_open();
    debug_step = 1;
    debug_core = 1;
    log_info("debugger: debugging of core 6502 enabled");
    core6502_cpu_debug.debug_enable(1);
}

static void enable_tube_debug(void)
{
    if (curtube != -1)
    {
        debug_cons_open();
        debug_step = 1;
        debug_tube = 1;
        log_info("debugger: debugging of tube CPU enabled");
        tubes[curtube].debug->debug_enable(1);
    }
}

static void disable_core_debug(void)
{
    core6502_cpu_debug.debug_enable(0);
    log_info("debugger: debugging of core 6502 disabled");
    debug_core = 0;
    debug_memview_close();
    debug_cons_close();
}

static void disable_tube_debug(void)
{
    if (curtube != -1)
    {
        tubes[curtube].debug->debug_enable(0);
        log_info("debugger: debugging of tube CPU disabled");
        debug_tube = 0;
        debug_cons_close();
    }
}

void debug_start(void)
{
    if (debug_core)
        enable_core_debug();
    if (debug_tube)
        enable_tube_debug();
}

void debug_end(void)
{
    if (debug_tube)
        disable_tube_debug();
    if (debug_core)
        disable_core_debug();
}

void debug_toggle_core(void)
{
    if (debug_core)
        disable_core_debug();
    else
        enable_core_debug();
}

void debug_toggle_tube(void)
{
    if (debug_tube)
        disable_tube_debug();
    else
        enable_tube_debug();
}

int readc[65536], writec[65536], fetchc[65536];

static uint32_t debug_memaddr=0;
static uint32_t debug_disaddr=0;
static uint8_t  debug_lastcommand=0;

static int tbreak = -1;
static int breakpoints[NUM_BREAKPOINTS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakr[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakw[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breaki[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breako[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchr[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchw[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchi[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watcho[NUM_BREAKPOINTS]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int contcount = 0;

void debug_reset()
{
    if (trace_fp) {
        fputs("Processor reset!\n", trace_fp);
        fflush(trace_fp);
    }
}

static const char helptext[] =
    "\n    Debugger commands :\n\n"
    "    bclear n   - clear breakpoint n or breakpoint at n\n"
    "    bclearr n  - clear read breakpoint n or read breakpoint at n\n"
    "    bclearw n  - clear write breakpoint n or write breakpoint at n\n"
    "    bcleari n  - clear input breakpoint n or input breakpoint at n\n"
    "    bclearo n  - clear output breakpoint n or output breakpoint at n\n"
    "    blist      - list current breakpoints\n"
    "    break n    - set a breakpoint at n\n"
    "    breakr n   - break on reads from address n\n"
    "    breakw n   - break on writes to address n\n"
    "    breaki n   - break on input from I/O port\n"
    "    breako n   - break on output to I/O port\n"
    "    c          - continue running until breakpoint\n"
    "    c n        - continue until the nth breakpoint\n"
    "    d [n]      - disassemble from address n\n"
    "    n          - step, but treat a called subroutine as one step\n"
    "    m [n]      - memory dump from address n\n"
    "    q          - force emulator exit\n"
    "    r          - print 6502 registers\n"
    "    r sysvia   - print System VIA registers\n"
    "    r uservia  - print User VIA registers\n"
    "    r crtc     - print CRTC registers\n"
    "    r vidproc  - print VIDPROC registers\n"
    "    r sound    - print Sound registers\n"
    "    reset      - reset emulated machine\n"
    "    s [n]      - step n instructions (or 1 if no parameter)\n"
    "    trace fn   - trace disassembly/registers to file, close file if no fn\n"
    "    vrefresh t - extra video refresh on entering debugger.  t=on or off\n"
    "    watchr n   - watch reads from address n\n"
    "    watchw n   - watch writes to address n\n"
    "    watchi n   - watch inputs from port n\n"
    "    watcho n   - watch outputs to port n\n"
    "    wclearr n  - clear read watchpoint n or read watchpoint at n\n"
    "    wclearw n  - clear write watchpoint n or write watchpoint at n\n"
    "    wcleari n  - clear input watchpoint n or input watchpoint at n\n"
    "    wclearo n  - clear output watchpoint n or output watchpoint at n\n"
    "    wlist      - list watchpoints\n"
    "    writem a v - write to memory, a = address, v = value\n";

static void print_registers(cpu_debug_t *cpu) {
    const char **np, *name;
    char buf[50];
    size_t len;
    int r;

    for (r = 0, np = cpu->reg_names; (name = *np++); ) {
        debug_outf(" %s=", name);
        len = cpu->reg_print(r++, buf, sizeof buf);
        debug_out(buf, len);
    }
    debug_out("\n", 1);
}

static void set_point(int *table, char *arg, const char *desc)
{
    int c;

    if (*arg) {
        for (c = 0; c < NUM_BREAKPOINTS; c++) {
            if (table[c] == -1) {
                sscanf(arg, "%X", &table[c]);
                debug_outf("    %s %i set to %04X\n", desc, c, table[c]);
                return;
            }
        }
        debug_outf("    unable to set %s breakpoint, table full\n", desc);
    } else
        debug_out("    missing parameter\n!", 24);
}

static void clear_point(int *table, char *arg, const char *desc)
{
    int c, e;

    if (*arg) {
        sscanf(arg, "%X", &e);
        for (c = 0; c < 8; c++) {
            if (table[c] == e || c == e) {
                debug_outf("    %s %i at %04X cleared\n", desc, c, table[c]);
                table[c] = -1;
            }
        }
    } else
        debug_out("    missing parameter\n!", 24);
}

static void list_points(int *table, const char *desc)
{
    int c;

    for (c = 0; c < NUM_BREAKPOINTS; c++)
        if (table[c] != -1)
            debug_outf("    %s %i : %04X\n", desc, c, table[c]);
}

void debugger_do(cpu_debug_t *cpu, uint32_t addr)
{
    int c, d, e, f;
    uint8_t temp;
    uint32_t next_addr;
    char dump[256], *dptr;
    char ins[256], *iptr, *cmd, *eptr;

    main_pause();
    indebug = 1;
    log_debug("debugger: about to call disassembler, addr=%04X", addr);
    next_addr = cpu->disassemble(addr, ins, sizeof ins);
    debug_out(ins, strlen(ins));
    if (vrefresh)
        video_poll(CLOCKS_PER_FRAME, 0);

    while (1) {
        debug_out(">", 1);
        debug_in(ins, 255);
        // Skip past any leading spaces.
        for (iptr = ins; (c = *iptr) && isspace(c); iptr++);
        if (c) {
            cmd = iptr;
            // Find the first space and terminate command name.
            while (c && !isspace(c))
                c = *++iptr;
            *iptr = '\0';
            // Skip past any separating spaces.
            while (c && isspace(c))
                c = *++iptr;
            // iptr now points to the parameter.
        } else {
            cmd = iptr = ins;
            *iptr++ = debug_lastcommand;
            *iptr = '\0';
        }
        switch (*cmd) {
            case 'b':
            case 'B':
                if (!strcasecmp(cmd, "breaki"))
                    set_point(breaki, iptr, "Input breakpoint");
                else if (!strcasecmp(cmd, "breako"))
                    set_point(breako, iptr, "Output breakpoint");
                else if (!strcasecmp(cmd, "breakr"))
                    set_point(breakr, iptr, "Read breakpoint");
                else if (!strcasecmp(cmd, "breakw"))
                    set_point(breakw, iptr, "Write breakpoint");
                else if (!strcasecmp(cmd, "break"))
                    set_point(breakpoints, iptr, "Breakpoint");
                else if (!strcasecmp(ins, "blist")) {
                    list_points(breakpoints, "Breakpoint");
                    list_points(breakr, "Read breakpoint");
                    list_points(breakw, "Write breakpoint");
                    list_points(breaki, "Input breakpoint");
                    list_points(breako, "Output breakpoint");
                } else if (!strcasecmp(cmd, "bcleari"))
                    clear_point(breaki, iptr, "Input breakpoint");
                else if (!strcasecmp(cmd, "bclearo"))
                    clear_point(breako, iptr, "Output breakpoint");
                else if (!strcasecmp(cmd, "bclearr"))
                    clear_point(breakr, iptr, "Read breakpoint");
                else if (!strcasecmp(cmd, "bclearw"))
                    clear_point(breakw, iptr, "Write breakpoint");
                else if (!strcasecmp(ins, "bclear"))
                    clear_point(breakpoints, iptr, "Breakpoint");
                break;

            case 'q':
            case 'Q':
                main_setquit();
                /* FALLTHOUGH */

            case 'c':
            case 'C':
                if (*iptr)
                    sscanf(iptr, "%d", &contcount);
                debug_lastcommand = 'c';
                indebug = 0;
                main_resume();
                return;

            case 'd':
            case 'D':
                if (*iptr)
                    sscanf(iptr, "%X", (unsigned int *)&debug_disaddr);
                for (c = 0; c < 12; c++) {
                    debug_out("    ", 4);
                    debug_disaddr = cpu->disassemble(debug_disaddr, ins, sizeof ins);
                    debug_out(ins, strlen(ins));
                    debug_out("\n", 1);
                }
                debug_lastcommand = 'd';
                break;

            case 'h':
            case 'H':
            case '?':
                debug_out(helptext, sizeof helptext - 1);
                break;

            case 'm':
            case 'M':
                if (*iptr)
                    sscanf(iptr, "%X", (unsigned int *)&debug_memaddr);
                for (c = 0; c < 16; c++) {
                    debug_outf("    %04X : ", debug_memaddr);
                    for (d = 0; d < 16; d++)
                        debug_outf("%02X ", cpu->memread(debug_memaddr + d));
                    debug_out("  ", 2);
                    dptr = dump;
                    for (d = 0; d < 16; d++) {
                        temp = cpu->memread(debug_memaddr + d);
                        if (temp < ' ' || temp >= 0x7f)
                            *dptr++ = '.';
                        else
                            *dptr++ = temp;
                    }
                    *dptr++ = '\n';
                    debug_out(dump, dptr - dump);
                    debug_memaddr += 16;
                }
                debug_lastcommand = 'm';
                break;

            case 'n':
            case 'N':
                tbreak = next_addr;
                debug_lastcommand = 'n';
                indebug = 0;
                main_resume();
                return;

            case 'r':
            case 'R':
                if (!strcasecmp(cmd, "reset")) {
                    main_reset();
                    debug_outf("Emulator reset\n");
                } else if (*iptr) {
                    if (!strncasecmp(iptr, "sysvia", 6)) {
                        debug_outf("    System VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", sysvia.ora, sysvia.orb, sysvia.ira, sysvia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", sysvia.ddra, sysvia.ddrb, sysvia.acr, sysvia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", sysvia.t1l / 2, (sysvia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", sysvia.t2l / 2, (sysvia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", sysvia.ier, sysvia.ifr);
                    } else if (!strncasecmp(iptr, "uservia", 7)) {
                        debug_outf("    User VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", uservia.ora, uservia.orb, uservia.ira, uservia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", uservia.ddra, uservia.ddrb, uservia.acr, uservia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", uservia.t1l / 2, (uservia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", uservia.t2l / 2, (uservia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", uservia.ier, uservia.ifr);
                    } else if (!strncasecmp(iptr, "crtc", 4)) {
                        debug_outf("    CRTC registers :\n");
                        debug_outf("    Index=%i\n", crtc_i);
                        debug_outf("    R0 =%02X  R1 =%02X  R2 =%02X  R3 =%02X  R4 =%02X  R5 =%02X  R6 =%02X  R7 =%02X  R8 =%02X\n", crtc[0], crtc[1], crtc[2], crtc[3], crtc[4], crtc[5], crtc[6], crtc[7], crtc[8]);
                        debug_outf("    R9 =%02X  R10=%02X  R11=%02X  R12=%02X  R13=%02X  R14=%02X  R15=%02X  R16=%02X  R17=%02X\n", crtc[9], crtc[10], crtc[11], crtc[12], crtc[13], crtc[14], crtc[15], crtc[16], crtc[17]);
                        debug_outf("    VC=%i SC=%i HC=%i MA=%04X\n", vc, sc, hc, ma);
                    }
                    if (!strncasecmp(iptr, "vidproc", 7)) {
                        debug_outf("    VIDPROC registers :\n");
                        debug_outf("    Control=%02X\n", ula_ctrl);
                        debug_outf("    Palette entries :\n");
                        debug_outf("     0=%01X   1=%01X   2=%01X   3=%01X   4=%01X   5=%01X   6=%01X   7=%01X\n", ula_palbak[0], ula_palbak[1], ula_palbak[2], ula_palbak[3], ula_palbak[4], ula_palbak[5], ula_palbak[6], ula_palbak[7]);
                        debug_outf("     8=%01X   9=%01X  10=%01X  11=%01X  12=%01X  13=%01X  14=%01X  15=%01X\n", ula_palbak[8], ula_palbak[9], ula_palbak[10], ula_palbak[11], ula_palbak[12], ula_palbak[13], ula_palbak[14], ula_palbak[15]);
                        debug_outf("    NULA palette :\n");
                        debug_outf("     0=%06X   1=%06X   2=%06X   3=%06X   4=%06X   5=%06X   6=%06X   7=%06X\n", nula_collook[0], nula_collook[1], nula_collook[2], nula_collook[3], nula_collook[4], nula_collook[5], nula_collook[6], nula_collook[7]);
                        debug_outf("     8=%06X   9=%06X  10=%06X  11=%06X  12=%06X  13=%06X  14=%06X  15=%06X\n", nula_collook[8], nula_collook[9], nula_collook[10], nula_collook[11], nula_collook[12], nula_collook[13], nula_collook[14], nula_collook[15]);
                        debug_outf("    NULA flash :\n");
                        debug_outf("     %01X%01X%01X%01X%01X%01X%01X%01X\n", nula_flash[0], nula_flash[1], nula_flash[2], nula_flash[3], nula_flash[4], nula_flash[5], nula_flash[6], nula_flash[7]);
                        debug_outf("    NULA registers :\n");
                        debug_outf("     Palette Mode=%01X  Horizontal Offset=%01X  Left Blank Size=%01X  Disable=%01X  Attribute Mode=%01X  Attribute Text=%01X\n", nula_palette_mode, nula_horizontal_offset, nula_left_blank, nula_disable, nula_attribute_mode, nula_attribute_text);
                    }
                    if (!strncasecmp(iptr, "sound", 5)) {
                        debug_outf("    Sound registers :\n");
                        debug_outf("    Voice 0 frequency = %04X   volume = %i  control = %02X\n", sn_latch[0] >> 6, sn_vol[0], sn_noise);
                        debug_outf("    Voice 1 frequency = %04X   volume = %i\n", sn_latch[1] >> 6, sn_vol[1]);
                        debug_outf("    Voice 2 frequency = %04X   volume = %i\n", sn_latch[2] >> 6, sn_vol[2]);
                        debug_outf("    Voice 3 frequency = %04X   volume = %i\n", sn_latch[3] >> 6, sn_vol[3]);
                    }
                } else {
                    debug_outf("    registers for %s\n", cpu->cpu_name);
                    print_registers(cpu);
                }
                break;

            case 's':
            case 'S':
                if (*iptr)
                    sscanf(iptr, "%i", &debug_step);
                else
                    debug_step = 1;
                debug_lastcommand = 's';
                indebug = 0;
                main_resume();
                return;

            case 't':
            case 'T':
                if (trace_fp)
                    fclose(trace_fp);
                if (*iptr) {
                    if ((eptr = strchr(iptr, '\n')))
                        *eptr = '\0';
                    if ((trace_fp = fopen(iptr, "a")))
                        debug_outf("Tracing to %s\n", iptr);
                    else
                        debug_outf("Unable to open trace file '%s' for append: %s\n", iptr, strerror(errno));
                } else
                    debug_outf("Trace file closed");
                break;

            case 'v':
            case 'V':
                if (!strcasecmp(cmd, "vrefresh")) {
                    if (*iptr) {
                        if (!strncasecmp(iptr, "on", 2)) {
                            debug_outf("Extra video refresh enabled\n");
                            vrefresh = 1;
                            video_poll(CLOCKS_PER_FRAME, 0);
                        } else if (!strncasecmp(iptr, "off", 3)) {
                            debug_outf("Extra video refresh disabled\n");
                            vrefresh = 0;
                        }
                    }
                }
                break;

            case 'w':
            case 'W':
                if (!strcasecmp(cmd, "watchr"))
                    set_point(watchr, iptr, "Read watchpoint");
                else if (!strcasecmp(cmd, "watchw"))
                    set_point(watchw, iptr, "Write watchpoint");
                else if (!strcasecmp(cmd, "watchi"))
                    set_point(watchi, iptr, "Input watchpoint");
                else if (!strcasecmp(cmd, "watcho"))
                    set_point(watcho, iptr, "Output watchpoint");
                else if (!strcasecmp(cmd, "wlist")) {
                    list_points(watchr, "Read watchpoint");
                    list_points(watchw, "Write watchpoint");
                    list_points(watchi, "Input watchpoint");
                    list_points(watcho, "Output watchpoint");
                }
                if (!strcasecmp(cmd, "wclearr"))
                    clear_point(watchr, iptr, "Read watchpoint");
                else if (!strcasecmp(cmd, "wclearw"))
                    clear_point(watchw, iptr, "Write watchpoint");
                else if (!strcasecmp(cmd, "wcleari"))
                    clear_point(watchi, iptr, "Input watchpoint");
                else if (!strcasecmp(cmd, "wclearo"))
                    clear_point(watcho, iptr, "Output watchpoint");
                else if (!strcasecmp(cmd, "writem")) {
                    if (*iptr) {
                        sscanf(iptr, "%X %X", &e, &f);
                        log_debug("debugger: writem %04X %04X\n", e, f);
                        cpu->memwrite(e, f);
                        if (cpu == &core6502_cpu_debug && vrefresh)
                            video_poll(CLOCKS_PER_FRAME, 0);
                    }
                }
                break;
        }
    }
    fcount = 0;
    indebug = 0;
    main_resume();
}

static inline void check_points(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size, int *break_tab, int *watch_tab, const char *desc)
{
    int c;
    uint32_t iaddr;

    for (c = 0; c < NUM_BREAKPOINTS; c++) {
        if (break_tab[c] == addr) {
            iaddr = cpu->get_instr_addr();
            debug_outf("cpu %s: %04X: break on %s %04X, value=%X\n", cpu->cpu_name, iaddr, desc, addr, value);
            debugger_do(cpu, iaddr);
        }
        if (watch_tab[c] == addr) {
            iaddr = cpu->get_instr_addr();
            debug_outf("cpu %s: %04X: %s %04X, value=%0*X\n", cpu->cpu_name, iaddr, desc, addr, size*2, value);
        }
    }
}

void debug_memread (cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, breakr, watchr, "read from");
}

void debug_memwrite(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, breakw, watchw, "write to");
}

void debug_ioread (cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, breaki, watchi, "input from");
}

void debug_iowrite(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, breako, watcho, "output to");
}

void debug_preexec (cpu_debug_t *cpu, uint32_t addr) {
    char buf[256];
    size_t len;
    int c, r, enter = 0;
    const char **np, *name;

    if (trace_fp) {
        cpu->disassemble(addr, buf, sizeof buf);
        fputs(buf, trace_fp);
        *buf = ' ';

        for (r = 0, np = cpu->reg_names; (name = *np++);) {
            len = cpu->reg_print(r++, buf + 1, sizeof buf - 1);
            fwrite(buf, len + 1, 1, trace_fp);
        }
        putc('\n', trace_fp);
    }

    if (addr == tbreak) {
        log_debug("debugger; enter for CPU %s on tbreak at %04X", cpu->cpu_name, addr);
        enter = 1;
    }
    else {
        for (c = 0; c < NUM_BREAKPOINTS; c++) {
            if (breakpoints[c] == addr) {
                debug_outf("cpu %s: Break at %04X\n", cpu->cpu_name, addr);
                if (contcount) {
                    contcount--;
                    return;
                }
                log_debug("debugger; enter for CPU %s on breakpoint at %04X", cpu->cpu_name, addr);
                enter = 1;
            }
        }
        if (debug_step) {
            debug_step--;
            if (debug_step)
                return;
            log_debug("debugger; enter for CPU %s on single-step at %04X", cpu->cpu_name, addr);
            enter = 1;
        }
    }
    if (enter) {
        tbreak = -1;
        debugger_do(cpu, addr);
    }
}

void debug_trap(cpu_debug_t *cpu, uint32_t addr, int reason)
{
    const char *desc = cpu->trap_names[reason];
    debug_outf("cpu %s: %s at %04X\n", cpu->cpu_name, desc, addr);
    debugger_do(cpu, addr);
}
