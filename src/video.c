/*B-em 1.4 by Tom Walker*/
/*Video emulation*/

#include <stdio.h>
#include <allegro.h>
#include <winalleg.h>
#include "bbctext.h"
#include "b-em.h"
#include "2xsai.h"

int drawfull=2;
int fullscreen=0;
int chunkid,chunklen,intone;
int vsyncint=0;
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

BITMAP *buffer,*buffer2,*buf16,*buf162,*vbuffer;
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
unsigned long cursordat[4]={0xFF,0xFFFF0000,0xFFFF,0xFFFFFFFF};
unsigned long cursoraddr;
int cursorstart,cursorend;
int cursorblink,cursoron,cursorcount;
unsigned char cursorram[65536/*<<1*/]; /*Horrible method of generating cursor, but
                                  (hopefully) fast*/

unsigned long lookuptab[256],lookuptab2[256];
unsigned long lookuptabh[256],lookuptabh2[256],lookuptabh3[256],lookuptabh4[256];
unsigned char ulactrl;
int clut[16],clut2[16];
int remakelookup=1;

void redocursor()
{
        int y;
        int size;
//        return;
        for (y=cursorstart;y<=cursorend;y++)
        {
                if (y<16)
                   cursorram[(cursoraddr+y)&0xFFFF]=cursorram[(cursoraddr+16+y)&0xFFFF]=cursorram[(cursoraddr+32+y)&0xFFFF]=cursorram[(cursoraddr+48+y)&0xFFFF]=0;
        }
        cursoraddr=(crtc[15]|(crtc[14]<<8))<<4;
        cursorstart=crtc[10]&0x1F;
        cursorend=(crtc[11]&0x1F);
        cursorblink=(crtc[10]>>5)&3;
//        printf("Cursoraddr %04X %04X %02X %02X %i %i %04X\n",cursoraddr,cursoraddr>>1,crtc[15],crtc[14],cursorstart,cursorend,startaddr);
        size=(ulactrl>>5)&3;
        if (cursoraddr&0x20000) return;
        while (cursoraddr&0x10000) cursoraddr-=(screenlen[scrsize]<<1);
        if (!(ulactrl&0xE0) || (crtc[8]&0xC0)==0xC0 || cursorend<=cursorstart) return;
        for (y=cursorstart;y<=cursorend;y++)
        {
                if (y<16 && cursoron)
                {
                        cursorram[(cursoraddr+y)&0xFFFF]=cursordat[size]&0xFF;
                        cursorram[(cursoraddr+16+y)&0xFFFF]=cursordat[size]>>8;
                        cursorram[(cursoraddr+32+y)&0xFFFF]=cursordat[size]>>16;
                        cursorram[(cursoraddr+48+y)&0xFFFF]=cursordat[size]>>24;
                }
        }
}

void writecrtc(unsigned short addr, unsigned char val)
{
        if (!(addr&1))
           crtcreg=val&31;
        else
        {
                crtc[crtcreg]=val&crtcmask[crtcreg];
                if (crtcreg==0 || crtcreg==1 || crtcreg==4 || crtcreg==6) drawfull=2;
                if (crtcreg==6) clear(buffer);
                if (crtcreg==14 || crtcreg==15 || crtcreg==10 || crtcreg==11)
                {
                        redocursor();
                }
//                rpclog("Write CRTC R%i %04X %02X %04X\n",crtcreg,addr,val,pc);
        }
}
unsigned char readcrtc(unsigned short addr)
{
        if (!(addr&1))
           return crtcreg;
        return crtc[crtcreg];
}

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

void writeula(unsigned short addr, unsigned char val)
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

