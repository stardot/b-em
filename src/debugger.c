/*B-em v2.0 by Tom Walker
  Debugger*/

int debug;
int indebug=0;
extern int fcount;

#ifdef WIN32
#include <windows.h>
#include <wingdi.h>
#include "b-em.h"


HANDLE debugthread;
HWND dhwnd;
char DebugszClassName[ ] = "B-emDebugWnd";
LRESULT CALLBACK DebugWindowProcedure (HWND, UINT, WPARAM, LPARAM);
HINSTANCE hinst;
uint8_t *usdat;

void _debugthread(PVOID pvoid)
{
        MSG messages = {0};     /* Here messages to the application are saved */
        WNDCLASSEX wincl;        /* Data structure for the windowclass */

        HDC hDC;
        PAINTSTRUCT ps;
        int x,y;
        int c,d;

        usdat=malloc(256*256*4);

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
        dhwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           DebugszClassName,         /* Classname */
           "Memory viewer",       /* Title Text */
           WS_OVERLAPPEDWINDOW, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           256+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),/* The programs width */
           256+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYCAPTION)+2,/* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           NULL, /* No menu */
           hinst,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );
           printf("Window create %08X\n",dhwnd);
        ShowWindow (dhwnd, SW_SHOWNORMAL);

                hDC = GetDC(dhwnd);

    HDC memDC = CreateCompatibleDC ( hDC );
    if (!memDC) printf("memDC failed!\n");
    HBITMAP memBM = CreateCompatibleBitmap ( hDC, 256, 256 );
    if (!memBM) printf("memBM failed!\n");
    SelectObject ( memDC, memBM );

    BITMAPINFO lpbmi;
    lpbmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    lpbmi.bmiHeader.biWidth=256;
    lpbmi.bmiHeader.biHeight=-256;
    lpbmi.bmiHeader.biPlanes=1;
    lpbmi.bmiHeader.biBitCount=32;
    lpbmi.bmiHeader.biCompression=BI_RGB;
    lpbmi.bmiHeader.biSizeImage=256*256*4;
    lpbmi.bmiHeader.biClrUsed=0;
        lpbmi.bmiHeader.biClrImportant=0;

        while (1)
        {
                Sleep(20);


                c+=16;
                d=0;
                for (x=0;x<65536;x++)
                {
                        usdat[d++]=fetchc[x]*8;
                        usdat[d++]=readc[x]*8;
                        usdat[d++]=writec[x]*8;
                        usdat[d++]=0;
                        if (fetchc[x]) fetchc[x]--;
                        if (readc[x]) readc[x]--;
                        if (writec[x]) writec[x]--;
                }

                SetDIBitsToDevice(memDC,0,0,256,256,0,0,0,256,usdat,&lpbmi,DIB_RGB_COLORS);
                BitBlt(hDC,0,0,256,256,memDC,0,0,SRCCOPY);

                if (PeekMessage(&messages,NULL,0,0,PM_REMOVE))
                {
                        /* Translate virtual-key messages into character messages */
                        TranslateMessage(&messages);
                        /* Send message to WindowProcedure */
                        DispatchMessage(&messages);
                }
//                if (indebug) pollmainwindow();
        }
}

HANDLE consf,cinf;

BOOL CtrlHandler(DWORD fdwCtrlType)
{
        setquit();
        return TRUE;
}

void startdebug()
{

        if (debug)
        {
        hinst=GetModuleHandle(NULL);
        debugthread=(HANDLE)_beginthread(_debugthread,0,NULL);
                AllocConsole();
                SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler,TRUE);
                consf=GetStdHandle(STD_OUTPUT_HANDLE);
                cinf=GetStdHandle(STD_INPUT_HANDLE);
        }
//        AllocConsole();
//        consf=GetStdHandle(STD_OUTPUT_HANDLE);
//        cinf=GetStdHandle(STD_INPUT_HANDLE);
//        WriteConsole(consf,"Hello",5,NULL,NULL);
}

void killdebug()
{
        TerminateThread(debugthread,0);
        if (usdat) free(usdat);
}

