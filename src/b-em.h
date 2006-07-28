#define printf rpclog
#include <allegro.h>

/*6502*/
void reset6502();
void dumpregs();
void exec6502(int lines, int cpl);
int interrupt;
int nmi,oldnmi,nmilock;
unsigned char a,x,y,s;
unsigned short pc;
struct
{
        int c,z,i,d,v,n;
} p;
unsigned char *ram;
unsigned char os[16384];
int output,timetolive;

/*VIAs*/
typedef struct VIA
{
        unsigned char ora,orb,ira,irb;
        unsigned char ddra,ddrb;
        unsigned long t1l,t2l;
        int t1c,t2c;
        unsigned char acr,pcr,ifr,ier;
        int t1hit,t2hit;
        unsigned char porta,portb;
} VIA;
VIA sysvia,uservia;

void resetsysvia();
unsigned char readsysvia(unsigned short addr);
//void writesysvia(unsigned short addr, unsigned char val,);
void updatesystimers();

/*Video*/
unsigned short vidbank;

/*Disc*/
#define SIDES 2
#define TRACKS 80
#define SECTORS 16
#define SECTORSIZE 256

unsigned char discs[2][SIDES][SECTORS*TRACKS][SECTORSIZE];
int ddensity,inreadop;
int idmarks;
int sectorsleft;
int cursec[2];
int curtrack[2],inttrack[2];
int fdiin[2];
int discaltered[2];
int sides[2];
int adfs[2];
int motorofff;
SAMPLE *seeksmp;
SAMPLE *seek2smp;
SAMPLE *seek3smp;
SAMPLE *stepsmp;
SAMPLE *motorsmp;
SAMPLE *motoroffsmp;
SAMPLE *motoronsmp;
int curside,curdisc;
int discint;
int sectorpos;
int readflash;
int driveled;

/*Config*/
int ddnoise;
int fasttape;
int hires;
int soundfilter;
char uefname[260];
int uefena;
int soundon;
int curwave;
int blurred,mono;
int ddnoise;
int model,tube;
char discname[2][260];
int quit;
int autoboot;
int fasttape;

/*Sound*/
AUDIOSTREAM *as;
int logging;

/*Video*/
BITMAP *buffer;
unsigned char crtc[32];
int interlaceline;
unsigned short vidmask;
unsigned long vidlimit;

/*ADC*/
int adcconvert;

/*Tube*/
int tubeirq;
int tubecycs;

/*Tape*/
int tapelcount,tapellatch;
int chunklen;
int blocks,motor;
int chunkid,chunklen,intone;