unsigned long cursorcol;
void remaketab()
{
        int c;
                unsigned char *tb;
//              return;
        cursorcol=clut2[15]|(clut2[15]<<8)|(clut2[15]<<16)|(clut2[15]<<24);
        if (hires&1)
        {
                switch (bbcmode)
                {
                        case 0:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptabh[c];
                                                                tb[0] = clut[table4bpp[c][0]]&7;
                                                                tb[1] = clut[table4bpp[c][1]]&7;
                                                                tb[2] = clut[table4bpp[c][2]]&7;
                                tb[3] = clut[table4bpp[c][3]]&7;

                                                        tb = (unsigned char *)&lookuptabh2[c];
                                tb[0]=clut[table4bpp[c][4]]&7;
                                tb[1]=clut[table4bpp[c][5]]&7;
                                tb[2]=clut[table4bpp[c][6]]&7;
                                tb[3]=clut[table4bpp[c][7]]&7;
                        }
                        break;
                        case 1:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptabh[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][0]]&7;
                                                                tb[2] = tb[3] = clut[table4bpp[c][1]]&7;
                                                        tb = (unsigned char *)&lookuptabh2[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][2]]&7;
                                                                tb[2] = tb[3] = clut[table4bpp[c][3]]&7;
                        }
                        break;
                        case 2:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptabh[c];
                                                                tb[0] = tb[1] =
                                                                tb[2] = tb[3] = clut[table4bpp[c][0]]&7;

                                                        tb = (unsigned char *)&lookuptabh2[c];
                                                                tb[0] = tb[1] =
                                                                tb[2] = tb[3] = clut[table4bpp[c][1]]&7;
                        }
                        break;
                        case 4:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptabh[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][0]];
                                                                tb[2] = tb[3] = clut[table4bpp[c][1]];

                                                        tb = (unsigned char *)&lookuptabh2[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][2]];
                                                                tb[2] = tb[3] = clut[table4bpp[c][3]];

                                                        tb = (unsigned char *)&lookuptabh3[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][4]];
                                                                tb[2] = tb[3] = clut[table4bpp[c][5]];

                                                        tb = (unsigned char *)&lookuptabh4[c];
                                                                tb[0] = tb[1] = clut[table4bpp[c][6]];
                                                                tb[2] = tb[3] = clut[table4bpp[c][7]];
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
                                                        tb = (unsigned char *)&lookuptab[c];
                                tb[0]=clut[table4bpp[c][0]]&7;
                                tb[1]=clut[table4bpp[c][2]]&7;
                                tb[2]=clut[table4bpp[c][4]]&7;
                                tb[3]=clut[table4bpp[c][6]]&7;
                        }
                        break;
                        case 1:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptab[c];
                                tb[0]=clut[table4bpp[c][0]]&7;
                                tb[1]=clut[table4bpp[c][1]]&7;
                                tb[2]=clut[table4bpp[c][2]]&7;
                                tb[3]=clut[table4bpp[c][3]]&7;
                        }
                        break;
                        case 2:
                        for (c=0;c<256;c++)
                        {
                                                        tb = (unsigned char *)&lookuptab[c];
                                tb[0]=clut[table4bpp[c][0]]&7;
                                tb[1]=clut[table4bpp[c][0]]&7;
                                tb[2]=clut[table4bpp[c][1]]&7;
                                tb[3]=clut[table4bpp[c][1]]&7;
                        }
                        break;
                        case 4:
                        for (c=0;c<256;c++)
                        {
                                tb = (unsigned char *)&lookuptab[c];
                                tb[0] = clut[table4bpp[c][0]];
                                tb[1] = clut[table4bpp[c][1]];
                                tb[2] = clut[table4bpp[c][2]];
                                tb[3] = clut[table4bpp[c][3]];
                                tb = (unsigned char *)&lookuptab2[c];
                                tb[0] = clut[table4bpp[c][4]];
                                tb[1] = clut[table4bpp[c][5]];
                                tb[2] = clut[table4bpp[c][6]];
                                tb[3] = clut[table4bpp[c][7]];
                        }
                        break;
                        case 5:
                        for (c=0;c<256;c++)
                        {
                                tb = (unsigned char *)&lookuptab[c];
                                tb[0] = clut[table4bpp[c][0]];
                                tb[1] = clut[table4bpp[c][0]];
                                tb[2] = clut[table4bpp[c][1]];
                                tb[3] = clut[table4bpp[c][1]];
                                tb = (unsigned char *)&lookuptab2[c];
                                tb[0] = clut[table4bpp[c][2]];
                                tb[1] = clut[table4bpp[c][2]];
                                tb[2] = clut[table4bpp[c][3]];
                                tb[3] = clut[table4bpp[c][3]];
                        }
                        break;
                }
        }
}

void latchpen()
{
        int temp=(vc*crtc[9]*128)+(sc*128);
        crtc[0x10]=(temp>>7)&0x3F;
        crtc[0x11]=temp<<1;
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
int firstx=65536,lastx=-1;

void drawmode2line()
{
        int addr=(startaddr+sc)|vidbank,caddr=(startaddr<<1)+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<1);
        unsigned long *bufferp=(unsigned long *)buffer->line[physline&511];
        if (firstx>xoffset) firstx=xoffset;
        if (sc&8)
        {
                hline(buffer,0,physline&511,399,0);
        }
        else
        {
                for (x=0;x<crtc[1];x++)
                {
                        if (addr & vidlimit) { addr-=screenlen[scrsize]; addr&=0xFFFF; }
                        val=ram[addr];
                        addr+=8;
                        bufferp[(xoffset>>2)&127]=lookuptab[val];
                        xoffset+=4;
                }
                if (lastx<xoffset) lastx=xoffset;
                if (drawfull) hline(buffer,xoffset,physline,399,0);
                xoffset=200-(crtc[1]<<1);
                if (drawfull) hline(buffer,0,physline,xoffset,0);
        }
        if (sc>=cursorstart && sc<=cursorend)
        {
                xoffset=200-(crtc[1]<<1);
                for (x=0;x<crtc[1];x++)
                {
                        if (caddr&0x10000) caddr-=(screenlen[scrsize]<<1);
                        if (cursorram[caddr])
                           bufferp[(xoffset>>2)&127]^=0x07070707;
                        caddr+=16;
                        xoffset+=4;
                }
        }
}

