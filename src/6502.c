/*           ██████████            █████████  ████      ████
             ██        ██          ██         ██  ██  ██  ██
             ██        ██          ██         ██    ██    ██
             ██████████     █████  █████      ██          ██
             ██        ██          ██         ██          ██
             ██        ██          ██         ██          ██
             ██████████            █████████  ██          ██

                     BBC Model B Emulator Version 0.3


              All of this code is (C)opyright Tom Walker 1999
         You may use SMALL sections from this program (ie 20 lines)
       If you want to use larger sections, you must contact the author

              If you don't agree with this, don't use B-Em

*/

/*6502 emulation*/

#define INLINE static inline

#define UNDOCUMENTEDINS
int screencount=0;
int videocycles;
int temp=0;
int acaitime=33333;
int videoline,scancount;
static int cycletable[256]=
{
        7,6,0,0,0,3,5,5,3,2,2,0,0,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
        6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
        6,6,0,0,0,3,5,0,3,2,2,2,3,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
        6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
        2,6,0,0,3,3,3,3,2,0,2,0,4,4,4,0,
        2,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
        2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0,
        2,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
        2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,4,4,7,0,
        2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0,
        2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0
};

int waitforkey=0;
char string[80];
int oldpc;
unsigned int Cycles=0;
unsigned long TotalCycles=0;
int OldNMIStatus;
int viacycles=0;
int videodelay=0;
int sounddelay=0;
int printertime,printtarget;
char disassemble[20];
unsigned short tempaddr;
INLINE unsigned char readmem(unsigned short address)
{
/*        if (log && !(p&4))
        {
                ////sprintf(string,"Read %X %X\n",address,ram[address]);
                fputs(string,logfile);
        }*/
//        oldraddr=address;
        if ((address > 0xfc00) && (address < 0xff00))
        {
                if (address>0xFE0F&&address<0xFE20)
                   return readserial(address);
                if ((address&~0x1F)==0xFEC0)
                   return readadc(address);
                if ((address & ~0xf)==0xfe40 || (address & ~0xf)==0xfe50)
                   return SysVIARead(address & 0xf);
                if (address==0xFE60)
                   return rand()&255;
                if (address==0xFE70)
                   return rand()&255;
                if ((address & ~0xf)==0xfe60 || (address & ~0xf)==0xfe70)
                   return UVIARead(address & 0xf);
                if ((address & ~15)==0xfe00)
                   return CRTCRead(address & 0x1);
                if ((address & ~0xf)==0xfe20)
                   return ULARead(address & 0xf);
                if ((address&~0xF)==0xFE30)
                   return currom;
                if ((address&~0x1F)==0xFE80)
                   return read8271(address);
                return 0xfe;
        }
//        ////sprintf(string,"Read %X\n",address);
//        fputs(string,logfile);
        return ram[address];
}

#define fastwrite(addr,val)    \
        tempaddr=addr;           \
        if (tempaddr<0x8000)       \
           ram[tempaddr]=val;      \
        else                   \
           writemem(tempaddr,val);

//#define fastread(addr) ((tempaddr=addr)<0xFC00) ? ram[tempaddr] : readmem(tempaddr)
#define fastread(addr) readmem(addr)
int inint;

void init6502()
{
        a=x=y=0;
        s=0xFF;
        intStatus=0;
        NMIStatus=0;
        NMILock=0;
        pc=fastread(0xfffc) | (fastread(0xfffd)<<8);
        p=0x24;
        inint=0;
}

unsigned char val,oldval;
signed char branch;

#define PUSH(val)\
        fastwrite(0x100+s,val);\
        s--;

#define PULL() fastread(0x100+(++s))

#ifdef FASTFLAGS

#define setzn(val) p&=~0x82; p|=((val==0)<<1) | (val & 128)
#define setczn(c,z,n) p&=~131; if (c) p|=1; if (z) p|=2; if (n) p|=128
#define setczvn(c,z,v,n) p&=~0xC3; p|=(c!=0) | ((z!=0)<<1) | ((v!=0)<<6) | ((n!=0)<<7)
#define setzvn(z,v,n) p&=~194; p |= ((z!=0)<<1) | ((v!=0)<<6) | ((n!=0)<<7)

#else

INLINE void setzn(const unsigned char in)
{
  p&=~(2 | 0x80);
  p|=((in==0)<<1) | (in & 128);
}

/*INLINE void setzn(unsigned char val)
{
        p &=~130;
        if (val==0)
           p|=2;
        p |=(val & 128);
}*/

INLINE void setczn(int c,int z, int n)
{
  p&=~(3 | 0x80);
  p|=(c!=0) | ((z!=0)<<1) | ((n!=0)<<7);
}

/*INLINE void setczn(int c,int z, int n)
{
        p &= ~131;
        if (c)
           p|=1;
        if (z)
           p|=2;
        if (n)
           p|=128;
//        p |= (c!=0) | ((z!=0)<<1) | ((n!=0)<<7);
}*/

INLINE void setczvn(int c,int z,int v, int n)
{
        p&=~0xC3;
        p|=(c!=0) | ((z!=0)<<1) | ((v!=0)<<6) | ((n!=0)<<7);
}

/*INLINE void setczvn(int c, int z, int v, int n)
{
//        p &= ~;
        if (!oldczvn)
        {
        p|=1;
        p|=2;
        p|=64;
        p|=128;
        p^=1;
        p^=2;
        p^=64;
        p^=128;
        if (c)
           p|=1;
        if (z)
           p|=2;
        if (v)
           p|=64;
        if (n)
           p|=128;
        }
        else
        {
        p|=1;
        p|=2;
        p|=64;
        p|=128;
        p^=1;
        p^=2;
        p^=64;
        p^=128;
        p |= (c!=0) | ((z!=0)<<1) | ((v!=0)<<6) | ((n!=0)<<7);
        }
}*/

INLINE void setzvn(int z, int v, int n)
{
        p &= ~194;
        if (z)
           p|=2;
        if (v)
           p|=64;
        if (n)
           p|=128;
//        p |= ((z!=0)<<1) | ((v!=0)<<6) | ((n!=0)<<7);
}
#endif

INLINE void ADCBCD(unsigned char s)
{
        unsigned
           AL=a&0xF, /* low nybble of accumulator */
           AH=a>>4, /* high nybble of accumulator */

           C=p&1,  /* Carry flag */
           Z=p&2,  /* Zero flag */
           V=p&64,  /* oVerflow flag */
           N=p&128;  /* Negative flag */
        unsigned char olda=a;
                int                     vlo = (s) & 0x0f;
                int                     vhi = (s) >> 4;

                AL += vlo + C;
                if ( AL >= 10 )
                {
                        AL -= 10;
                        AH++;
                }
                AH += vhi;
                a = ( AL + ( AH << 4 )) & 0xff;
//                Z=(a==0);
//                N=(a&0x80);
//                V=(!((olda^s)&0x80)&&((olda^a)&0x80));
                C = 0;
                if ( AH >= 10 )
                {
                        AH += 6;
                        if ( AH > 15 )
                                C=1;
                        AH &= 0xf;
                }
                a = AL + ( AH << 4 );

        setczvn(C,Z,V,N);
}

