/*B-em v2.1 by Tom Walker
  Video emulation
  Incorporates 6845 CRTC, Video ULA and SAA5050*/

#include <stdio.h>
#include <allegro.h>
#include <stdint.h>
#include <alleggl.h>
#include "b-em.h"
#include "bbctext.h"
#include "serial.h"


int opengl=0;
uint16_t vidbank;
uint8_t crtc[32];
uint8_t crtcmask[32]={0xFF,0xFF,0xFF,0xFF,0x7F,0x1F,0x7F,0x7F,0xF3,0x1F,0x7F,0x1F,0x3F,0xFF,0x3F,0xFF,0x3F,0xFF};
int crtci;
int screenlen[4]={0x4000,0x5000,0x2000,0x2800};

int fullborders=1;
int collook[8];
uint32_t col0;
uint8_t m7lookup[8][8][16];

int fskipmax=3,fskipcount=0;
int vsynctime;
uint16_t ma,maback;
int interline;
int vdispen,dispen;
int hvblcount;
int frameodd;
int con,cdraw,coff;
int cursoron;
int frcount;
int charsleft;
int hc,vc,sc,vadj;

void resetcrtc()
{
        hc=vc=sc=vadj=0;
        interline=0;
        vsynctime=0;
        hvblcount=0;
        frameodd=0;
        con=cdraw=0;
        cursoron=0;
        charsleft=0;
        crtc[9]=10;
//      printf("%i %i\n",sizeof(uint16_t),sizeof(uint32_t));
}

void writecrtc(uint16_t addr, uint8_t val)
{
        if (!(addr&1)) crtci=val&31;
        else
        {
                crtc[crtci]=val&crtcmask[crtci];
//                if (crtci!=12 && crtci!=13 && crtci!=14 && crtci!=15) printf("Write CRTC R%i %02X\n",crtci,val);
        }
}

uint8_t readcrtc(uint16_t addr)
{
        if (!(addr&1)) return crtci;
        return crtc[crtci];
}

uint8_t ulactrl;
uint8_t ulapal[16],bakpal[16];
int ulamode;
int crtcmode;

uint32_t pixlookuph[4][256][2];
uint32_t pixlookupl[4][256][4];
uint8_t table4bpp[4][256][16];

void makelookuplow()
{
/*        int c;
        switch (ulamode)
        {
                case 0:
                for (c=0;c<256;c++)
                {
                        pixlookupl[0][c][0]=pixlookupl[0][c][1]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                        pixlookupl[0][c][2]=pixlookupl[0][c][3]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                }
                break;

                case 1:
                for (c=0;c<256;c++)
                {
                        pixlookupl[1][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                        pixlookupl[1][c][1]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                        pixlookupl[1][c][2]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][2]]<<24);
                        pixlookupl[1][c][3]=ulapal[table4bpp[c][3]]|(ulapal[table4bpp[c][3]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                }
                break;

                case 2:
                for (c=0;c<256;c++)
                {
                        pixlookupl[2][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                        pixlookupl[2][c][1]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                        pixlookupl[2][c][2]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][4]]<<8)|(ulapal[table4bpp[c][5]]<<16)|(ulapal[table4bpp[c][5]]<<24);
                        pixlookupl[2][c][3]=ulapal[table4bpp[c][6]]|(ulapal[table4bpp[c][6]]<<8)|(ulapal[table4bpp[c][7]]<<16)|(ulapal[table4bpp[c][7]]<<24);
                }
                break;

                case 3:
                for (c=0;c<256;c++)
                {
                        pixlookupl[3][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                        pixlookupl[3][c][1]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][5]]<<8)|(ulapal[table4bpp[c][6]]<<16)|(ulapal[table4bpp[c][7]]<<24);
                        pixlookupl[3][c][2]=ulapal[table4bpp[c][8]]|(ulapal[table4bpp[c][9]]<<8)|(ulapal[table4bpp[c][10]]<<16)|(ulapal[table4bpp[c][11]]<<24);
                        pixlookupl[3][c][3]=ulapal[table4bpp[c][12]]|(ulapal[table4bpp[c][13]]<<8)|(ulapal[table4bpp[c][14]]<<16)|(ulapal[table4bpp[c][15]]<<24);
                }
                break;
        }*/
}

void makelookuphigh()
{
/*        int c;
        switch (ulamode)
        {
                case 0:
                for (c=0;c<256;c++)
                {
                        pixlookuph[0][c][0]=pixlookuph[0][c][1]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                }
                break;

                case 1:
                for (c=0;c<256;c++)
                {
                        pixlookuph[1][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                        pixlookuph[1][c][1]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                }
                break;

                case 2:
                for (c=0;c<256;c++)
                {
                        pixlookuph[2][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                        pixlookuph[2][c][1]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                }
                break;

                case 3:
                for (c=0;c<256;c++)
                {
                        pixlookuph[3][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                        pixlookuph[3][c][1]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][5]]<<8)|(ulapal[table4bpp[c][6]]<<16)|(ulapal[table4bpp[c][7]]<<24);
                }
                break;
        }*/
}

