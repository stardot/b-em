/*B-em 0.71b by Tom Walker*/
/*Video emulation*/

#include <allegro.h>
#include "bbctext.h"
#include "b-em.h"
#include "2xsai.h"

int fullscreen;
int hires=1;
int interlaceline=0;
int lns;
unsigned short vidmask;
int output;

PALETTE beebpal=
{
        {0 ,0 ,0},
        {63,0 ,0},
        {0 ,63,0},
        {63,63,0},
        {0, 0, 63},
        {63,0, 63},
        {0, 63,63},
        {63,63,63},
};

PALETTE monopal =
{
      {0,0,0},
      {16,16,16},
      {24,24,24},
      {40,40,40},
      {8,8,8},
      {24,24,24},
      {32,32,32},
      {56,56,56},
      {0,0,0},
      {16,16,16},
      {24,24,24},
      {40,40,40},
      {8,8,8},
      {24,24,24},
      {32,32,32},
      {56,56,56},
};

int flash=0,flashint=0;
unsigned short pc;

unsigned char *ram;

int linesdrawn=0;

BITMAP *buffer,*buffer2,*buf16,*buf162;
int multable[128];
int scrsize;
int screenlen[4]={0x4000,0x5000,0x2000,0x2800};
int bbcmode;

int mono=0;

int blurred=0;
int frames;
volatile int fps=0;

static void secint()
{
        fps=frames;
        frames=0;
}

END_OF_FUNCTION(secint);

unsigned char table4bpp[256][8];

int crtcreg;
unsigned char basecrtc[32]={127,40,98,0x28,38,0,25,34,0,7,0,0,6,0,0,0,0,0};
unsigned char crtc[32];

unsigned short startaddr;
int curline=0,physline=0;

int vc=0,sc=0,sc6=0;
int model;
unsigned char crtcmask[32]={0xFF,0xFF,0xFF,0xFF,0x7F,0x1F,0x7F,0x7F,0xF3,0x1F,0x7F,0x1F,0x3F,0xFF,0x3F,0xFF,0x3F,0xFF};
void writecrtc(unsigned short addr, unsigned char val)
{
        if (!(addr&1))
           crtcreg=val&31;
        else
        {
                crtc[crtcreg]=val&crtcmask[crtcreg];
//                printf("CRTCreg %i=%02X %i\n",crtcreg,val,lns);
//                if (output) exit(-1);
                if (crtcreg==6) clear(buffer);
        }
}

unsigned char readcrtc(unsigned short addr)
{
        if (!(addr&1))
           return crtcreg;
        return crtc[crtcreg];
}

unsigned long lookuptab[256];
unsigned long lookuptabh[256],lookuptabh2[256],lookuptabh3[256],lookuptabh4[256];
unsigned char ulactrl;
int clut[16],clut2[16];
int remakelookup=1;

void updateclut()
{
        int c;
        for (c=0;c<16;c++)
        {
                if (clut2[c]&8)
                {
                        if (ulactrl&1) clut[c]=clut2[c]^7;
                        else           clut[c]=clut2[c];
                }
                else
                   clut[c]=clut2[c];
        }
}

void writeula(unsigned short addr, unsigned char val, int line)
{
        remakelookup=1;
        if (addr&1)
        {
                clut2[val>>4]=(val^7)&0xF;
                updateclut();
//                printf("CLUT %i=%i at line %i %04X\n",val>>4,(val^7)&0xF,line,pc);
        }
        else
        {
                ulactrl=val;
                if (val&2) bbcmode=7;
                else switch (val&0x1C)
                {
                        case 0x00: bbcmode=8; break;
                        case 0x04: bbcmode=5; break;
                        case 0x08: bbcmode=4; break;
                        case 0x14: bbcmode=2; break;
                        case 0x18: bbcmode=1; break;
                        case 0x1C: bbcmode=0; break;
                }
                updateclut();
        }
}