INLINE void ADC(unsigned short addr)
{
        unsigned char val=fastread(addr);
        short tempc=0,tempv=0,tempn=0,tempz=0;
        int tempres,ln,hn;
        int newa;
        unsigned char olda=a;
        int C=p&1,Z=p&2,V=p&64,N=p&128;
        if (!(p&8))
        {
                newa = a + (val) + C;
                C = newa >> 8;
                a = newa & 0xff;
                Z=(a==0);
                N=(a&0x80);
                V=(!((olda^val)&0x80)&&((olda^a)&0x80));
                setczvn(C,Z,V,N);
        }
        else
        {
                ADCBCD(val);
        }
}

#define ANC(val) a=val&a; setzn(a); if (a&128) p|=1
#define AND(addr) a&=fastread(addr); setzn(a)
#define ANE(addr) a=(a|0xEE)&x&fastread(addr)

INLINE void ARR(unsigned short addr)
{
        int C=p&1,Z=p&2,V=p&0x40,N=p&0x80;
        unsigned char AL,AH;
        val=fastread(addr);
        if (!(p&8))
        {
                a&=val;
                a>>=1;
                if (C)
                   a|=128;
                Z=(a==0);
                N=a&0x80;
                V=a&0x40;
                C=(V?1:0)^((a&0x20)?1:0);
                setczvn(C,Z,V,N);
        }
        else
        {
                oldval = a & val;

                AH = oldval >> 4;
                AL = oldval & 15;

                N = C;
                Z = !(a = (oldval >> 1) | (C << 7));
                V = (val^a) & 64;

                if (AL + (AL & 1) > 5)
                   a = (a & 0xF0) | ((a + 6) & 0xF);

                if (C = AH + (AH & 1) > 5)
                   a = (a + 0x60) & 0xFF;
                setczvn(C,Z,V,N);
        }
}

INLINE void ASL(unsigned short addr)
{
        unsigned char val=fastread(addr);
        unsigned char oldval=val;
        fastwrite(addr,val);
        val<<=1;
        fastwrite(addr,val);
        setczn((oldval & 0x80)>0, val==0, val & 128);
}

//#define ASLA() oldval=a; a<<=1; setczn((oldval & 0x80)>0, a==0, a & 128)

INLINE void ASLA()
{
        unsigned char olda=a;
        a<<=1;
        setczn((olda & 0x80)>0, a==0, a & 128);
        Cycles=2;
}

#define ASR(addr) a&=fastread(addr); a>>=1

#define BRANCH(true,val) \
        if (true)    \
        {            \
                pc+=val; \
                Cycles++; \
                if ((pc-val)&0xFF00!=pc&0xFF00) \
                   Cycles++;                    \
        }                                       \
        Cycles--

#define BCC(val) branch=(signed char)val; BRANCH(!(p&1),branch)
#define BCS(val) branch=(signed char)val; BRANCH(p&1,branch)
#define BEQ(val) branch=(signed char)val; BRANCH(p&2,branch)
#define BMI(val) branch=(signed char)val; BRANCH(p&0x80,branch)
#define BNE(val) branch=(signed char)val; BRANCH(!(p&2),branch)
#define BPL(val) branch=(signed char)val; BRANCH(!(p&0x80),branch)
#define BVC(val) branch=(signed char)val; BRANCH(!(p&0x40),branch)
#define BVS(val) branch=(signed char)val; BRANCH(p&0x40,branch)

#define BIT(addr)          \
        val=readmem(addr);  \
        p&=~(2 | 128 | 64);                  \
        p|=(((a & val)==0)<<1) | (val & 192)

INLINE void BRK()
{
        PUSH((pc+1) >> 8);
        PUSH((pc+1) & 0xFF);
        p|=0x10;
        PUSH(p);
        p|=0x4;
        pc=fastread(0xFFFE)|(fastread(0xFFFF)<<8);
        Cycles=7;
}

#define CLC() p&=0xFE
#define CLD() p&=(0xFF-8)
#define CLI() p&=(~4)
#define CLV() p&=(0xFF-64)

#define CMP(addr) \
        val=fastread(addr); \
        setczn(a>=val,a==val,(a-val) & 0x80); Cycles=2

#define CPX(addr) \
        val=fastread(addr); \
        setczn(x>=val,x==val,(x-val) & 0x80); Cycles=2

#define CPY(addr) \
        val=fastread(addr); \
        setczn(y>=val,y==val,(y-val) & 0x80); Cycles=2

#define DCP(addr) \
        val=fastread(addr)-1; \
        fastwrite(addr,val+1);  \
        fastwrite(addr,val);  \
        setczn(a>=val,a==val,(a-val)&0x80)

/*#define DEC(addr) \
        val=fastread(addr)-1; \
        fastwrite(addr,val+1);  \
        fastwrite(addr,val);  \
        setzn(val)*/

INLINE void DEC(unsigned short addr)
{
        unsigned char val=fastread(addr)-1;
        fastwrite(addr,val+1);
        fastwrite(addr,val);
        setzn(val);
}

#define DEX() x-=1; setzn(x)
#define DEY() y-=1; setzn(y)

#define EOR(addr) a^=fastread(addr); setzn(a)

/*#define INC(addr) \
        val=fastread(addr); \
        fastwrite(addr,val+1);  \
        setzn(val+1)*/

INLINE void INC(unsigned short addr)
{
        unsigned char val=fastread(addr)+1;
        fastwrite(addr,val-1);
        val&=0xFF;
        fastwrite(addr,val);
        setzn(val);
}

#define INX() x++; setzn(x)
#define INY() y++; setzn(y)

#define ISB(addr) \
        val=fastread(addr)+1; \
        fastwrite(addr,val);  \
        SBC(addr)

#define JMP(addr) pc=addr

/*#define JSR(addr) \
        PUSH((pc-1) >> 8); \
        PUSH((pc-1) & 0xFF); \
        pc=addr;             \
        Cycles=6*/

INLINE void JSR(unsigned short addr)
{
        PUSH((pc-1) >> 8);
        PUSH((pc-1) & 0xFF);
        pc=addr;
        Cycles=6;
}