void makelookup()
{
/*        int c;
        for (c=0;c<256;c++)
        {
                pixlookuph[0][c][0]=pixlookuph[0][c][1]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);

                pixlookuph[1][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                pixlookuph[1][c][1]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);

                pixlookuph[2][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                pixlookuph[2][c][1]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);

                pixlookuph[3][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                pixlookuph[3][c][1]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][5]]<<8)|(ulapal[table4bpp[c][6]]<<16)|(ulapal[table4bpp[c][7]]<<24);

                pixlookupl[0][c][0]=pixlookupl[0][c][1]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                pixlookupl[0][c][2]=pixlookupl[0][c][3]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);

                pixlookupl[1][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][0]]<<16)|(ulapal[table4bpp[c][0]]<<24);
                pixlookupl[1][c][1]=ulapal[table4bpp[c][1]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                pixlookupl[1][c][2]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][2]]<<24);
                pixlookupl[1][c][3]=ulapal[table4bpp[c][3]]|(ulapal[table4bpp[c][3]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);

                pixlookupl[2][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][0]]<<8)|(ulapal[table4bpp[c][1]]<<16)|(ulapal[table4bpp[c][1]]<<24);
                pixlookupl[2][c][1]=ulapal[table4bpp[c][2]]|(ulapal[table4bpp[c][2]]<<8)|(ulapal[table4bpp[c][3]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                pixlookupl[2][c][2]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][4]]<<8)|(ulapal[table4bpp[c][5]]<<16)|(ulapal[table4bpp[c][5]]<<24);
                pixlookupl[2][c][3]=ulapal[table4bpp[c][6]]|(ulapal[table4bpp[c][6]]<<8)|(ulapal[table4bpp[c][7]]<<16)|(ulapal[table4bpp[c][7]]<<24);

                pixlookupl[3][c][0]=ulapal[table4bpp[c][0]]|(ulapal[table4bpp[c][1]]<<8)|(ulapal[table4bpp[c][2]]<<16)|(ulapal[table4bpp[c][3]]<<24);
                pixlookupl[3][c][1]=ulapal[table4bpp[c][4]]|(ulapal[table4bpp[c][5]]<<8)|(ulapal[table4bpp[c][6]]<<16)|(ulapal[table4bpp[c][7]]<<24);
                pixlookupl[3][c][2]=ulapal[table4bpp[c][8]]|(ulapal[table4bpp[c][9]]<<8)|(ulapal[table4bpp[c][10]]<<16)|(ulapal[table4bpp[c][11]]<<24);
                pixlookupl[3][c][3]=ulapal[table4bpp[c][12]]|(ulapal[table4bpp[c][13]]<<8)|(ulapal[table4bpp[c][14]]<<16)|(ulapal[table4bpp[c][15]]<<24);
        }*/
}

void writeula(uint16_t addr, uint8_t val)
{
        int c;
//        rpclog("ULA write %04X %02X %i %i\n",addr,val,hc,vc);
        if (!(addr&1))
        {
//        printf("ULA write %04X %02X\n",addr,val);
                if ((ulactrl^val)&1)
                {
                        if (val&1)
                        {
                                for (c=0;c<16;c++)
                                {
                                        if (bakpal[c]&8) ulapal[c]=collook[bakpal[c]&7];
                                        else             ulapal[c]=collook[(bakpal[c]&7)^7];
                                }
                        }
                        else
                        {
                                for (c=0;c<16;c++)
                                    ulapal[c]=collook[(bakpal[c]&7)^7];
                        }
//                        makelookup();
                }
//                if ((ulactrl^val)&1) makelookup();
                ulactrl=val;
                ulamode=(ulactrl>>2)&3;
/*                if (crtcmode==1 && (val&0x12))       makelookuplow();
                else if (crtcmode!=1 && !(val&0x12)) makelookuphigh();*/
                if (val&2)         crtcmode=0;
                else if (val&0x10) crtcmode=1;
                else               crtcmode=2;
//                printf("ULAmode %i\n",ulamode);
        }
        else
        {
//        rpclog("ULA write %04X %02X\n",addr,val);
                c=bakpal[val>>4];
                bakpal[val>>4]=val&15;
                ulapal[val>>4]=collook[(val&7)^7];
                if (val&8) ulapal[val>>4]=collook[val&7];
//                ulapal[val>>4]=collook[(val>>4)&7];
//                makelookup();
/*                if ((val&15)!=c)
                {
                        makelookup();
                        if (crtcmode==1) makelookuphigh();
                        else             makelookuplow();
                }*/
        }
}


BITMAP *b,*b16,*b16x,*tb;
#ifdef WIN32
BITMAP *vb;
#endif

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

int inverttbl[256];
int dcol;
PALETTE pal;
BITMAP *tvb;