void drawmode4line()
{
        int addr=(startaddr+sc)|vidbank,caddr=(startaddr<<1)+sc,x;
        unsigned char val;
        int xoffset=200-(crtc[1]<<2);
        unsigned long *bufferp=(unsigned long *)buffer->line[physline&511];
        if (firstx>xoffset) firstx=xoffset;
        if (sc&8)
        {
//                rpclog("Blanking line %i\n",physline);
                hline(buffer,0,physline&511,398,0);
        }
        else
        {
//                rpclog("Drawling line %i\n",physline);
                for (x=0;x<crtc[1];x++)
                {
                        if (addr & vidlimit) { addr-=screenlen[scrsize]; addr&=0xFFFF; }
                        val=ram[addr];
                        addr+=8;
                        bufferp[(xoffset>>2)&127]=lookuptab[val];
                        bufferp[((xoffset+4)>>2)&127]=lookuptab2[val];
                        xoffset+=8;
                }
                if (lastx<xoffset) lastx=xoffset;
                if (drawfull) hline(buffer,xoffset,physline,399,0);
                xoffset=200-(crtc[1]<<2);
                if (drawfull) hline(buffer,0,physline,xoffset,0);
        }
        if (sc>=cursorstart && sc<=cursorend)
        {
                xoffset=200-(crtc[1]<<2);
//                rpclog("Cursor on at line %i\n",physline);
                for (x=0;x<crtc[1];x++)
                {
                        if (caddr&0x10000) caddr-=(screenlen[scrsize]<<1);
                        if (cursorram[caddr]) {
                           bufferp[(xoffset>>2)&127]^=0x07070707;
                           bufferp[((xoffset+4)>>2)&127]^=0x07070707; }
                        caddr+=16;
                        xoffset+=8;
                }
        }
}

void drawmode2lineh(int line)
{
        int addr=(startaddr+sc)|vidbank,caddr=(startaddr<<1)+sc,x,xp;
        unsigned char val;
        unsigned long *bufferp=(unsigned long *)buffer->line[line];
        unsigned long *bufferp2=(unsigned long *)buffer->line[line+1];
        int xoffset=200-(crtc[1]<<1);
        if (firstx>xoffset) firstx=xoffset;
        if (drawfull)
        {
                hline(buffer,0,line,xoffset<<1,0);
                if (hires==3) hline(buffer,0,line+1,xoffset<<1,0);
        }
        if (sc&8)
        {
                hline(buffer,0,line,799,0);
                if (hires==3)
                   hline(buffer,0,line+1,799,0);
        }
        else
        {
                for (x=0;x<crtc[1];x++)
                {
                        if (addr & vidlimit) { addr-=screenlen[scrsize]; addr&=0xFFFF; }
                        val=ram[addr];
                        addr+=8;
                        xp=(xoffset>>1)&255;
                        bufferp[xp]=lookuptabh[val];
                        bufferp[xp+1]=lookuptabh2[val];
                        if (hires==3)
                        {
                                bufferp2[xp]=lookuptabh[val];
                                bufferp2[xp+1]=lookuptabh2[val];
                        }
                        xoffset+=4;
                }
                if (lastx<xoffset) lastx=xoffset;
                if (drawfull)
                {
                        hline(buffer,xoffset<<1,line,799,0);
                        if (hires==3) hline(buffer,xoffset<<1,line+1,799,0);
                }
        }
        xoffset=200-(crtc[1]<<1);
        if (sc>=cursorstart && sc<=cursorend)
        {
                for (x=0;x<crtc[1];x++)
                {
                        xp=(xoffset>>1)&255;
                        if (caddr&0x10000) caddr-=(screenlen[scrsize]<<1);
                        if (cursorram[caddr]) {
                           bufferp[xp]^=0x07070707;
                           bufferp[xp+1]^=0x07070707;
                           if (hires==3) {
                              bufferp2[xp]^=0x07070707;
                              bufferp2[xp+1]^=0x07070707;
                           }
                        }
                        caddr+=16;
                        xoffset+=4;
                }
        }
}