#define LAS(addr)          \
        val=fastread(addr); \
        a&=val;            \
        x=s=a;             \
        setzn(a)

#define LAX(addr) a=x=fastread(addr); setzn(a)
#define LDA(addr) a=fastread(addr); setzn(a)
#define LDX(addr) x=fastread(addr); setzn(x)
#define LDY(addr) y=fastread(addr); setzn(y)

INLINE void LSR(unsigned short addr)
{
        unsigned char val=fastread(addr);
        unsigned char oldval=val;
        fastwrite(addr,val);
        val>>=1;
        fastwrite(addr,val);
        setczn(oldval & 1, val==0, 0);
}

#define LSRA() \
        oldval=a; \
        a>>=1;    \
        setczn(oldval&1,a==0,0)

#define LXA(addr) a=x=(a&fastread(addr)); setzn(a)

#define NOP()

#define ORA(addr) a|=fastread(addr); setzn(a)

#define PHA() PUSH(a); Cycles=3
#define PHP() PUSH(p|0x10); Cycles=3

#define PLA() a=PULL(); setzn(a); Cycles=4
#define PLP() p=PULL(); Cycles=4

INLINE void RLA(unsigned short addr)
{
        unsigned char val,oldval;
        val=oldval=fastread(addr);
        val<<=1;
        val&=254;
        val|=(p&1);
        a&=val;
        fastwrite(addr,val);
        setczn(oldval & 128, a==0, a & 128);
}

#define ROLA() \
        oldval=a; \
        a=((a<<1)&0xFE)|(p&1); \
        setczn(oldval&128,a==0,a&128)

INLINE void ROL(unsigned short addr)
{
        unsigned char val,oldval;
        oldval=val=fastread(addr);
        fastwrite(addr,val);
        val<<=1;
        val&=254;
        val|=(p&1);
        fastwrite(addr,val);
        setczn(oldval & 128, val==0, val & 128);
}

INLINE void RORA()
{
        unsigned char olda=a;
        a>>=1;
        a&=127;
        if (p & 1)
           a|=128;
        setczn(olda & 1, a==0, a & 128);
}

INLINE void ROR(unsigned short addr)
{
        unsigned char val,oldval;
        oldval=val=fastread(addr);
        fastwrite(addr,val);
        val>>=1;
        val&=127;
        if (p & 1)
           val|=128;
        fastwrite(addr,val);
        setczn(oldval & 1, val==0, val & 128);
}

INLINE void RRA(unsigned short addr)
{
        unsigned char val,oldval;
        oldval=val=fastread(addr);
        fastwrite(addr,val);
        val>>=1;
        val&=127;
        if (p & 1)
           val|=128;
        fastwrite(addr,val);
        ADC(addr);
}

INLINE void RTI()
{
        p=PULL();
        pc=PULL()|(PULL()<<8);
        NMILock=0;
        inint=0;
        Cycles=6;
}

#define RTS() pc=(PULL()|(PULL()<<8))+1; Cycles=6

#define SAX(addr) fastwrite(addr,a&x)

INLINE void SBCBCD(unsigned char s)
{
        unsigned
           AL=a&0xF, /* low nybble of accumulator */
           AH=a>>4, /* high nybble of accumulator */

           C=p&1,  /* Carry flag */
           Z=p&2,  /* Zero flag */
           V=p&64,  /* oVerflow flag */
           N=p&128;  /* Negative flag */

        unsigned char olda=a;
        C = (a - s - !C) & 256 != 0;
//        Z = (a - s - !C) & 255 != 0;
//        V = ((a - s - !C) ^ s) & 128 && (a ^ s) & 128;
//        N = (a - s - !C) & 128 != 0;

                AL -= (s&0xF) + (p&1) - 1;
                if ( AL < 0 )
                {
                        AL += 10;
                        AH--;
                }

                if (AL>9)
                {
                        AL=9;
                        AH--;
                }
                AH -= ((s>>4)&0xF);
                if ( AH < 0 )
                        AH += 10;
        a = ((AH << 4) | (AL & 15)) & 255;
        setczvn(C,Z,V,N);
}

INLINE void SBC(unsigned short addr)
{
        unsigned char val=fastread(addr);
        unsigned char olda=a;
        short tempc=0,tempv,tempn,tempz;
        int tempres,ln,hn;
        int C=p&1,Z=p&2,V=p&64,N=p&128;
        if (!(p&8))
        {
                tempv=(signed char)olda-(signed char)val-(1-(p&1));
                tempc=a-val-(1-(p&1));
                a=(unsigned char)tempc & 0xFF;
                setczvn(tempc>=0, a==0, ((a & 128)>0) ^ ((tempv & 0xFF)!=0), a & 128);
        }
        else
        {
                SBCBCD(val);
        }
}

#define SBX(addr) \
        val=fastread(addr); \
        x=(a&x)-val;       \
        setczn((a&x)>=val,x==0,x & 0x80)

#define SEC() p|=1
#define SED() p|=8
#define SEI() p|=4

INLINE void SHA(unsigned short addr)
{
        val=(a&x)&(((addr>>8)+1)&0xff);
        fastwrite(addr,val);
}

INLINE void SHX(unsigned short addr)
{
        val=x&(((addr>>8)+1)&0xff);
        fastwrite(addr,val);
}

INLINE void SHY(unsigned short addr)
{
        val=y&(((addr>>8)+1)&0xff);
        fastwrite(addr,val);
}

INLINE void SHS(unsigned short addr)
{
        s=a&x;
        val=(a&x)&(((addr>>8)+1)&0xff);
        fastwrite(addr,val);
}

INLINE void SLO(unsigned short addr)
{
        unsigned char oldval,newval;
        oldval=fastread(addr);
        if (oldval&128)
           p|=1;
        else
           p&=0xFE;
        newval=(oldval<<1)&0xFF;
        a|=newval;
        fastwrite(addr,newval);
        p&=0x7D;
        p|=((a==0)<<1)|(a&128);
//        setczn((oldval & 128)>0, newval==0,newval & 128);
}

#define SRE(addr) \
        oldval=fastread(addr); \
        val=oldval>>1;        \
        fastwrite(addr,val);   \
        a^=val;               \
        setzvn(a==0,oldval&1,a&0x80)

#define STA(addr) fastwrite(addr,a)
#define STX(addr) fastwrite(addr,x)
#define STY(addr) fastwrite(addr,y)

#define TAX() x=a; setzn(a)
#define TAY() y=a; setzn(a)
#define TSX() x=s; setzn(x)
#define TXA() a=x; setzn(a)
#define TXS() s=x
#define TYA() a=y; setzn(a)