void initvideo()
{
        int c,d;
        int temp,temp2,left;

        dcol=desktop_color_depth();
        set_color_depth(dcol);
        if (opengl) openglinit();
#ifdef WIN32
        else        set_gfx_mode(GFX_AUTODETECT_WINDOWED,2048,2048,0,0);
#else
        else        set_gfx_mode(GFX_AUTODETECT_WINDOWED,640,480,0,0);
#endif
        #ifdef WIN32
        if (!opengl) vb=create_video_bitmap(832,614);
        #endif
        b16x=create_bitmap(832,614);
        b16=create_bitmap(832,614);
        set_color_depth(8);
        for (c=1;c<16;c++)
        {
                for (d=0;d<8;d++)
                {
                        beebpal[(c<<4)+d].r=(beebpal[d].r*(15-c))/15;
                        beebpal[(c<<4)+d].g=(beebpal[d].g*(15-c))/15;
                        beebpal[(c<<4)+d].b=(beebpal[d].b*(15-c))/15;
                }
        }
        generate_332_palette(pal);
        set_palette(pal);
        collook[0]=makecol(0,0,0);
        col0=collook[0]|(collook[0]<<8)|(collook[0]<<16)|(collook[0]<<24);
        collook[1]=makecol(255,0,0);
        collook[2]=makecol(0,255,0);
        collook[3]=makecol(255,255,0);
        collook[4]=makecol(0,0,255);
        collook[5]=makecol(255,0,255);
        collook[6]=makecol(0,255,255);
        collook[7]=makecol(255,255,255);
        for (c=0;c<16;c++)
        {
                for (d=0;d<64;d++)
                {
                        m7lookup[d&7][(d>>3)&7][c]=makecol(     (((d&1)*c)*255)/15 + ((((d&8)>>3)*(15-c))*255)/15,
                                                            ((((d&2)>>1)*c)*255)/15 + ((((d&16)>>4)*(15-c))*255)/15,
                                                            ((((d&4)>>2)*c)*255)/15 + ((((d&32)>>5)*(15-c))*255)/15);
                }
        }
        for (temp=0;temp<256;temp++)
        {
                temp2=temp;
                for (c=0;c<16;c++)
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
                     table4bpp[3][temp][c]=left;
                     temp2<<=1; temp2|=1;
                }
                for (c=0;c<16;c++)
                {
                        table4bpp[2][temp][c]=table4bpp[3][temp][c>>1];
                        table4bpp[1][temp][c]=table4bpp[3][temp][c>>2];
                        table4bpp[0][temp][c]=table4bpp[3][temp][c>>3];
                }
        }
//        set_palette(beebpal);
        b=create_bitmap(1280,800);
        clear_to_color(b,collook[0]);
        for (c=0;c<256;c++) inverttbl[c]=makecol(255-pal[c].r,255-pal[c].g,255-pal[c].b);
}

uint8_t mode7chars[96*160],mode7charsi[96*160],mode7graph[96*160],mode7graphi[96*160],mode7sepgraph[96*160],mode7sepgraphi[96*160],mode7tempi[96*120],mode7tempi2[96*120];

