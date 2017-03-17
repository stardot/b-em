/*B-em v2.2 by Tom Walker
  Debugger*/

#include <stdarg.h>
#include <stdio.h>

#include "cpu_debug.h"
#include "debugger.h"
#include "b-em.h"

#define NUM_BREAKPOINTS 8

int debug;
int indebug=0;
extern int fcount;
static int debugstep = 0;
static FILE *trace_fp = NULL;

extern cpu_debug_t core6502_cpu_debug;
extern cpu_debug_t tube6502_cpu_debug;
extern cpu_debug_t tubez80_cpu_debug;
extern cpu_debug_t n32016_cpu_debug;

static cpu_debug_t *debuggables[] = {
    &core6502_cpu_debug,
    &tube6502_cpu_debug,
    &tubez80_cpu_debug,
    &n32016_cpu_debug,
};

#ifdef WIN32
#include <windows.h>
#include <wingdi.h>
#include <process.h>

static HANDLE debugthread;
static HWND dhwnd;
static int debugstarted = 0;
static char DebugszClassName[ ] = "B-emDebugWnd";
LRESULT CALLBACK DebugWindowProcedure (HWND, UINT, WPARAM, LPARAM);
static HINSTANCE hinst;
static uint8_t *usdat;

void _debugthread(PVOID pvoid)
{
    MSG messages = {0};     /* Here messages to the application are saved */
    WNDCLASSEX wincl;        /* Data structure for the windowclass */

    HDC hDC;
    HDC memDC;
    HBITMAP memBM;
    BITMAPINFO lpbmi;
    int x;
    int c,d;

    usdat = malloc(256 * 256 * 4);
    if (!debugstarted)
    {
        wincl.hInstance = hinst;
        wincl.lpszClassName = DebugszClassName;
        wincl.lpfnWndProc = DebugWindowProcedure;      /* This function is called by windows */
        wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
        wincl.cbSize = sizeof (WNDCLASSEX);

        /* Use default icon and mouse-pointer */
        wincl.hIcon = LoadIcon(hinst, "allegro_icon");
        wincl.hIconSm = LoadIcon(hinst, "allegro_icon");
        wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
        wincl.lpszMenuName = NULL;                 /* No menu */
        wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
        wincl.cbWndExtra = 0;                      /* structure or the window instance */
        /* Use Windows's default color as the background of the window */
        wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

        /* Register the window class, and if it fails quit the program */
        if (!RegisterClassEx(&wincl))
        {
            printf("Registerclass failed\n");
            return;
        }
    }
    dhwnd = CreateWindowEx (
        0,                   /* Extended possibilites for variation */
        DebugszClassName,         /* Classname */
        "Memory viewer",       /* Title Text */
        WS_OVERLAPPEDWINDOW, /* default window */
        CW_USEDEFAULT,       /* Windows decides the position */
        CW_USEDEFAULT,       /* where the window ends up on the screen */
        256 + (GetSystemMetrics(SM_CXFIXEDFRAME) * 2),/* The programs width */
        256 + (GetSystemMetrics(SM_CYFIXEDFRAME) * 2) + GetSystemMetrics(SM_CYCAPTION) + 2,/* and height in pixels */
        HWND_DESKTOP,        /* The window is a child-window to desktop */
        NULL, /* No menu */
        hinst,       /* Program Instance handler */
        NULL                 /* No Window Creation data */
    );
    printf("Window create %08X\n", (uint32_t)dhwnd);
    ShowWindow (dhwnd, SW_SHOWNORMAL);

    hDC = GetDC(dhwnd);

    memDC = CreateCompatibleDC ( hDC );
    if (!memDC) printf("memDC failed!\n");
        memBM = CreateCompatibleBitmap ( hDC, 256, 256 );
    if (!memBM) printf("memBM failed!\n");
        SelectObject ( memDC, memBM );

    lpbmi.bmiHeader.biSize         = sizeof(BITMAPINFOHEADER);
    lpbmi.bmiHeader.biWidth        = 256;
    lpbmi.bmiHeader.biHeight       = -256;
    lpbmi.bmiHeader.biPlanes       = 1;
    lpbmi.bmiHeader.biBitCount     = 32;
    lpbmi.bmiHeader.biCompression  = BI_RGB;
    lpbmi.bmiHeader.biSizeImage    = 256 * 256 * 4;
    lpbmi.bmiHeader.biClrUsed      = 0;
    lpbmi.bmiHeader.biClrImportant = 0;

    debugstarted = 1;
    while (debugon)
    {
        Sleep(20);
        c += 16;
        d = 0;
        for (x = 0; x < 65536; x++)
        {
            usdat[d++] = fetchc[x] * 8;
            usdat[d++] = readc[x]  * 8;
            usdat[d++] = writec[x] * 8;
            usdat[d++] = 0;
            if (fetchc[x]) fetchc[x]--;
            if (readc[x])  readc[x]--;
            if (writec[x]) writec[x]--;
        }

        SetDIBitsToDevice(memDC, 0, 0, 256, 256, 0, 0, 0, 256, usdat, &lpbmi, DIB_RGB_COLORS);
        BitBlt(hDC, 0, 0, 256, 256, memDC, 0, 0, SRCCOPY);

        if (PeekMessage(&messages, NULL, 0, 0, PM_REMOVE))
        {
            /* Translate virtual-key messages into character messages */
            TranslateMessage(&messages);
            /* Send message to WindowProcedure */
            DispatchMessage(&messages);
        }
        // if (indebug) pollmainwindow();
    }
    free(usdat);
}