INLINE unsigned short preindexx()
{
        unsigned char temp=(ram[pc++]+x)&0xFF;
        unsigned short tmp=ram[(unsigned short)temp]|(ram[(unsigned short)temp+1]<<8);
        return tmp;
}

INLINE unsigned short postindexy()
{
        unsigned char temp=ram[pc++];
        unsigned short tmp=((ram[(unsigned short)temp])|(ram[(unsigned short)temp+1]<<8))+y;
        return tmp;
}

#define relative() (signed char)ram[pc++]

#define absolute() fastread(pc++)|(fastread(pc++)<<8)

#define zeropagex() ((ram[pc++]+x)&0xFF)

#define zeropagey() ((ram[pc++]+y)&0xFF)

/*INLINE unsigned short zeropagex()
{
        unsigned short temp=fastread(pc++)+x;
        return (unsigned short)temp & 0xFF;
}*/

/*INLINE unsigned short zeropagey()
{
        unsigned short temp=fastread(pc++)+y;
        return (unsigned short)temp & 0xFF;
}*/

#define absolutex() ((fastread(pc++)|(fastread(pc++)<<8))+x)&0xFFFF

#define absolutey() ((fastread(pc++)|(fastread(pc++)<<8))+y)&0xFFFF

INLINE unsigned short indirect()
{
        unsigned short temp=fastread(pc++)|(fastread(pc++)<<8);
        unsigned short temp2=fastread(temp)|(fastread(temp+1)<<8);
        return temp2;
}

INLINE void doint()
{
        PUSH(pc >> 8);
        PUSH(pc & 0xFF);
        PUSH(p & ~0x10);
        pc=fastread(0xFFFE) | (fastread(0xFFFF)<<8);
        p|=4;
        inint=1;
//        intStatus=0;
}

INLINE void doNMI()
{
        char str[40];
        NMILock=1;
        PUSH(pc >> 8);
        PUSH(pc & 0xFF);
        PUSH(p);
        pc=fastread(0xFFFA) | (fastread(0xFFFB)<<8);
        p|=4;
        inint=1;
        //sprintf(str,"NMI\n");
//        fputs(str,f8271);
}

#define GETABS (ram[pc-2]|(ram[pc-1]<<8))
#define GETZP  (ram[pc-1])