void makemode7chars()
{
        int c,d,y;
        int offs1=0,offs2=0;
        float x;
        int x2;
        int stat;
        uint8_t *p=teletext_characters,*p2=mode7tempi;
        for (c=0;c<96*60;c++) teletext_characters[c]*=15;
        for (c=0;c<96*60;c++) teletext_graphics[c]*=15;
        for (c=0;c<96*60;c++) teletext_separated_graphics[c]*=15;
        for (c=0;c<(96*120);c++) mode7tempi2[c]=teletext_characters[c>>1];
        for (c=0;c<960;c++)
        {
                x=0;
                x2=0;
                for (d=0;d<16;d++)
                {
                        mode7graph[offs2+d]=(int)(((float)teletext_graphics[offs1+x2]*(1.0-x))+((float)teletext_graphics[offs1+x2+1]*x));
                        mode7sepgraph[offs2+d]=(int)(((float)teletext_separated_graphics[offs1+x2]*(1.0-x))+((float)teletext_separated_graphics[offs1+x2+1]*x));
                        if (!d)
                        {
                                mode7graph[offs2+d]=mode7graphi[offs2+d]=teletext_graphics[offs1];
                                mode7sepgraph[offs2+d]=mode7sepgraphi[offs2+d]=teletext_separated_graphics[offs1];
                        }
                        else if (d==15)
                        {
                                mode7graph[offs2+d]=mode7graphi[offs2+d]=teletext_graphics[offs1+5];
                                mode7sepgraph[offs2+d]=mode7sepgraphi[offs2+d]=teletext_separated_graphics[offs1+5];
                        }
                        else
                        {
                                mode7graph[offs2+d]=mode7graphi[offs2+d]=teletext_graphics[offs1+x2];
                                mode7sepgraph[offs2+d]=mode7sepgraphi[offs2+d]=teletext_separated_graphics[offs1+x2];
                        }
                        x+=(5.0/15.0);
                        if (x>=1.0)
                        {
                                x2++;
                                x-=1.0;
                        }
                        mode7charsi[offs2+d]=0;
                }

                for (d=0;d<16;d++)
                {
/*                        mode7chars[offs2+d]=(15-mode7chars[offs2+d])<<4;
                        if (mode7chars[offs2+d]>=240) mode7chars[offs2+d]=255;
                        mode7graph[offs2+d]=(15-mode7graph[offs2+d])<<4;
                        if (mode7graph[offs2+d]>=240) mode7graph[offs2+d]=255;
                        mode7sepgraph[offs2+d]=(15-mode7sepgraph[offs2+d])<<4;
                        if (mode7sepgraph[offs2+d]>=240) mode7sepgraph[offs2+d]=255;*/
                }

                offs1+=6;
                offs2+=16;
        }
        for (c=0;c<96;c++)
        {
                for (y=0;y<10;y++)
                {
                        for (d=0;d<6;d++)
                        {
                                stat=0;
                                if (y<9 && p[(y*6)+d] && p[(y*6)+d+6]) stat=3; /*Above + below - set both*/
                                if (y<9 && d>0 && p[(y*6)+d] && p[(y*6)+d+5] && !p[(y*6)+d-1]) stat|=1; /*Above + left - set left*/
                                if (y<9 && d>0 && p[(y*6)+d+6] && p[(y*6)+d-1] && !p[(y*6)+d+5]) stat|=1; /*Below + left - set left*/
                                if (y<9 && d<5 && p[(y*6)+d] && p[(y*6)+d+7] && !p[(y*6)+d+1]) stat|=2; /*Above + right - set right*/
                                if (y<9 && d<5 && p[(y*6)+d+6] && p[(y*6)+d+1] && !p[(y*6)+d+7]) stat|=2; /*Below + right - set right*/
//                                if (y<9 && p[(y*6)+d] && p[(y*6)+d+6]) stat=3; /*Above + below - set both*/
//                                if (y<9 && d>0 && p[(y*6)+d] && p[(y*6)+d+5]) stat|=1; /*Above + left - set left*/
//                                if (y<9 && d>0 && p[(y*6)+d+6] && p[(y*6)+d-1]) stat|=1; /*Below + left - set left*/
//                                if (y<9 && d<5 && p[(y*6)+d] && p[(y*6)+d+7]) stat|=2; /*Above + right - set right*/
//                                if (y<9 && d<5 && p[(y*6)+d+6] && p[(y*6)+d+1]) stat|=2; /*Below + right - set right*/
                                p2[0]=(stat&1)?15:0;
                                p2[1]=(stat&2)?15:0;
                                p2+=2;
                        }
                }
                p+=60;
        }
        offs1=offs2=0;
        for (c=0;c<960;c++)
        {
                x=0;
                x2=0;
                for (d=0;d<16;d++)
                {
                        mode7chars[offs2+d]=(int)(((float)mode7tempi2[offs1+x2]*(1.0-x))+((float)mode7tempi2[offs1+x2+1]*x));
                        mode7charsi[offs2+d]=(int)(((float)mode7tempi[offs1+x2]*(1.0-x))+((float)mode7tempi[offs1+x2+1]*x));
                        x+=(11.0/15.0);
                        if (x>=1.0)
                        {
                                x2++;
                                x-=1.0;
                        }
                        if (c>=320 && c<640)
                        {
                                mode7graph[offs2+d]=mode7sepgraph[offs2+d]=mode7chars[offs2+d];
                                mode7graphi[offs2+d]=mode7sepgraphi[offs2+d]=mode7charsi[offs2+d];
                        }
                }
                offs1+=12;
                offs2+=16;
        }
}


int vidclocks=0;
int oddclock=0;
int vidbytes=0;

int scrx,scry;

int mode7col=7,mode7bg=0;
uint8_t m7buf[2];
uint8_t *mode7p[2]={mode7chars,mode7charsi};
int mode7sep=0;
int mode7dbl,mode7nextdbl,mode7wasdbl;
int mode7gfx;
int mode7flash,m7flashon=0,m7flashtime=0;
uint8_t heldchar,holdchar;
char *heldp[2];

int interlace=0,interlline=0,oldr8;