void remaketab()
{
        int c;
        if (hires==1)
        {
                switch (bbcmode)
                {
                        case 0:
                        for (c=0;c<256;c++)
                        {
                                lookuptabh[c]=clut[table4bpp[c][0]]&7;
                                lookuptabh[c]|=(clut[table4bpp[c][1]]&7)<<8;
                                lookuptabh[c]|=(clut[table4bpp[c][2]]&7)<<16;
                                lookuptabh[c]|=(clut[table4bpp[c][3]]&7)<<24;
                                lookuptabh2[c]=clut[table4bpp[c][4]]&7;
                                lookuptabh2[c]|=(clut[table4bpp[c][5]]&7)<<8;
                                lookuptabh2[c]|=(clut[table4bpp[c][6]]&7)<<16;
                                lookuptabh2[c]|=(clut[table4bpp[c][7]]&7)<<24;
                        }
                        break;
                        case 1:
                        for (c=0;c<256;c++)
                        {
                                lookuptabh[c]=(clut[table4bpp[c][0]]&7)|((clut[table4bpp[c][0]]&7)<<8);
                                lookuptabh[c]|=((clut[table4bpp[c][1]]&7)<<16)|((clut[table4bpp[c][1]]&7)<<24);
                                lookuptabh2[c]=(clut[table4bpp[c][2]]&7)|((clut[table4bpp[c][2]]&7)<<8);
                                lookuptabh2[c]|=((clut[table4bpp[c][3]]&7)<<16)|((clut[table4bpp[c][3]]&7)<<24);
//                                lookuptabh[c]|=(clut[table4bpp[c][1]]&7)<<16;
//                                lookuptabh[c]|=(lookuptabh[c]<<8);
//                                lookuptabh2[c]|=(clut[table4bpp[c][2]]&7);
//                                lookuptabh2[c]|=(clut[table4bpp[c][3]]&7)<<16;
//                                lookuptabh2[c]|=(lookuptabh2[c]<<8);
                        }
                        break;
                        case 2:
                        for (c=0;c<256;c++)
                        {
                                lookuptabh[c]= (clut[table4bpp[c][0]]&7)|((clut[table4bpp[c][0]]&7)<<8);
                                lookuptabh[c]|=((clut[table4bpp[c][0]]&7)<<16)|((clut[table4bpp[c][0]]&7)<<24);
                                lookuptabh2[c]= (clut[table4bpp[c][1]]&7)|((clut[table4bpp[c][1]]&7)<<8);
                                lookuptabh2[c]|=((clut[table4bpp[c][1]]&7)<<16)|((clut[table4bpp[c][1]]&7)<<24);
/*                                lookuptabh[c]|=(lookuptabh[c]<<8);
                                lookuptabh[c]|=(lookuptabh[c]<<16);
                                lookuptabh2[c]|=clut[table4bpp[c][1]]&7;
                                lookuptabh2[c]|=(lookuptabh2[c]<<8);
                                lookuptabh2[c]|=(lookuptabh2[c]<<16);*/
                        }
                        break;
                        case 4:
                        for (c=0;c<256;c++)
                        {
                                lookuptabh[c]=   (clut[table4bpp[c][0]]&7)|     ((clut[table4bpp[c][0]]&7)<<8);
                                lookuptabh[c]|= ((clut[table4bpp[c][1]]&7)<<16)|((clut[table4bpp[c][1]]&7)<<24);
                                lookuptabh2[c]=  (clut[table4bpp[c][2]]&7)|     ((clut[table4bpp[c][2]]&7)<<8);
                                lookuptabh2[c]|=((clut[table4bpp[c][3]]&7)<<16)|((clut[table4bpp[c][3]]&7)<<24);
                                lookuptabh3[c]=  (clut[table4bpp[c][4]]&7)|     ((clut[table4bpp[c][4]]&7)<<8);
                                lookuptabh3[c]|=((clut[table4bpp[c][5]]&7)<<16)|((clut[table4bpp[c][5]]&7)<<24);
                                lookuptabh4[c]=  (clut[table4bpp[c][6]]&7)|     ((clut[table4bpp[c][6]]&7)<<8);
                                lookuptabh4[c]|=((clut[table4bpp[c][7]]&7)<<16)|((clut[table4bpp[c][7]]&7)<<24);
                        }
                        break;
                        case 5:
                        for (c=0;c<256;c++)
                        {
                                lookuptabh[c]=   (clut[table4bpp[c][0]]&7)|     ((clut[table4bpp[c][0]]&7)<<8);
                                lookuptabh[c]|= ((clut[table4bpp[c][0]]&7)<<16)|((clut[table4bpp[c][0]]&7)<<24);
                                lookuptabh2[c]=  (clut[table4bpp[c][1]]&7)|     ((clut[table4bpp[c][1]]&7)<<8);
                                lookuptabh2[c]|=((clut[table4bpp[c][1]]&7)<<16)|((clut[table4bpp[c][1]]&7)<<24);
                                lookuptabh3[c]=  (clut[table4bpp[c][2]]&7)|     ((clut[table4bpp[c][2]]&7)<<8);
                                lookuptabh3[c]|=((clut[table4bpp[c][2]]&7)<<16)|((clut[table4bpp[c][2]]&7)<<24);
                                lookuptabh4[c]=  (clut[table4bpp[c][3]]&7)|     ((clut[table4bpp[c][3]]&7)<<8);
                                lookuptabh4[c]|=((clut[table4bpp[c][3]]&7)<<16)|((clut[table4bpp[c][3]]&7)<<24);
                        }
                        break;
                }
        }
        else
        {
                switch (bbcmode)
                {
                        case 0:
                        for (c=0;c<256;c++)
                        {
                                lookuptab[c]=clut[table4bpp[c][0]]&7;
                                lookuptab[c]|=(clut[table4bpp[c][2]]&7)<<8;
                                lookuptab[c]|=(clut[table4bpp[c][4]]&7)<<16;
                                lookuptab[c]|=(clut[table4bpp[c][6]]&7)<<24;
                        }
                        break;
                        case 1:
                        for (c=0;c<256;c++)
                        {
                                lookuptab[c]=clut[table4bpp[c][0]]&7;
                                lookuptab[c]|=(clut[table4bpp[c][1]]&7)<<8;
                                lookuptab[c]|=(clut[table4bpp[c][2]]&7)<<16;
                                lookuptab[c]|=(clut[table4bpp[c][3]]&7)<<24;
                        }
                        break;
                        case 2:
                        for (c=0;c<256;c++)
                        {
                                lookuptab[c]=clut[table4bpp[c][0]]&7;
                                lookuptab[c]|=(clut[table4bpp[c][0]]&7)<<8;
                                lookuptab[c]|=(clut[table4bpp[c][1]]&7)<<16;
                                lookuptab[c]|=(clut[table4bpp[c][1]]&7)<<24;
                        }
                        break;
                }
        }
}

