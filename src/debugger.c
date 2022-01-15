/*B-em v2.2 by Tom Walker
  Debugger*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cpu_debug.h"
#include "debugger.h"
#include "b-em.h"
#include "main.h"
#include "mem.h"
#include "model.h"
#include "6502.h"
#include "debugger_symbols.h"

#include <allegro5/allegro_primitives.h>

typedef enum {
    BREAK_EXEC,
    BREAK_READ,
    BREAK_WRITE,
    BREAK_INPUT,
    BREAK_OUTPUT,
    WATCH_EXEC,
    WATCH_READ,
    WATCH_WRITE,
    WATCH_INPUT,
    WATCH_OUTPUT
} break_type;

static const char break_names[][11] = {
    "breakpoint",
    "breakr",
    "breakw",
    "breaki",
    "breako",
    "watch",
    "watchr",
    "watchw",
    "watchi",
    "watcho"
};

struct breakpoint {
    breakpoint *next;
    uint32_t   address;
    break_type type;
    int        num;
};

int debug_core = 0;
int debug_tube = 0;
int debug_step = 0;
int indebug = 0;
extern int fcount;
static int vrefresh = 1;
static FILE *trace_fp = NULL;

struct file_stack;

struct filestack {
    struct filestack *next;
    FILE *fp;
} *exec_stack;

static int breakpseq = 0;
static int contcount = 0;

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
                if ((region = al_lock_bitmap(bitmap, ALLEGRO_PIXEL_FORMAT_ANY, ALLEGRO_LOCK_WRITEONLY))) {
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

static inline bool debug_in(char *buf, size_t bufsize)
{
    DWORD len;

    if (ReadConsole(cinf, buf, bufsize, &len, NULL)) {
        buf[len] = 0;
        log_debug("debugger: read console, len=%d, s=%s", (int)len, buf);
        return true;
    } else {
        log_error("debugger: unable to read from console: %lu", GetLastError());
        return false;
    }
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

static inline bool debug_in(char *buf, size_t bufsize)
{
    return fgets(buf, bufsize, stdin);
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


static void push_exec(const char *fn)
{
    struct filestack *fs = malloc(sizeof(struct filestack));
    if (fs) {
        if ((fs->fp = fopen(fn, "r"))) {
            fs->next = exec_stack;
            exec_stack = fs;
        }
        else
            debug_outf("unable to open exec file '%s': %s\n", fn, strerror(errno));
    }
    else
        debug_outf("out of memory for exec file\n");
}

static void pop_exec(void)
{
    struct filestack *cur = exec_stack;
    fclose(cur->fp);
    exec_stack = cur->next;
    free(cur);
}

void debug_start(const char *exec_fn)
{
    if (debug_core)
        enable_core_debug();
    if (debug_tube)
        enable_tube_debug();
    if (exec_fn)
        push_exec(exec_fn);
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
    "    exec f     - take commands from file f\n"
    "    n          - step, but treat a called subroutine as one step\n"
    "    m [n]      - memory dump from address n\n"
    "    paste s    - paste string s as keyboard input\n"
    "    q          - force emulator exit\n"
    "    r          - print 6502 registers\n"
    "    r sysvia   - print System VIA registers\n"
    "    r uservia  - print User VIA registers\n"
    "    r crtc     - print CRTC registers\n"
    "    r vidproc  - print VIDPROC registers\n"
    "    r sound    - print Sound registers\n"
    "    reset      - reset emulated machine\n"
    "    rset r v   - set a CPU register\n"
    "    s [n]      - step n instructions (or 1 if no parameter)\n"
    "    symbol name=[rom:]addr\n"
    "               - add debugger symbol\n"
    "    symlist    - list all symbols\n"
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

static char xdigs[] = "0123456789ABCDEF";

size_t debug_print_8bit(uint32_t value, char *buf, size_t bufsize)
{
    if (bufsize >= 3) {
        buf[2] = 0;
        buf[0] = xdigs[(value >> 4) & 0x0f];
        buf[1] = xdigs[value & 0x0f];
    }
    return 3;
}

size_t debug_print_16bit(uint32_t value, char *buf, size_t bufsize)
{
    if (bufsize >= 5) {
        buf[4] = 0;
        for (int i = 3; i >= 0; i--) {
            buf[i] = xdigs[value & 0x0f];
            value >>= 4;
        }
    }
    return 5;
}

size_t debug_print_32bit(uint32_t value, char *buf, size_t bufsize)
{
    if (bufsize >= 9) {
        buf[8] = 0;
        for (int i = 7; i >= 0; i--) {
            buf[i] = xdigs[value & 0x0f];
            value >>= 4;
        }
    }
    return 9;
}

size_t debug_print_addr16(cpu_debug_t *cpu, uint32_t value, char *buf, size_t bufsize, bool include_symbol) {
    const char *sym = NULL;
    size_t ret;
    if (!include_symbol || !symbol_find_by_addr(cpu->symbols, value, &sym))
        sym = NULL;
    if (sym) {
        ret = snprintf(buf, bufsize, "%04X (%s)", value, sym);
    }
    else
        ret = snprintf(buf, bufsize, "%04X", value);

    if (ret > bufsize)
        return bufsize;
    else
        return ret;
}

size_t debug_print_addr32(cpu_debug_t *cpu, uint32_t value, char *buf, size_t bufsize, bool include_symbol) {
    const char *sym = NULL;
    size_t ret;
    if (!include_symbol || !symbol_find_by_addr(cpu->symbols, value, &sym))
        sym = NULL;
    if (sym) {
        ret = snprintf(buf, bufsize, "%08X (%s)", value, sym);
    }
    else
        ret = snprintf(buf, bufsize, "%08X", value);

    if (ret > bufsize)
        return bufsize;
    else
        return ret;
}

uint32_t debug_parse_addr(cpu_debug_t *cpu, const char *buf, const char **end)
{
    return strtoul(buf, (char **)end, 16);
}

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

static uint32_t parse_address_or_symbol(cpu_debug_t *cpu, const char *arg, const char **endret) {

    uint32_t a;
    //first see if there is a symbol
    if (symbol_find_by_name(cpu->symbols, arg, &a, endret))
        return a;
    return cpu->parse_addr(cpu, arg, endret);
}

static void set_sym(cpu_debug_t *cpu, const char *arg) {
    if (!cpu->symbols)
        cpu->symbols = symbol_new();
    if (cpu->symbols) {

        int n;
        char name[SYM_MAX + 1], rest[SYM_MAX + 1];

        n = sscanf(arg, "%" STRINGY(SYM_MAX) "[^= ] = %" STRINGY(SYM_MAX) "s", name, rest);
        const char *e;
        uint32_t addr;
        if (n == 2)
            addr = parse_address_or_symbol(cpu, rest, &e);


        if (n == 2 && e != rest && strlen(name)) {
            char abuf[17];
            cpu->print_addr(cpu, addr, abuf, 16, false);

            symbol_add(cpu->symbols, name, addr);

            debug_outf("SYMBOL %s set to %s\n", name, abuf);

        }
        else {
            debug_outf("Bad command\n");
        }
    }
    else {
        debug_outf("no symbol table\n");
    }

}

static void list_syms(cpu_debug_t *cpu, const char *arg) {
    symbol_list(cpu->symbols, cpu, &debug_outf);
}

static void set_point(cpu_debug_t *cpu, break_type type, char *arg, const char *desc)
{
    const char *end1;
    uint32_t a = parse_address_or_symbol(cpu, arg, &end1);
    if (end1 > arg) {
        char addrbuf[16 + SYM_MAX];
        cpu->print_addr(cpu, a, addrbuf, sizeof(addrbuf), true);
        for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
            if (bp->address == a && bp->type == type) {
                debug_outf("    %s %i already set to %s\n", desc, bp->num, addrbuf);
                return;
            }
        }
        breakpoint *bp = malloc(sizeof(breakpoint));
        if (bp) {
            bp->next = cpu->breakpoints;
            bp->address = a;
            bp->type = type;
            bp->num = breakpseq++;
            cpu->breakpoints = bp;
            debug_outf("    %s %i set to %s\n", desc, bp->num, addrbuf);
        }
        else
            debug_outf("    unable to set %s breakpoint, out of memory\n", desc);
    }
    else
        debug_outf("    '%s' is not a valid address\n", arg);
}

static void clear_point(cpu_debug_t *cpu, break_type type, char *arg, const char *desc)
{
    const char *end1;
    uint32_t a = parse_address_or_symbol(cpu, arg, &end1);
    if (end1 > arg) {
        //DB: changed this to search by address first then by index.
        breakpoint *found = NULL;
        breakpoint *prev = NULL;
        for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
            if (bp->address == a && bp->type == type) {
                found = bp;
                break;
            }
            prev = bp;
        }
        if (!found) {
            prev = NULL;
            for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
                if (bp->num == a && bp->type == type) {
                    found = bp;
                    break;
                }
                prev = bp;
            }
            if (!found) {
                debug_outf("    Can't find that breakpoint\n");
                return;
            }
        }
        if (prev)
            prev->next = found->next;
        else
            cpu->breakpoints = found->next;
        char addrbuf[16 + SYM_MAX];
        cpu->print_addr(cpu, found->address, addrbuf, sizeof(addrbuf), true);
        debug_outf("    %s %i at %s cleared\n", desc, found->num, addrbuf);
        free(found);
    }
    else
        debug_outf("    '%s' is not a valid address or number\n", arg);
}

static void list_points(cpu_debug_t *cpu, break_type type, const char *desc)
{
    char addr_buf[17 + SYM_MAX];
    for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
        if (bp->type == type) {
            cpu->print_addr(cpu, bp->address, addr_buf, sizeof(addr_buf), true);
            debug_outf("    %s %i : %-8s\n", desc, bp->num, addr_buf);
        }
    }
}

static void debug_paste(const char *iptr)
{
    int ch;
    char *str, *dptr;

    if ((ch = *iptr++)) {
        if ((str = al_malloc(strlen(iptr) + 1))) {
            dptr = str;
            do {
                if (ch == '|') {
                    if (!(ch = *iptr++))
                        break;
                    if (ch != '|')
                        ch &= 0x1f;
                }
                *dptr++ = ch;
                ch = *iptr++;
            } while (ch);
            *dptr = '\0';
            os_paste_start(str);
        }
    }
}

static void debugger_save(cpu_debug_t *cpu, char *iptr)
{
    breakpoint *bp = cpu->breakpoints;
    if (bp) {
        char *eptr = strchr(iptr, '\n');
        if (eptr)
            *eptr = '\0';
        FILE *sfp = fopen(iptr, "w");
        if (sfp) {
            do {
                char addr_buf[17 + SYM_MAX];
                cpu->print_addr(cpu, bp->address, addr_buf, sizeof(addr_buf), true);
                fprintf(sfp, "%s %s\n", break_names[bp->type], addr_buf);
                bp = bp->next;
            }
            while (bp);
            debug_outf("Breakpoints for CPU %s saved to %s\n", cpu->cpu_name, iptr);
            fclose(sfp);
        }
        else
            debug_outf("unable to open '%s' for writing: %s\n", strerror(errno));
    }
    else
        debug_outf("Nothing to save (no breakpoints)\n");
}

void trimnl(char *buf) {
    int len = strlen(buf);

    while (len >= 1 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
        buf[--len] = '\0';

}

typedef enum {
    SWS_GROUND,
    SWS_GOT_SQUARE,
    SWS_GOT_CURLY,
    SWS_IN_NAME,
    SWS_TOO_LONG,
    SWS_NAME_END,
    SWS_IN_VALUE,
    SWS_AWAIT_COMMA
} swstate;

static void swiftsym(cpu_debug_t *cpu, char *iptr)
{
    uint32_t romaddr = 0;
    char *sep = strpbrk(iptr, " \t");
    if (sep) {
        *sep++ = 0;
        romaddr = strtoul(sep, NULL, 16) << 28;
    }
    FILE *fp = fopen(iptr, "r");
    if (fp) {
        swstate state = SWS_GROUND;
        char name[80], *name_ptr, *name_end = name+sizeof(name);
        uint32_t addr;
        int ch, syms = 0;
        if (!cpu->symbols)
            cpu->symbols = symbol_new();
        while ((ch = getc(fp)) != EOF) {
            switch(state) {
                case SWS_GROUND:
                    if (ch == '[')
                        state = SWS_GOT_SQUARE;
                    break;
                case SWS_GOT_SQUARE:
                    if (ch == '{')
                        state = SWS_GOT_CURLY;
                    else if (ch != '[')
                        state = SWS_GROUND;
                    break;
                case SWS_GOT_CURLY:
                    if (ch == '\'') {
                        name_ptr = name;
                        state = SWS_IN_NAME;
                    }
                    else if (!strchr(" \t\r\n", ch))
                        state = SWS_GROUND;
                    break;
                case SWS_IN_NAME:
                    if (ch == '\'') {
                        *name_ptr = 0;
                        state = SWS_NAME_END;
                    }
                    else if (name_ptr >= name_end) {
                        debug_outf("swift import name too long");
                        state = SWS_TOO_LONG;
                    }
                    else
                        *name_ptr++ = ch;
                    break;
                case SWS_TOO_LONG:
                    if (ch == '\'') {
                        name_ptr = 0;
                        state = SWS_NAME_END;
                    }
                    break;
                case SWS_NAME_END:
                    if (ch == ':') {
                        addr = 0;
                        state = SWS_IN_VALUE;
                    }
                    else if (!strchr(" \t\r\n", ch))
                        state = SWS_GROUND;
                    break;
                case SWS_IN_VALUE:
                    if (ch >= '0' && ch <= '9')
                        addr = addr * 10 + ch - '0';
                    else if (ch == 'L') {
                        symbol_add(cpu->symbols, name, addr|romaddr);
                        state = SWS_AWAIT_COMMA;
                        syms++;
                    }
                    else if (ch == ',') {
                        symbol_add(cpu->symbols, name, addr|romaddr);
                        state = SWS_GOT_CURLY;
                        syms++;
                    }
                    else
                        state = SWS_GROUND;
                    break;
                case SWS_AWAIT_COMMA:
                    if (ch == ',')
                        state = SWS_GOT_CURLY;
                    else if (!strchr(" \t\r\n", ch))
                        state = SWS_GROUND;
            }
        }
        fclose(fp);
        debug_outf("%d symbols loaded from %s\n", syms, iptr);
    }
    else
        debug_outf("unable to open '%s': %s\n", iptr, strerror(errno));
}

static void debugger_dumpmem(cpu_debug_t *cpu, const char *iptr, int rows)
{
    if (*iptr) {
        const char *e;
        debug_memaddr = parse_address_or_symbol(cpu, iptr, &e);
    }
    for (int row = 0; row < rows; row++) {
        char dump[256], *dptr;
        cpu->print_addr(cpu, debug_memaddr, dump, sizeof(dump), false);
        debug_outf("%s : ", dump);
        for (int d = 0; d < 16; d++)
            debug_outf("%02X ", cpu->memread(debug_memaddr + d));
        debug_out("  ", 2);
        dptr = dump;
        for (int d = 0; d < 16; d++) {
            uint32_t temp = cpu->memread(debug_memaddr + d);
            if (temp < ' ' || temp >= 0x7f)
                *dptr++ = '.';
            else
                *dptr++ = temp;
        }
        *dptr++ = '\n';
        debug_out(dump, dptr - dump);
        debug_memaddr += 16;
    }
}

typedef struct {
    uint32_t start;
    unsigned count;
} prof_pair;

static int prof_cmp(const void *a, const void *b)
{
    const prof_pair *pa = a;
    const prof_pair *pb = b;
    int d = pb->count - pa->count;
    if (d == 0)
        d = pa->start - pb->start;
    return d;
}

static prof_pair *debugger_profcount(cpu_debug_t *cpu, unsigned *nitem)
{
    unsigned *c_ptr = cpu->prof_counts;
    unsigned *c_end = c_ptr + (cpu->prof_end - cpu->prof_start);
    unsigned items = 0;
    while (c_ptr < c_end)
        if (*c_ptr++)
            ++items;
    log_debug("debugger: %u profile items counted", items);
    prof_pair *pairs = malloc(items * sizeof(prof_pair));
    if (pairs) {
        prof_pair *p_ptr = pairs;
        unsigned addr = cpu->prof_start;
        c_ptr = cpu->prof_counts;
        while (c_ptr < c_end) {
            unsigned count = *c_ptr++;
            if (count > 0) {
                p_ptr->start = addr;
                p_ptr->count = count;
                ++p_ptr;
            }
            ++addr;
        }
        *nitem = items;
        qsort(pairs, items, sizeof(prof_pair), prof_cmp);
    }
    else
        debug_outf("out of memory processing profile entries");
    return pairs;
}

static void debugger_profile(cpu_debug_t *cpu, const char *iptr)
{
    if (cpu->prof_counts) {
        if (!strcasecmp(iptr, "stop")) {
            free(cpu->prof_counts);
            cpu->prof_counts = NULL;
            cpu->prof_start = 0;
            cpu->prof_end = 0;
        }
        else if (!strcasecmp(iptr, "reset")) {
            unsigned *ptr = cpu->prof_counts;
            unsigned *end = ptr + (cpu->prof_end - cpu->prof_start);
            while (ptr < end)
                *ptr++ = 0;
        }
        else if (!strcasecmp(iptr, "print")) {
            unsigned nitem;
            prof_pair *pairs = debugger_profcount(cpu, &nitem);
            if (pairs) {
                prof_pair *ptr = pairs;
                prof_pair *end = pairs + nitem;
                while (ptr < end) {
                    char addr_buf[17 + SYM_MAX];
                    cpu->print_addr(cpu, ptr->start, addr_buf, sizeof(addr_buf), true);
                    debug_outf("%s %u\n", addr_buf, ptr->count);
                    ++ptr;
                }
                free(pairs);
            }
        }
        else if (!strncasecmp(iptr, "file", 4)) {
            iptr += 4;
            while (isspace(*iptr))
                ++iptr;
            FILE *fp = fopen(iptr, "w");
            if (fp) {
                unsigned nitem;
                prof_pair *pairs = debugger_profcount(cpu, &nitem);
                if (pairs) {
                    prof_pair *ptr = pairs;
                    prof_pair *end = pairs + nitem;
                    while (ptr < end) {
                        char addr_buf[17 + SYM_MAX];
                        cpu->print_addr(cpu, ptr->start, addr_buf, sizeof(addr_buf), true);
                        fprintf(fp, "%s %u\n", addr_buf, ptr->count);
                        ++ptr;
                    }
                    free(pairs);
                }
                fclose(fp);
                debug_outf("profile stats written to %s\n", iptr);
            }
            else
                debug_outf("unable to open %s for writing: %s\n", iptr, strerror(errno));
        }
        else
            debug_outf("unrecognised sub-command: stop, reset, print, file available\n");
    }
    else {
        const char *end1;
        uint32_t startaddr = parse_address_or_symbol(cpu, iptr, &end1);
        if (end1 > iptr) {
            const char *end2;
            uint32_t endaddr = parse_address_or_symbol(cpu, end1, &end2);
            if (end2 > end1) {
                unsigned *ptr = malloc((endaddr - startaddr) * sizeof(unsigned));
                if (ptr) {
                    unsigned *end = ptr + (endaddr - startaddr);
                    char addr_buf_s[17 + SYM_MAX], addr_buf_e[17 + SYM_MAX];
                    cpu->prof_counts = ptr;
                    while (ptr < end)
                        *ptr++ = 0;
                    cpu->prof_start = startaddr;
                    cpu->prof_end = endaddr;
                    cpu->print_addr(cpu, startaddr, addr_buf_s, sizeof(addr_buf_s), true);
                    cpu->print_addr(cpu, endaddr, addr_buf_e, sizeof(addr_buf_e), true);
                    debug_outf("profiling for cpu %s from %s to %s\n", cpu->cpu_name, addr_buf_s, addr_buf_e);
                    return;
                }
                else {
                    debug_outf("out of memory enabling profiling");
                    return;
                }
            }
        }
        debug_outf("missing address\nUsage: profile <start-addr> <end-addr>\n");
    }
}

static void debugger_ruler(const char *iptr)
{
    unsigned start = 0;
    unsigned count = 16;
    if (*iptr) {
        char *end;
        start = strtoul(iptr, &end, 0);
        if (end > iptr) {
            iptr = end;
            if (*iptr)
                count = strtoul(iptr, &end, 0);
        }
    }
    if (count) {
        debug_out("      ", 6);
        while (count--)
            debug_outf(" %02X", start++);
        debug_out("\n", 1);
    }
}

static const char err_norname[] = "missing register name\n";
static const char err_novalue[] = "missing value\n";

static void debugger_rset(cpu_debug_t *cpu, const char *iptr)
{
    if (*iptr) {
        const char *eptr = iptr;
        int ch = *eptr;
        while (ch && ch != ' ' && ch != '\t')
            ch = *++eptr;
        size_t namelen = eptr - iptr;
        while (ch == ' ' || ch == '\t')
            ch = *++eptr;
        if (!ch) {
            debug_out(err_novalue, sizeof(err_novalue)-1);
            return;
        }
        int regno = 0;
        const char *rname = cpu->reg_names[regno];
        while (rname) {
            if (!strncasecmp(rname, iptr, namelen)) {
                cpu->reg_parse(regno, eptr);
                return;
            }
            rname = cpu->reg_names[++regno];
        }
        debug_outf("unkown register '%.*s'\n", namelen, iptr);
    }
    else
        debug_out(err_norname, sizeof(err_norname)-1);
}

static int hexdig(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

static const char err_noaddr[] = "missing address\n";

static void debugger_writemem(cpu_debug_t *cpu, const char *iptr)
{
    const char *end;
    uint32_t addr = cpu->parse_addr(cpu, iptr, &end);
    if (end > iptr) {
        iptr = end;
        int ch = *iptr;
        while (ch) {
            while (ch == ' ' || ch == '\t')
                ch = *++iptr;
            if (ch == '"' || ch == '\'') {
                int quote = ch;
                ch = *++iptr;
                while (ch && ch != quote) {
                    cpu->memwrite(addr++, ch);
                    ch = *++iptr;
                }
            }
            else {
                int value = hexdig(ch);
                if (value >= 0) {
                    ch = *++iptr;
                    int val2 = hexdig(ch);
                    if (val2 >= 0) {
                        value = (value << 4) | val2;
                        ch = *++iptr;
                    }
                    cpu->memwrite(addr++, value);
                }
                else {
                    debug_outf("bad hex '%s'\n", iptr);
                    break;
                }
            }
        }
        if (cpu == &core6502_cpu_debug && vrefresh)
            video_poll(CLOCKS_PER_FRAME, 0);
    }
    else
        debug_out(err_noaddr, sizeof(err_noaddr)-1);
}

void debugger_do(cpu_debug_t *cpu, uint32_t addr)
{
    uint32_t next_addr;
    char ins[256];

    main_pause("debugging");
    indebug = 1;
    const char *sym;
    if (symbol_find_by_addr(cpu->symbols, addr, &sym)) {
        debug_outf("%s:\n", sym);
    }
    log_debug("debugger: about to call disassembler, addr=%04X", addr);
    next_addr = cpu->disassemble(cpu, addr, ins, sizeof ins);
    debug_out(ins, strlen(ins));
    if (vrefresh)
        video_poll(CLOCKS_PER_FRAME, 0);

    for (;;) {
        char *iptr, *cmd;
        size_t cmdlen;
        int c;
        bool badcmd = false;

        if (exec_stack) {
            if (!fgets(ins, sizeof ins, exec_stack->fp)) {
                pop_exec();
                continue;
            }
        }
        else {
            debug_out(">", 1);
            if (!debug_in(ins, 255)) {
                static const char msg[] = "\nTreating EOF on console as 'continue'\n";
                debug_out(msg, sizeof(msg)-1);
                indebug = 0;
                main_resume();
                return;
            }
        }

        trimnl(ins);
        // Skip past any leading spaces.
        for (iptr = ins; (c = *iptr) && isspace(c); iptr++);
        if (c) {
            cmd = iptr;
            // Find the first space and terminate command name.
            while (c && !isspace(c)) {
                *iptr++ = tolower(c);
                c = *iptr;
            }
            *iptr = '\0';
            cmdlen = iptr - cmd;
            // Skip past any separating spaces.
            while (c && isspace(c))
                c = *++iptr;
            // iptr now points to the parameter.
        } else {
            cmd = iptr = ins;
            *iptr++ = debug_lastcommand;
            *iptr = '\0';
            cmdlen = 1;
        }
        switch (*cmd) {
            case '#':
                /* do nothing - for comments in exec file */
                break;
            case 'b':
                if (!strncmp(cmd, "break", cmdlen))
                    set_point(cpu, BREAK_EXEC, iptr, "Breakpoint");
                else if (!strncmp(cmd, "breaki", cmdlen))
                    set_point(cpu, BREAK_INPUT, iptr, "Input breakpoint");
                else if (!strncmp(cmd, "breako", cmdlen))
                    set_point(cpu, BREAK_OUTPUT, iptr, "Output breakpoint");
                else if (!strncmp(cmd, "breakr", cmdlen))
                    set_point(cpu, BREAK_READ, iptr, "Read breakpoint");
                else if (!strncmp(cmd, "breakw", cmdlen))
                    set_point(cpu, BREAK_WRITE, iptr, "Write breakpoint");
                else if (!strncmp(ins, "blist", cmdlen)) {
                    list_points(cpu, BREAK_EXEC, "Breakpoint");
                    list_points(cpu, BREAK_READ, "Read breakpoint");
                    list_points(cpu, BREAK_WRITE, "Write breakpoint");
                    list_points(cpu, BREAK_INPUT, "Input breakpoint");
                    list_points(cpu, BREAK_OUTPUT, "Output breakpoint");
                }
                else if (!strncmp(ins, "bclear", cmdlen))
                    clear_point(cpu, BREAK_EXEC, iptr, "Breakpoint");
                else if (!strncmp(cmd, "bcleari", cmdlen))
                    clear_point(cpu, BREAK_INPUT, iptr, "Input breakpoint");
                else if (!strncmp(cmd, "bclearo", cmdlen))
                    clear_point(cpu, BREAK_OUTPUT, iptr, "Output breakpoint");
                else if (!strncmp(cmd, "bclearr", cmdlen))
                    clear_point(cpu, BREAK_READ, iptr, "Read breakpoint");
                else if (!strncmp(cmd, "bclearw", cmdlen))
                    clear_point(cpu, BREAK_WRITE, iptr, "Write breakpoint");
                else
                    badcmd = true;
                break;

            case 'q':
                main_setquit();
                /* FALLTHOUGH */

            case 'c':
                if (*iptr)
                    sscanf(iptr, "%d", &contcount);
                debug_lastcommand = 'c';
                indebug = 0;
                main_resume();
                return;

            case 'd':
                if (*iptr) {
                    const char *e;
                    debug_disaddr = parse_address_or_symbol(cpu, iptr, &e);
                }
                for (c = 0; c < 12; c++) {
                    const char *sym;
                    if (symbol_find_by_addr(cpu->symbols, debug_disaddr, &sym)) {
                        debug_outf("%s:\n", sym);
                    }
                    debug_out("    ", 4);
                    debug_disaddr = cpu->disassemble(cpu, debug_disaddr, ins, sizeof ins);
                    debug_out(ins, strlen(ins));
                    debug_out("\n", 1);
                }
                debug_lastcommand = 'd';
                break;
            case 'e':
                if (!strncmp(cmd, "exec", cmdlen)) {
                    if (*iptr) {
                        char *eptr = strchr(iptr, '\n');
                        if (eptr)
                            *eptr = 0;
                        push_exec(iptr);
                    }
                }
                else
                    badcmd = true;
                break;

            case 'h':
            case '?':
                debug_out(helptext, sizeof helptext - 1);
                break;

            case 'm':
                debugger_dumpmem(cpu, iptr, cmd[1] == 'b' ? 1 : 16);
                debug_lastcommand = 'm';
                break;

            case 'n':
                cpu->tbreak = next_addr;
                debug_lastcommand = 'n';
                indebug = 0;
                main_resume();
                return;

            case 'p':
                if (!strncmp(cmd, "paste", cmdlen))
                    debug_paste(iptr);
                else if (!strncmp(cmd, "profile", cmdlen))
                    debugger_profile(cpu, iptr);
                else
                    badcmd = true;
                break;

            case 'r':
                if (cmdlen >= 3 && !strncmp(cmd, "reset", cmdlen)) {
                    main_reset();
                    debug_outf("Emulator reset\n");
                }
                else if (cmdlen >= 2 && !strncmp(cmd, "rset", cmdlen))
                    debugger_rset(cpu, iptr);
                else if (cmdlen >= 2 && !strncmp(cmd, "ruler", cmdlen))
                    debugger_ruler(iptr);
                else if (*iptr) {
                    size_t arglen = strcspn(iptr, " \t\n");
                    iptr[arglen] = 0;
                    if (!strncasecmp(iptr, "sysvia", arglen)) {
                        debug_outf("    System VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", sysvia.ora, sysvia.orb, sysvia.ira, sysvia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", sysvia.ddra, sysvia.ddrb, sysvia.acr, sysvia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", sysvia.t1l / 2, (sysvia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", sysvia.t2l / 2, (sysvia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", sysvia.ier, sysvia.ifr);
                    }
                    else if (!strncasecmp(iptr, "uservia", arglen)) {
                        debug_outf("    User VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", uservia.ora, uservia.orb, uservia.ira, uservia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", uservia.ddra, uservia.ddrb, uservia.acr, uservia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", uservia.t1l / 2, (uservia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", uservia.t2l / 2, (uservia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", uservia.ier, uservia.ifr);
                    }
                    else if (!strncasecmp(iptr, "crtc", arglen)) {
                        debug_outf("    CRTC registers :\n");
                        debug_outf("    Index=%i\n", crtc_i);
                        debug_outf("    R0 =%02X  R1 =%02X  R2 =%02X  R3 =%02X  R4 =%02X  R5 =%02X  R6 =%02X  R7 =%02X  R8 =%02X\n", crtc[0], crtc[1], crtc[2], crtc[3], crtc[4], crtc[5], crtc[6], crtc[7], crtc[8]);
                        debug_outf("    R9 =%02X  R10=%02X  R11=%02X  R12=%02X  R13=%02X  R14=%02X  R15=%02X  R16=%02X  R17=%02X\n", crtc[9], crtc[10], crtc[11], crtc[12], crtc[13], crtc[14], crtc[15], crtc[16], crtc[17]);
                        debug_outf("    VC=%i SC=%i HC=%i MA=%04X\n", vc, sc, hc, ma);
                    }
                    else if (!strncasecmp(iptr, "vidproc", arglen)) {
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
                    else if (!strncasecmp(iptr, "sound", arglen)) {
                        debug_outf("    Sound registers :\n");
                        debug_outf("    Voice 0 frequency = %04X   volume = %i  control = %02X\n", sn_latch[0] >> 6, sn_vol[0], sn_noise);
                        debug_outf("    Voice 1 frequency = %04X   volume = %i\n", sn_latch[1] >> 6, sn_vol[1]);
                        debug_outf("    Voice 2 frequency = %04X   volume = %i\n", sn_latch[2] >> 6, sn_vol[2]);
                        debug_outf("    Voice 3 frequency = %04X   volume = %i\n", sn_latch[3] >> 6, sn_vol[3]);
                    }
                    else if (!strncasecmp(iptr, "ram", arglen))
                       debug_outf("    System RAM registers :\n    ROMSEL=%02X ACCCON=%02X(%s%s%s%s %c%c%c%c)\n    ram1k=%02X ram4k=%02X ram8k=%02X vidbank=%02X\n", ram_fe30, ram_fe34, (ram_fe34 & 0x80) ? "IRR" : "---", (ram_fe34 & 0x40) ? "TST" : "---", (ram_fe34 & 0x20) ? "IFJ" : "---", (ram_fe34 & 0x10) ? "ITU" : "---", (ram_fe34 & 0x08) ? 'Y' : '-', (ram_fe34 & 0x04) ? 'X' : '-', (ram_fe34 & 0x02) ? 'E' : '-', (ram_fe34 & 0x01) ? 'D' : '-', ram1k, ram4k, ram8k, vidbank);
                    else
                        debug_outf("Register set %s not known\n", iptr);
                } else {
                    debug_outf("    registers for %s\n", cpu->cpu_name);
                    print_registers(cpu);
                }
                break;

            case 's':
                if (cmdlen > 1) {
                    if (!strncmp(cmd, "swiftsym", cmdlen)) {
                        if (iptr)
                            swiftsym(cpu, iptr);
                        else
                            debug_outf("Missing filename\n");
                    }
                    else if (!strncmp(cmd, "symbol", cmdlen)) {
                        if (*iptr)
                            set_sym(cpu, iptr);
                        else
                            debug_outf("Missing parameters\n");
                    }
                    else if (!strncmp(cmd, "symlist", cmdlen))
                        list_syms(cpu, iptr);
                    else if (!strncmp(cmd, "save", cmdlen)) {
                        if (*iptr)
                            debugger_save(cpu, iptr);
                        else
                            debug_outf("Missing filename\n");
                    }
                    else
                        badcmd = true;
                    break;
                }
                else {
                    if (*iptr)
                        sscanf(iptr, "%i", &debug_step);
                    else
                        debug_step = 1;
                    debug_lastcommand = 's';
                    indebug = 0;
                    main_resume();
                    return;
                }
            case 't':
                if (!strncmp(cmd, "trace", cmdlen)) {
                    if (trace_fp) {
                        fclose(trace_fp);
                        trace_fp = NULL;
                    }
                    if (*iptr) {
                        char *eptr = strchr(iptr, '\n');
                        if (eptr)
                            *eptr = '\0';
                        if ((trace_fp = fopen(iptr, "a")))
                            debug_outf("Tracing to %s\n", iptr);
                        else
                            debug_outf("Unable to open trace file '%s' for append: %s\n", iptr, strerror(errno));
                    } else
                        debug_outf("Trace file closed");
                }
                else
                    badcmd = true;
                break;

            case 'v':
                if (!strncmp(cmd, "vrefresh", cmdlen)) {
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
                else
                    badcmd = true;
                break;

            case 'w':
                if (!strncmp(cmd, "watchr", cmdlen))
                    set_point(cpu, WATCH_READ, iptr, "Read watchpoint");
                else if (!strncmp(cmd, "watchw", cmdlen))
                    set_point(cpu, WATCH_WRITE, iptr, "Write watchpoint");
                else if (!strncmp(cmd, "watchi", cmdlen))
                    set_point(cpu, WATCH_INPUT, iptr, "Input watchpoint");
                else if (!strncmp(cmd, "watcho", cmdlen))
                    set_point(cpu, WATCH_OUTPUT, iptr, "Output watchpoint");
                else if (!strncmp(cmd, "wlist", cmdlen)) {
                    list_points(cpu, WATCH_READ, "Read watchpoint");
                    list_points(cpu, WATCH_WRITE, "Write watchpoint");
                    list_points(cpu, WATCH_INPUT, "Input watchpoint");
                    list_points(cpu, WATCH_OUTPUT, "Output watchpoint");
                }
                else if (!strncmp(cmd, "wclearr", cmdlen))
                    clear_point(cpu, WATCH_READ, iptr, "Read watchpoint");
                else if (!strncmp(cmd, "wclearw", cmdlen))
                    clear_point(cpu, WATCH_WRITE, iptr, "Write watchpoint");
                else if (!strncmp(cmd, "wcleari", cmdlen))
                    clear_point(cpu, WATCH_INPUT, iptr, "Input watchpoint");
                else if (!strncmp(cmd, "wclearo", cmdlen))
                    clear_point(cpu, WATCH_OUTPUT, iptr, "Output watchpoint");
                else if (!strncmp(cmd, "writem", cmdlen))
                    debugger_writemem(cpu, iptr);
                else
                    badcmd = true;
                break;
            default:
                badcmd = true;
        }
        if (badcmd)
            debug_out("Bad command\n", 12);

    }
}

static inline void check_points(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size, break_type btype, break_type wtype, const char *desc)
{
    for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
        if (bp->address == addr) {
            if (bp->type == btype) {
                char addr_str[20 + SYM_MAX], iaddr_str[20 + SYM_MAX];
                uint32_t iaddr = cpu->get_instr_addr();
                cpu->print_addr(cpu, addr, addr_str, sizeof(addr_str), true);
                cpu->print_addr(cpu, iaddr, iaddr_str, sizeof(iaddr_str), true);
                debug_outf("cpu %s: %s: break on %s %s, value=%X\n", cpu->cpu_name, iaddr_str, desc, addr_str, value);
                debugger_do(cpu, iaddr);
            }
            else if (bp->type == wtype) {
                char addr_str[10], iaddr_str[10];
                uint32_t iaddr = cpu->get_instr_addr();
                cpu->print_addr(cpu, addr, addr_str, sizeof(addr_str), true);
                cpu->print_addr(cpu, iaddr, iaddr_str, sizeof(iaddr_str), true);
                debug_outf("cpu %s: %s: %s %s, value=%0*X\n", cpu->cpu_name, iaddr_str, desc, addr_str, size*2, value);
            }
        }
    }
}