LRESULT CALLBACK DebugWindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HMENU hmenu;
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

void debugout(char *s)
{
        WriteConsole(consf,s,strlen(s),NULL,NULL);
}

#else

#include <stdio.h>
#include "b-em.h"

#undef printf

void debugout(char *s)
{
        printf(s);
fflush(stdout);
}

void startdebug()
{
}

#endif

int debugopen=0;

uint16_t debugmemaddr=0;
uint16_t debugdisaddr=0;
uint8_t debuglastcommand=0;

uint8_t dreadmem(uint16_t addr)
{
        if (addr>=0xFC00 && addr<0xFF00) return 0xFF;
        return readmem(addr);
}
enum
{
        IMP,IMPA,IMM,ZP,ZPX,ZPY,INDX,INDY,IND,ABS,ABSX,ABSY,IND16,IND1X,BRA
};
uint8_t dopname[256][6]=
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

int dopaddr[256]=
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

uint8_t dopnamenmos[256][6]=
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

int dopaddrnmos[256]=
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


void debugdisassemble()
{
        uint16_t temp;
        char s[256];
        uint8_t op=dreadmem(debugdisaddr);
        uint8_t p1=dreadmem(debugdisaddr+1),p2=dreadmem(debugdisaddr+2);
        sprintf(s,"%04X : ",debugdisaddr);
        debugout(s);
        debugout(dopname[op]);
        debugout(" ");
        switch (dopaddr[op])
        {
                case IMP:
                sprintf(s,"        ",p1);
                debugout(s);
                break;
                case IMPA:
                sprintf(s,"A       ",p1);
                debugout(s);
                break;
                case IMM:
                sprintf(s,"#%02X     ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case ZP:
                sprintf(s,"%02X      ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case ZPX:
                sprintf(s,"%02X,X    ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case ZPY:
                sprintf(s,"%02X,Y    ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case IND:
                sprintf(s,"(%02X)    ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case INDX:
                sprintf(s,"(%02X,X)  ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case INDY:
                sprintf(s,"(%02X),Y  ",p1);
                debugout(s);
                debugdisaddr++;
                break;
                case ABS:
                sprintf(s,"%02X%02X    ",p2,p1);
                debugout(s);
                debugdisaddr+=2;
                break;
                case ABSX:
                sprintf(s,"%02X%02X,X  ",p2,p1);
                debugout(s);
                debugdisaddr+=2;
                break;
                case ABSY:
                sprintf(s,"%02X%02X,Y  ",p2,p1);
                debugout(s);
                debugdisaddr+=2;
                break;
                case IND16:
                sprintf(s,"(%02X%02X)  ",p2,p1);
                debugout(s);
                debugdisaddr+=2;
                break;
                case IND1X:
                sprintf(s,"(%02X%02X,X)",p2,p1);
                debugout(s);
                debugdisaddr+=2;
                break;
                case BRA:
                temp=debugdisaddr+2+(signed char)p1;
                sprintf(s,"%04X    ",temp);
                debugout(s);
                debugdisaddr++;
                break;
        }
        debugdisaddr++;
//        WriteConsole(consf,"\n",strlen("\n"),NULL,NULL);
}

int breakpoints[8]={-1,-1,-1,-1,-1,-1,-1,-1};
int breakr[8]={-1,-1,-1,-1,-1,-1,-1,-1};
int breakw[8]={-1,-1,-1,-1,-1,-1,-1,-1};
int watchr[8]={-1,-1,-1,-1,-1,-1,-1,-1};
int watchw[8]={-1,-1,-1,-1,-1,-1,-1,-1};
int debugstep=0;

void debugread(uint16_t addr)
{
        int c;
        char outs[256];
        for (c=0;c<8;c++)
        {
                if (breakr[c]==addr)
                {
                        debug=1;
                        sprintf(outs,"    Break on read from %04X\n",addr);
                        debugout(outs);
                        return;
                }
                if (watchr[c]==addr)
                {
                        sprintf(outs,"    Read from %04X - A=%02X X=%02X Y=%02X PC=%04X\n",addr,a,x,y,pc);
                        debugout(outs);
                }
        }
}
void debugwrite(uint16_t addr, uint8_t val)
{
        int c;
        char outs[256];
        for (c=0;c<8;c++)
        {
                if (breakw[c]==addr)
                {
                        debug=1;
                        sprintf(outs,"    Break on write to %04X - val %02X\n",addr,val);
                        debugout(outs);
                        return;
                }
                if (watchw[c]==addr)
                {
                        sprintf(outs,"    Write %02X to %04X - A=%02X X=%02X Y=%02X PC=%04X\n",val,addr,a,x,y,pc);
                        debugout(outs);
                }
        }
}

void dodebugger()
{
        int c,d,e;
        int params;
        uint8_t temp;
        char outs[256];
        char ins[256];
        if (pc==0xFFDD)
        {
                sprintf(outs,"    OSFILE!\n",pc);
                debugout(outs);
                c=4;
                d=ram[x+(y<<8)]|(ram[x+(y<<8)+1]<<8);
                while (ram[d]!=13)
                {
                        outs[c++]=ram[d++];
                }
                outs[c]=0;
                debugout(outs);
                sprintf(outs,"  %02X%02X\n",ram[x+(y<<8)+3],ram[x+(y<<8)+2]);
                debugout(outs);
        }

//        if (!opcode) debug=1;

        for (c=0;c<8;c++)
        {
                if (breakpoints[c]==pc)
                {
                        debug=1;
                        sprintf(outs,"    Break at %04X\n",pc);
                        debugout(outs);
                }
        }
        if (!debug) return;
        if (!opcode) printf("BRK at %04X\n",pc);
        if (debugstep)
        {
                debugstep--;
                if (debugstep) return;
        }
/*        if (!debugopen)
        {
                debugopen=1;
                debugdisaddr=pc;
        }*/
        indebug=1;
        while (1)
        {
                d=debugdisaddr;
                debugdisaddr=pc;
                debugdisassemble();
                debugdisaddr=d;
                sprintf(outs,"  >",pc);
                debugout(outs);
#ifdef WIN32
                c=ReadConsoleA(cinf,ins,255,&d,NULL);
                ins[d]=0;
#else
                d=fgets(ins,255,stdin);
//                gets(ins);
#endif
//printf("Got %s %i\n",ins,d);
//                rpclog("Got %s\n",ins);
//                rpclog("%i chars %i chars return %i\n",strlen(ins),d,c);
                d=0;
                while (ins[d]!=32 && ins[d]!=0xA && ins[d]!=0xD && ins[d]!=0) d++;
                while (ins[d]==32) d++;
                if (ins[d]==0xA || ins[d]==0xD || ins[d]==0) params=0;
                else                                         params=1;

                if (ins[0]==0xA || ins[0]==0xD) ins[0]=debuglastcommand;
//debugout("Processing!\n");
                switch (ins[0])
                {
                        case 'c': case 'C':
                        debug=0;
//                        debugopen=0;
//                        FreeConsole();
                        indebug=0;
                        return;
                        case 'm': case 'M':
                        if (params) sscanf(&ins[d],"%X",&debugmemaddr);
                        for (c=0;c<16;c++)
                        {
                                sprintf(outs,"    %04X : ",debugmemaddr);
                                debugout(outs);
                                for (d=0;d<16;d++)
                                {
                                        sprintf(outs,"%02X ",dreadmem(debugmemaddr+d));
                                        debugout(outs);
                                }
                                debugout("  ");
                                for (d=0;d<16;d++)
                                {
                                        temp=dreadmem(debugmemaddr+d);
                                        if (temp<32) sprintf(outs,".");
                                        else         sprintf(outs,"%c",temp);
                                        debugout(outs);
                                }
                                debugmemaddr+=16;
                                debugout("\n");
                        }
                        break;
                        case 'd': case 'D':
                        if (params) sscanf(&ins[d],"%X",&debugdisaddr);
                        for (c=0;c<12;c++)
                        {
                                debugout("    ");
                                debugdisassemble();
                                debugout("\n");
                        }
                        break;
                        case 'r': case 'R':
                        sprintf(outs,"    6502 registers :\n");
                        debugout(outs);
                        sprintf(outs,"    A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n",a,x,y,s,pc);
                        debugout(outs);
                        sprintf(outs,"    Status : %c%c%c%c%c%c\n",(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ');
                        debugout(outs);
                        break;
                        case 's': case 'S':
                        if (params) sscanf(&ins[d],"%i",&debugstep);
                        else        debugstep=1;
                        debuglastcommand=ins[0];
                        indebug=0;
                        return;
                        case 'b': case 'B':
                        if (!strncasecmp(ins,"breakr",6))
                        {
                                if (!params) break;
                                for (c=0;c<8;c++)
                                {
                                        if (breakpoints[c]==-1)
                                        {
                                                sscanf(&ins[d],"%X",&breakr[c]);
                                                sprintf(outs,"    Read breakpoint %i set to %04X\n",c,breakr[c]);
                                                debugout(outs);
                                                break;
                                        }
                                }
                        }
                        else if (!strncasecmp(ins,"breakw",6))
                        {
                                if (!params) break;
                                for (c=0;c<8;c++)
                                {
                                        if (breakpoints[c]==-1)
                                        {
                                                sscanf(&ins[d],"%X",&breakw[c]);
                                                sprintf(outs,"    Write breakpoint %i set to %04X\n",c,breakw[c]);
                                                debugout(outs);
                                                break;
                                        }
                                }
                        }
                        else if (!strncasecmp(ins,"break",5))
                        {
                                if (!params) break;
                                for (c=0;c<8;c++)
                                {
                                        if (breakpoints[c]==-1)
                                        {
                                                sscanf(&ins[d],"%X",&breakpoints[c]);
                                                sprintf(outs,"    Breakpoint %i set to %04X\n",c,breakpoints[c]);
                                                debugout(outs);
                                                break;
                                        }
                                }
                        }
                        if (!strncasecmp(ins,"blist",5))
                        {
                                for (c=0;c<8;c++)
                                {
                                        if (breakpoints[c]!=-1)
                                        {
                                                sprintf(outs,"    Breakpoint %i : %04X\n",c,breakpoints[c]);
                                                debugout(outs);
                                        }
                                }
                                for (c=0;c<8;c++)
                                {
                                        if (breakr[c]!=-1)
                                        {
                                                sprintf(outs,"    Read breakpoint %i : %04X\n",c,breakr[c]);
                                                debugout(outs);
                                        }
                                }
                                for (c=0;c<8;c++)
                                {
                                        if (breakw[c]!=-1)
                                        {
                                                sprintf(outs,"    Write breakpoint %i : %04X\n",c,breakr[c]);
                                                debugout(outs);
                                        }
                                }
                        }
                        if (!strncasecmp(ins,"bclearr",7))
                        {
                                if (!params) break;
                                sscanf(&ins[d],"%X",&e);
                                for (c=0;c<8;c++)
                                {
                                        if (breakr[c]==e) breakr[c]=-1;
                                        if (c==e) breakr[c]=-1;
                                }
                        }
                        else if (!strncasecmp(ins,"bclearw",7))
                        {
                                if (!params) break;
                                sscanf(&ins[d],"%X",&e);
                                for (c=0;c<8;c++)
                                {
                                        if (breakw[c]==e) breakw[c]=-1;
                                        if (c==e) breakw[c]=-1;
                                }
                        }
                        else if (!strncasecmp(ins,"bclear",6))
                        {
                                if (!params) break;
                                sscanf(&ins[d],"%X",&e);
                                for (c=0;c<8;c++)
                                {
                                        if (breakpoints[c]==e) breakpoints[c]=-1;
                                        if (c==e) breakpoints[c]=-1;
                                }
                        }
                        break;
                        case 'w': case 'W':
                        if (!strncasecmp(ins,"watchr",6))
                        {
                                if (!params) break;
                                for (c=0;c<8;c++)
                                {
                                        if (watchr[c]==-1)
                                        {
                                                sscanf(&ins[d],"%X",&watchr[c]);
                                                sprintf(outs,"    Read watchpoint %i set to %04X\n",c,watchr[c]);
                                                debugout(outs);
                                                break;
                                        }
                                }
                                break;
                        }
                        if (!strncasecmp(ins,"watchw",6))
                        {
                                if (!params) break;
                                for (c=0;c<8;c++)
                                {
                                        if (watchw[c]==-1)
                                        {
                                                sscanf(&ins[d],"%X",&watchw[c]);
                                                sprintf(outs,"    Write watchpoint %i set to %04X\n",c,watchw[c]);
                                                debugout(outs);
                                                break;
                                        }
                                }
                                break;
                        }
                        if (!strncasecmp(ins,"wlist",5))
                        {
                                for (c=0;c<8;c++)
                                {
                                        if (watchr[c]!=-1)
                                        {
                                                sprintf(outs,"    Read watchpoint %i : %04X\n",c,watchr[c]);
                                                debugout(outs);
                                        }
                                }
                                for (c=0;c<8;c++)
                                {
                                        if (watchw[c]!=-1)
                                        {
                                                sprintf(outs,"    Write watchpoint %i : %04X\n",c,watchr[c]);
                                                debugout(outs);
                                        }
                                }
                        }
                        if (!strncasecmp(ins,"wclearr",7))
                        {
                                if (!params) break;
                                sscanf(&ins[d],"%X",&e);
                                for (c=0;c<8;c++)
                                {
                                        if (watchr[c]==e) watchr[c]=-1;
                                        if (c==e) watchr[c]=-1;
                                }
                        }
                        else if (!strncasecmp(ins,"wclearw",7))
                        {
                                if (!params) break;
                                sscanf(&ins[d],"%X",&e);
                                for (c=0;c<8;c++)
                                {
                                        if (watchw[c]==e) watchw[c]=-1;
                                        if (c==e) watchw[c]=-1;
                                }
                        }
                        break;
                        case 'q': case 'Q':
                        setquit();
                        while (1);
                        break;
                        case 'h': case 'H': case '?':
                        sprintf(outs,"\n    Debugger commands :\n\n");
                        debugout(outs);
                        sprintf(outs,"    bclear n  - clear breakpoint n or breakpoint at n\n");
                        debugout(outs);
                        sprintf(outs,"    bclearr n - clear read breakpoint n or read breakpoint at n\n");
                        debugout(outs);
                        sprintf(outs,"    bclearw n - clear write breakpoint n or write breakpoint at n\n");
                        debugout(outs);
                        sprintf(outs,"    blist     - list current breakpoints\n");
                        debugout(outs);
                        sprintf(outs,"    break n   - set a breakpoint at n\n");
                        debugout(outs);
                        sprintf(outs,"    breakr n  - break on reads from address n\n");
                        debugout(outs);
                        sprintf(outs,"    breakw n  - break on writes to address n\n");
                        debugout(outs);
                        sprintf(outs,"    c         - continue running indefinitely\n");
                        debugout(outs);
                        sprintf(outs,"    d [n]     - disassemble from address n\n");
                        debugout(outs);
                        sprintf(outs,"    m [n]     - memory dump from address n\n");
                        debugout(outs);
                        sprintf(outs,"    q         - force emulator exit\n");
                        debugout(outs);
                        sprintf(outs,"    r         - print 6502 registers\n");
                        debugout(outs);
                        sprintf(outs,"    s [n]     - step n instructions (or 1 if no parameter)\n\n");
                        debugout(outs);
                        sprintf(outs,"    watchr n  - watch reads from address n\n");
                        debugout(outs);
                        sprintf(outs,"    watchw n  - watch writes to address n\n");
                        debugout(outs);
                        sprintf(outs,"    wclearr n - clear read watchpoint n or read watchpoint at n\n");
                        debugout(outs);
                        sprintf(outs,"    wclearw n - clear write watchpoint n or write watchpoint at n\n");
                        debugout(outs);
                        break;
                }
                debuglastcommand=ins[0];
//                WriteConsole(consf,"\n",1,NULL,NULL);
//                WriteConsole(consf,ins,strlen(ins),NULL,NULL);
        }
        fcount=0;
        indebug=0;
}