int firstx,firsty,lastx,lasty;
static inline void rendermode7(uint8_t dat)
{
        int t,c;
        int off;
        int mcolx=mode7col;
        int holdoff=0,holdclear=0;
        char *mode7px[2];
        int mode7flashx=mode7flash,mode7dblx=mode7dbl;
        uint8_t *on;

        t=m7buf[0];
        m7buf[0]=m7buf[1];
        m7buf[1]=dat;
        dat=t;
        mode7px[0]=mode7p[0];
        mode7px[1]=mode7p[1];

        if (!mode7dbl && mode7nextdbl) on=m7lookup[mode7bg&7][mode7bg&7];
        if (dat==255)
        {
                for (c=0;c<16;c++)
                {
                        b->line[scry][scrx+c+16]=collook[0];
                }
                if (linedbl)
                {
                        for (c=0;c<16;c++)
                        {
                                b->line[scry+1][scrx+c+16]=collook[0];
                        }
                }
                return;
        }

        if (dat<0x20)
        {
                switch (dat)
                {
                        case 1: case 2: case 3: case 4: case 5: case 6: case 7:
                        mode7gfx=0;
                        mode7col=dat;
                        mode7p[0]=mode7chars;
                        mode7p[1]=mode7charsi;
                        holdclear=1;
//                        heldchar=32;
                        break;
                        case 8: mode7flash=1; break;
                        case 9: mode7flash=0; break;
                        case 12: case 13:
                        mode7dbl=dat&1;
                        if (mode7dbl) mode7wasdbl=1;
                        break;
                        case 17: case 18: case 19: case 20: case 21: case 22: case 23:
                        mode7gfx=1;
                        mode7col=dat&7;
                        if (mode7sep)
                        {
                                mode7p[0]=mode7sepgraph;
                                mode7p[1]=mode7sepgraphi;
                        }
                        else
                        {
                                mode7p[0]=mode7graph;
                                mode7p[1]=mode7graphi;
                        }
                        break;
                        case 24: mode7col=mcolx=mode7bg; break;
                        case 25: if (mode7gfx) { mode7p[0]=mode7graph;    mode7p[1]=mode7graphi;    } mode7sep=0; break;
                        case 26: if (mode7gfx) { mode7p[0]=mode7sepgraph; mode7p[1]=mode7sepgraphi; } mode7sep=1; break;
                        case 28: mode7bg=0; break;
                        case 29: mode7bg=mode7col; break;
                        case 30: holdchar=1; break;
                        case 31: /*holdchar=0; heldchar=32; */ holdoff=1; break;
                }
                if (holdchar)// && mode7p[0]!=mode7chars)
                {
                        dat=heldchar;
                        if (dat>=0x40 && dat<0x60) dat=32;
                        mode7px[0]=heldp[0];
                        mode7px[1]=heldp[1];
                }
                else
                   dat=0x20;
                if (mode7dblx!=mode7dbl) dat=32; /*Double height doesn't respect held characters*/
        }
        else if (mode7p[0]!=mode7chars)
        {
                heldchar=dat;
                heldp[0]=mode7px[0];
                heldp[1]=mode7px[1];
        }

        if (mode7dblx && !mode7nextdbl) t=((dat-0x20)*160)+((sc>>1)*16);
        else if (mode7dblx)             t=((dat-0x20)*160)+((sc>>1)*16)+(5*16);
        else                           t=((dat-0x20)*160)+(sc*16);

        off=m7lookup[0][mode7bg&7][0];
        if (!mode7dbl && mode7nextdbl) on=m7lookup[mode7bg&7][mode7bg&7];
        else                           on=m7lookup[mcolx&7][mode7bg&7];

        for (c=0;c<16;c++)
        {
                if (mode7flashx && !m7flashon)    b->line[scry][scrx+c+16]=off;
                else if (mode7dblx/* || !(interlace || linedbl)*/) b->line[scry][scrx+c+16]=on[mode7px[sc&1][t]&15];
                else                             b->line[scry][scrx+c+16]=on[mode7px[interlace&interlline][t]&15];
                t++;
        }
//        if (dat!=0x20) rpclog("%i %i %08X %08X %08X %i\n",interlace,interlline,mode7p[0],mode7chars,mode7charsi,t);
        if (linedbl)
        {
//                rpclog("linedbl!\n");
                t-=16;
                for (c=0;c<16;c++)
                {
                        if (mode7flashx && !m7flashon)   b->line[scry+1][scrx+c+16]=off;
                        else if (mode7dblx)             b->line[scry+1][scrx+c+16]=on[mode7px[sc&1][t]&15];
                        else                            b->line[scry+1][scrx+c+16]=on[mode7px[1][t]&15];
                        t++;
                }
        }
        if ((scrx+16)<firstx) firstx=scrx+16;
        if ((scrx+32)>lastx) lastx=scrx+32;
        if (holdoff)
        {
                holdchar=0;
                heldchar=32;
        }
        if (holdclear) heldchar=32;
}

uint8_t cursorlook[7]={0,0,0,0x80,0x40,0x20,0x20};
int cdrawlook[4]={3,2,1,0};

int cmask[4]={0,0,16,32};

int lasthc0=0,lasthc;
int olddispen;
int oldlen;
int ccount=0;
int mcount=8;

void latchpen()
{
        crtc[0x10]=(ma>>8)&0x3F;
        crtc[0x11]=ma&0xFF;
}