void debug_memread (cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, BREAK_READ, WATCH_READ, "read from");
}

void debug_memwrite(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, BREAK_WRITE, WATCH_WRITE, "write to");
}

void debug_ioread (cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, BREAK_INPUT, WATCH_INPUT, "input from");
}

void debug_iowrite(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    check_points(cpu, addr, value, size, BREAK_OUTPUT, WATCH_OUTPUT, "output to");
}

void debug_preexec (cpu_debug_t *cpu, uint32_t addr) {
    bool enter = false;

    if (trace_fp) {
        const char *symlbl;
        char buf[256];
        if (symbol_find_by_addr(cpu->symbols, addr, &symlbl)) {
            fputs(symlbl, trace_fp);
            fputs(":\n", trace_fp);
        }
        cpu->disassemble(cpu, addr, buf, sizeof buf);

        char *sym = strchr(buf, '\\');
        if (sym) {
            *(sym++) = '\0';
        }

        fputs("\t", trace_fp);
        fputs(buf, trace_fp);
        *buf = ' ';

        const char **np = cpu->reg_names;
        const char *name;
        int r = 0;
        while ((name = *np++)) {
            size_t len = cpu->reg_print(r++, buf + 1, sizeof buf - 1);
            fwrite(buf, len + 1, 1, trace_fp);
        }

        if (sym)
        {
            fputs(" \\", trace_fp);
            fputs(sym, trace_fp);
        }

        putc('\n', trace_fp);
    }

    if (cpu->prof_counts && addr >= cpu->prof_start && addr < cpu->prof_end)
        cpu->prof_counts[addr - cpu->prof_start]++;

    if (addr == cpu->tbreak) {
        log_debug("debugger; enter for CPU %s on tbreak at %04X", cpu->cpu_name, addr);
        enter = 1;
    }
    else {
        for (breakpoint *bp = cpu->breakpoints; bp; bp = bp->next) {
            if (bp->address == addr && bp->type == BREAK_EXEC) {
                char addr_str[16+SYM_MAX];
                cpu->print_addr(cpu, addr, addr_str, sizeof(addr_str), true);
                debug_outf("cpu %s: Break at %s\n", cpu->cpu_name, addr_str);
                if (contcount) {
                    contcount--;
                    return;
                }
                log_debug("debugger; enter for CPU %s on breakpoint at %s", cpu->cpu_name, addr_str);
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
        cpu->tbreak = -1;
        debugger_do(cpu, addr);
    }
}

void debug_trap(cpu_debug_t *cpu, uint32_t addr, int reason)
{
    const char *desc = cpu->trap_names[reason];
    char addr_str[20 + SYM_MAX];
    cpu->print_addr(cpu, addr, addr_str, sizeof(addr_str), true);
    debug_outf("cpu %s: %s at %s\n", cpu->cpu_name, desc, addr_str);
    debugger_do(cpu, addr);
}