void initframe()
{
        unsigned char tmphigh;
        if (ulactrl&2)
        {
                tmphigh=crtc[12];
                tmphigh^=0x20;
                tmphigh+=0x74;
                tmphigh&=255;
                startaddr=crtc[13]|(tmphigh<<8);
        }
        else
           startaddr=(crtc[13]|(crtc[12]<<8))<<3;
//        if (startaddr==0x5300) output=1;
}

void dumpcrtc()
{
        int c;
        initframe();
        printf("Start addr %04X\n",startaddr);
        for (c=0;c<17;c++) printf("%02X ",crtc[c]);
}

void resetcrtc()
{
        vc=sc=curline=0;
        for (sc6=0;sc6<32;sc6++) crtc[sc6]=basecrtc[sc6];
}

void drawmode0line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<1);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                ((unsigned long *)buffer->line[physline&511])[(xoffset>>2)&127]=lookuptab[val];
                xoffset+=4;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode1line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<1);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                ((unsigned long *)buffer->line[physline&511])[(xoffset>>2)&127]=lookuptab[val];
                xoffset+=4;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode2line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<1);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                ((unsigned long *)buffer->line[physline&511])[(xoffset>>2)&127]=lookuptab[val];
                xoffset+=4;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode4line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<2);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                buffer->line[physline&511][(xoffset)&511]=clut[table4bpp[val][0]]&7;
                buffer->line[physline&511][(xoffset+1)&511]=clut[table4bpp[val][1]]&7;
                buffer->line[physline&511][(xoffset+2)&511]=clut[table4bpp[val][2]]&7;
                buffer->line[physline&511][(xoffset+3)&511]=clut[table4bpp[val][3]]&7;
                buffer->line[physline&511][(xoffset+4)&511]=clut[table4bpp[val][4]]&7;
                buffer->line[physline&511][(xoffset+5)&511]=clut[table4bpp[val][5]]&7;
                buffer->line[physline&511][(xoffset+6)&511]=clut[table4bpp[val][6]]&7;
                buffer->line[physline&511][(xoffset+7)&511]=clut[table4bpp[val][7]]&7;
                xoffset+=8;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode5line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<2);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                buffer->line[physline&511][(xoffset)&511]=buffer->line[physline&511][(xoffset+1)&511]=clut[table4bpp[val][0]]&7;
                buffer->line[physline&511][(xoffset+2)&511]=buffer->line[physline&511][(xoffset+3)&511]=clut[table4bpp[val][1]]&7;
                buffer->line[physline&511][(xoffset+4)&511]=buffer->line[physline&511][(xoffset+5)&511]=clut[table4bpp[val][2]]&7;
                buffer->line[physline&511][(xoffset+6)&511]=buffer->line[physline&511][(xoffset+7)&511]=clut[table4bpp[val][3]]&7;
                xoffset+=8;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode8line()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<2);
        if (sc&8)
        {
                hline(buffer,0,physline,400,0);
                return;
        }
        hline(buffer,0,physline,xoffset,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                buffer->line[physline&511][(xoffset)&511]=buffer->line[physline&511][(xoffset+1)&511]=clut[table4bpp[val][0]]&7;
                buffer->line[physline&511][(xoffset+2)&511]=buffer->line[physline&511][(xoffset+3)&511]=clut[table4bpp[val][0]]&7;
                buffer->line[physline&511][(xoffset+4)&511]=buffer->line[physline&511][(xoffset+5)&511]=clut[table4bpp[val][1]]&7;
                buffer->line[physline&511][(xoffset+6)&511]=buffer->line[physline&511][(xoffset+7)&511]=clut[table4bpp[val][1]]&7;
                xoffset+=8;
        }
        hline(buffer,xoffset,physline,399,0);
}