int firstdispen=0;
void pollvideo(int clocks)
{
        int c,oldvc;
        uint16_t addr;
        uint8_t dat;
        while (clocks--)
        {
                scrx+=8;
                vidclocks++;
                oddclock=!oddclock;
                if (!(ulactrl&0x10) && !oddclock) continue;

/*                if (!hc && crtc[9])
                {
                        printf("VC %02i SC %02i Y %03i %i\n",vc,sc,scry,vidclocks-oldlen);
                        oldlen=vidclocks;
                }*/

                if (hc==crtc[1])
                {
                        if (ulactrl&2 && dispen) charsleft=3;
                        else charsleft=0;
                        dispen=0;
                }
                if (hc==crtc[2])
                {
                        if (ulactrl&0x10) scrx=128-((crtc[3]&15)*4);
                        else              scrx=128-((crtc[3]&15)*8);
                        /*Don't need this as we blit from 240 onwards*/
//                        if (interlace) memset(b->line[(scry<<1)+interlline],0,scrx);
//                        else           memset(b->line[scry],0,scrx);
//                        scrx=0;
                        scry++;
                        if (scry>=384)
                        {
                                scry=0;
                                doblit();
                        }
                }
//                if (vc==7 && !sc && !hc && crtc[0]) printf("MA %04X R14 %02X R15 %02X\n",ma,crtc[14],crtc[15]);
                if (interlace) scry=(scry<<1)+interlline;
                if (linedbl)   scry<<=1;
                if (dispen)
                {
//                if (!ccount)
//                {
//                        if (!hc && crtc[9]) printf("%i start from %04X\n",scry,ma);
                        if (!((ma^(crtc[15]|(crtc[14]<<8)))&0x3FFF) && con) cdraw=cdrawlook[crtc[8]>>6];
//                        if (vc==7 && crtc[0]) printf("%04X %i %i\n",ma,con,cdraw);
                        if (ma&0x2000) dat=ram[0x7C00|(ma&0x3FF)|vidbank];
                        else
                        {
                                if ((crtc[8]&3)==3) addr=(ma<<3)|((sc&3)<<1)|interlline;
                                else           addr=(ma<<3)|(sc&7);
                                if (addr&0x8000) addr-=screenlen[scrsize];
                                dat=ram[(addr&0x7FFF)|vidbank];
                        }
/*                        if (firstdispen)
                        {
                                rpclog("DISPEN %i %i\n",scrx,scry);
                        }*/
                        if (scrx<1280)
                        {
                                if ((crtc[8]&0x30)==0x30 || ((sc&8) && !(ulactrl&2)))
                                {
                                        for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                        {
                                                ((uint32_t *)b->line[scry])[(scrx+c)>>2]=col0;
                                        }
                                        if (linedbl)
                                        {
                                                for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                {
                                                        ((uint32_t *)b->line[scry+1])[(scrx+c)>>2]=col0;
                                                }
                                        }
                                }
                                else switch (crtcmode)
                                {
                                        case 0:
                                        rendermode7(dat&0x7F);
                                        break;
                                        case 1:
                                        if (scrx<firstx) firstx=scrx;
                                        if ((scrx+8)>lastx) lastx=scrx+8;
                                        for (c=0;c<8;c++)
                                        {
                                                b->line[scry][scrx+c]=ulapal[table4bpp[ulamode][dat][c]];
                                        }
                                        if (linedbl)
                                        {
                                                for (c=0;c<8;c++)
                                                {
                                                        b->line[scry+1][scrx+c]=ulapal[table4bpp[ulamode][dat][c]];
                                                }
                                        }
//                                        ((uint32_t *)(&b->line[scry][scrx]))[0]=pixlookuph[ulamode][dat][0];
//                                        ((uint32_t *)(&b->line[scry][scrx]))[1]=pixlookuph[ulamode][dat][1];
                                        break;
                                        case 2:
                                        if (scrx<firstx) firstx=scrx;
                                        if ((scrx+16)>lastx) lastx=scrx+16;
                                        for (c=0;c<16;c++)
                                        {
                                                b->line[scry][scrx+c]=ulapal[table4bpp[ulamode][dat][c]];
                                        }
                                        if (linedbl)
                                        {
                                                for (c=0;c<16;c++)
                                                {
                                                        b->line[scry+1][scrx+c]=ulapal[table4bpp[ulamode][dat][c]];
                                                }
                                        }
/*                                        ((uint32_t *)(&b->line[scry][scrx]))[0]=pixlookupl[ulamode][dat][0];
                                        ((uint32_t *)(&b->line[scry][scrx]))[1]=pixlookupl[ulamode][dat][1];
                                        ((uint32_t *)(&b->line[scry][scrx]))[2]=pixlookupl[ulamode][dat][2];
                                        ((uint32_t *)(&b->line[scry][scrx]))[3]=pixlookupl[ulamode][dat][3];*/
                                        break;
                                }
                                if (cdraw)
                                {
                                        if (cursoron && (ulactrl & cursorlook[cdraw]))
                                        {
//                                                rpclog("Render cursor at %i,%i  %i\n",scrx,scry,hc);
                                                for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                {
                                                        b->line[scry][scrx+c]=inverttbl[b->line[scry][scrx+c]];
                                                }
                                                if (linedbl)
                                                {
                                                        for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                        {
                                                                b->line[scry+1][scrx+c]=inverttbl[b->line[scry+1][scrx+c]];
                                                        }
                                                }
                                        }
                                        cdraw++;
                                        if (cdraw==7) cdraw=0;
                                }
                        }
//                }
                        ma++;
                        vidbytes++;
                }
                else //if (!ccount)
                {
                        if (charsleft)
                        {
                                if (charsleft!=1) rendermode7(255);
                                charsleft--;
//                                if (vc==7) rpclog("Charleft at %i,%i %i %i %02X %02X\n",scrx,scry,cdraw,cursoron,ulactrl,cursorlook[cdraw]);
/*                                if (cdraw)
                                {
                                        if (cursoron && (ulactrl & cursorlook[cdraw]))
                                        {
                                                rpclog("Render cursor at %i,%i  %i\n",scrx,scry,hc);
                                                for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                {
                                                        b->line[scry][scrx+c]=inverttbl[b->line[scry][scrx+c]];
                                                }
                                                if (linedbl)
                                                {
                                                        for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                        {
                                                                b->line[scry+1][scrx+c]=inverttbl[b->line[scry+1][scrx+c]];
                                                        }
                                                }
                                        }
                                        cdraw++;
                                        if (cdraw==7) cdraw=0;
                                }*/

                        }
                        else if (scrx<1280)
                        {
//                                if (!crtcmode) scrx+=16;
//                                if (!vdispen) printf("%i %i\n",scrx,scry);
                                for (c=0;c<((ulactrl&0x10)?8:16);c+=4)
                                {
                                        *((uint32_t *)&b->line[scry][scrx+c])=col0;
                                }
//                                if (vdispen && scrx<304) rpclog("Clear %i %i %i\n",scrx,scry,c);
                                if (linedbl)
                                {
                                        for (c=0;c<((ulactrl&0x10)?8:16);c+=4)
                                        {
                                                *((uint32_t *)&b->line[scry+1][scrx+c])=col0;
                                        }
                                }
//                                if (!crtcmode) scrx-=16;
                                if (!crtcmode)
                                {
                                        for (c=0;c<16;c+=4)
                                        {
                                                *((uint32_t *)&b->line[scry][scrx+c+16])=col0;
                                        }
                                        if (linedbl)
                                        {
                                                for (c=0;c<16;c+=4)
                                                {
                                                        *((uint32_t *)&b->line[scry+1][scrx+c+16])=col0;
                                                }
                                        }
                                }
                        }
                        if (cdraw && scrx<1280)
                        {
                                if (cursoron && (ulactrl & cursorlook[cdraw]))
                                {
//                                        rpclog("Render cursor at %i,%i  %i\n",scrx,scry,hc);
                                        for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                        {
                                                b->line[scry][scrx+c]=inverttbl[b->line[scry][scrx+c]];
                                        }
                                        if (linedbl)
                                        {
                                                for (c=0;c<((ulactrl&0x10)?8:16);c++)
                                                {
                                                        b->line[scry+1][scrx+c]=inverttbl[b->line[scry+1][scrx+c]];
                                                }
                                        }
                                }
                                cdraw++;
                                if (cdraw==7) cdraw=0;
                        }
                }

                if (linedbl)   scry>>=1;
                if (interlace) scry>>=1;

                if (hvblcount)
                {
                        hvblcount--;
                        if (!hvblcount)
                        {
                                vblankintlow();
//                                if (crtc[0]) printf("VBlank low %i %i %i\n",scry,vc,sc);
                        }
                }

                if (interline && hc==(crtc[0]>>1))
                {
                        hc=interline=0;
                        lasthc0=1;
//                        if (crtc[9]) printf("Interlace line\n");
                }
                else if (hc==crtc[0])
                {
                        mode7col=7;
                        mode7bg=0;
                        holdchar=0;
                        heldchar=0x20;
                        mode7p[0]=mode7chars;
                        mode7p[1]=mode7charsi;
                        mode7flash=0;
                        mode7sep=0;
                        mode7gfx=0;
                        heldchar=32;
                        heldp[0]=mode7p[0];
                        heldp[1]=mode7p[1];

                        hc=0;
                        if (sc==(crtc[11]&31) || ((crtc[8]&3)==3 && sc==((crtc[11]&31)>>1))) { con=0; coff=1; }
                        if (vadj)
                        {
                                sc++;
                                sc&=31;
                                ma=maback;
                                vadj--;
                                if (!vadj)
                                {
                                        vdispen=1;
                                        ma=maback=(crtc[13]|(crtc[12]<<8))&0x3FFF;
//                                        if (crtc[9]) printf("1MA %04X\n",ma);
                                        sc=0;
                                }
                        }
                        else if (sc==crtc[9] || ((crtc[8]&3)==3 && sc==(crtc[9]>>1)))
                        {
                                maback=ma;
//                                if (crtc[9]) printf("SC over %i %i %04X %i\n",scry,vc,maback,vdispen);
                                sc=0;
                                con=0;
                                coff=0;
                                if (mode7nextdbl) mode7nextdbl=0;
                                else              mode7nextdbl=mode7wasdbl;
                                oldvc=vc;
                                vc++;
                                vc&=127;
                                if (vc==crtc[6]) vdispen=0;
                                if (oldvc==crtc[4])
                                {
                                        vc=0;
                                        vadj=crtc[5];
                                        if (!vadj) vdispen=1;
                                        if (!vadj) ma=maback=(crtc[13]|(crtc[12]<<8))&0x3FFF;
//                                        if (crtc[9] && !vadj) printf("2MA %04X\n",ma);

                                        frcount++;
                                        if (!(crtc[10]&0x60)) cursoron=1;
                                        else                 cursoron=frcount&cmask[(crtc[10]&0x60)>>5];
//                                        if (crtc[9]) printf("Frame over! %i %i %04X %04X\n",scry,vdispen,ma,maback);
                                }
                                if (vc==crtc[7])
                                {
                                        if (!(crtc[8]&1) && oldr8) clear_to_color(b,col0);
                                        frameodd^=1;
                                        if (frameodd) interline=(crtc[8]&1);
                                        interlline=frameodd && (crtc[8]&1);
                                        oldr8=crtc[8]&1;
                                        if (vidclocks>2 && !ccount)
                                        {
                                                doblit();
                                        }
                                        ccount++;
                                        if (ccount==10 || ((!motor || !fasttape) && !key[KEY_PGUP])) ccount=0;
                                        scry=0;
                                        vblankint();
//                                        if (crtc[0]) printf("VBlank high %i %i %i\n",scry,vc,sc);
                                        vsynctime=(crtc[3]>>4)+1;
                                        if (!(crtc[3]>>4)) vsynctime=17;
//                                        vsynctime=16;
                                        m7flashtime++;
                                        if ((m7flashon && m7flashtime==32) || (!m7flashon && m7flashtime==16))
                                        {
                                                m7flashon=!m7flashon;
                                                m7flashtime=0;
                                        }
//                                        if (vidclocks>2) printf("%i clocks in frame %i bytes MA %04X %i %i %i %i %02X\n",vidclocks,vidbytes,ma,crtc[10]&31,crtc[11]&31,cursoron,frcount,crtc[9]);
                                        vidclocks=vidbytes=0;
                                }
                        }
                        else
                        {
                                sc++;
                                sc&=31;
                                ma=maback;
                        }

                        mode7dbl=mode7wasdbl=0;
                        if ((sc==(crtc[10]&31) || ((crtc[8]&3)==3 && sc==((crtc[10]&31)>>1))) && !coff) con=1;

                        if (vsynctime)
                        {
                                vsynctime--;
                                if (!vsynctime)
                                {
                                        hvblcount=1;
//                                        hvblcount=(crtc[3]&15)>>1;
//                                printf("VBL int %i clocks\n",clocks);
//                                        vblankintlow();
                                        if (frameodd) interline=(crtc[8]&1);
                                }
                        }

                        dispen=vdispen;
                        if (dispen || vadj)
                        {
                                if (scry<firsty) firsty=scry;
                                if ((scry+1)>lasty) lasty=scry;
                        }
//                        if (crtc[0]) rpclog("Dispen %i %i %i\n",dispen,scrx,scry);
                        firstdispen=1;
                        lasthc0=1;

                        mcount--;
                        if (!mcount)
                        {
                                mcount=24;
                                if (curtube==3) domouse();
                        }
                        if (adcconvert)
                        {
                                adcconvert--;
                                if (!adcconvert) polladc();
                        }
                }
                else
                {
                        hc++;
                        hc&=255;
                }
                lasthc=hc;
        }
}

