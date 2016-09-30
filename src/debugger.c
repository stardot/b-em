/*B-em v2.2 by Tom Walker
  Debugger*/
  
int debug;
int indebug=0;
extern int fcount;

#ifdef WIN32
#include <windows.h>
#include <wingdi.h>
#include <process.h>
#include "b-em.h"
#include "debugger.h"

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
//                if (indebug) pollmainwindow();
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
//                debugconsolethread=(HANDLE)_beginthread(_debugconsolethread,0,NULL);

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
                default:                      /* for messages that we don't deal with */
                return DefWindowProc (hwnd, message, wParam, lParam);
        }
        return 0;
}

static void debug_out(char *s)
{
        startblit();
        WriteConsole(consf, s, strlen(s), NULL, NULL);
        endblit();
        bem_debugf("%s", s);
}

#else

#include <stdio.h>
#include "b-em.h"
#include "debugger.h"

#undef printf

static void debug_out(char *s)
{
        printf(s);
        fflush(stdout);
}

void debug_start()
{
}

#endif

#include "6502.h"
#include "via.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "sn76489.h"
#include "model.h"

int readc[65536], writec[65536], fetchc[65536];

static uint16_t debug_memaddr=0;
static uint16_t debug_disaddr=0;
static uint8_t  debug_lastcommand=0;

static uint8_t debug_readmem(uint16_t addr)
{
        if (addr >= 0xFC00 && addr < 0xFF00) return 0xFF;
        return readmem(addr);
}

enum
{
        IMP,IMPA,IMM,ZP,ZPX,ZPY,INDX,INDY,IND,ABS,ABSX,ABSY,IND16,IND1X,BRA
};

static char dopname[256][6]=
{
/*00*/  "BRK","ORA","---","---","TSB","ORA","ASL","---","PHP","ORA","ASL","---","TSB","ORA","ASL","---",
/*10*/  "BPL","ORA","ORA","---","TRB","ORA","ASL","---","CLC","ORA","INC","---","TRB","ORA","ASL","---",
/*20*/  "JSR","AND","---","---","BIT","AND","ROL","---","PLP","AND","ROL","---","BIT","AND","ROL","---",
/*30*/  "BMI","AND","AND","---","BIT","AND","ROL","---","SEC","AND","DEC","---","BIT","AND","ROL","---",
/*40*/  "RTI","EOR","---","---","---","EOR","LSR","---","PHA","EOR","LSR","---","JMP","EOR","LSR","---",
/*50*/  "BVC","EOR","EOR","---","---","EOR","LSR","---","CLI","EOR","PHY","---","---","EOR","LSR","---",
/*60*/  "RTS","ADC","---","---","STZ","ADC","ROR","---","PLA","ADC","ROR","---","JMP","ADC","ROR","---",
/*70*/  "BVS","ADC","ADC","---","STZ","ADC","ROR","---","SEI","ADC","PLY","---","JMP","ADC","ROR","---",
/*80*/  "BRA","STA","---","---","STY","STA","STX","---","DEY","BIT","TXA","---","STY","STA","STX","---",
/*90*/  "BCC","STA","STA","---","STY","STA","STX","---","TYA","STA","TXS","---","STZ","STA","STZ","---",
/*A0*/  "LDY","LDA","LDX","---","LDY","LDA","LDX","---","TAY","LDA","TAX","---","LDY","LDA","LDX","---",
/*B0*/  "BCS","LDA","LDA","---","LDY","LDA","LDX","---","CLV","LDA","TSX","---","LDY","LDA","LDX","---",
/*C0*/  "CPY","CMP","---","---","CPY","CMP","DEC","---","INY","CMP","DEX","WAI","CPY","CMP","DEC","---",
/*D0*/  "BNE","CMP","CMP","---","---","CMP","DEC","---","CLD","CMP","PHX","STP","---","CMP","DEC","---",
/*E0*/  "CPX","SBC","---","---","CPX","SBC","INC","---","INX","SBC","NOP","---","CPX","SBC","INC","---",
/*F0*/  "BEQ","SBC","SBC","---","---","SBC","INC","---","SED","SBC","PLX","---","---","SBC","INC","---",
};

