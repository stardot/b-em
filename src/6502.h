typedef enum {
  sysVia,
  userVia,
} IRQ_Nums;

int singlestep;
unsigned char a,x,y,p,s;
unsigned short pc;

unsigned char intStatus;
unsigned char NMIStatus;
unsigned int NMILock;

unsigned char lastins;

int hadcli;

//void do6502();
void init6502();

FILE *logfile;
int log;

int truecycles;

#define CycleCountWrap 512

#define SetTrigger(after,var) var=totalcycles+after;
#define IncTrigger(after,var) var+=(after);

#define ClearTrigger(var) var=CycleCountTMax;

#define AdjustTrigger(var) if (var!=CycleCountTMax) var-=CycleCountWrap;