void drawmodelowlineh(int line)
{
        int addr=(startaddr+sc)|vidbank,caddr=(startaddr<<1)+sc,x,xp;
        unsigned char val;
        unsigned long *bufferp=(unsigned long *)buffer->line[line];
        unsigned long *bufferp2=(unsigned long *)buffer->line[line+1];
        int xoffset=200-(crtc[1]<<2);
        if (firstx>xoffset) firstx=xoffset;
        if (drawfull)
        {
                hline(buffer,0,line,xoffset<<1,0);
                if (hires==3) hline(buffer,0,line+1,xoffset<<1,0);
        }
        if (sc&8)
        {
                hline(buffer,0,line,799,0);
                if (hires==3)
                   hline(buffer,0,line+1,799,0);
        }
        else
        {
                for (x=0;x<crtc[1];x++)
                {
                        if (addr & vidlimit) { addr-=screenlen[scrsize]; addr&=0xFFFF; }
                        val=ram[addr];
                        xp=(xoffset>>1)&255;
                        addr+=8;
                        bufferp[xp]=lookuptabh[val];
                        bufferp[xp+1]=lookuptabh2[val];
                        bufferp[xp+2]=lookuptabh3[val];
                        bufferp[xp+3]=lookuptabh4[val];
                        if (hires==3)
                        {
                                bufferp2[xp]=lookuptabh[val];
                                bufferp2[xp+1]=lookuptabh2[val];
                                bufferp2[xp+2]=lookuptabh3[val];
                                bufferp2[xp+3]=lookuptabh4[val];
                        }
                        xoffset+=8;
                }
        }
        if (drawfull)
        {
                hline(buffer,xoffset<<1,line,799,0);
                if (hires==3) hline(buffer,xoffset<<1,line+1,799,0);
        }
        if (lastx<xoffset) lastx=xoffset;
        xoffset=200-(crtc[1]<<2);
        if (sc>=cursorstart && sc<=cursorend)
        {
                for (x=0;x<crtc[1];x++)
                {
                        xp=(xoffset>>1)&255;
                        if (caddr&0x10000) caddr-=(screenlen[scrsize]<<1);
                        if (cursorram[caddr]) {
                           bufferp[xp  ]^=0x07070707; bufferp[xp+1]^=0x07070707;
                           bufferp[xp+2]^=0x07070707; bufferp[xp+3]^=0x07070707;
                           if (hires==3) {
                              bufferp2[xp  ]^=0x07070707; bufferp2[xp+1]^=0x07070707;
                              bufferp2[xp+2]^=0x07070707; bufferp2[xp+3]^=0x07070707;
                           }
                        }
                        caddr+=16;
                        xoffset+=8;
                }
        }
}

int olddbl[64],dbl[64];
int curlinedbl,nextlinedbl=0;

void drawteletextline(int line)
{
        int addr=startaddr,x,xx,temp2;
        int prntdbl=0;
        int xoffset=200-(crtc[1]*3);
        int cr,temp,held=0;
        unsigned char heldchar=0;
        unsigned char *chrset=teletext_characters;
        int colours[2]={0,7};
        int dblhigh=0,sepgfx=0,flashing=0,graph=0;
        unsigned char *bufferp,*bufferp2;
        if (firstx>xoffset) firstx=xoffset;
        if (!sc)
        {
                curlinedbl=nextlinedbl;
                nextlinedbl=0;
        }
        if (drawfull)
        {
                if (hires&1 && hires!=5) hline(buffer,0,line,xoffset<<1,0);
                else         hline(buffer,0,line,xoffset,0);
                if (hires==3) hline(buffer,0,line+1,xoffset<<1,0);
        }
        bufferp=buffer->line[line];
        bufferp2=buffer->line[line+1];
        for (x=0;x<crtc[1];x++)
        {
                if (addr&0x8000) addr-=0x400;
                cr=ram[(addr&vidmask)|vidbank];
//                if (!sc) rpclog("Loaded char %04X %i %02X ",addr,x,cr);
                cr&=0x7F;
                if (graph && (cr&32)) heldchar=cr;
                if (cr<32)
                {
                        switch (cr|0x80)
                        {
                                case 129: case 130: case 131: case 132:
                                case 133: case 134: case 135:
                                colours[1]=cr&7;
                                chrset=teletext_characters;
                                graph=0;
                                break;
                                case 136:
                                flashing=1;
                                break;
                                case 137:
                                flashing=0;
                                break;
                                case 140: case 141:
                                dblhigh=cr&1;
                                if (dblhigh && !curlinedbl)
                                   nextlinedbl=1;
                                break;
                                case 145: case 146: case 147: case 148:
                                case 149: case 150: case 151:
                                colours[1]=cr&7;
                                if (sepgfx) chrset=teletext_separated_graphics;
                                else        chrset=teletext_graphics;
                                graph=1;
                                break;
                                case 152:
                                colours[1]=colours[0];
                                break;
                                case 153:
                                chrset=teletext_graphics;
                                sepgfx=0;
                                graph=1;
                                break;
                                case 154:
                                chrset=teletext_separated_graphics;
                                sepgfx=1;
                                graph=1;
                                break;
                                case 156:
                                colours[0]=0;
                                break;
                                case 157:
                                colours[0]=colours[1];
                                break;
                                case 158:
                                held=1;
                                break;
                                case 159:
                                held=0;
                                break;
                        }
                        if (held && graph)
                           cr=heldchar;
                        else
                           cr=32;
                }
                if (cr<32) cr=0;
                else cr-=32;
                if (curlinedbl && !dblhigh) cr=0;
//                if (!sc) rpclog("Ended up as %02X\n",cr);
                temp=cr;
                cr=multable[cr]+sc6;
                if (dblhigh)
                   cr=multable[temp]+((sc>>1)*6);
                if (curlinedbl) /*Last line was doublehigh*/
                   cr=multable[temp]+((sc>>1)*6)+30;
                temp2=colours[1];
                if (flashing && !flash) colours[1]=colours[0];
                if (hires&1)
                {
                        for (xx=0;xx<6;xx++)
                        {
                                bufferp[((xoffset+xx)&511)<<1]=colours[chrset[cr]];
                                bufferp[(((xoffset+xx)&511)<<1)+1]=colours[chrset[cr]];
                                if (hires==3)
                                {
                                        bufferp2[((xoffset+xx)&511)<<1]=colours[chrset[cr]];
                                        bufferp2[(((xoffset+xx)&511)<<1)+1]=colours[chrset[cr]];
                                }
                                cr++;
                        }
                }
                else
                {
                        for (xx=0;xx<6;xx++)
                        {
                                if (chrset[cr++])
                                   bufferp[(xoffset+xx)&511]=colours[1];
                                else
                                   bufferp[(xoffset+xx)&511]=colours[0];
                        }
                }
                colours[1]=temp2;
                xoffset+=6;
                addr++;
                if (!sc) dbl[x]=dblhigh;
        }
        if (lastx<xoffset) lastx=xoffset;
        if (drawfull)
        {
                if (hires&1 && hires!=5) hline(buffer,xoffset<<1,line,799,0);
                else         hline(buffer,xoffset,   line,399,0);
                if (hires==3) hline(buffer,xoffset<<1,line+1,799,0);
        }
}