static int dopaddr[256]=
{
/*00*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*10*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABS,  ABSX, ABSX, IMP,
/*20*/  ABS,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*30*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABSX, ABSX, ABSX, IMP,
/*40*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*50*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*60*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  IND16,ABS,  ABS,  IMP,
/*70*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  IND1X,ABSX, ABSX, IMP,
/*80*/  BRA,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*90*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*A0*/  IMM,  INDX, IMM,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*B0*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABSX, ABSX, ABSY, IMP,
/*C0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*D0*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*E0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*F0*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
};

static char dopnamenmos[256][6]=
{
/*00*/  "BRK","ORA","HLT","SLO","NOP","ORA","ASL","SLO","PHP","ORA","ASL","ANC","NOP","ORA","ASL","SLO",
/*10*/  "BPL","ORA","HLT","SLO","NOP","ORA","ASL","SLO","CLC","ORA","NOP","SLO","NOP","ORA","ASL","SLO",
/*20*/  "JSR","AND","HLT","RLA","NOP","AND","ROL","RLA","PLP","AND","ROL","ANC","BIT","AND","ROL","RLA",
/*30*/  "BMI","AND","HLT","RLA","NOP","AND","ROL","RLA","SEC","AND","NOP","RLA","NOP","AND","ROL","RLA",
/*40*/  "RTI","EOR","HLT","SRE","NOP","EOR","LSR","SRE","PHA","EOR","LSR","ASR","JMP","EOR","LSR","SRE",
/*50*/  "BVC","EOR","HLT","SRE","NOP","EOR","LSR","SRE","CLI","EOR","NOP","SRE","NOP","EOR","LSR","SRE",
/*60*/  "RTS","ADC","HLT","RRA","NOP","ADC","ROR","RRA","PLA","ADC","ROR","ARR","JMP","ADC","ROR","RRA",
/*70*/  "BVS","ADC","HLT","RRA","NOP","ADC","ROR","RRA","SEI","ADC","NOP","RRA","NOP","ADC","ROR","RRA",
/*80*/  "BRA","STA","NOP","SAX","STY","STA","STX","SAX","DEY","NOP","TXA","ANE","STY","STA","STX","SAX",
/*90*/  "BCC","STA","HLT","SHA","STY","STA","STX","SAX","TYA","STA","TXS","SHS","SHY","STA","SHX","SHA",
/*A0*/  "LDY","LDA","LDX","LAX","LDY","LDA","LDX","LAX","TAY","LDA","TAX","LXA","LDY","LDA","LDX","LAX",
/*B0*/  "BCS","LDA","HLT","LAX","LDY","LDA","LDX","LAX","CLV","LDA","TSX","LAS","LDY","LDA","LDX","LAX",
/*C0*/  "CPY","CMP","NOP","DCP","CPY","CMP","DEC","DCP","INY","CMP","DEX","SBX","CPY","CMP","DEC","DCP",
/*D0*/  "BNE","CMP","HLT","DCP","NOP","CMP","DEC","DCP","CLD","CMP","NOP","DCP","NOP","CMP","DEC","DCP",
/*E0*/  "CPX","SBC","NOP","ISB","CPX","SBC","INC","ISB","INX","SBC","NOP","SBC","CPX","SBC","INC","ISB",
/*F0*/  "BEQ","SBC","HLT","ISB","NOP","SBC","INC","ISB","SED","SBC","NOP","ISB","NOP","SBC","INC","ISB",
};