void drawmode2lineh()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<1);
        if (sc&8)
        {
                hline(buffer,0,physline<<1,800,0);
                return;
        }
        hline(buffer,0,physline<<1,xoffset<<1,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                ((unsigned long *)buffer->line[(physline&511)<<1])[(xoffset>>1)&255]=lookuptabh[val];
                ((unsigned long *)buffer->line[(physline&511)<<1])[((xoffset>>1)+1)&255]=lookuptabh2[val];
                xoffset+=4;
        }
        hline(buffer,xoffset<<1,physline<<1,799,0);
}

void drawmodelowlineh()
{
        int addr=startaddr+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<2);
        if (sc&8)
        {
                hline(buffer,0,physline<<1,800,0);
                return;
        }
        hline(buffer,0,physline<<1,xoffset<<1,0);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=screenlen[scrsize];
                val=ram[(addr&vidmask)|vidbank];
                addr+=8;
                ((unsigned long *)buffer->line[(physline&511)<<1])[(xoffset>>1)&255]=lookuptabh[val];
                ((unsigned long *)buffer->line[(physline&511)<<1])[((xoffset>>1)+1)&255]=lookuptabh2[val];
                ((unsigned long *)buffer->line[(physline&511)<<1])[((xoffset>>1)+2)&255]=lookuptabh3[val];
                ((unsigned long *)buffer->line[(physline&511)<<1])[((xoffset>>1)+3)&255]=lookuptabh4[val];
                xoffset+=8;
        }
        hline(buffer,xoffset<<1,physline<<1,799,0);
}

int olddbl[64],dbl[64];