static HANDLE consf, cinf;

BOOL CtrlHandler(DWORD fdwCtrlType)
{
    setquit();
    return TRUE;
}

/*void _debugconsolethread(PVOID pvoid)
{
    int c,d;
    return;
    while (debug)
    {
        if (!gotstr)
        {
            c=ReadConsoleA(cinf,debugconsoleins,255,(LPDWORD)&d,NULL);
            debugconsoleins[d]=0;
            gotstr=1;
        }
        else
            Sleep(10);
    }
}*/

void debug_start()
{
    if (debug)
    {
        hinst = GetModuleHandle(NULL);
        debugthread = (HANDLE)_beginthread(_debugthread, 0, NULL);
        // debugconsolethread=(HANDLE)_beginthread(_debugconsolethread,0,NULL);

        AllocConsole();
        SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
        consf = GetStdHandle(STD_OUTPUT_HANDLE);
        cinf  = GetStdHandle(STD_INPUT_HANDLE);
    }
}

void debug_end()
{
    FreeConsole();
    debug = debugon = 0;
}

void debug_kill()
{
    if (trace_fp) {
        fputs("Trace finished due to emulator quit\n", trace_fp);
        fclose(trace_fp);
    }
    TerminateThread(debugthread, 0);
    if (usdat) free(usdat);
}

LRESULT CALLBACK DebugWindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)                  /* handle the messages */
    {
        case WM_CREATE:
            return 0;
        case WM_DESTROY:
            PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
            break;
        default:                       /* for messages that we don't deal with */
            return DefWindowProc (hwnd, message, wParam, lParam);
    }
    return 0;
}

static void debug_out(const char *s, size_t len)
{
    startblit();
    WriteConsole(consf, s, len, NULL, NULL);
    endblit();
}

static void debug_outf(const char *fmt, ...)
{
    va_list ap;
    char s[256];
    size_t len;

    va_start(ap, fmt);
    len = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    startblit();
    WriteConsole(consf, s, len, NULL, NULL);
    endblit();
}

#else

#undef printf

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

void debug_kill() {
    if (trace_fp) {
        fputs("Trace finished due to emulator quit\n", trace_fp);
        fclose(trace_fp);
    }
}

