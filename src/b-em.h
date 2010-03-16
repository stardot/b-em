/*B-em v2.0 by Tom Walker
  Main header file*/

#include <stdint.h>
#define printf rpclog

extern uint8_t *ram,*rom,*os;
extern int romsel;
extern int swram[16];

extern uint8_t a,x,y,s;
extern uint16_t pc;
typedef struct PREG
{
        int c,z,i,d,v,n;
} PREG;

extern PREG p;


extern int output;
extern int timetolive;
extern int interrupt;
extern int nmi;

extern int motorspin;
extern int fdctime;
extern int motoron;
extern int disctime;

extern int frames;

typedef struct VIA
{
        uint8_t ora,orb,ira,irb;
        uint8_t ddra,ddrb;
        uint32_t t1l,t2l;
        int t1c,t2c;
        uint8_t acr,pcr,ifr,ier;
        int t1hit,t2hit;
        uint8_t porta,portb;
        int ca1,ca2;
} VIA;
extern VIA sysvia,uservia;

extern int scrsize;
extern uint16_t vidbank;
extern int tapelcount,tapellatch;

//#define WD1770 0
//#define I8271 1

extern int WD1770,I8271,BPLUS,x65c02,MASTER,MODELA,OS01;

void (*fdccallback)();
void (*fdcdata)(uint8_t dat);
void (*fdcspindown)();
void (*fdcfinishread)();
void (*fdcnotfound)();
void (*fdcdatacrcerror)();
void (*fdcheadercrcerror)();
void (*fdcwriteprotect)();
int  (*fdcgetdata)(int last);

extern int writeprot[2],fwriteprot[2];
//extern int hc;
extern uint8_t opcode;

void initvideo();
void closevideo();
void makemode7chars();
void pollvideo(int clocks);
uint8_t readcrtc(uint16_t addr);
void writecrtc(uint16_t addr, uint8_t val);

void initmem();
void loadroms();

void reset6502();
void exec6502();
void exec65c02();
void dumpregs();
void dumpram();
void writemem(uint16_t addr, uint8_t val);

void updatesystimers();
void vblankint();
void vblankintlow();
void writesysvia(uint16_t addr, uint8_t val);
uint8_t readsysvia(uint16_t addr);
void resetsysvia();
void checkkeys();
void clearkeys();

void updateusertimers();
void writeuservia(uint16_t addr, uint8_t val);
uint8_t readuservia(uint16_t addr);
void resetuservia();

void ssd_reset();
void ssd_load(int drive, char *fn);
void ssd_close(int drive);
void dsd_load(int drive, char *fn);
void ssd_seek(int drive, int track);
void ssd_readsector(int drive, int sector, int track, int side, int density);
void ssd_writesector(int drive, int sector, int track, int side, int density);
void ssd_readaddress(int drive, int sector, int side, int density);
void ssd_format(int drive, int sector, int side, int density);
void ssd_poll();

void adf_reset();
void adf_load(int drive, char *fn);
void adf_close(int drive);
void adl_load(int drive, char *fn);
void adf_seek(int drive, int track);
void adf_readsector(int drive, int sector, int track, int side, int density);
void adf_writesector(int drive, int sector, int track, int side, int density);
void adf_readaddress(int drive, int sector, int side, int density);
void adf_format(int drive, int sector, int side, int density);
void adf_poll();

void fdi_reset();
void fdi_load(int drive, char *fn);
void fdi_close(int drive);
void fdi_seek(int drive, int track);
void fdi_readsector(int drive, int sector, int track, int side, int density);
void fdi_writesector(int drive, int sector, int track, int side, int density);
void fdi_readaddress(int drive, int sector, int side, int density);
void fdi_format(int drive, int sector, int side, int density);
void fdi_poll();

void loaddisc(int drive, char *fn);
void newdisc(int drive, char *fn);
void closedisc(int drive);
void disc_reset();
void disc_poll();
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);

void setejecttext(int drive, char *fn);

