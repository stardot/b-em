typedef struct {
  unsigned char ora,orb;
  unsigned char ira,irb;
  unsigned char ddra,ddrb;
  unsigned char acr,pcr;
  unsigned char ifr,ier;
  unsigned char sr;
  int timer1c,timer2c;
  int timer1l,timer2l;
  int timer1hasshot;
  int timer2hasshot;
  unsigned char pal,pbl;
  unsigned char pale,pble;
} VIAState;
VIAState SysVIA;
VIAState UserVIA;
int oddcycle;
unsigned char IC32State;
unsigned char UIC32State;
void updateIFRtopbit(void);
void checkkey();
void IC32Write(unsigned char Value);
void SlowDataBusWrite(unsigned char Value);
int SlowDataBusRead();
void SVIAWrite(int Address, int Value);
int SysVIARead(int Address);
int UVIARead(int Address);
void UVIAWrite(int Address, int Value);
#define SysVIA_poll(ncycles) \
  SysVIA.timer1c-=(ncycles>>1)+oddcycle; \
  SysVIA.timer2c-=(ncycles>>1)+oddcycle; \
  if ((SysVIA.timer1c<0) || (SysVIA.timer2c<0)) SysVIA_poll_real();

#define UserVIA_poll(ncycles) \
  UserVIA.timer1c-=(ncycles>>1)+oddcycle; \
  UserVIA.timer2c-=(ncycles>>1)+oddcycle; \
  oddcycle=ncycles&1;\
  if ((UserVIA.timer1c<0) || (UserVIA.timer2c<0)) UserVIA_poll_real();
void UserVIA_poll_real();
void SysVIA_poll_real();
void SVIAReset();
void UVIAReset();
void SysVIATriggerCA1Int(int value);
void presskey(int row, int col);
void releasekey(int row, int col);
int lastcol,lastrow;
unsigned int KBDRow;
unsigned int KBDCol;
int Keysdown;
char bbckey[10][8];