void drawteletextline()
{
        int addr=startaddr,x,xx,temp2;
        int xoffset=200-(crtc[1]*3);
        int cr,temp;
        unsigned char *chrset=teletext_characters;
        int colours[2]={0,7};
        int dblhigh=0,sepgfx=0,flashing=0;
        if (!sc)
        {
                for (x=0;x<64;x++)
                {
                        if (dbl[x] && olddbl[x]) olddbl[x]=0;
                        else                     olddbl[x]=dbl[x];
                }
        }
//                memcpy(olddbl,dbl,sizeof(dbl));
        if (hires==1) hline(buffer,0,physline<<1,xoffset<<1,0);
        else          hline(buffer,0,physline,xoffset,0);
//        memset(buffer->line[physline],0,xoffset);
//        if (!sc) printf("addr %04X\n",(addr&vidmask)|vidbank);
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=0x400;
                cr=ram[(addr&vidmask)|vidbank];
//                if (!sc) printf("%04X : CHR %02X  ",(addr&vidmask)|vidbank,cr);
                if (cr&0x80)
                {
                        switch (cr)
                        {
                                case 129: case 130: case 131: case 132:
                                case 133: case 134: case 135:
                                colours[1]=cr&7;
                                chrset=teletext_characters;
                                break;
                                case 136:
                                flashing=1;
                                break;
                                case 137:
                                flashing=0;
                                break;
                                case 140: case 141:
                                dblhigh=cr&1;
                                break;
                                case 145: case 146: case 147: case 148:
                                case 149: case 150: case 151:
                                colours[1]=cr&7;
                                if (sepgfx) chrset=teletext_separated_graphics;
                                else        chrset=teletext_graphics;
                                break;
                                case 153:
                                chrset=teletext_graphics;
                                sepgfx=0;
                                break;
                                case 154:
                                chrset=teletext_separated_graphics;
                                sepgfx=1;
                                break;
                                case 156:
                                colours[0]=0;
                                break;
                                case 157:
                                colours[0]=colours[1];
                                break;
                                default:
//                                if (cr>127 && cr<160)
//                                   printf("Bad control character %i\n",cr);
//                                cr&=127;
                        }
//                        if (cr&0x80) cr=0;
                }
//                else
//                {
                        if (cr<32 || (cr>127 && cr<160)) cr=0;
                        else cr=(cr-32)&127;
//                }
                temp=cr;
                cr=multable[cr]+sc6;
                if (dblhigh && chrset==teletext_characters)
                   cr=multable[temp]+((sc>>1)*6);
                if (dblhigh && olddbl[x] && chrset==teletext_characters) /*Last line was doublehigh*/
                   cr=multable[temp]+((sc>>1)*6)+30;
                temp2=colours[1];
                if (flashing && !flash) colours[1]=colours[0];
//                if (!sc) printf(" %i %i  %i\n",colours[0],colours[1],physline&511);
                if (hires==1)
                {
                        for (xx=0;xx<6;xx++)
                        {
                                if (chrset[cr++])
                                {
                                        buffer->line[(physline&511)<<1][((xoffset+xx)&511)<<1]=colours[1];
                                        buffer->line[(physline&511)<<1][(((xoffset+xx)&511)<<1)+1]=colours[1];
                                }
                                else
                                {
                                        buffer->line[(physline&511)<<1][((xoffset+xx)&511)<<1]=colours[0];
                                        buffer->line[(physline&511)<<1][(((xoffset+xx)&511)<<1)+1]=colours[0];
                                }
                        }
                }
                else
                {
                        for (xx=0;xx<6;xx++)
                        {
                                if (!buffer)
                                {
                                        *(unsigned char *)(0)=1;
                                }
                                if (chrset[cr++])
                                   buffer->line[physline&511][(xoffset+xx)&511]=colours[1];
                                else
                                   buffer->line[physline&511][(xoffset+xx)&511]=colours[0];
                        }
                }
                colours[1]=temp2;
                xoffset+=6;
                addr++;
                if (!sc) dbl[x]=dblhigh;
        }
        if (hires==1) hline(buffer,xoffset<<1,physline<<1,799,0);
        else       hline(buffer,xoffset,physline,399,0);
//        memset(buffer->line[physline]+xoffset,0,400-xoffset);
}

int firstline=0;