void loaddiscsamps();
void mixddnoise();

void resetacia();
uint8_t readacia(uint16_t addr);
void writeacia(uint16_t addr, uint8_t val);
void dcd();
void dcdlow();
void receive(uint8_t val);
void pollacia();

void reset1770();
uint8_t read1770(uint16_t addr);
void write1770(uint16_t addr, uint8_t val);

void reset8271();
uint8_t read8271(uint16_t addr);
void write8271(uint16_t addr, uint8_t val);

void writecrtc(uint16_t addr, uint8_t val);
void writeula(uint16_t addr, uint8_t val);

void initserial();
void writeserial(uint16_t addr, uint8_t val);

void openuef(char *fn);

int OSFILE();
int OSFSC();

void rpclog(const char *format, ...);

void polltape();
void pollcsw();

int getpd();

extern char exedir[512];

typedef struct
{
        char name[32];
        int I8271,WD1770;
        int x65c02;
        int bplus;
        int master;
        int swram;
        int modela;
        int os01;
        char os[32];
        char romdir[32];
        char cmos[32];
        void (*romsetup)();
        int tube;
} MODEL;

extern int curmodel;
extern int curtube;
extern int oldmodel;

void romsetup_os01();
void romsetup_bplus128();
void romsetup_master128();
void romsetup_mastercompact();
void fillswram();
void loadcmos(MODEL m);

struct
{
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density);
        void (*writesector)(int drive, int sector, int track, int side, int density);
        void (*readaddress)(int drive, int track, int side, int density);
        void (*format)(int drive, int track, int side, int density);
        void (*poll)();
} drives[2];

int curdrive;

extern int logging;


void initalmain(int argc, char *argv[]);
void closeal();
void inital();
void givealbuffer(short *buf);

int readc[65536],writec[65536],fetchc[65536];

extern int debug,debugon;
void startdebug();
void debugread(uint16_t addr);
void debugwrite(uint16_t addr, uint8_t val);
void dodebugger();

extern int tubetimetolive,tubeoutput;
extern int tubecycs,tubelinecycs;
extern int tube;
extern int tubeskipint;
extern int tubenmi,tubeoldnmi,tubenmilock;
extern int tubeirq;
int tubeshift;
int tubespeed;

void resettube();
void tubeinit6502();
void tubeinitarm();
void tubeinitz80();
void tubeinitx86();
void tubeinit65816();
void tubeexec65c02();
void execarm();
void execz80();
void execx86();
void exec65816();
void tubereset6502();
void resetarm();
void resetz80();
void resetx86();
void reset65816();
void dumpregs65816();
uint8_t readtubehost(uint16_t addr);
void writetubehost(uint16_t addr, uint8_t val);
uint8_t readtube(uint32_t addr);
void writetube(uint32_t addr, uint8_t val);

void (*tubeexec)();
extern int tubecycles;

extern int interlace;
extern int fskipmax,comedyblit;

extern int adcconvert;
extern int joybutton[2];
extern int fasttape;
extern int fullborders;

extern int selecttube;

extern int cursid,sidmethod;

char *getmodel();

void initresid();
void resetsid();
void setsidtype(int resamp, int model);
uint8_t readsid(uint16_t addr);
void writesid(uint16_t addr, uint8_t val);

void initsound();

void initadc();
uint8_t readadc(uint16_t addr);
void writeadc(uint16_t addr, uint8_t val);

void waitforready();
void resumeready();

extern int sndinternal,sndbeebsid,sndddnoise,sndtape;

extern int keylookup[128];
extern int keyas;

extern char discfns[2][260];
extern char tapefn[260];
extern int tube6502speed;
extern int fullscreen;
extern int soundfilter;
extern int curwave;
extern int ddvol,ddtype;
extern int linedbl;
extern int cswena;


extern int savescrshot;
extern char scrshotname[260];
extern int tapeloaded;
extern int defaultwriteprot;
void setquit();