int firstline=-1,lastline=-1;

void drawcursor()
{
        unsigned short temp,tempx,tempy;
        int xoffset=200-(crtc[1]*3);
//        return;
        if ((ulactrl & 0xe0) == 0 || (crtc[10] & 0x40) == 0 || crtc[1]==0 || (crtc[8]&0xC0)==0xC0 || !cursoron)
           return;
        if ((crtc[10]&0x1F)>=(crtc[11]&0x1F)) return;
        temp=crtc[15]+((((crtc[14] ^ 0x20) + 0x74) & 0xff)<<8);
        initframe();
        temp-=startaddr;
        tempx=temp%crtc[1];
        tempy=temp/crtc[1];
//        sprintf(st,"_");
//        if (flash)
//        {
                if (hires==5) hline(buffer,((tempx*6)+xoffset+1)<<1,((tempy*10)+firstline+9),(((tempx*6)+5)+xoffset)<<1,7);
                else if (hires&1) hline(buffer,((tempx*6)+xoffset+1)<<1,((tempy*10)+firstline+9)<<1,(((tempx*6)+5)+xoffset)<<1,7);
                else         hline(buffer,(tempx*6)+xoffset+1,(tempy*10)+firstline+9,((tempx*6)+5)+xoffset,7);
                if (hires==3) hline(buffer,((tempx*6)+xoffset+1)<<1,(((tempy*10)+firstline+9)<<1)+1,(((tempx*6)+5)+xoffset)<<1,7);
//        }
//           drawstring(b,font,tempx<<3,tempy<<3,st,15);
}