void drawcursor()
{
        unsigned short temp,tempx,tempy;
        int xoffset=200-(crtc[1]*3);
        if ((ulactrl & 0xe0) == 0 || (crtc[10] & 0x40) == 0 || crtc[1]==0)
           return;
        temp=crtc[15]+((((crtc[14] ^ 0x20) + 0x74) & 0xff)<<8);
        initframe();
        temp-=startaddr;
        tempx=temp%crtc[1];
        tempy=temp/crtc[1];
//        sprintf(st,"_");
        if (flash)
        {
                if (hires==1) hline(buffer,((tempx*6)+xoffset+1)<<1,((tempy*10)+firstline+9)<<1,(((tempx*6)+5)+xoffset)<<1,7);
                else       hline(buffer,(tempx*6)+xoffset+1,(tempy*10)+firstline+9,((tempx*6)+5)+xoffset,7);
        }
//           drawstring(b,font,tempx<<3,tempy<<3,st,15);
}

int delaylcount=0;
void drawline(int line6502)
{
        int tline;
        int x,y;
        int c;
        if (interlaceline)
        {
                interlaceline=0;
//                return;
        }
        if (curline>511 || physline>511) /*Something has obviously gone wrong in this case*/
        {
                curline=physline=0;
        }
        if (!curline)
        {
                sc=vc=0;
                if (physline>0 && physline<300)
                {
                        if (hires==1) memset(buffer->line[physline<<1],0,800);
                        else       memset(buffer->line[physline],0,400);
                }
                delaylcount=crtc[5];
        }
        if (delaylcount)//curline<crtc[5])
        {
                delaylcount--;
                curline++;
                physline++;
                if (physline>0 && physline<300)
                {
                        if (hires==1) memset(buffer->line[physline<<1],0,800);
                        else       memset(buffer->line[physline],0,400);
                }
                return;
        }
        tline=curline-crtc[5];
        if (!vc && !sc) initframe();
        if (vc<crtc[6])
        {
                if (remakelookup) remaketab();
                if ((crtc[8]&0x30)==0x30)
                {
                        if (physline>0 && physline<300)
                        {
                                if (hires==1) memset(buffer->line[physline<<1],0,800);
                                else       memset(buffer->line[physline],0,400);
                        }
                }
                else
                {
//                        if (!vc) printf("%i %i : %04X %04X\n",vc,sc,((startaddr+sc)&vidmask)|vidbank,vidbank);
                        if (!firstline) firstline=physline;
                        switch (bbcmode)
                        {
                                case 0:
                                if (hires==1) drawmode2lineh();
                                else       drawmode0line();
                                break;
                                case 1:
                                if (hires==1) drawmode2lineh();
                                else       drawmode1line();
                                break;
                                case 2:
                                if (hires==1) drawmode2lineh();
                                else       drawmode2line();
                                break;
                                case 4:
                                if (hires==1) drawmodelowlineh();
                                else       drawmode4line();
                                break;
                                case 5:
                                if (hires==1) drawmodelowlineh();
                                else       drawmode5line();
                                break;
                                case 7:
                                sc6=sc*6;
                                drawteletextline();
                                break;
                                case 8:
                                drawmode8line();
                                break;
                        }
                }
                sc++;
                sc&=31;
                if (sc==crtc[9]+1 || ((sc==(crtc[9]>>1)+1) && crtc[8]&2))
                {
                        sc=0;
                        vc++;
                        if (ulactrl&2)
                           startaddr+=crtc[1];
                        else
                           startaddr+=crtc[1]<<3;
                }
        }
        else
        {
                if (physline>15 && physline<300)
                {
                        if (hires==1) memset(buffer->line[physline<<1],0,800);
                        else          memset(buffer->line[physline],0,400);
                }
                sc++;
                if (sc==crtc[9]+1 || ((sc==(crtc[9]>>1)+1) && crtc[8]&2))
                {
                        sc=0;
                        vc++;
                }
        }
        curline++;
        physline++;
        linesdrawn++;
        if (vc==crtc[7] && (sc==((crtc[5]==4)?1:3)))
        {
                vblankint();
//                printf("Framefly %i\n",lns);
//                printf("Framefly - after %i lines\n6502 reckons it's line %i, SYSVIA IER %02X, I flag %i, PC %04X\n",linesdrawn,line6502,sysvia.ier,p.i,pc);
                linesdrawn=0;
                physline=0;
                if (bbcmode==7)
                   drawcursor();
                firstline=0;
                if (hires==1)
                {
                        if (blurred)
                        {
                                for (y=16;y<300;y++)
                                {
                                        buffer2->line[y<<1][0]=buffer->line[y<<1][0];
                                        for (x=1;x<400;x++)
                                        {
                                                buffer2->line[y<<1][x<<1]=buffer2->line[y<<1][(x<<1)+1]=128+buffer->line[y<<1][x<<1]+(buffer->line[y<<1][(x-1)<<1]<<3);
                                        }
                                }
                                blit(buffer2,screen,0,32,0,16,800,568);
                        }
                        else
                           blit(buffer,screen,0,32,0,16,800,568);
                }
                else if (hires==2)
                {
                        blit(buffer,buf16,0,16,0,0,400,284);
                        Super2xSaI(buf16,buf162,0,0,0,0,800,568);
                        blit(buf162,screen,0,0,0,16,800,568);
                }
                else
                {
                        if (blurred)
                        {
                                for (y=16;y<300;y++)
                                {
                                        buffer2->line[y][0]=buffer->line[y][0];
                                        for (x=1;x<400;x++)
                                        {
                                                buffer2->line[y][x]=128+buffer->line[y][x]+(buffer->line[y][x-1]<<3);
                                        }
                                }
                                blit(buffer2,screen,0,16,0,0,400,284);
                        }
                        else
                           blit(buffer,screen,0,16,0,0,400,284);
                }
                flashint++;
                if (flash && flashint==35)
                   flashint=flash=0;
                else if (!flash && flashint==15)
                {
                        flashint=0;
                        flash=1;
                }
                interlaceline=1;
        }
        if (vc==crtc[4]+1)
        {
                if (crtc[7]>=crtc[4])
                {
//                        printf("%i %i\n",crtc[7],crtc[4]);
                        vc=sc=0;
                        initframe();
//                        printf("Rupture at %i\n",lns);
                        delaylcount=crtc[5];
                }
                else
                {
//                        printf("Vblank started %i\n",lns);
                        curline=0;
//                        waitforsync();
                        frames++;
                }
        }
}