void debug_start()
{
    cpu_debug_t **end, **cp, *c;
    int i = 0;
    char buf[80];

    if (debug) {
        debug_outf("\nDebuggable CPUSs are as follows:\n");
        end = debuggables + sizeof(debuggables)/sizeof(cpu_debug_t *);
        for (cp = debuggables; cp < end; cp++) {
            c = *cp;
            debug_outf("  %d: %s\n", ++i, c->cpu_name);
        }
        do {
            debug_outf("Debug which CPU? ");
            if (fgets(buf, sizeof buf, stdin) == NULL)
                return;
            i = atoi(buf);
        } while (i == 0 || i > sizeof(debuggables)/sizeof(cpu_debug_t *));
        debuggables[i-1]->debug_enable(1);
        debugstep = 1;
    }
}

#endif

#include <errno.h>
#include "6502.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "sn76489.h"
#include "model.h"

int readc[65536], writec[65536], fetchc[65536];

static uint32_t debug_memaddr=0;
static uint32_t debug_disaddr=0;
static uint8_t  debug_lastcommand=0;

static int breakpoints[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakr[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakw[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchr[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchw[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int contcount = 0;

void debug_reset() {
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
    "    blist      - list current breakpoints\n"
    "    break n    - set a breakpoint at n\n"
    "    breakr n   - break on reads from address n\n"
    "    breakw n   - break on writes to address n\n"
    "    c          - continue running indefinitely\n"
    "    d [n]      - disassemble from address n\n"
    "    m [n]      - memory dump from address n\n"
    "    q          - force emulator exit\n"
    "    r          - print 6502 registers\n"
    "    r sysvia   - print System VIA registers\n"
    "    r uservia  - print User VIA registers\n"
    "    r crtc     - print CRTC registers\n"
    "    r vidproc  - print VIDPROC registers\n"
    "    r sound    - print Sound registers\n"
    "    s [n]      - step n instructions (or 1 if no parameter)\n\n"
    "    watchr n   - watch reads from address n\n"
    "    watchw n   - watch writes to address n\n"
    "    wclearr n  - clear read watchpoint n or read watchpoint at n\n"
    "    wclearw n  - clear write watchpoint n or write watchpoint at n\n"
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
}

void debugger_do(cpu_debug_t *cpu, uint32_t addr)
{
    int c, d, e, f;
    int params;
    uint8_t temp;
    char dump[256], *dptr;
    char ins[256], *iptr;

    indebug = 1;
    cpu->disassemble(addr, ins, sizeof ins);
    debug_out(ins, strlen(ins));

    while (1)
    {
        debug_out("  >", 3);
#ifdef WIN32
        c = ReadConsoleA(cinf, ins, 255, (LPDWORD)&d, NULL);
        ins[d] = 0;
#else
        fgets(ins, 255, stdin);
#endif
        for (d = 0; ((c = ins[d])) && c != ' '; d++)
            ;
        if (c == ' ') {
            do d++; while (ins[d] == ' ');
            params = 1;
        } else
            params = 0;

        if (ins[0] == 0)
            ins[0] = debug_lastcommand;

        switch (ins[0])
        {
            case 'b': case 'B':
                if (!strncasecmp(ins, "breakr", 6))
                {
                    if (!params) break;
                    for (c = 0; c < 8; c++)
                    {
                        if (breakpoints[c] == -1)
                        {
                            sscanf(&ins[d], "%X", &breakr[c]);
                            debug_outf("    Read breakpoint %i set to %04X\n", c, breakr[c]);
                            break;
                        }
                    }
                }
                else if (!strncasecmp(ins, "breakw", 6))
                {
                    if (!params) break;
                    for (c = 0; c < 8; c++)
                    {
                        if (breakpoints[c] == -1)
                        {
                            sscanf(&ins[d], "%X", &breakw[c]);
                            debug_outf("    Write breakpoint %i set to %04X\n", c, breakw[c]);
                            break;
                        }
                    }
                }
                else if (!strncasecmp(ins, "break", 5))
                {
                    if (!params) break;
                    for (c = 0; c < 8; c++)
                    {
                        if (breakpoints[c] == -1)
                        {
                            sscanf(&ins[d], "%X", &breakpoints[c]);
                            debug_outf("    Breakpoint %i set to %04X\n", c, breakpoints[c]);
                            break;
                        }
                    }
                }
                else if (!strncasecmp(ins, "blist", 5))
                {
                    for (c = 0; c < 8; c++)
                    {
                        if (breakpoints[c] != -1)
                            debug_outf("    Breakpoint %i : %04X\n", c, breakpoints[c]);
                    }
                    for (c = 0; c < 8; c++)
                    {
                        if (breakr[c] != -1)
                            debug_outf("    Read breakpoint %i : %04X\n", c, breakr[c]);
                    }
                    for (c = 0; c < 8; c++)
                    {
                        if (breakw[c] != -1)
                        debug_outf("    Write breakpoint %i : %04X\n", c, breakw[c]);
                    }
                }
                else if (!strncasecmp(ins, "bclearr", 7))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X", &e);
                    for (c = 0; c < 8; c++)
                    {
                        if (breakr[c] == e) breakr[c] = -1;
                        if (c == e) breakr[c] = -1;
                    }
                }
                else if (!strncasecmp(ins, "bclearw", 7))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X", &e);
                    for (c = 0; c < 8; c++)
                    {
                        if (breakw[c] == e) breakw[c] = -1;
                        if (c == e) breakw[c] = -1;
                    }
                }
                else if (!strncasecmp(ins, "bclear", 6))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X", &e);
                    for (c = 0; c < 8; c++)
                    {
                        if (breakpoints[c] == e) breakpoints[c] = -1;
                        if (c == e) breakpoints[c] = -1;
                    }
                }
                break;

            case 'q': case 'Q':
                setquit();
                /* FALLTHOUGH */

            case 'c': case 'C':
                if (params) sscanf(&ins[d], "%d", &contcount);
                debug = 0;
                indebug = 0;
                return;

            case 'd': case 'D':
                if (params) sscanf(&ins[d], "%X", (unsigned int *)&debug_disaddr);
                for (c = 0; c < 12; c++)
                {
                    debug_out("    ", 4);
                    debug_disaddr = cpu->disassemble(debug_disaddr, ins, sizeof ins);
		    debug_out(ins, strlen(ins));
                    debug_out("\n", 1);
                }
                break;

            case 'h': case 'H': case '?':
                debug_out(helptext, sizeof helptext-1);
                break;

            case 'm': case 'M':
                if (params)
                    sscanf(&ins[d], "%X", (unsigned int *)&debug_memaddr);
                for (c = 0; c < 16; c++)
                {
                    debug_outf("    %04X : ", debug_memaddr);
                    for (d = 0; d < 16; d++)
                        debug_outf("%02X ", cpu->memread(debug_memaddr + d));
                    debug_out("  ", 2);
                    dptr = dump;
                    for (d = 0; d < 16; d++)
                    {
                        temp = cpu->memread(debug_memaddr + d);
                        if (temp < ' ' || temp >= 0x7f)
                            *dptr++ = '.';
                        else
                            *dptr++ = temp;
                    }
                    *dptr++ = '\n';
                    debug_out(dump, dptr-dump);
                    debug_memaddr += 16;
                }
                break;

            case 'r': case 'R':
                if (params)
                {
                    if (!strncasecmp(&ins[d], "sysvia", 6))
                    {
                        debug_outf("    System VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", sysvia.ora,  sysvia.orb,  sysvia.ira, sysvia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", sysvia.ddra, sysvia.ddrb, sysvia.acr, sysvia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", sysvia.t1l / 2, (sysvia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", sysvia.t2l / 2, (sysvia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", sysvia.ier, sysvia.ifr);
                    }
                    else if (!strncasecmp(&ins[d], "uservia", 7))
                    {
                        debug_outf("    User VIA registers :\n");
                        debug_outf("    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", uservia.ora,  uservia.orb,  uservia.ira, uservia.irb);
                        debug_outf("    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", uservia.ddra, uservia.ddrb, uservia.acr, uservia.pcr);
                        debug_outf("    Timer 1 latch %04X   count %04X\n", uservia.t1l / 2, (uservia.t1c / 2) & 0xFFFF);
                        debug_outf("    Timer 2 latch %04X   count %04X\n", uservia.t2l / 2, (uservia.t2c / 2) & 0xFFFF);
                        debug_outf("    IER %02X IFR %02X\n", uservia.ier, uservia.ifr);
                    }
                    else if (!strncasecmp(&ins[d], "crtc", 4))
                    {
                        debug_outf("    CRTC registers :\n");
                        debug_outf("    Index=%i\n", crtc_i);
                        debug_outf("    R0 =%02X  R1 =%02X  R2 =%02X  R3 =%02X  R4 =%02X  R5 =%02X  R6 =%02X  R7 =%02X  R8 =%02X\n", crtc[0], crtc[1],  crtc[2],  crtc[3],  crtc[4],  crtc[5],  crtc[6],  crtc[7],  crtc[8]);
                        debug_outf("    R9 =%02X  R10=%02X  R11=%02X  R12=%02X  R13=%02X  R14=%02X  R15=%02X  R16=%02X  R17=%02X\n", crtc[9], crtc[10], crtc[11], crtc[12], crtc[13], crtc[14], crtc[15], crtc[16], crtc[17]);
                        debug_outf("    VC=%i SC=%i HC=%i MA=%04X\n", vc, sc, hc, ma);
                    }
                    if (!strncasecmp(&ins[d],"vidproc",7))
                    {
                        debug_outf("    VIDPROC registers :\n");
                        debug_outf("    Control=%02X\n", ula_ctrl);
                        debug_outf("    Palette entries :\n");
                        debug_outf("     0=%01X   1=%01X   2=%01X   3=%01X   4=%01X   5=%01X   6=%01X   7=%01X\n", ula_palbak[0], ula_palbak[1], ula_palbak[2],  ula_palbak[3],  ula_palbak[4],  ula_palbak[5],  ula_palbak[6],  ula_palbak[7]);
                        debug_outf("     8=%01X   9=%01X  10=%01X  11=%01X  12=%01X  13=%01X  14=%01X  15=%01X\n", ula_palbak[8], ula_palbak[9], ula_palbak[10], ula_palbak[11], ula_palbak[12], ula_palbak[13], ula_palbak[14], ula_palbak[15]);
                    }
                    if (!strncasecmp(&ins[d],"sound",5))
                    {
                        debug_outf("    Sound registers :\n");
                        debug_outf("    Voice 0 frequency = %04X   volume = %i  control = %02X\n", sn_latch[0] >> 6, sn_vol[0], sn_noise);
                        debug_outf("    Voice 1 frequency = %04X   volume = %i\n", sn_latch[1] >> 6, sn_vol[1]);
                        debug_outf("    Voice 2 frequency = %04X   volume = %i\n", sn_latch[2] >> 6, sn_vol[2]);
                        debug_outf("    Voice 3 frequency = %04X   volume = %i\n", sn_latch[3] >> 6, sn_vol[3]);
                    }
                }
                else
                {
                    debug_outf("    registers for %s\n", cpu->cpu_name);
		    print_registers(cpu);
                }
                break;

            case 's': case 'S':
                if (params) sscanf(&ins[d], "%i", &debugstep);
                else        debugstep = 1;
                debug_lastcommand = ins[0];
                indebug = 0;
                return;

            case 't': case 'T':
                if (trace_fp)
                    fclose(trace_fp);
                if (params) {
                    if ((iptr = strchr(&ins[d], '\n')))
                        *iptr = '\0';
                    if ((trace_fp = fopen(&ins[d], "a")))
                        debug_outf("Tracing to %s\n", &ins[d]);
                    else
                        debug_outf("Unable to open trace file '%s' for append: %s\n", &ins[d], strerror(errno));
                } else
                    debug_outf("Trace file closed");
                break;

            case 'w': case 'W':
                if (!strncasecmp(ins, "watchr", 6))
                {
                    if (!params) break;
                    for (c = 0; c < 8; c++)
                    {
                        if (watchr[c] == -1)
                        {
                            sscanf(&ins[d], "%X", &watchr[c]);
                            debug_outf("    Read watchpoint %i set to %04X\n", c, watchr[c]);
                            break;
                        }
                    }
                    break;
                }
                else if (!strncasecmp(ins, "watchw", 6))
                {
                    if (!params) break;
                    for (c = 0; c < 8; c++)
                    {
                        if (watchw[c] == -1)
                        {
                            sscanf(&ins[d], "%X", &watchw[c]);
                            debug_outf("    Write watchpoint %i set to %04X\n", c, watchw[c]);
                            break;
                        }
                    }
                    break;
                }
                else if (!strncasecmp(ins, "wlist", 5))
                {
                    for (c = 0; c < 8; c++)
                        if (watchr[c] != -1)
                            debug_outf("    Read watchpoint %i : %04X\n", c, watchr[c]);
                    for (c = 0; c < 8; c++)
                        if (watchw[c] != -1)
                            debug_outf("    Write watchpoint %i : %04X\n", c, watchw[c]);
                }
                if (!strncasecmp(ins, "wclearr", 7))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X", &e);
                    for (c = 0; c < 8; c++)
                    {
                        if (watchr[c] == e) watchr[c] = -1;
                        if (c == e) watchr[c] = -1;
                    }
                }
                else if (!strncasecmp(ins, "wclearw", 7))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X", &e);
                    for (c = 0; c < 8; c++)
                    {
                        if (watchw[c] == e) watchw[c] = -1;
                        if (c == e) watchw[c] = -1;
                    }
                }
                else if (!strncasecmp(ins, "writem", 6))
                {
                    if (!params) break;
                    sscanf(&ins[d], "%X %X", &e, &f);
                    log_debug("WriteM %04X %04X\n", e, f);
                    writemem(e, f);
                }
                break;
        }
        debug_lastcommand = ins[0];
    }
    fcount = 0;
    indebug = 0;
}

void debug_memread (cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    int c;
    uint32_t iaddr;

    for (c = 0; c < NUM_BREAKPOINTS; c++)
    {
        if (breakr[c] == addr)
        {
	    iaddr = cpu->get_instr_addr();
            debug_outf("cpu %s: %04x: break on read from %04X, value=%X\n", iaddr, cpu->cpu_name, addr, value);
	    debugger_do(cpu, iaddr);
        }
        if (watchr[c] == addr) {
	    iaddr = cpu->get_instr_addr();
            debug_outf("cpu %s: %04x: read from %04X, value=%X\n", cpu->cpu_name, iaddr, addr, value);
	}
    }
}

void debug_memwrite(cpu_debug_t *cpu, uint32_t addr, uint32_t value, uint8_t size) {
    int c;
    uint32_t iaddr;

    for (c = 0; c < NUM_BREAKPOINTS; c++)
    {
        if (breakw[c] == addr)
        {
	    iaddr = cpu->get_instr_addr();
            debug_outf("cpu %s: %04x: break on write to %04X, value=%X\n", cpu->cpu_name, iaddr, addr, value);
	    debugger_do(cpu, cpu->get_instr_addr());
        }
        if (watchw[c] == addr) {
	    iaddr = cpu->get_instr_addr();
	    debug_outf("cpu %s: %04x: write to %04X, value=%X\n", cpu->cpu_name, iaddr, value, addr);
	}
    }
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

	for (r = 0, np = cpu->reg_names; (name = *np++); ) {
	    len = cpu->reg_print(r++, buf+1, sizeof buf-1);
	    fwrite(buf, len+1, 1, trace_fp);
	}
	putc('\n', trace_fp);
    }

    for (c = 0; c < NUM_BREAKPOINTS; c++)
    {
        if (breakpoints[c] == addr)
        {
            debug_outf("cpu %s: Break at %04X\n", cpu->cpu_name, addr);
            if (contcount) {
                contcount--;
		return;
	    }
	    enter = 1;
        }
    }
    if (debugstep)
    {
        debugstep--;
        if (debugstep) return;
	enter = 1;
    }
    if (enter)
	debugger_do(cpu, addr);
}