int haddiscting=0;
int frakhack=0;
INLINE void do6502()
{
        unsigned char val;
        int c;
        int redoins=0;
        for (c=0;c<20000;c++)
        {
        oldpc=pc;
        if (frakhack)
           ram[0xDCF]=0x60;
        val=ram[pc++];
        lastins=val;
        Cycles=2;
        doins:
        redoins=0;
        switch (val)
        {
                /*Documented instructions*/
                case 0x00:
                BRK();
                //sprintf(disassemble,"BRK        ");
                break;
                case 0x01:
                ORA(preindexx());
                Cycles=6;
                //sprintf(disassemble,"ORA (%X,%X)",GETABS,x);
                break;
                case 0x02:
                c=OSFSC();
                if (c==6||c==8||c==0||c==5)
                   pc=(PULL()|(PULL()<<8))+1;
                if (c==0x80)
                {
                        val=0xC9;
                        redoins=1;
                }
                //sprintf(disassemble,"OSFSC %X   ",a);
                break;
                case 0x05:
                ORA(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"ORA %X     ",GETZP);
                break;
                case 0x06:
                ASL(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"ASL %X     ",GETZP);
                break;
                case 0x08:
                PHP();
                //sprintf(disassemble,"PHP        ");
                break;
                case 0x09:
                ORA(pc++);
                //sprintf(disassemble,"ORA #%X    ",GETZP);
                break;
                case 0x0A:
                ASLA();
                //sprintf(disassemble,"ASL A      ");
                break;
                case 0x0D:
                ORA(absolute());
                Cycles=4;
                //sprintf(disassemble,"ORA %X     ",GETABS);
                break;
                case 0x0E:
                ASL(absolute());
                Cycles=6;
                //sprintf(disassemble,"ASL %X     ",GETABS);
                break;
                case 0x10:
                BPL(relative());
                //sprintf(disassemble,"BPL %X     ",(p&128)?pc+GETZP:pc);
                break;
                case 0x11:
                ORA(postindexy());
                Cycles=6;
                //sprintf(disassemble,"ORA (%X)),%X",GETZP,y);
                break;
                case 0x15:
                ORA(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"ORA %X,%X  ",GETZP,x);
                break;
                case 0x16:
                ASL(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"ASL %X,%X  ",GETZP,x);
                break;
                case 0x18:
                CLC();
                //sprintf(disassemble,"CLC        ");
                break;
                case 0x19:
                ORA(absolutey());
                Cycles=5;
                //sprintf(disassemble,"ORA %X,%X  ",GETABS,y);
                break;
                case 0x1D:
                ORA(absolutex());
                Cycles=5;
                //sprintf(disassemble,"ORA %X,%X  ",GETABS,x);
                break;
                case 0x1E:
                ASL(absolutex());
                Cycles=7;
                //sprintf(disassemble,"ASL %X,%X",GETABS,x);
                break;
                case 0x20:
                JSR(absolute());
                Cycles=6;
                //sprintf(disassemble,"JSR %X   ",GETABS);
                break;
                case 0x21:
                AND(preindexx());
                Cycles=6;
                //sprintf(disassemble,"AND (%X,%X)",GETABS,x);
                break;
                case 0x24:
                BIT(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"BIT %X   ",GETZP);
                break;
                case 0x25:
                AND(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"AND %X   ",GETZP);
                break;
                case 0x26:
                ROL(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"ROL %X   ",GETZP);
                break;
                case 0x28:
                PLP();
                //sprintf(disassemble,"PLP      ");
                break;
                case 0x29:
                AND(pc++);
                //sprintf(disassemble,"AND %X  ",GETZP);
                break;
                case 0x2A:
                ROLA();
                //sprintf(disassemble,"ROL A   ");
                break;
                case 0x2C:
                BIT(absolute());
                Cycles=4;
                //sprintf(disassemble,"BIT %X  ",GETABS);
                break;
                case 0x2D:
                AND(absolute());
                Cycles=4;
                //sprintf(disassemble,"AND %X  ",GETABS);
                break;
                case 0x2E:
                ROL(absolute());
                Cycles=6;
                //sprintf(disassemble,"ROL %X  ",GETABS);
                break;
                case 0x30:
                BMI(relative());
                //sprintf(disassemble,"BMI %X  ",(p&128)?pc:GETZP+pc);
                break;
                case 0x31:
                AND(postindexy());
                Cycles=6;
                //sprintf(disassemble,"AND (%X),%X",GETZP,y);
                break;
                case 0x35:
                AND(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"AND %X,%X",GETZP,x);
                break;
                case 0x36:
                ROL(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"ROL %X,%X",GETZP,x);
                break;
                case 0x38:
                p|=1;
                //sprintf(disassemble,"SEC     ");
                break;
                case 0x39:
                AND(absolutey());
                Cycles=5;
                //sprintf(disassemble,"AND %X,%X",GETABS,y);
                break;
                case 0x3D:
                AND(absolutex());
                Cycles=5;
                //sprintf(disassemble,"AND %X,%X",GETABS,x);
                break;
                case 0x3E:
                ROL(absolutex());
                Cycles=7;
                //sprintf(disassemble,"ROL %X,%X",GETABS,x);
                break;
                case 0x40:
                RTI();
                //sprintf(disassemble,"RTI      ");
                break;
                case 0x41:
                EOR(preindexx());
                Cycles=6;
                //sprintf(disassemble,"EOR (%X,%X)",GETABS,x);
                break;
                case 0x45:
                EOR(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"EOR %X   ",GETZP);
                break;
                case 0x46:
                LSR(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"LSR %X   ",GETZP);
                break;
                case 0x48:
                PUSH(a);
                //sprintf(disassemble,"PHA    ");
                break;
                case 0x49:
                EOR(pc++);
                //sprintf(disassemble,"EOR #%X  ",GETZP);
                break;
                case 0x4A:
                LSRA();
                //sprintf(disassemble,"LSR A   ");
                break;
                case 0x4C:
                JMP(absolute());
                Cycles=3;
                //sprintf(disassemble,"JMP %X ",GETABS);
                break;
                case 0x4D:
                EOR(absolute());
                Cycles=4;
                //sprintf(disassemble,"EOR %X",GETABS);
                break;
                case 0x4E:
                LSR(absolute());
                Cycles=6;
                //sprintf(disassemble,"LSR %X",GETABS);
                break;
                case 0x50:
                BVC(relative());
                //sprintf(disassemble,"BVC %X",(p&64)?pc+GETZP:pc);
                break;
                case 0x51:
                EOR(postindexy());
                Cycles=6;
                //sprintf(disassemble,"EOR (%X),%X",GETZP,y);
                break;
                case 0x55:
                EOR(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"EOR %X,%X",GETZP,x);
                break;
                case 0x56:
                LSR(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"LSR %X,%X",GETZP,x);
                break;
                case 0x58:
                CLI();
                //sprintf(disassemble,"CLI    ");
                break;
                case 0x59:
                EOR(absolutey());
                Cycles=5;
                //sprintf(disassemble,"EOR %X,%X",GETABS,y);
                break;
                case 0x5D:
                EOR(absolutex());
                Cycles=5;
                //sprintf(disassemble,"EOR %X,%X",GETABS,x);
                break;
                case 0x5E:
                LSR(absolutex());
                Cycles=7;
                //sprintf(disassemble,"LSR %X,%X",GETABS,x);
                break;
                case 0x60:
                RTS();
                //sprintf(disassemble,"RTS    ");
                break;
                case 0x61:
                ADC(preindexx());
                Cycles=6;
                //sprintf(disassemble,"ADC (%X,%X)",GETABS,x);
                break;
                case 0x65:
                ADC(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"ADC %X  ",GETZP);
                break;
                case 0x66:
                ROR(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"ROR %X  ",GETZP);
                break;
                case 0x68:
                PLA();
                //sprintf(disassemble,"PLA   ");
                break;
                case 0x69:
                ADC(pc++);
                //sprintf(disassemble,"ADC #%X  ",GETZP);
                break;
                case 0x6A:
                RORA();
                //sprintf(disassemble,"ROR A");
                break;
                case 0x6C:
                JMP(indirect());
                Cycles=5;
                //sprintf(disassemble,"JMP (%X)",GETABS);
                break;
                case 0x6D:
                ADC(absolute());
                Cycles=4;
                //sprintf(disassemble,"ADC %X",GETABS);
                break;
                case 0x6E:
                ROR(absolute());
                Cycles=6;
                //sprintf(disassemble,"ROR %X",GETABS);
                break;
                case 0x70:
                BVS(relative());
                //sprintf(disassemble,"BVS %X",(p&64)?pc:GETZP+pc);
                break;
                case 0x71:
                ADC(postindexy());
                Cycles=6;
                //sprintf(disassemble,"ADC (%X),%X",GETZP,y);
                break;
                case 0x75:
                ADC(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"ADC %X,%X",GETZP,x);
                break;
                case 0x76:
                ROR(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"ROR %X,%X",GETZP,x);
                break;
                case 0x78:
                SEI();
                //sprintf(disassemble,"SEI    ");
                break;
                case 0x79:
                ADC(absolutey());
                Cycles=5;
                //sprintf(disassemble,"ADC %X,%X",GETABS,y);
                break;
                case 0x7D:
                ADC(absolutex());
                Cycles=5;
                //sprintf(disassemble,"ADC %X,%X",GETABS,x);
                break;
                case 0x7E:
                ROR(absolutex());
                Cycles=7;
                //sprintf(disassemble,"ROR %X,%X",GETABS,x);
                break;
                case 0x81:
                STA(preindexx());
                Cycles=6;
                //sprintf(disassemble,"STA (%X,%X)",GETABS,x);
                break;
                case 0x84:
                STY(ram[pc++]);
                //sprintf(disassemble,"STY %X  ",GETZP);
                break;
                case 0x85:
                STA(ram[pc++]);
                //sprintf(disassemble,"STA %X  ",GETZP);
                break;
                case 0x86:
                STX(ram[pc++]);
                //sprintf(disassemble,"STX %X  ",GETZP);
                break;
                case 0x88:
                DEY();
                //sprintf(disassemble,"DEY      ");
                break;
                case 0x8A:
                a=x;
                setzn(x);
                //sprintf(disassemble,"TXA     ");
                break;
                case 0x8C:
                STY(absolute());
                Cycles=4;
                //sprintf(disassemble,"STY %X",GETABS);
                break;
                case 0x8D:
                STA(absolute());
                Cycles=4;
                //sprintf(disassemble,"STA %X",GETABS);
                break;
                case 0x8E:
                STX(absolute());
                Cycles=4;
                //sprintf(disassemble,"STX %X",GETABS);
                break;
                case 0x90:
                BCC(relative());
                //sprintf(disassemble,"BCC %X",(p&1)?pc+GETZP:pc);
                break;
                case 0x91:
                STA(postindexy());
                Cycles=6;
                //sprintf(disassemble,"STA (%X),%X",GETZP,y);
                break;
                case 0x92:
                a=OSFILE();
                if (c==0x80)
                {
                        val=0x48;
                        redoins=1;
                }
                else if (c!=0x7F)
                    pc=(PULL()|(PULL()<<8))+1;
                //sprintf(disassemble,"OSFILE  ");
                break;
                case 0x94:
                STY(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"ORA %X,%X",GETZP,x);
                break;
                case 0x95:
                STA(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"ORA %X,%X",GETZP,x);
                break;
                case 0x96:
                STX(zeropagey());
                Cycles=4;
                //sprintf(disassemble,"STX %X,%X",GETZP,y);
                break;
                case 0x98:
                a=y;
                setzn(y);
                //sprintf(disassemble,"TYA     ");
                break;
                case 0x99:
                STA(absolutey());
                Cycles=5;
                //sprintf(disassemble,"STA %X,%X",GETABS,y);
                break;
                case 0x9A:
                s=x;
                //sprintf(disassemble,"TXS      ");
                break;
                case 0x9D:
                STA(absolutex());
                Cycles=5;
                //sprintf(disassemble,"STA %X,%X",GETABS,x);
                break;
                case 0xA0:
                LDY(pc++);
                //sprintf(disassemble,"LDY #%X  ",GETZP);
                break;
                case 0xA1:
                LDA(preindexx());
                Cycles=6;
                //sprintf(disassemble,"LDA (%X,%X)",GETABS,x);
                break;
                case 0xA2:
                LDX(pc++);
                //sprintf(disassemble,"LDX #%X  ",GETZP);
                break;
                case 0xA4:
                LDY(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"LDY %X    ",GETZP);
                break;
                case 0xA5:
                LDA(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"LDA %X    ",GETZP);
                break;
                case 0xA6:
                LDX(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"LDX %X    ",GETZP);
                break;
                case 0xA8:
                y=a;
                setzn(y);
                //sprintf(disassemble,"TAY    ");
                break;
                case 0xA9:
                LDA(pc++);
                //sprintf(disassemble,"LDA #%X  ",GETZP);
                break;
                case 0xAA:
                x=a;
                setzn(x);
                //sprintf(disassemble,"TAX    ");
                break;
                case 0xAC:
                LDY(absolute());
                Cycles=4;
                //sprintf(disassemble,"LDY %X  ",GETABS);
                break;
                case 0xAD:
                LDA(absolute());
                Cycles=4;
                //sprintf(disassemble,"LDA %X  ",GETABS);
                break;
                case 0xAE:
                LDX(absolute());
                Cycles=4;
                //sprintf(disassemble,"LDX %X  ",GETABS);
                break;
                case 0xB0:
                BCS(relative());
                //sprintf(disassemble,"BCS %X",(p&1)?GETZP+pc:pc);
                break;
                case 0xB1:
                LDA(postindexy());
                Cycles=6;
                //sprintf(disassemble,"LDA (%X),%X",GETZP,y);
                break;
                case 0xB4:
                LDY(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"LDY %X,%X",GETZP,x);
                break;
                case 0xB5:
                LDA(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"LDA %X,%X",GETZP,x);
                break;
                case 0xB6:
                LDX(zeropagey());
                Cycles=4;
                //sprintf(disassemble,"LDX %X,%X",GETZP,y);
                break;
                case 0xB8:
                CLV();
                //sprintf(disassemble,"CLV     ");
                break;
                case 0xB9:
                LDA(absolutey());
                Cycles=5;
                //sprintf(disassemble,"LDA %X,%X",GETABS,y);
                break;
                case 0xBA:
                x=s;
                setzn(x);
                //sprintf(disassemble,"TSX    ");
                break;
                case 0xBC:
                LDY(absolutex());
                Cycles=5;
                //sprintf(disassemble,"LDY %X,%X",GETABS,x);
                break;
                case 0xBD:
                LDA(absolutex());
                Cycles=5;
                //sprintf(disassemble,"LDA %X,%X",GETABS,x);
                break;
                case 0xBE:
                LDX(absolutey());
                Cycles=5;
                //sprintf(disassemble,"LDX %X,%X",GETABS,y);
                break;
                case 0xC0:
                CPY(pc++);
                //sprintf(disassemble,"CPY #%X  ",GETZP);
                break;
                case 0xC1:
                CMP(preindexx());
                Cycles=6;
                //sprintf(disassemble,"CMP (%X,%X)",GETABS,x);
                break;
                case 0xC4:
                CPY(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"CPY %X  ",GETZP);
                break;
                case 0xC5:
                CMP(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"CMP %X  ",GETZP);
                break;
                case 0xC6:
                DEC(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"DEC %X  ",GETZP);
                break;
                case 0xC8:
                INY();
                //sprintf(disassemble,"INY    ");
                break;
                case 0xC9:
                CMP(pc++);
                //sprintf(disassemble,"CMP #%X  ",GETZP);
                break;
                case 0xCA:
                DEX();
                //sprintf(disassemble,"DEX    ");
                break;
                case 0xCC:
                CPY(absolute());
                Cycles=4;
                //sprintf(disassemble,"CPY %X",GETABS);
                break;
                case 0xCD:
                CMP(absolute());
                Cycles=4;
                //sprintf(disassemble,"CMP %X",GETABS);
                break;
                case 0xCE:
                DEC(absolute());
                Cycles=6;
                //sprintf(disassemble,"DEC %X",GETABS);
                break;
                case 0xD0:
                BNE(relative());
                //sprintf(disassemble,"BNE %X",(p&2)?pc+GETZP:pc);
                break;
                case 0xD1:
                CMP(postindexy());
                Cycles=6;
                //sprintf(disassemble,"CMP (%X),%X",GETZP,y);
                break;
                case 0xD5:
                CMP(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"CMP %X,%X",GETZP,x);
                break;
                case 0xD6:
                DEC(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"DEC %X,%X",GETZP,x);
                break;
                case 0xD8:
                CLD();
                //sprintf(disassemble,"CLD     ");
                break;
                case 0xD9:
                CMP(absolutey());
                Cycles=5;
                //sprintf(disassemble,"CMP %X,%X",GETABS,y);
                break;
                case 0xDD:
                CMP(absolutex());
                Cycles=5;
                //sprintf(disassemble,"CMP %X,%X",GETABS,x);
                break;
                case 0xDE:
                DEC(absolutex());
                Cycles=7;
                //sprintf(disassemble,"DEC %X,%X",GETABS,x);
                break;
                case 0xE0:
                CPX(pc++);
                //sprintf(disassemble,"CPX #%X  ",GETZP);
                break;
                case 0xE1:
                SBC(preindexx());
                Cycles=6;
                //sprintf(disassemble,"SBC (%X,%X)",GETABS,x);
                break;
                case 0xE4:
                CPX(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"CPX %X   ",GETZP);
                break;
                case 0xE5:
                SBC(ram[pc++]);
                Cycles=3;
                //sprintf(disassemble,"SBC %X  ",GETZP);
                break;
                case 0xE6:
                INC(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"INC %X  ",GETZP);
                break;
                case 0xE8:
                INX();
                //sprintf(disassemble,"INX   ");
                break;
                case 0xE9:
                SBC(pc++);
                //sprintf(disassemble,"SBC #%X  ",GETZP);
                break;
                case 0xEC:
                CPX(absolute());
                Cycles=4;
                //sprintf(disassemble,"CPX %X",GETABS);
                break;
                case 0xED:
                SBC(absolute());
                Cycles=4;
                //sprintf(disassemble,"SBC %X",GETABS);
                break;
                case 0xEE:
                INC(absolute());
                Cycles=6;
                //sprintf(disassemble,"INC %X",GETABS);
                break;
                case 0xF0:
                BEQ(relative());
                //sprintf(disassemble,"BEQ %X",(p&2)?pc:pc+GETZP);
                break;
                case 0xF1:
                SBC(postindexy());
                Cycles=6;
                //sprintf(disassemble,"SBC (%X),%X",GETABS,y);
                break;
                case 0xF5:
                SBC(zeropagex());
                Cycles=4;
                //sprintf(disassemble,"SBC %X,%X",GETZP,x);
                break;
                case 0xF6:
                INC(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"INC %X,%X",GETZP,x);
                break;
                case 0xF8:
                p|=8;
                //sprintf(disassemble,"SED    ");
                break;
                case 0xF9:
                SBC(absolutey());
                Cycles=5;
                //sprintf(disassemble,"SBC %X,%X",GETABS,y);
                break;
                case 0xFD:
                SBC(absolutex());
                Cycles=5;
                //sprintf(disassemble,"SBC %X,%X",GETABS,x);
                break;
                case 0xFE:
                INC(absolutex());
                Cycles=7;
                //sprintf(disassemble,"INC %X,%X",GETABS,x);
                break;
                /*Undocumented instructions*/
                #ifdef UNDOCUMENTEDINS
                case 0x03: /*SLO - ASL prex + ORA*/
                SLO(preindexx());
                Cycles=6;
                //sprintf(disassemble,"SLO (%X,X) ",GETZP);
                break;
                case 0x07: /*SLO - ASL zp + ORA*/
                SLO(ram[pc++]);
                Cycles=5;
                //sprintf(disassemble,"SLO %X     ",GETZP);
                break;
                case 0x0B: /*ANC - A=byte&A*/
                ANC(ram[pc++]);
                //sprintf(disassemble,"ANC #%X    ",GETZP);
                break;
                case 0x0F: /*SLO - ASL abs + ORA*/
                SLO(absolute());
                Cycles=6;
                //sprintf(disassemble,"SLO %X     ",GETABS);
                break;
                case 0x13: /*SLO - ASL posty + ORA*/
                SLO(postindexy());
                Cycles=8;
                //sprintf(disassemble,"SLO (%X),Y ",GETZP);
                break;
                case 0x17: /*SLO - ASL zpx + ORA*/
                SLO(zeropagex());
                Cycles=6;
                //sprintf(disassemble,"SLO %X,X   ",GETZP);
                break;
                case 0x1B: /*SLO - ASL absy + ORA*/
                SLO(absolutey());
                Cycles=7;
                //sprintf(disassemble,"SLO %X,Y   ",GETZP);
                break;
                case 0x1F: /*SLO - ASL absx + ORA*/
                SLO(absolutex());
                Cycles=7;
                //sprintf(disassemble,"SLO %X,X   ",GETZP);
                break;
                case 0x2B: /*ANC - A=byte&A*/
                ANC(ram[pc++]);
                //sprintf(disassemble,"ANC #%X    ",GETZP);
                break;
                case 0x2F: /*RLA - ROL abs + AND*/
                RLA(absolute());
                Cycles=6;
                break;
                case 0x43: /*SRE - LSR + EOR*/
                SRE(preindexx());
                Cycles=6;
                break;
                case 0x47: /*SRE - LSR + EOR*/
                SRE(ram[pc++]);
                Cycles=5;
                break;
                case 0x4B: /*ASR - A=(A&byte)>>1*/
                ASR(pc++);
                break;
                case 0x4F: /*SRE - LSR + EOR*/
                SRE(absolute());
                Cycles=6;
                break;
                case 0x53: /*SRE - LSR + EOR*/
                SRE(postindexy());
                Cycles=8;
                break;
                case 0x57: /*SRE - LSR + EOR*/
                SRE(zeropagex());
                Cycles=6;
                break;
                case 0x5B: /*SRE - LSR + EOR*/
                SRE(absolutey());
                Cycles=7;
                break;
                case 0x5F: /*SRE - LSR + EOR*/
                SRE(absolutex());
                Cycles=7;
                break;
                case 0x63: /*RRA - ROR + ADC*/
                RRA(preindexx());
                Cycles=6;
                break;
                case 0x67: /*RRA - ROR + ADC*/
                RRA(ram[pc++]);
                Cycles=5;
                break;
                case 0x6B: /*ARR - AND + ROR + weird ADC stuff*/
                ARR(pc++);
                break;
                case 0x6F: /*RRA - ROR + ADC*/
                RRA(absolute());
                Cycles=6;
                break;
                case 0x73: /*RRA - ROR + ADC*/
                RRA(postindexy());
                Cycles=8;
                break;
                case 0x77: /*RRA - ROR + ADC*/
                RRA(zeropagex());
                Cycles=6;
                break;
                case 0x7B: /*RRA - ROR + ADC*/
                RRA(absolutey());
                Cycles=7;
                break;
                case 0x7F: /*RRA - ROR + ADC*/
                RRA(absolutex());
                Cycles=7;
                break;
                case 0x83: /*SAX - store A & X*/
                SAX(preindexx());
                Cycles=6;
                break;
                case 0x87: /*SAX - store A & X*/
                SAX(ram[pc++]);
                Cycles=3;
                break;
                case 0x8B: /*ANE - A=(A|&EE)&x&byte*/
                ANE(pc++);
                break;
                case 0x8F: /*SAX - store A & X*/
                SAX(absolute());
                Cycles=4;
                break;
                case 0x93: /*SHA - mem=(A & X) & (ADDR_HI+1)*/
                SHA(postindexy());
                Cycles=6;
                break;
                case 0x97: /*SAX - store A & X*/
                SAX(zeropagey());
                Cycles=4;
                break;
                case 0x9B: /*SHX - mem=X & (ADDR_HI+1)*/
                SHS(absolutey());
                Cycles=5;
                break;
                case 0x9C: /*SHY - mem=Y & (ADDR_HI+1)*/
                SHY(absolutex());
                Cycles=5;
                break;
                case 0x9E: /*SHX - mem=X & (ADDR_HI+1)*/
                SHX(absolutey());
                Cycles=5;
                break;
                case 0x9F: /*SHA - mem=(A&X)&(ADDR_HI+1)*/
                SHA(absolutey());
                Cycles=5;
                break;
                case 0xAB: /*LXA - A=X=(A&byte) - apparently some randomness involved?*/
                LXA(pc++);
                break;
                case 0xAF: /*LAX - A=X=byte*/
                LAX(absolute());
                Cycles=4;
                break;
                case 0xBB: /*LAS - X=S=A=(S&byte)*/
                LAS(absolutey());
                Cycles=5;
                break;
                case 0xC3: /*DCP - DEC + CMP*/
                DCP(preindexx());
                Cycles=6;
                break;
                case 0xC7: /*DCP - DEC + CMP*/
                DCP(ram[pc++]);
                break;
                case 0xCB: /*SBX - X = (A&X) - byte*/
                SBX(pc++);
                break;
                case 0xCF: /*DCP - DEC + CMP*/
                DCP(absolute());
                Cycles=6;
                break;
                case 0xD3: /*DCP - DEC + CMP*/
                DCP(postindexy());
                Cycles=8;
                break;
                case 0xD7: /*DCP - DEC + CMP*/
                DCP(zeropagex());
                Cycles=6;
                break;
                case 0xDB: /*DCP - DEC + CMP*/
                DCP(absolutey());
                Cycles=7;
                break;
                case 0xDF: /*DCP - DEC + CMP*/
                DCP(absolutex());
                Cycles=7;
                break;
                case 0xE3: /*ISB - mem=mem+1, A=A-mem*/
                ISB(preindexx());
                Cycles=6;
                break;
                case 0xE7: /*ISB - mem=mem+1, A=A-mem*/
                ISB(ram[pc++]);
                Cycles=5;
                break;
                case 0xEB: /*SBC imm (undocumented copy of &E9)*/
                SBC(pc++);
                break;
                case 0xEF: /*ISB - mem=mem+1, A=A-mem*/
                ISB(absolute());
                Cycles=6;
                break;
                case 0xF3: /*ISB - mem=mem+1, A=A-mem*/
                ISB(postindexy());
                Cycles=8;
                break;
                case 0xFB: /*ISB - mem=mem+1, A=A-mem*/
                ISB(absolutey());
                Cycles=7;
                break;
                case 0xFF: /*ISB - mem=mem+1, A=A-mem*/
                ISB(absolutex());
                Cycles=7;
                break;
                /*NOPs*/
                case 0x04:
                case 0x0C:
                case 0x14:
                case 0x1A:
                case 0x1C:
                case 0x34:
                case 0x3A:
                case 0x3C:
                case 0x44:
                case 0x54:
                case 0x5A:
                case 0x5C:
                case 0x64:
                case 0x74:
                case 0x7A:
                case 0x7C:
                case 0x80:
                case 0x82:
                case 0x89:
                case 0xC2:
                case 0xD4:
                case 0xDA:
                case 0xDC:
                case 0xE2:
                case 0xEA:
                case 0xF4:
                case 0xFA:
                case 0xFC:
                switch (val&0xF)
                {
                        case 0xA:
                        break;
                        case 0x0:
                        case 0x2:
                        case 0x3:
                        case 0x4:
                        case 0x7:
                        case 0x9:
                        case 0xB:
                        pc++;
                        break;
                        case 0xC:
                        case 0xE:
                        case 0xF:
                        pc+=2;
                        break;
                }
                //sprintf(disassemble,"NOP     ");
                break;
                /*HALTs*/
//                case 0x02: OSFSC
                case 0x22:
                case 0x42:
                case 0x62:
                case 0x12:
                case 0x32:
                case 0x52:
                case 0x72:
//                case 0x92: OSFILE
                case 0xB2:
                case 0xD2:
                case 0xF2:
                pc--;
                //sprintf(disassemble,"HALT    ");
                break;
                #else
                case 0xEA:
                break;
                #endif
                default:
                closegfx();
                printf("Illegal instruction %X at %X\n",val,pc);
                exit(-1);
                break;
        }
        if (redoins)
           goto doins;
        if (log)
        {
                //sprintf(string,"%s \tPC %X A %X X %X Y %X S %X P %X I %X\n",disassemble,pc,a,x,y,s,p,lastins/*,ram[pc-3],ram[pc-2],ram[pc-1],ram[pc+1],ram[pc+2],ram[pc+3],currom*/);
                fputs(string,logfile);
        }
        OldNMIStatus=NMIStatus;

        if (intStatus && !(p&4))
        {
                doint();
        }
        TotalCycles+=Cycles;
//        viacycles+=Cycles;
//        if (viacycles>255)
//        {
                SysVIA_poll(Cycles);
                UserVIA_poll(Cycles);
//                viacycles-=256;
//        }

        videodelay+=Cycles;
        screencount+=Cycles;
        if (!scanlinedraw||VideoState.IsTeletext)
        {
                if (!us)
                {
                if (videodelay>40000)
                {
                        SysVIATriggerCA1Int(0);
                        if (screencount>fskip)
                        {
                                doscreen();
                                screencount-=fskip;
                        }
                        videodelay-=40000;
                }
                }
                else
                {
                if (videodelay>33333)
                {
                        SysVIATriggerCA1Int(0);
                        if (screencount>fskip)
                        {
                                doscreen();
                                screencount-=fskip;
                        }
                        videodelay-=33333;
                }
                }
        }
        else
        {
                if (videodelay>videoline)
                {
                        doscreen();
                        videodelay-=videoline;
                }
                if (!us)
                {
                if (scancount==300)
                   SysVIATriggerCA1Int(1);
                if (scancount==0)
                   SysVIATriggerCA1Int(0);
                }
                else
                {
                if (scancount==258)
                   SysVIATriggerCA1Int(1);
                if (scancount==0)
                   SysVIATriggerCA1Int(0);
                }
        }

        polladc(Cycles);
        if (discint)
        {
                disctime+=(Cycles);
                if (disctime>discint)
                {
                        discint=0;
                        disctime=0;
                        poll8271();
                }
        }

        printertime+=Cycles;
        if (printertime>printtarget)
           pollprinter();
/*        acaitime-=Cycles;
        if (acaitime<0)
        {
                pollacia();
                acaitime+=33333;
        }*/
        numcycles+=Cycles;
        if ((NMIStatus) && (!OldNMIStatus)) doNMI();
        }
}