void maketable2()
{
        int temp,left,right,temp2,c,col,bits;
        for (temp=0;temp<256;temp++)
        {
                temp2=temp;
                for (c=0;c<8;c++)
                {
                     left=0;
                     if (temp2&2)
                        left|=1;
                     if (temp2&8)
                        left|=2;
                     if (temp2&32)
                        left|=4;
                     if (temp2&128)
                        left|=8;
                     table4bpp[temp][c]=left;
                     temp2<<=1;
                }
        }
}

void genpal()
{
        int c,d;
        for (c=0;c<8;c++)
        {
                for (d=0;d<8;d++)
                {
                        beebpal[c+(d<<3)+128].r=beebpal[c&7].r>>1;
                        beebpal[c+(d<<3)+128].r+=beebpal[d&7].r>>1;
                        beebpal[c+(d<<3)+128].g=beebpal[c&7].g>>1;
                        beebpal[c+(d<<3)+128].g+=beebpal[d&7].g>>1;
                        beebpal[c+(d<<3)+128].b=beebpal[c&7].b>>1;
                        beebpal[c+(d<<3)+128].b+=beebpal[d&7].b>>1;
                        monopal[c+(d<<3)+128].r=monopal[c&7].r>>1;
                        monopal[c+(d<<3)+128].r+=monopal[d&7].r>>1;
                        monopal[c+(d<<3)+128].g=monopal[c&7].g>>1;
                        monopal[c+(d<<3)+128].g+=monopal[d&7].g>>1;
                        monopal[c+(d<<3)+128].b=monopal[c&7].b>>1;
                        monopal[c+(d<<3)+128].b+=monopal[d&7].b>>1;
                }
        }
}