static int dopaddrnmos[256]=
{
/*00*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*10*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*20*/  ABS,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*30*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*40*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*50*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*60*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  IND16,ABS,  ABS,  ABS,
/*70*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*80*/  BRA,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*90*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*A0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*B0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSY, ABSX,
/*C0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*D0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*E0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*F0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
};


static void debug_disassemble()
{
        uint16_t temp;
        char s[256];
        uint8_t op = debug_readmem(debug_disaddr);
        uint8_t p1 = debug_readmem(debug_disaddr + 1), p2 = debug_readmem(debug_disaddr + 2);
        sprintf(s, "%04X : ", debug_disaddr);
        debug_out(s);
        debug_out(MASTER ? dopname[op] : dopnamenmos[op]);
        debug_out(" ");
        switch (MASTER ? dopaddr[op] : dopaddrnmos[op])
        {
                case IMP:
                sprintf(s, "        ");
                debug_out(s);
                break;
                case IMPA:
                sprintf(s, "A       ");
                debug_out(s);
                break;
                case IMM:
                sprintf(s, "#%02X     ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case ZP:
                sprintf(s, "%02X      ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case ZPX:
                sprintf(s, "%02X,X    ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case ZPY:
                sprintf(s, "%02X,Y    ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case IND:
                sprintf(s, "(%02X)    ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case INDX:
                sprintf(s, "(%02X,X)  ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case INDY:
                sprintf(s, "(%02X),Y  ", p1);
                debug_out(s);
                debug_disaddr++;
                break;
                case ABS:
                sprintf(s, "%02X%02X    ", p2, p1);
                debug_out(s);
                debug_disaddr += 2;
                break;
                case ABSX:
                sprintf(s, "%02X%02X,X  ", p2, p1);
                debug_out(s);
                debug_disaddr += 2;
                break;
                case ABSY:
                sprintf(s, "%02X%02X,Y  ", p2, p1);
                debug_out(s);
                debug_disaddr += 2;
                break;
                case IND16:
                sprintf(s, "(%02X%02X)  ", p2, p1);
                debug_out(s);
                debug_disaddr += 2;
                break;
                case IND1X:
                sprintf(s, "(%02X%02X,X)", p2, p1);
                debug_out(s);
                debug_disaddr += 2;
                break;
                case BRA:
                temp = debug_disaddr + 2 + (signed char)p1;
                sprintf(s, "%04X    ", temp);
                debug_out(s);
                debug_disaddr++;
                break;
        }
        debug_disaddr++;
//        WriteConsole(consf,"\n",strlen("\n"),NULL,NULL);
}

static int breakpoints[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakr[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int breakw[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchr[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int watchw[8]      = {-1, -1, -1, -1, -1, -1, -1, -1};
static int debugstep = 0;

void debug_read(uint16_t addr)
{
        int c;
        char outs[256];
        for (c = 0; c < 8; c++)
        {
                if (breakr[c] == addr)
                {
                        debug = 1;
                        sprintf(outs, "    Break on read from %04X\n", addr);
                        debug_out(outs);
                        return;
                }
                if (watchr[c] == addr)
                {
                        sprintf(outs, "    Read from %04X - A=%02X X=%02X Y=%02X PC=%04X\n", addr, a, x, y, pc);
                        debug_out(outs);
                }
        }
}
void debug_write(uint16_t addr, uint8_t val)
{
        int c;
        char outs[256];
        for (c = 0; c < 8; c++)
        {
                if (breakw[c] == addr)
                {
                        debug = 1;
                        sprintf(outs, "    Break on write to %04X - val %02X\n", addr, val);
                        debug_out(outs);
                        return;
                }
                if (watchw[c] == addr)
                {
                        sprintf(outs, "    Write %02X to %04X - A=%02X X=%02X Y=%02X PC=%04X\n", val, addr, a, x, y, pc);
                        debug_out(outs);
                }
        }
}

void debugger_do()
{
        int c, d, e, f;
        int params;
        uint8_t temp;
        char outs[256];
        char ins[256];
        if (!opcode)
        {
                sprintf(outs, "BRK %04X! %04X %04X\n", pc, oldpc, oldoldpc);
                debug_out(outs);
        }
        if (!opcode) debug = 1;
        
        for (c = 0; c < 8; c++)
        {
                if (breakpoints[c] == pc)
                {
                        debug = 1;
                        sprintf(outs, "    Break at %04X\n", pc);
                        debug_out(outs);
                }
        }
        if (!debug) return;
//        if (!opcode) printf("BRK at %04X\n",pc);
        if (debugstep)
        {
                debugstep--;
                if (debugstep) return;
        }
        indebug = 1;
        while (1)
        {
                d = debug_disaddr;
                debug_disaddr = pc;
                debug_disassemble();
                debug_disaddr = d;
                sprintf(outs, "  >");
                debug_out(outs);
#ifdef WIN32
                c = ReadConsoleA(cinf, ins, 255, (LPDWORD)&d, NULL);
                ins[d] = 0;
#else
		(void)fgets(ins, 255, stdin);
#endif
                d = 0;
                while (ins[d] != 32 && ins[d] != 0xA && ins[d] != 0xD && ins[d] != 0) d++;
                while (ins[d] == 32) d++;
                if (ins[d] == 0xA || ins[d] == 0xD || ins[d] == 0) params = 0;
                else                                               params = 1;
                
                if (ins[0] == 0xA || ins[0] == 0xD) ins[0] = debug_lastcommand;
//debug_out("Processing!\n");
                switch (ins[0])
                {
                        case 'c': case 'C':
                        debug = 0;
                        indebug = 0;
                        return;
                        case 'm': case 'M':
                        if (params) sscanf(&ins[d], "%X", (unsigned int *)&debug_memaddr);
                        for (c = 0; c < 16; c++)
                        {
                                sprintf(outs, "    %04X : ", debug_memaddr);
                                debug_out(outs);
                                for (d = 0; d < 16; d++)
                                {
                                        sprintf(outs, "%02X ", debug_readmem(debug_memaddr + d));
                                        debug_out(outs);
                                }
                                debug_out("  ");
                                for (d = 0; d < 16; d++)
                                {
                                        temp = debug_readmem(debug_memaddr + d);
                                        if (temp < 32) sprintf(outs, ".");
                                        else           sprintf(outs, "%c", temp);
                                        debug_out(outs);
                                }
                                debug_memaddr += 16;
                                debug_out("\n");
                        }
                        break;
                        case 'd': case 'D':
                        if (params) sscanf(&ins[d], "%X", (unsigned int *)&debug_disaddr);
                        for (c = 0; c < 12; c++)
                        {
                                debug_out("    ");
                                debug_disassemble();
                                debug_out("\n");
                        }
                        break;
                        case 'r': case 'R':
                        if (params)
                        {
                                if (!strncasecmp(&ins[d], "sysvia", 6))
                                {
                                        sprintf(outs, "    System VIA registers :\n");
                                        debug_out(outs);
                                        sprintf(outs, "    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", sysvia.ora,  sysvia.orb,  sysvia.ira, sysvia.irb);
                                        debug_out(outs);
                                        sprintf(outs, "    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", sysvia.ddra, sysvia.ddrb, sysvia.acr, sysvia.pcr);
                                        debug_out(outs);
                                        sprintf(outs, "    Timer 1 latch %04X   count %04X\n", sysvia.t1l / 2, (sysvia.t1c / 2) & 0xFFFF);
                                        debug_out(outs);
                                        sprintf(outs, "    Timer 2 latch %04X   count %04X\n", sysvia.t2l / 2, (sysvia.t2c / 2) & 0xFFFF);
                                        debug_out(outs);
                                        sprintf(outs, "    IER %02X IFR %02X\n", sysvia.ier, sysvia.ifr);
                                        debug_out(outs);
                                }
                                if (!strncasecmp(&ins[d], "uservia", 7))
                                {
                                        sprintf(outs, "    User VIA registers :\n");
                                        debug_out(outs);
                                        sprintf(outs, "    ORA  %02X ORB  %02X IRA %02X IRB %02X\n", uservia.ora,  uservia.orb,  uservia.ira, uservia.irb);
                                        debug_out(outs);
                                        sprintf(outs, "    DDRA %02X DDRB %02X ACR %02X PCR %02X\n", uservia.ddra, uservia.ddrb, uservia.acr, uservia.pcr);
                                        debug_out(outs);
                                        sprintf(outs, "    Timer 1 latch %04X   count %04X\n", uservia.t1l / 2, (uservia.t1c / 2) & 0xFFFF);
                                        debug_out(outs);
                                        sprintf(outs, "    Timer 2 latch %04X   count %04X\n", uservia.t2l / 2, (uservia.t2c / 2) & 0xFFFF);
                                        debug_out(outs);
                                        sprintf(outs, "    IER %02X IFR %02X\n", uservia.ier, uservia.ifr);
                                        debug_out(outs);
                                }
                                if (!strncasecmp(&ins[d], "crtc", 4))
                                {
                                        sprintf(outs, "    CRTC registers :\n");
                                        debug_out(outs);
                                        sprintf(outs, "    Index=%i\n", crtc_i);
                                        debug_out(outs);
                                        sprintf(outs, "    R0 =%02X  R1 =%02X  R2 =%02X  R3 =%02X  R4 =%02X  R5 =%02X  R6 =%02X  R7 =%02X  R8 =%02X\n", crtc[0], crtc[1],  crtc[2],  crtc[3],  crtc[4],  crtc[5],  crtc[6],  crtc[7],  crtc[8]);
                                        debug_out(outs);
                                        sprintf(outs, "    R9 =%02X  R10=%02X  R11=%02X  R12=%02X  R13=%02X  R14=%02X  R15=%02X  R16=%02X  R17=%02X\n", crtc[9], crtc[10], crtc[11], crtc[12], crtc[13], crtc[14], crtc[15], crtc[16], crtc[17]);
                                        debug_out(outs);
                                        sprintf(outs, "    VC=%i SC=%i HC=%i MA=%04X\n", vc, sc, hc, ma);
                                        debug_out(outs);
                                }
                                if (!strncasecmp(&ins[d],"vidproc",7))
                                {
                                        sprintf(outs, "    VIDPROC registers :\n");
                                        debug_out(outs);
                                        sprintf(outs, "    Control=%02X\n", ula_ctrl);
                                        debug_out(outs);
                                        sprintf(outs, "    Palette entries :\n");
                                        debug_out(outs);
                                        sprintf(outs, "     0=%01X   1=%01X   2=%01X   3=%01X   4=%01X   5=%01X   6=%01X   7=%01X\n", ula_palbak[0], ula_palbak[1], ula_palbak[2],  ula_palbak[3],  ula_palbak[4],  ula_palbak[5],  ula_palbak[6],  ula_palbak[7]);
                                        debug_out(outs);
                                        sprintf(outs, "     8=%01X   9=%01X  10=%01X  11=%01X  12=%01X  13=%01X  14=%01X  15=%01X\n", ula_palbak[8], ula_palbak[9], ula_palbak[10], ula_palbak[11], ula_palbak[12], ula_palbak[13], ula_palbak[14], ula_palbak[15]);
                                        debug_out(outs);
                                }
                                if (!strncasecmp(&ins[d],"sound",5))
                                {
                                        sprintf(outs, "    Sound registers :\n");
                                        debug_out(outs);
                                        sprintf(outs, "    Voice 0 frequency = %04X   volume = %i  control = %02X\n", sn_latch[0] >> 6, sn_vol[0], sn_noise);
                                        debug_out(outs);
                                        sprintf(outs, "    Voice 1 frequency = %04X   volume = %i\n", sn_latch[1] >> 6, sn_vol[1]);
                                        debug_out(outs);
                                        sprintf(outs, "    Voice 2 frequency = %04X   volume = %i\n", sn_latch[2] >> 6, sn_vol[2]);
                                        debug_out(outs);
                                        sprintf(outs, "    Voice 3 frequency = %04X   volume = %i\n", sn_latch[3] >> 6, sn_vol[3]);
                                        debug_out(outs);
                                }
                        }
                        else
                        {
                                sprintf(outs, "    6502 registers :\n");
                                debug_out(outs);
                                sprintf(outs, "    A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n", a, x, y, s, pc);
                                debug_out(outs);
                                sprintf(outs, "    Status : %c%c%c%c%c%c\n", (p.n) ? 'N' : ' ', (p.v) ? 'V' : ' ', (p.d) ? 'D' : ' ', (p.i) ? 'I' : ' ', (p.z) ? 'Z' : ' ', (p.c) ? 'C' : ' ');
                                debug_out(outs);
                        }
                        break;
                        case 's': case 'S':
                        if (params) sscanf(&ins[d], "%i", &debugstep);
                        else        debugstep = 1;
                        debug_lastcommand = ins[0];
                        indebug = 0;
                        return;
                        case 'b': case 'B':
                        if (!strncasecmp(ins, "breakr", 6))
                        {
                                if (!params) break;
                                for (c = 0; c < 8; c++)
                                {
                                        if (breakpoints[c] == -1)
                                        {
                                                sscanf(&ins[d], "%X", &breakr[c]);
                                                sprintf(outs, "    Read breakpoint %i set to %04X\n", c, breakr[c]);
                                                debug_out(outs);
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
                                                sprintf(outs, "    Write breakpoint %i set to %04X\n", c, breakw[c]);
                                                debug_out(outs);
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
                                                sprintf(outs, "    Breakpoint %i set to %04X\n", c, breakpoints[c]);
                                                debug_out(outs);
                                                break;
                                        }
                                }
                        }
                        if (!strncasecmp(ins, "blist", 5))
                        {
                                for (c = 0; c < 8; c++)
                                {
                                        if (breakpoints[c] != -1)
                                        {
                                                sprintf(outs, "    Breakpoint %i : %04X\n", c, breakpoints[c]);
                                                debug_out(outs);
                                        }
                                }
                                for (c = 0; c < 8; c++)
                                {
                                        if (breakr[c] != -1)
                                        {
                                                sprintf(outs, "    Read breakpoint %i : %04X\n", c, breakr[c]);
                                                debug_out(outs);
                                        }
                                }
                                for (c = 0; c < 8; c++)
                                {
                                        if (breakw[c] != -1)
                                        {
                                                sprintf(outs, "    Write breakpoint %i : %04X\n", c, breakw[c]);
                                                debug_out(outs);
                                        }
                                }
                        }
                        if (!strncasecmp(ins, "bclearr", 7))
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
                        case 'w': case 'W':
                        if (!strncasecmp(ins, "watchr", 6))
                        {
                                if (!params) break;
                                for (c = 0; c < 8; c++)
                                {
                                        if (watchr[c] == -1)
                                        {
                                                sscanf(&ins[d], "%X", &watchr[c]);
                                                sprintf(outs, "    Read watchpoint %i set to %04X\n", c, watchr[c]);
                                                debug_out(outs);
                                                break;
                                        }
                                }
                                break;
                        }
                        if (!strncasecmp(ins, "watchw", 6))
                        {
                                if (!params) break;
                                for (c = 0; c < 8; c++)
                                {
                                        if (watchw[c] == -1)
                                        {
                                                sscanf(&ins[d], "%X", &watchw[c]);
                                                sprintf(outs, "    Write watchpoint %i set to %04X\n", c, watchw[c]);
                                                debug_out(outs);
                                                break;
                                        }
                                }
                                break;
                        }
                        if (!strncasecmp(ins, "wlist", 5))
                        {
                                for (c = 0; c < 8; c++)
                                {
                                        if (watchr[c] != -1)
                                        {
                                                sprintf(outs, "    Read watchpoint %i : %04X\n", c, watchr[c]);
                                                debug_out(outs);
                                        }
                                }
                                for (c = 0; c < 8; c++)
                                {
                                        if (watchw[c] != -1)
                                        {
                                                sprintf(outs, "    Write watchpoint %i : %04X\n", c, watchw[c]);
                                                debug_out(outs);
                                        }
                                }
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
                                bem_debugf("WriteM %04X %04X\n", e, f);
                                writemem(e, f);
                        }
                        break;
                        case 'q': case 'Q': 
                        setquit();
                        debug = 0;
                        indebug = 0;
                        return;
                        case 'h': case 'H': case '?':
                        sprintf(outs, "\n    Debugger commands :\n\n");
                        debug_out(outs);
                        sprintf(outs, "    bclear n   - clear breakpoint n or breakpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    bclearr n  - clear read breakpoint n or read breakpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    bclearw n  - clear write breakpoint n or write breakpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    blist      - list current breakpoints\n");
                        debug_out(outs);
                        sprintf(outs, "    break n    - set a breakpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    breakr n   - break on reads from address n\n");
                        debug_out(outs);
                        sprintf(outs, "    breakw n   - break on writes to address n\n");
                        debug_out(outs);
                        sprintf(outs, "    c          - continue running indefinitely\n");
                        debug_out(outs);
                        sprintf(outs, "    d [n]      - disassemble from address n\n");
                        debug_out(outs);
                        sprintf(outs, "    m [n]      - memory dump from address n\n");
                        debug_out(outs);
                        sprintf(outs, "    q          - force emulator exit\n");
                        debug_out(outs);
                        sprintf(outs, "    r          - print 6502 registers\n");
                        debug_out(outs);
                        sprintf(outs, "    r sysvia   - print System VIA registers\n");
                        debug_out(outs);
                        sprintf(outs, "    r uservia  - print User VIA registers\n");
                        debug_out(outs);
                        sprintf(outs, "    r crtc     - print CRTC registers\n");
                        debug_out(outs);
                        sprintf(outs, "    r vidproc  - print VIDPROC registers\n");
                        debug_out(outs);
                        sprintf(outs, "    r sound    - print Sound registers\n");
                        debug_out(outs);
                        sprintf(outs, "    s [n]      - step n instructions (or 1 if no parameter)\n\n");
                        debug_out(outs);
                        sprintf(outs, "    watchr n   - watch reads from address n\n");
                        debug_out(outs);
                        sprintf(outs, "    watchw n   - watch writes to address n\n");
                        debug_out(outs);
                        sprintf(outs, "    wclearr n  - clear read watchpoint n or read watchpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    wclearw n  - clear write watchpoint n or write watchpoint at n\n");
                        debug_out(outs);
                        sprintf(outs, "    writem a v - write to memory, a = address, v = value\n");
                        debug_out(outs);
                        break;
                }
                debug_lastcommand = ins[0];
//                WriteConsole(consf,"\n",1,NULL,NULL);
//                WriteConsole(consf,ins,strlen(ins),NULL,NULL);
        }
        fcount = 0;
        indebug = 0;
}