int delaylcount=0;
int olx,ofx,oll,ofl;
int vblcount=0;
int interlaceframe=0;
void drawline(int line6502)
{
        int tline;
        int x,y;
//        return;
        if (interlaceline)
        {
                interlaceline=0;
                return;
        }
        lns=curline;
        if (vblcount)
        {
                vblcount--;
                if (!vblcount)
                   vblankint();
        }
        if (curline>511 || physline>511) /*Something has obviously gone wrong in this case*/
        {
                curline=physline=0;
                if (model==3) physline+=16;
        }
        if (!curline)
        {
                sc=vc=0;
                cursorcount++;
                switch (cursorblink)
                {
                        case 0: cursoron=1; break;
                        case 1: cursoron=0; break;
                        case 2: cursorcount&=15; if (!cursorcount) cursoron^=1; break;
                        case 3: cursorcount&=31; if (!cursorcount) cursoron^=1; break;
                }
                redocursor();
                if (!(fasttape && motor) || !flashint)
                {
                        if (physline>0 && physline<300)
                        {
                                if (hires&1 && hires!=5)  memset(buffer->line[physline<<1],0,800);
                                else          memset(buffer->line[physline],0,(hires==5)?800:400);
                                if (hires==3) memset(buffer->line[(physline<<1)+1],0,800);
                        }
                }
                delaylcount=crtc[5];
                initframe();
        }
        if (vc==crtc[7] && sc==0)
        {
                vblankintlow();
                vblcount=crtc[3]>>4;
                linesdrawn=0;
                physline=0;
                if (model==3)   physline+=16;
                if (bbcmode==7) drawcursor();
                if (!(fasttape && motor) || !flashint)
                {
                        lastline++;
                        if ((olx!=lastx) || (ofx!=firstx) || (oll!=lastline) || (ofl!=firstline) && (drawfull<3))
                           drawfull=2;
                        oll=lastline; ofl=firstline;
                        olx=lastx; ofx=firstx;
                        if (drawfull==3)
                        {
                                clear_to_color(screen,makecol(0,0,0));
                        }
                        if (drawfull)
                        {
                                firstline=16;
                                firstx=0;
                                lastline=300;
                                lastx=400;
                        }
//                        textprintf(buffer,font,0,firstline,7,"%i %i %i %i",firstline,lastline,firstx,lastx);
                        if (hires==1)
                        {
                                if (blurred)
                                {
                                        for (y=firstline;y<lastline;y++)
                                        {
                                                buffer2->line[y<<1][0]=buffer->line[y<<1][0];
                                                for (x=(firstx+1);x<lastx;x++)
                                                {
                                                        buffer2->line[y<<1][x<<1]=buffer2->line[y<<1][(x<<1)+1]=128+buffer->line[y<<1][x<<1]+(buffer->line[y<<1][(x-1)<<1]<<3);
                                                }
                                        }
                                        blit(buffer2,screen,firstx<<1,firstline<<1,firstx<<1,(firstline<<1)-32,(lastx-firstx)<<1,(lastline-firstline)<<1);
                                }
                                else
                                   blit(buffer,screen,firstx<<1,firstline<<1,firstx<<1,(firstline<<1)-32,(lastx-firstx)<<1,(lastline-firstline)<<1);
                        }
                        else if (hires==2)
                        {
                                blit(buffer,buf16,firstx,firstline,firstx,firstline-16,lastx-firstx,lastline-firstline);
                                Super2xSaI(buf16,buf162,(firstx-1),(firstline-1)-16,(firstx-1)<<1,((firstline-1)-16)<<1,(lastx-firstx)+2,(lastline-firstline)+2);
                                hline(screen,firstx<<1,((firstline-16)<<1)-1,lastx<<1,0);
                                blit(buf162,screen,(firstx<<1),((firstline-16)<<1),firstx<<1,(firstline-16)<<1,(lastx-firstx)<<1,(lastline-firstline)<<1);
//                                blit(buffer,buf16,firstx,firstline,0,0,lastx-firstx,lastline-firstline);
//                                Super2xSaI(buf16,buf162,0,0,0,0,(lastx-firstx)<<1,(lastline-firstline)<<1);
//                                blit(buf162,screen,1,1,(firstx<<1)+1,(firstline<<1)-31,((lastx-firstx)<<1)-2,((lastline-firstline)<<1)-2);
                        }
                        else if (hires==3)
                        {
                                if (blurred)
                                {
                                        for (y=(firstline<<1);y<(lastline<<1);y++)
                                        {
                                                buffer2->line[y][0]=buffer->line[y][0];
                                                for (x=firstx+1;x<lastx;x++)
                                                {
                                                        buffer2->line[y][x<<1]=buffer2->line[y][(x<<1)+1]=128+buffer->line[y][x<<1]+(buffer->line[y][(x-1)<<1]<<3);
                                                }
                                        }
                                        blit(buffer2,screen,firstx<<1,firstline<<1,firstx<<1,(firstline<<1)-32,(lastx-firstx)<<1,(lastline-firstline)<<1);
                                }
                                else
                                   blit(buffer,screen,firstx<<1,firstline<<1,firstx<<1,(firstline<<1)-32,(lastx-firstx)<<1,(lastline-firstline)<<1);
                        }
                        else if (hires==5)
                        {
                                if (blurred)
                                {
                                        for (y=firstline;y<lastline;y++)
                                        {
                                                buffer2->line[y][0]=buffer->line[y][0];
                                                for (x=firstx+1;x<lastx;x++)
                                                {
                                                        buffer2->line[y][x<<1]=buffer2->line[y][(x<<1)+1]=128+buffer->line[y][x<<1]+(buffer->line[y][(x-1)<<1]<<3);
                                                }
                                        }
                                        blit(buffer2,vbuffer,firstx<<1,firstline,firstx<<1,firstline-16,(lastx-firstx)<<1,lastline-firstline);
                                        stretch_blit(vbuffer,screen,0,0,800,300,0,0,800,600);
                                }
                                else
                                {
                                        blit(buffer,vbuffer,firstx<<1,firstline,firstx<<1,firstline-16,(lastx-firstx)<<1,lastline-firstline);
                                        stretch_blit(vbuffer,screen,0,0,800,300,0,0,800,600);
                                }
                        }
                        else
                        {
                                if (blurred)
                                {
                                        for (y=firstline;y<lastline;y++)
                                        {
                                                buffer2->line[y][0]=buffer->line[y][0];
                                                for (x=firstx+1;x<lastx;x++)
                                                {
                                                        buffer2->line[y][x]=128+buffer->line[y][x]+(buffer->line[y][x-1]<<3);
                                                }
                                        }
                                        blit(buffer2,screen,firstx,firstline,firstx,firstline-16,lastx-firstx,lastline-firstline);
                                }
                                else
                                {
                                        if (readflash) rectfill(buffer,368,0,400,20,readflash);
                                        readflash=0;
                                        blit(buffer,screen,0,16,0,0,400,300);
//                                        blit(buffer,screen,firstx,firstline,firstx,firstline-16,lastx-firstx,lastline-firstline);
                                }
                        }
//                        textprintf(screen,font,0,0,makecol(255,255,255),"%04X %i  ",pc,motor);
                        if (drawfull)
                           drawfull--;
                }
                firstline=lastline=lastx=-1;
                firstx=65536;
                flashint++;
                if (flash && flashint==35)
                   flashint=flash=0;
                else if (!flash && flashint==15)
                {
                        flashint=0;
                        flash=1;
                }
                interlaceframe^=1;
                interlaceline=interlaceframe&(crtc[8]&1);
        }
        if (delaylcount)
        {
                delaylcount--;
                curline++;
                physline++;
                if (!(fasttape && motor) || !flashint)
                {
                        if (physline>0 && physline<300)
                        {
                                if (hires&1 && hires!=5)  memset(buffer->line[physline<<1],0,800);
                                else          memset(buffer->line[physline],0,(hires==5)?800:400);
                                if (hires==3) memset(buffer->line[(physline<<1)+1],0,800);
                        }
                }
                return;
        }
        tline=curline-crtc[5];
        if (vc<crtc[6])
        {
                if (remakelookup)
                {
                        remaketab();
                        remakelookup=0;
                }
                if (!(fasttape && motor) || !flashint)
                {
                        if ((crtc[8]&0x30)==0x30)
                        {
                                if (physline>0 && physline<300)
                                {
                                        if (hires&1 && hires!=5)  memset(buffer->line[physline<<1],0,800);
                                        else          memset(buffer->line[physline],0,(hires==5)?800:400);
                                        if (hires==3) memset(buffer->line[(physline<<1)+1],0,800);
                                }
                        }
                        else
                        {
                                if (firstline==-1) firstline=physline;
                                lastline=physline;
                                switch (bbcmode)
                                {
                                        case 0: case 1: case 2:
                                        if (hires&1)
                                        {
                                                if (hires==5) drawmode2lineh(physline&511);
                                                else          drawmode2lineh((physline&511)<<1);
                                        }
                                        else       drawmode2line();
                                        break;
                                        case 4: case 5: case 8:
                                        if (hires&1)
                                        {
                                                if (hires==5) drawmodelowlineh(physline&511);
                                                else          drawmodelowlineh((physline&511)<<1);
                                        }
                                        else       drawmode4line();
                                        break;
                                        case 7:
                                        sc6=sc*6;
                                        if (hires&1 && hires!=5) drawteletextline((physline&511)<<1);
                                        else                     drawteletextline(physline&511);
                                        break;
                                }
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
                        if (hires&1 && hires!=5)  memset(buffer->line[physline<<1],0,800);
                        else          memset(buffer->line[physline],0,(hires==5)?800:400);
                        if (hires==3) memset(buffer->line[(physline<<1)+1],0,800);
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
        if (vc==crtc[4]+1)
        {
                if (crtc[7]>=crtc[4])
                {
                        vc=sc=0;
                        initframe();
                        delaylcount=crtc[5];
                        cursorcount++;
                        switch (cursorblink)
                        {
                                case 0: cursoron=1; break;
                                case 1: cursoron=0; break;
                                case 2: cursorcount&=15; if (!cursorcount) cursoron^=1; break;
                                case 3: cursorcount&=31; if (!cursorcount) cursoron^=1; break;
                        }
                        redocursor();
                }
                else
                {
                        curline=0;
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

int deskdepth;
int depth16=16;

void updategfxmode()
{
        int x,y;
        int depth;
        destroy_bitmap(vbuffer);
        destroy_bitmap(buf16);
        destroy_bitmap(buf162);
        set_color_depth(16);
        if (fullscreen)
        {
                depth16=16;
//                if (hires!=2 && hires!=5) set_color_depth(8);
                set_color_depth(16);
                if (hires)
                {
                        if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0);
                        }
                }
                else
                {
                        if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0);
                        }
                }
        }
        else
        {
                if (hires) {x=800; y=600;}
                else       {x=400; y=300;}
                if (deskdepth==16)
                {
                        set_color_depth(16);
                        if (set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                        }
                }
                else if (deskdepth)
                {
                        set_color_depth(deskdepth);
                        depth16=deskdepth;
                        set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                }
                else
                   set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                if (deskdepth==15) depth16=15;
        }
        depth=get_color_depth();
        set_color_depth(depth16);
        Init_2xSaI(depth16);
        buf16=create_bitmap(400,284);
        buf162=create_bitmap(800,568);
        clear(buf16);
        clear(buf162);
          rest(500);
        set_color_depth(depth);
        if (mono) set_palette(monopal);
        else      set_palette(beebpal);
        clear(buffer);
        clear(buffer2);
        clear_to_color(screen,makecol(0,0,0));
        drawfull=3;
        vbuffer=create_system_bitmap(800,300);
        clear(vbuffer);
}

void initvideo()
{
        int c;
        int x,y,depth;
        for (c=8;c<256;c++)
            beebpal[c]=beebpal[c&7];
        for (c=8;c<256;c++)
            monopal[c]=monopal[c&7];
        genpal();
        buffer=create_bitmap(1024,1024);
        buffer2=create_bitmap(808,608);
        set_color_depth(8);
        clear(buffer);
        clear(buffer2);
//      set_color_depth(16);
        depth=deskdepth=desktop_color_depth();
        if (fullscreen)
        {
                set_color_depth(16);
                if (hires)
                {
                        if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,800,600,0,0);
                        }
                }
                else
                {
                        if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_FULLSCREEN,400,300,0,0);
                        }
                }
                depth=get_color_depth();
        }
        else
        {
                if (hires) {x=800; y=600;}
                else       {x=400; y=300;}
                if (depth==16)
                {
                        set_color_depth(16);
                        if (set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0))
                        {
                                set_color_depth(15);
                                depth16=15;
                                set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                        }
                }
                else if (depth)
                {
                        set_color_depth(depth);
                        depth16=depth;
                        set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                }
                else
                {
                        set_gfx_mode(GFX_AUTODETECT_WINDOWED,x,y,0,0);
                }
                if (depth==15) depth16=15;
                depth=get_color_depth();
        }
        set_color_depth(depth16);
        Init_2xSaI(depth16);
        buf16=create_bitmap(400,284);
        buf162=create_bitmap(800,568);
        clear(buf16);
        clear(buf162);
          rest(500);
        set_color_depth(depth);
        clear_to_color(screen,makecol(0,0,0));
        vbuffer=create_system_bitmap(800,300);
        clear(vbuffer);
        for (c=0;c<128;c++)
            multable[c]=c*60;
        maketable2();
        updatepalette();
        LOCK_VARIABLE(fps);
        LOCK_FUNCTION(secint);
        install_int_ex(secint,MSEC_TO_TIMER(1000));
        vidbank=0;
        drawfull=3;
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
        else
        {
                if (save_bmp(fn,buffer,beebpal))
                {
                        printf("Save failed : %s\n",allegro_error);
                }
        }
}

