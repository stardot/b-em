/*6502*/
void reset6502();
void dumpregs();
void exec6502(int lines, int cpl);
int interrupt;
int nmi,oldnmi,nmilock;
struct
{
        int c,z,i,d,v,n;
} p;

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