void savevideoulastate(FILE *f)
{
        int c;
        putc(ulactrl,f);
        for (c=0;c<16;c++) putc(bakpal[c],f);
}

void loadvideoulastate(FILE *f)
{
        int c;
        writeula(0,getc(f));
        for (c=0;c<16;c++) writeula(1,getc(f)|(c<<4));
}

void savecrtcstate(FILE *f)
{
        int c;
        for (c=0;c<18;c++) putc(crtc[c],f);
        putc(vc,f);
        putc(sc,f);
        putc(hc,f);
        putc(ma,f); putc(ma>>8,f);
        putc(maback,f); putc(maback>>8,f);
        putc(scrx,f); putc(scrx>>8,f);
        putc(scry,f); putc(scry>>8,f);
        putc(oddclock,f);
        putc(vidclocks,f); putc(vidclocks>>8,f); putc(vidclocks>>16,f); putc(vidclocks>>24,f);
}

void loadcrtcstate(FILE *f)
{
        int c;
        for (c=0;c<18;c++) crtc[c]=getc(f);
        vc=getc(f);
        sc=getc(f);
        hc=getc(f);
        ma=getc(f); ma|=getc(f)<<8;
        maback=getc(f); maback|=getc(f)<<8;
        scrx=getc(f); scrx|=getc(f)<<8;
        scry=getc(f); scry|=getc(f)<<8;
        vidclocks=getc(f); vidclocks=getc(f)<<8; vidclocks=getc(f)<<16; vidclocks=getc(f)<<24;
}