void savebuffers()
{
        save_bitmap("buffer.bmp",buffer,beebpal);
        save_bitmap("buffer2.bmp",buffer2,beebpal);
        save_bitmap("buf16.bmp",buf16,beebpal);
        save_bitmap("buf162.bmp",buf162,beebpal);
}

void savecrtcstate(FILE *f)
{
        fwrite(crtc,18,1,f);
        putc(crtcreg,f);
        putc(vc,f);
        putc(sc,f);
        putc(curline&0xFF,f);
        putc(curline>>8,f);
        putc(physline&0xFF,f);
        putc(physline>>8,f);
        putc(delaylcount,f);
        putc(interlaceline,f);
}

void loadcrtcstate(FILE *f)
{
        fread(crtc,18,1,f);
        crtcreg=getc(f);
        vc=getc(f);
        sc=getc(f);
        sc6=sc*6;
        curline=getc(f);
        curline|=(getc(f)<<8);
        physline=getc(f);
        physline|=(getc(f)<<8);
        delaylcount=getc(f);
        interlaceline=getc(f);
}

void saveulastate(FILE *f)
{
        int c;
        for (c=0;c<16;c++)
            putc(clut2[c],f);
        putc(ulactrl,f);
}

void loadulastate(FILE *f)
{
        int c;
        for (c=0;c<16;c++)
            clut2[c]=getc(f);
        ulactrl=getc(f);
        writeula(0,ulactrl);
}

void closevideo()
{
        destroy_bitmap(vbuffer);
        destroy_bitmap(buf162);
        destroy_bitmap(buf16);
        destroy_bitmap(buffer2);
        destroy_bitmap(buffer);
}