void updatepalette()
{
        if (mono) set_palette(monopal);
        else      set_palette(beebpal);
}

void updategfxmode()
{
        if (hires==2) set_color_depth(16);
        else          set_color_depth(8);
        if (fullscreen)
        {
                if (hires) set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0);
                else       set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0);
        }
        else
        {
                if (hires) set_gfx_mode(GFX_AUTODETECT_WINDOWED,800,600,0,0);
                else       set_gfx_mode(GFX_AUTODETECT_WINDOWED,400,300,0,0);
        }
        if (mono) set_palette(monopal);
        else      set_palette(beebpal);
        clear(buffer);
}

void initvideo()
{
        int c;
        Init_2xSaI(16);
        for (c=8;c<256;c++)
            beebpal[c]=beebpal[c&7];
        for (c=8;c<256;c++)
            monopal[c]=monopal[c&7];
        genpal();
        buffer=create_bitmap(1024,1024);
        buffer2=create_bitmap(808,608);
        set_color_depth(16);
        buf16=create_bitmap(400,284);
        buf162=create_bitmap(800,568);
        set_color_depth(8);
        clear(buffer);
        clear(buffer2);
        clear(buf16);
        clear(buf162);
        if (hires==2) set_color_depth(16);
        if (fullscreen)
        {
                if (hires) set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0);
                else       set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0);
        }
        else
        {
                if (hires) set_gfx_mode(GFX_AUTODETECT_WINDOWED,800,600,0,0);
                else       set_gfx_mode(GFX_AUTODETECT_WINDOWED,400,300,0,0);
        }
        for (c=0;c<128;c++)
            multable[c]=c*60;
        maketable2();
        updatepalette();
        LOCK_VARIABLE(fps);
        LOCK_FUNCTION(secint);
        install_int_ex(secint,MSEC_TO_TIMER(1000));
        vidbank=0;
}

void fadepal()
{
        int c;
        for (c=0;c<8;c++) beebpal[c].r>>=1;
        for (c=0;c<8;c++) beebpal[c].g>>=1;
        for (c=0;c<8;c++) beebpal[c].b>>=1;
        for (c=128;c<256;c++) beebpal[c].r>>=1;
        for (c=128;c<256;c++) beebpal[c].g>>=1;
        for (c=128;c<256;c++) beebpal[c].b>>=1;
        for (c=0;c<8;c++) monopal[c].r>>=1;
        for (c=0;c<8;c++) monopal[c].g>>=1;
        for (c=0;c<8;c++) monopal[c].b>>=1;
        for (c=128;c<256;c++) monopal[c].r>>=1;
        for (c=128;c<256;c++) monopal[c].g>>=1;
        for (c=128;c<256;c++) monopal[c].b>>=1;
        updatepalette();
}

void restorepal()
{
        int c;
        for (c=0;c<8;c++) beebpal[c].r<<=1;
        for (c=0;c<8;c++) beebpal[c].g<<=1;
        for (c=0;c<8;c++) beebpal[c].b<<=1;
        for (c=128;c<256;c++) beebpal[c].r<<=1;
        for (c=128;c<256;c++) beebpal[c].g<<=1;
        for (c=128;c<256;c++) beebpal[c].b<<=1;
        for (c=0;c<8;c++) monopal[c].r<<=1;
        for (c=0;c<8;c++) monopal[c].g<<=1;
        for (c=0;c<8;c++) monopal[c].b<<=1;
        for (c=128;c<256;c++) monopal[c].r<<=1;
        for (c=128;c<256;c++) monopal[c].g<<=1;
        for (c=128;c<256;c++) monopal[c].b<<=1;
        updatepalette();
}

void scrshot(char *fn)
{
        if (mono) save_bitmap(fn,buffer,monopal);
        else      save_bitmap(fn,buffer,beebpal);
}

void savebuffers()
{
        save_bitmap("buffer.bmp",buffer,beebpal);
        save_bitmap("buffer2.bmp",buffer2,beebpal);
        save_bitmap("buf16.bmp",buf16,beebpal);
        save_bitmap("buf162.bmp",buf162,beebpal);
}
