/*B-em v2.2 by Tom Walker
  Video emulation
  Incorporates 6845 CRTC, Video ULA and SAA5050*/

#include <stdio.h>
#include <allegro.h>
#include <stdint.h>
#include "b-em.h"

#include "bbctext.h"
#include "mem.h"
#include "model.h"
#include "serial.h"
#include "tape.h"
#include "via.h"
#include "sysvia.h"
#include "video.h"
#include "video_render.h"


int fullscreen = 0;

int scrx, scry;
int interlline = 0;

int colblack;
int colwhite;

/*6845 CRTC*/
uint8_t crtc[32];
static uint8_t crtc_mask[32]={0xFF,0xFF,0xFF,0xFF,0x7F,0x1F,0x7F,0x7F,0xF3,0x1F,0x7F,0x1F,0x3F,0xFF,0x3F,0xFF,0x3F,0xFF};
int crtc_i;

int hc,vc,sc,vadj;
uint16_t ma,maback;

void crtc_reset()
{
        hc = vc = sc = vadj = 0;
        crtc[9]   = 10;
}

void crtc_write(uint16_t addr, uint8_t val)
{
//        log_debug("Write CRTC %04X %02X %04X\n",addr,val,pc);
        if (!(addr & 1)) crtc_i = val & 31;
        else
        {
                crtc[crtc_i] = val & crtc_mask[crtc_i];
//                if (crtci!=12 && crtci!=13 && crtci!=14 && crtci!=15) printf("Write CRTC R%i %02X\n",crtci,val);
        }
}

uint8_t crtc_read(uint16_t addr)
{
        if (!(addr & 1)) return crtc_i;
        return crtc[crtc_i];
}

void crtc_latchpen()
{
        crtc[0x10]=(ma>>8)&0x3F;
        crtc[0x11]=ma&0xFF;
}

void crtc_savestate(FILE *f)
{
        int c;
        for (c=0;c<18;c++) putc(crtc[c],f);
        putc(vc,f);
        putc(sc,f);
        putc(hc,f);
        putc(ma,f); putc(ma>>8,f);
        putc(maback,f); putc(maback>>8,f);
}

void crtc_loadstate(FILE *f)
{
        int c;
        for (c=0;c<18;c++) crtc[c]=getc(f);
        vc=getc(f);
        sc=getc(f);
        hc=getc(f);
        ma=getc(f); ma|=getc(f)<<8;
        maback=getc(f); maback|=getc(f)<<8;
}


/*Video ULA (VIDPROC)*/
uint8_t ula_ctrl;
static int ula_pal[16];             // maps from actual physical colour to bitmap display
uint8_t ula_palbak[16];             // palette RAM in orginal ULA maps actual colour to logical colour
static int ula_mode;
static int crtc_mode;
int nula_collook[16];               // maps palette (logical) colours to 12-bit RGB

static uint8_t table4bpp[4][256][16];

static int nula_pal_write_flag = 0;
static uint8_t nula_pal_first_byte;
uint8_t nula_flash[8];

uint8_t nula_palette_mode;
uint8_t nula_horizontal_offset;
uint8_t nula_left_blank;
uint8_t nula_disable;
uint8_t nula_attribute_mode;
uint8_t nula_attribute_text;

static int nula_left_cut;
static int nula_left_edge;
static int mode7_need_new_lookup;

static inline void putpixel(BITMAP *bmp, int x, int y, int colour) {
    uint32_t *l = (uint32_t *)(bmp->line[y]);
    l[x] = colour;
}

static inline void nula_putpixel(BITMAP *bmp, int x, int y, int colour)
{
    if (crtc_mode && (nula_horizontal_offset || nula_left_blank) && (x < nula_left_cut || x >= nula_left_edge + (crtc[1] * crtc_mode * 8)))
    {
        putpixel(bmp, x, y, colblack);
    }
    else if( x < 1280 )
    {
        putpixel(bmp, x, y, colour);
    }
}

void videoula_write(uint16_t addr, uint8_t val)
{
        int c;
        // log_debug("ULA write %04X %02X %i %i\n",addr,val,hc,vc);
        if (nula_disable)
                addr &= ~2;       // nuke additional NULA addresses

        switch (addr & 3)
        {
        case 0:
        {
                //        printf("ULA write %04X %02X\n",addr,val);
                if ((ula_ctrl ^ val) & 1)
                {
                        if (val & 1)
                        {
                                for (c = 0; c < 16; c++)
                                {
                                        if ((ula_palbak[c] & 8) && nula_flash[c&7]) ula_pal[c] = nula_collook[ula_palbak[c] & 15];
                                        else                 ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                                }
                        }
                        else
                        {
                                for (c = 0; c < 16; c++)
                                        ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                        }
                }
                ula_ctrl = val;
                ula_mode = (ula_ctrl >> 2) & 3;
                if (val & 2)         crtc_mode = 0;    // Teletext
                else if (val & 0x10) crtc_mode = 1;    // High frequency
                else               crtc_mode = 2;      // Low frequency
                //                printf("ULAmode %i\n",ulamode);
        }
        break;

        case 1:
        {
                // log_debug("ULA write %04X %02X\n",addr,val);
                c = ula_palbak[val >> 4];
                ula_palbak[val >> 4] = val & 15;
                ula_pal[val >> 4] = nula_collook[(val & 15) ^ 7];
                if ((val & 8) && (ula_ctrl & 1) && nula_flash[val - 8]) ula_pal[val >> 4] = nula_collook[val & 15];
        }
        break;

        case 2:        // &FE22 = NULA CONTROL REG
        {
                uint8_t code = val >> 4;
                uint8_t param = val & 0xf;

                switch (code)
                {
                case 1:
                        nula_palette_mode = param & 1;
                        break;

                case 2:
                        nula_horizontal_offset = param & 7;
                        break;

                case 3:
                        nula_left_blank = param & 15;
                        break;

                case 4:
                        // Reset NULA
                        nula_palette_mode = 0;
                        nula_horizontal_offset = 0;
                        nula_left_blank = 0;
                        nula_attribute_mode = 0;
                        nula_attribute_text = 0;

                        // Reset palette
                        nula_collook[0] = makecol(0, 0, 0);
                        nula_collook[1] = makecol(255, 0, 0);
                        nula_collook[2] = makecol(0, 255, 0);
                        nula_collook[3] = makecol(255, 255, 0);
                        nula_collook[4] = makecol(0, 0, 255);
                        nula_collook[5] = makecol(255, 0, 255);
                        nula_collook[6] = makecol(0, 255, 255);
                        nula_collook[7] = makecol(255, 255, 255);
                        nula_collook[8] = makecol(0, 0, 0);
                        nula_collook[9] = makecol(255, 0, 0);
                        nula_collook[10] = makecol(0, 255, 0);
                        nula_collook[11] = makecol(255, 255, 0);
                        nula_collook[12] = makecol(0, 0, 255);
                        nula_collook[13] = makecol(255, 0, 255);
                        nula_collook[14] = makecol(0, 255, 255);
                        nula_collook[15] = makecol(255, 255, 255);

                        // Reset flash
                        for (c = 0; c < 8; c++)
                        {
                                nula_flash[c] = 1;
                        }
                        mode7_need_new_lookup = 1;
                        break;

                case 5:
                        nula_disable = TRUE;
                        break;

                case 6:
                        nula_attribute_mode = param & 1;
                        break;

                case 7:
                        nula_attribute_text = param & 1;
                        break;

                case 8:
                        nula_flash[0] = param & 8;
                        nula_flash[1] = param & 4;
                        nula_flash[2] = param & 2;
                        nula_flash[3] = param & 1;
                        break;

                case 9:
                        nula_flash[4] = param & 8;
                        nula_flash[5] = param & 4;
                        nula_flash[6] = param & 2;
                        nula_flash[7] = param & 1;
                        break;

                default:
                        break;
                }

        }
        break;

        case 3:        // &FE23 = NULA PALETTE REG
        {
                if (nula_pal_write_flag)
                {
                        // Commit the write to palette
                        int c = (nula_pal_first_byte >> 4);
                        int r = nula_pal_first_byte & 0x0f;
                        int g = (val & 0xf0) >> 4;
                        int b = val & 0x0f;
                        nula_collook[c] = makecol(r | r << 4, g | g << 4, b | b << 4);
                        // Manual states colours 8-15 are set solid by default
                        if (c & 8) nula_flash[c - 8] = 0;
                        // Reset all colour lookups
                        for (c = 0; c < 16; c++)
                        {
                                ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                                if ((ula_palbak[c] & 8) && (ula_ctrl & 1) && nula_flash[ula_palbak[c] - 8]) ula_pal[c] = nula_collook[ula_palbak[c] & 15];
                        }
                        mode7_need_new_lookup = 1;
                }
                else
                {
                        // Remember the first byte
                        nula_pal_first_byte = val;
                }

                nula_pal_write_flag = !nula_pal_write_flag;
        }
        break;

        }
}

void videoula_savestate(FILE *f)
{
        int c;
        putc(ula_ctrl,f);
        for (c=0;c<16;c++) putc(ula_palbak[c],f);
        for (c = 0; c < 16; c++)
        {
                putc(getr32(nula_collook[c]),f);
                putc(getg32(nula_collook[c]),f);
                putc(getb32(nula_collook[c]),f);
                putc(geta32(nula_collook[c]),f);
        }
        putc(nula_pal_write_flag, f);
        putc(nula_pal_first_byte, f);
        for (c = 0; c < 8; c++) putc(nula_flash[c], f);
        putc(nula_palette_mode, f);
        putc(nula_horizontal_offset, f);
        putc(nula_left_blank, f);
        putc(nula_disable, f);
        putc(nula_attribute_mode, f);
        putc(nula_attribute_text, f);
}

void videoula_loadstate(FILE *f)
{
        int c;
        videoula_write(0,getc(f));
        for (c=0;c<16;c++) videoula_write(1,getc(f)|(c<<4));
        // TODO NULA load state
        for (c = 0; c < 16; c++)
        {
                int r = getc(f);
                int g = getc(f);
                int b = getc(f);
                int a = getc(f);
                nula_collook[c] = makeacol32(r, g, b, a);
        }
        nula_pal_write_flag=getc(f);
        nula_pal_first_byte = getc(f);
        for (c = 0; c < 8; c++) nula_flash[c]=getc(f);
        nula_palette_mode=getc(f);
        nula_horizontal_offset=getc(f);
        nula_left_blank=getc(f);
        nula_disable=getc(f);
        nula_attribute_mode=getc(f);
        nula_attribute_text=getc(f);
}


/*Mode 7 (SAA5050)*/
static uint8_t mode7_chars[96*160], mode7_charsi[96*160], mode7_graph[96*160], mode7_graphi[96*160], mode7_sepgraph[96*160], mode7_sepgraphi[96*160], mode7_tempi[96*120], mode7_tempi2[96*120];
static int mode7_lookup[8][8][16];

static int mode7_col = 7, mode7_bg = 0;
static int mode7_sep = 0;
static int mode7_dbl, mode7_nextdbl, mode7_wasdbl;
static int mode7_gfx;
static int mode7_flash, mode7_flashon = 0, mode7_flashtime = 0;
static uint8_t mode7_buf[2];
static uint8_t *mode7_p[2] = {mode7_chars, mode7_charsi};
static uint8_t mode7_heldchar, mode7_holdchar;
static uint8_t *mode7_heldp[2];

void mode7_makechars()
{
        int c, d, y;
        int offs1 = 0, offs2 = 0;
        float x;
        int x2;
        int stat;
        uint8_t *p = teletext_characters, *p2 = mode7_tempi;
        for (c = 0; c < (96 *  60); c++) teletext_characters[c]         *= 15;
        for (c = 0; c < (96 *  60); c++) teletext_graphics[c]           *= 15;
        for (c = 0; c < (96 *  60); c++) teletext_separated_graphics[c] *= 15;
        for (c = 0; c < (96 * 120); c++) mode7_tempi2[c] = teletext_characters[c >> 1];
        for (c = 0; c < 960; c++)
        {
                x = 0;
                x2 = 0;
                for (d = 0; d < 16; d++)
                {
                        mode7_graph[offs2 + d]  = (int)(((float)teletext_graphics[offs1 + x2] * (1.0 - x))+((float)teletext_graphics[offs1 + x2 + 1] * x));
                        mode7_sepgraph[offs2+d] = (int)(((float)teletext_separated_graphics[offs1 + x2] * (1.0 - x)) + ((float)teletext_separated_graphics[offs1 + x2 + 1] * x));
                        if (!d)
                        {
                                mode7_graph[offs2 + d]    = mode7_graphi[offs2 + d]    = teletext_graphics[offs1];
                                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1];
                        }
                        else if (d==15)
                        {
                                mode7_graph[offs2 + d]    = mode7_graphi[offs2 + d]    = teletext_graphics[offs1 + 5];
                                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + 5];
                        }
                        else
                        {
                                mode7_graph[offs2 + d]    = mode7_graphi[offs2 + d]    = teletext_graphics[offs1 + x2];
                                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + x2];
                        }
                        x += (5.0 / 15.0);
                        if (x >= 1.0)
                        {
                                x2++;
                                x -= 1.0;
                        }
                        mode7_charsi[offs2 + d] = 0;
                }

                offs1 += 6;
                offs2 += 16;
        }
        for (c = 0; c < 96; c++)
        {
                for (y = 0; y < 10; y++)
                {
                        for (d = 0; d < 6; d++)
                        {
                                stat = 0;
                                if (y < 9 &&          p[(y * 6) + d]     && p[(y * 6) + d + 6])                        stat|=3; /*Above + below - set both*/
                                if (y < 9 && d > 0 && p[(y * 6) + d]     && p[(y * 6) + d + 5] && !p[(y * 6) + d - 1]) stat|=1; /*Above + left  - set left*/
                                if (y < 9 && d > 0 && p[(y * 6) + d + 6] && p[(y * 6) + d - 1] && !p[(y * 6) + d + 5]) stat|=1; /*Below + left  - set left*/
                                if (y < 9 && d < 5 && p[(y * 6) + d]     && p[(y * 6) + d + 7] && !p[(y * 6) + d + 1]) stat|=2; /*Above + right - set right*/
                                if (y < 9 && d < 5 && p[(y * 6) + d + 6] && p[(y * 6) + d + 1] && !p[(y * 6) + d + 7]) stat|=2; /*Below + right - set right*/

                                p2[0] = (stat & 1) ? 15 : 0;
                                p2[1] = (stat & 2) ? 15 : 0;
                                p2 += 2;
                        }
                }
                p += 60;
        }
        offs1 = offs2 = 0;
        for (c = 0; c < 960; c++)
        {
                x = 0;
                x2 = 0;
                for (d = 0; d < 16; d++)
                {
                        mode7_chars[offs2 + d]  = (int)(((float)mode7_tempi2[offs1 + x2] * (1.0 - x)) + ((float)mode7_tempi2[offs1 + x2 + 1] * x));
                        mode7_charsi[offs2 + d] = (int)(((float)mode7_tempi[offs1 + x2]  * (1.0 - x)) + ((float)mode7_tempi[offs1 + x2 + 1]  * x));
                        x += (11.0 / 15.0);
                        if (x >= 1.0)
                        {
                                x2++;
                                x -= 1.0;
                        }
                        if (c >= 320 && c < 640)
                        {
                                mode7_graph[offs2 + d]  = mode7_sepgraph[offs2 + d]  = mode7_chars[offs2 + d];
                                mode7_graphi[offs2 + d] = mode7_sepgraphi[offs2 + d] = mode7_charsi[offs2 + d];
                        }
                }
                offs1 += 12;
                offs2 += 16;
        }
}

static void mode7_gen_nula_lookup(void) {
    int fg_ix, fg_col, fg_red, fg_grn, fg_blu;
    int bg_ix, bg_col, bg_red, bg_grn, bg_blu;
    int weight, lu_red, lu_grn, lu_blu;

    for (fg_ix = 0; fg_ix < 8; fg_ix++) {
        fg_col = nula_collook[fg_ix];
        fg_red = getr(fg_col);
        fg_grn = getg(fg_col);
        fg_blu = getb(fg_col);
        for (bg_ix = 0; bg_ix < 8; bg_ix++) {
            bg_col = nula_collook[bg_ix];
            bg_red = getr(bg_col);
            bg_grn = getg(bg_col);
            bg_blu = getb(bg_col);
            for (weight = 0; weight < 16; weight++) {
                lu_red = bg_red + (((fg_red - bg_red) * weight) / 15);
                lu_grn = bg_grn + (((fg_grn - bg_grn) * weight) / 15);
                lu_blu = bg_blu + (((fg_blu - bg_blu) * weight) / 15);
                mode7_lookup[fg_ix][bg_ix][weight] = makecol(lu_red, lu_grn, lu_blu);
            }
        }
    }
    mode7_need_new_lookup = 0;
}

static inline void mode7_render(uint8_t dat)
{
        int t, c;
        int off;
        int mcolx = mode7_col;
        int holdoff = 0, holdclear = 0;
        uint8_t *mode7_px[2];
        int mode7_flashx = mode7_flash, mode7_dblx = mode7_dbl;
        int *on;

        if (mode7_need_new_lookup)
            mode7_gen_nula_lookup();

        t = mode7_buf[0];
        mode7_buf[0] = mode7_buf[1];
        mode7_buf[1] = dat;
        dat = t;
        mode7_px[0] = mode7_p[0];
        mode7_px[1] = mode7_p[1];

        if (!mode7_dbl && mode7_nextdbl) on = mode7_lookup[mode7_bg & 7][mode7_bg & 7];
        if (dat == 255)
        {
                for (c = 0; c < 16; c++)
                {
                        // b->line[scry][scrx + c + 16] = nula_collook[0];
                        putpixel(b, scrx + c + 16, scry, colblack);
                }
                if (vid_linedbl)
                {
                        for (c = 0; c < 16; c++)
                        {
                                //  b->line[scry + 1][scrx + c + 16] = nula_collook[0];
                                putpixel(b, scrx + c + 16, scry + 1, colblack);
                        }
                }
                return;
        }

        if (dat < 0x20)
        {
                switch (dat)
                {
                case 1: case 2: case 3: case 4: case 5: case 6: case 7:
                        mode7_gfx = 0;
                        mode7_col = dat;
                        mode7_p[0] = mode7_chars;
                        mode7_p[1] = mode7_charsi;
                        holdclear = 1;
                        break;
                case 8: mode7_flash = 1; break;
                case 9: mode7_flash = 0; break;
                case 12: case 13:
                        mode7_dbl = dat & 1;
                        if (mode7_dbl) mode7_wasdbl = 1;
                        break;
                case 17: case 18: case 19: case 20: case 21: case 22: case 23:
                        mode7_gfx = 1;
                        mode7_col = dat & 7;
                        if (mode7_sep)
                        {
                                mode7_p[0] = mode7_sepgraph;
                                mode7_p[1] = mode7_sepgraphi;
                        }
                        else
                        {
                                mode7_p[0] = mode7_graph;
                                mode7_p[1] = mode7_graphi;
                        }
                        break;
                case 24: mode7_col = mcolx = mode7_bg; break;
                case 25: if (mode7_gfx) { mode7_p[0] = mode7_graph;    mode7_p[1] = mode7_graphi;    } mode7_sep = 0; break;
                case 26: if (mode7_gfx) { mode7_p[0] = mode7_sepgraph; mode7_p[1] = mode7_sepgraphi; } mode7_sep = 1; break;
                case 28: mode7_bg = 0; break;
                case 29: mode7_bg = mode7_col; break;
                case 30: mode7_holdchar = 1; break;
                case 31: holdoff = 1; break;
                }
                if (mode7_holdchar)
                {
                        dat = mode7_heldchar;
                        if (dat >= 0x40 && dat < 0x60) dat = 32;
                        mode7_px[0] = mode7_heldp[0];
                        mode7_px[1] = mode7_heldp[1];
                }
                else
                        dat = 0x20;
                if (mode7_dblx != mode7_dbl) dat = 32; /*Double height doesn't respect held characters*/
        }
        else if (mode7_p[0] != mode7_chars)
        {
                mode7_heldchar = dat;
                mode7_heldp[0] = mode7_px[0];
                mode7_heldp[1] = mode7_px[1];
        }

        if (mode7_dblx && !mode7_nextdbl) t = ((dat - 0x20) * 160) + ((sc >> 1) * 16);
        else if (mode7_dblx)              t = ((dat - 0x20) * 160) + ((sc >> 1) * 16) + (5 * 16);
        else                              t = ((dat - 0x20) * 160) + (sc * 16);

        off = mode7_lookup[0][mode7_bg & 7][0];
        if (!mode7_dbl && mode7_nextdbl) on = mode7_lookup[mode7_bg & 7][mode7_bg & 7];
        else                             on = mode7_lookup[mcolx & 7][mode7_bg & 7];

        for (c = 0; c < 16; c++)
        {
                if (mode7_flashx && !mode7_flashon) putpixel(b, scrx + c + 16, scry, off);                                 //    b->line[scry][scrx + c + 16] = off;
                else if (mode7_dblx)            putpixel(b, scrx + c + 16, scry, on[mode7_px[sc & 1][t] & 15]);                  //    b->line[scry][scrx + c + 16] = on[mode7_px[sc & 1][t] & 15];
                else                        putpixel(b, scrx + c + 16, scry, on[mode7_px[vid_interlace & interlline][t] & 15]);     //    b->line[scry][scrx + c + 16] = on[mode7_px[vid_interlace & interlline][t] & 15];
                t++;
        }

        if (vid_linedbl)
        {
                t -= 16;
                for (c = 0; c < 16; c++)
                {
                        if (mode7_flashx && !mode7_flashon) putpixel(b, scrx + c + 16, scry + 1, off);                    //    b->line[scry + 1][scrx + c + 16] = off;
                        else if (mode7_dblx)            putpixel(b, scrx + c + 16, scry + 1, on[mode7_px[sc & 1][t] & 15]);     //    b->line[scry + 1][scrx + c + 16] = on[mode7_px[sc & 1][t] & 15];
                        else                        putpixel(b, scrx + c + 16, scry + 1, on[mode7_px[1][t] & 15]);           //    b->line[scry + 1][scrx + c + 16] = on[mode7_px[1][t] & 15];
                        t++;
                }
        }

        if ((scrx + 16) < firstx) firstx = scrx + 16;
        if ((scrx + 32) > lastx)  lastx  = scrx + 32;

        if (holdoff)
        {
                mode7_holdchar=0;
                mode7_heldchar=32;
        }
        if (holdclear) mode7_heldchar=32;
}





uint16_t vidbank;
int screenlen[4] = {0x4000, 0x5000, 0x2000, 0x2800};

//uint32_t col0;


int vsynctime;
int interline;
int hvblcount;
int frameodd;
int con, cdraw, coff;
int cursoron;
int frcount;
int charsleft;
int vdispen, dispen;


int vidclocks = 0;
int oddclock = 0;
int vidbytes = 0;

int oldr8;

int firstx, firsty, lastx, lasty;

int desktop_width, desktop_height;

BITMAP *b, *b16, *b16x, *b32, *tb;
#ifdef WIN32
BITMAP *vb;
#endif

//int inverttbl[256];
int dcol;
//PALETTE pal;
BITMAP *tvb;

void video_init()
{
        int c,d;
        int temp,temp2,left;

        if (get_desktop_resolution(&desktop_width, &desktop_height)) {
            log_warn("video: unable to find desktop size, defaulting to 800x600");
            desktop_width = 800;
            desktop_height = 600;
        }

        dcol = desktop_color_depth();
        set_color_depth(dcol);
#ifdef WIN32
        set_gfx_mode(GFX_AUTODETECT_WINDOWED, 2048, 2048, 0, 0);
        vb = create_video_bitmap(924, 614);
#else
        set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
        scr_x_start = 0;
        scr_y_start = 0;
        scr_x_size = SCREEN_W;
        scr_y_size = SCREEN_H;
#endif
        b16x = create_bitmap(832, 614);
        b16  = create_bitmap(832, 614);
        set_color_depth(32);
        b32  = create_bitmap(1536, 800);

        set_color_depth(32);
//     generate_332_palette(pal);
//    set_palette(pal);

        colblack = makecol(0, 0, 0);
        colwhite = makecol(255, 255, 255);

        nula_collook[0] = makecol(0, 0, 0);
//        col0 = nula_collook[0] | (nula_collook[0] << 8) | (nula_collook[0] << 16) | (nula_collook[0] << 24);
        nula_collook[1] = makecol(255, 0,   0);
        nula_collook[2] = makecol(0,   255, 0);
        nula_collook[3] = makecol(255, 255, 0);
        nula_collook[4] = makecol(0,   0,   255);
        nula_collook[5] = makecol(255, 0,   255);
        nula_collook[6] = makecol(0,   255, 255);
        nula_collook[7] = makecol(255, 255, 255);
        nula_collook[8] = makecol(0, 0, 0);
        nula_collook[9] = makecol(255, 0, 0);
        nula_collook[10] = makecol(0, 255, 0);
        nula_collook[11] = makecol(255, 255, 0);
        nula_collook[12] = makecol(0, 0, 255);
        nula_collook[13] = makecol(255, 0, 255);
        nula_collook[14] = makecol(0, 255, 255);
        nula_collook[15] = makecol(255, 255, 255);

        for (c = 0; c < 8; c++)
        {
                nula_flash[c] = 1;
        }
        for (c = 0; c < 16; c++)
        {
                for (d = 0; d < 64; d++)
                {
                        mode7_lookup[d & 7][(d >> 3) & 7][c] = makecol(      (((d & 1) * c) * 255) / 15 + ((((d &  8) >> 3) * (15 - c)) * 255) / 15,
                                                                             ((((d & 2) >> 1) * c) * 255) / 15 + ((((d & 16) >> 4) * (15 - c)) * 255) / 15,
                                                                             ((((d & 4) >> 2) * c) * 255) / 15 + ((((d & 32) >> 5) * (15 - c)) * 255) / 15);
                }
        }
        mode7_need_new_lookup = 0;
        for (temp = 0; temp < 256; temp++)
        {
                temp2 = temp;
                for (c = 0; c < 16; c++)
                {
                        left = 0;
                        if (temp2 & 2)
                                left |= 1;
                        if (temp2 & 8)
                                left |= 2;
                        if (temp2 & 32)
                                left |= 4;
                        if (temp2 & 128)
                                left |= 8;
                        table4bpp[3][temp][c] = left;
                        temp2 <<= 1; temp2 |= 1;
                }
                for (c = 0; c < 16; c++)
                {
                        table4bpp[2][temp][c] = table4bpp[3][temp][c >> 1];
                        table4bpp[1][temp][c] = table4bpp[3][temp][c >> 2];
                        table4bpp[0][temp][c] = table4bpp[3][temp][c >> 3];
                }
        }
//        set_palette(beebpal);
        b=create_bitmap(1280, 800);
        clear_to_color(b, colblack);
//        for (c = 0; c < 256; c++) inverttbl[c]=makecol((63 - pal[c].r) << 2,(63 - pal[c].g) << 2,(63 - pal[c].b) << 2);
}


uint8_t cursorlook[7] = {0, 0, 0, 0x80, 0x40, 0x20, 0x20};
int cdrawlook[4] = {3, 2, 1, 0};

int cmask[4] = {0, 0, 16, 32};

int lasthc0 = 0, lasthc;
int olddispen;
int oldlen;
int ccount = 0;

int vid_cleared;

int firstdispen = 0;


void video_reset()
{
        interline = 0;
        vsynctime = 0;
        hvblcount = 0;
        frameodd  = 0;
        con = cdraw = 0;
        cursoron  = 0;
        charsleft = 0;
        
        nula_left_cut = 0;
        nula_left_edge = 0;
        nula_left_blank = 0;
        nula_horizontal_offset = 0;
}

void video_poll(int clocks, int timer_enable)
{
        int c, oldvc;
        uint16_t addr;
        uint8_t dat;

        while (clocks--)
        {
                scrx += 8;
                vidclocks++;
                oddclock = !oddclock;
                if (!(ula_ctrl & 0x10) && !oddclock) continue;

                if (hc == crtc[1])
                {
                        if (ula_ctrl & 2 && dispen) charsleft = 3;
                        else charsleft = 0;
                        dispen = 0;
                }
                if (hc == crtc[2])
                {
                        if (ula_ctrl & 0x10) scrx = 128 - ((crtc[3] & 15) * 4);
                        else                scrx = 128 - ((crtc[3] & 15) * 8);
                        /*Don't need this as we blit from 240 onwards*/
//                        if (interlace) memset(b->line[(scry<<1)+interlline],0,scrx);
//                        else           memset(b->line[scry],0,scrx);
//                        scrx=0;
                        scry++;
                        if (scry >= 384)
                        {
                                scry = 0;
                                video_doblit();
                        }
                }

                if (vid_interlace) scry = (scry << 1) + interlline;
                if (vid_linedbl)   scry <<= 1;
                if (dispen)
                {
                        if (!((ma ^ (crtc[15] | (crtc[14] <<8 ))) & 0x3FFF) && con) cdraw = cdrawlook[crtc[8] >> 6];

                        if (ma & 0x2000) dat = ram[0x7C00 | (ma & 0x3FF) | vidbank];
                        else
                        {
                                if ((crtc[8] & 3) == 3) addr = (ma << 3) | ((sc & 3) << 1) | interlline;
                                else                    addr = (ma << 3) | (sc & 7);
                                if (addr & 0x8000) addr -= screenlen[scrsize];
                                dat = ram[(addr & 0x7FFF) | vidbank];
                        }

                        if (scrx < 1280)
                        {
                                if ((crtc[8] & 0x30) == 0x30 || ((sc & 8) && !(ula_ctrl & 2)))
                                {
                                        // Gaps between lines in modes 3 & 6.
                                        for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c+=4)
                                        {
                                                //        ((uint32_t *)b->line[scry])[(scrx + c) >> 2]=col0;
                                                nula_putpixel(b, scrx + c, scry, colblack);
                                                nula_putpixel(b, scrx + c + 1, scry, colblack);
                                                nula_putpixel(b, scrx + c + 2, scry, colblack);
                                                nula_putpixel(b, scrx + c + 3, scry, colblack);

                                        }
                                        if (vid_linedbl)
                                        {
                                                for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c+=4)
                                                {
                                                        //        ((uint32_t *)b->line[scry + 1])[(scrx + c) >> 2] = col0;
                                                        nula_putpixel(b, scrx + c, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 1, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 2, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 3, scry + 1, colblack);
                                                }
                                        }
                                }
                                else switch (crtc_mode)
                                     {
                                     case 0:
                                             mode7_render(dat & 0x7F);
                                             break;
                                     case 1:
                                     {
                                             if (scrx < firstx)      firstx = scrx;
                                             if ((scrx + 8) > lastx) lastx = scrx + 8;
                                             if (nula_attribute_mode && ula_mode > 1)
                                             {
                                                     if (ula_mode == 3)
                                                     {
                                                             // 1bpp
                                                             if (nula_attribute_text)
                                                             {
                                                                     int attribute = ((dat & 7) << 1);
                                                                     float pc = 0.0f;
                                                                     for (c = 0; c < 7; c++, pc += 0.75f)
                                                                     {
                                                                             //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                             int output = ula_pal[attribute | (dat >> (7 - (int)pc) & 1)];
                                                                             nula_putpixel(b, scrx + c, scry, output);
                                                                             if (vid_linedbl)nula_putpixel(b, scrx + c, scry + 1, output);
                                                                     }
                                                                     // Very loose approximation of the text attribute mode
                                                                     nula_putpixel(b, scrx + 7, scry, ula_pal[attribute]);
                                                                     if (vid_linedbl)nula_putpixel(b, scrx + 7, scry + 1, ula_pal[attribute]);
                                                             }
                                                             else
                                                             {
                                                                     int attribute = ((dat & 3) << 2);
                                                                     float pc = 0.0f;
                                                                     for (c = 0; c < 8; c++, pc += 0.75f)
                                                                     {
                                                                             //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                             int output = ula_pal[attribute | (dat >> (7 - (int)pc) & 1)];
                                                                             nula_putpixel(b, scrx + c, scry, output);
                                                                             if (vid_linedbl)nula_putpixel(b, scrx + c, scry + 1, output);
                                                                     }
                                                             }
                                                     }
                                                     else
                                                     {
                                                             int attribute = (((dat & 16) >> 1) | ((dat & 1) << 2));
                                                             float pc = 0.0f;
                                                             for (c = 0; c < 8; c++, pc += 0.75f)
                                                             {
                                                                     //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                     int a = 3 - ((int)pc) / 2;

                                                                     int output = ula_pal[attribute | ((dat >> (a+3)) & 2) | ((dat >> a) & 1)];

                                                                     nula_putpixel(b, scrx + c, scry, output);
                                                                     if (vid_linedbl)nula_putpixel(b, scrx + c, scry + 1, output);
                                                             }
                                                     }
                                             }
                                             else
                                             {
                                                     // Original pixel logic
                                                     for (c = 0; c < 8; c++)
                                                     {
                                                             //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                             nula_putpixel(b, scrx + c, scry, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                                     }
                                                     if (vid_linedbl)
                                                     {
                                                             for (c = 0; c < 8; c++)
                                                             {
                                                                     //        b->line[scry + 1][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                     nula_putpixel(b, scrx + c, scry + 1, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                                             }
                                                     }
                                             }
                                     }
                                     break;
                                     case 2:
                                     {
                                             if (scrx < firstx)       firstx = scrx;
                                             if ((scrx + 16) > lastx) lastx = scrx + 16;
                                             if (nula_attribute_mode && ula_mode > 1)
                                             {
                                                     // In low frequency clock can only have 1bpp modes
                                                     if (nula_attribute_text)
                                                     {
                                                             int attribute = ((dat & 7) << 1);
                                                             float pc = 0.0f;
                                                             for (c = 0; c < 14; c++, pc += 0.375f)
                                                             {
                                                                     //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                     int output = ula_pal[attribute | (dat >> (7 - (int)pc) & 1)];
                                                                     nula_putpixel(b, scrx + c, scry, output);
                                                                     if (vid_linedbl)nula_putpixel(b, scrx + c, scry + 1, output);
                                                             }

                                                             // Very loose approximation of the text attribute mode
                                                             nula_putpixel(b, scrx + 14, scry, ula_pal[attribute]);
                                                             nula_putpixel(b, scrx + 15, scry, ula_pal[attribute]);

                                                             if (vid_linedbl)
                                                             {
                                                                 nula_putpixel(b, scrx + 14, scry + 1, ula_pal[attribute]);
                                                                 nula_putpixel(b, scrx + 15, scry + 1, ula_pal[attribute]);
                                                             }
                                                     }
                                                     else
                                                     {
                                                             int attribute = ((dat & 3) << 2);
                                                             float pc = 0.0f;
                                                             for (c = 0; c < 16; c++, pc += 0.375f)
                                                             {
                                                                     //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                     int output = ula_pal[attribute | (dat >> (7 - (int)pc) & 1)];
                                                                     nula_putpixel(b, scrx + c, scry, output);
                                                                     if (vid_linedbl)nula_putpixel(b, scrx + c, scry + 1, output);
                                                             }
                                                     }
                                             }
                                             else
                                             {
                                                     for (c = 0; c < 16; c++)
                                                     {
                                                             //        b->line[scry][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                             nula_putpixel(b, scrx + c, scry, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                                     }
                                                     if (vid_linedbl)
                                                     {
                                                             for (c = 0; c < 16; c++)
                                                             {
                                                                     //        b->line[scry + 1][scrx + c] = ula_pal[table4bpp[ula_mode][dat][c]];
                                                                     nula_putpixel(b, scrx + c, scry + 1, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                                             }
                                                     }
                                             }
                                     }
                                     break;
                                     }
                                if (cdraw)
                                {
                                        if (cursoron && (ula_ctrl & cursorlook[cdraw]))
                                        {
                                                for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c++)
                                                {
                                                        // b->line[scry][scrx + c] = inverttbl[b->line[scry][scrx + c]];
                                                        nula_putpixel(b, scrx + c, scry, getpixel(b, scrx + c, scry) ^ colwhite);
                                                }
                                                if (vid_linedbl)
                                                {
                                                        for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c++)
                                                        {
                                                                // b->line[scry + 1][scrx + c] = inverttbl[b->line[scry + 1][scrx + c]];
                                                                nula_putpixel(b, scrx + c, scry + 1, getpixel(b, scrx + c, scry + 1) ^ colwhite);
                                                        }
                                                }
                                        }
                                        cdraw++;
                                        if (cdraw == 7) cdraw = 0;
                                }
                        }
                        ma++;
                        vidbytes++;
                }
                else
                {
                        if (charsleft)
                        {
                                if (charsleft != 1) mode7_render(255);
                                charsleft--;

                        }
                        else if (scrx < 1280)
                        {
                                for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c+=4)
                                {
                                        // *((uint32_t *)&b->line[scry][scrx + c]) = col0;
                                        nula_putpixel(b, scrx + c, scry, colblack);
                                        nula_putpixel(b, scrx + c + 1, scry, colblack);
                                        nula_putpixel(b, scrx + c + 2, scry, colblack);
                                        nula_putpixel(b, scrx + c + 3, scry, colblack);
                                }

                                if (vid_linedbl)
                                {
                                        for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c += 4)
                                        {
                                                // *((uint32_t *)&b->line[scry + 1][scrx + c])=col0;
                                                nula_putpixel(b, scrx + c, scry + 1, colblack);
                                                nula_putpixel(b, scrx + c + 1, scry + 1, colblack);
                                                nula_putpixel(b, scrx + c + 2, scry + 1, colblack);
                                                nula_putpixel(b, scrx + c + 3, scry + 1, colblack);
                                        }
                                }
                                if (!crtc_mode)
                                {
                                        for (c = 0;c < 16;c += 4)
                                        {
                                                // *((uint32_t *)&b->line[scry][scrx + c + 16]) = col0;
                                                nula_putpixel(b, scrx + c + 16, scry, colblack);
                                                nula_putpixel(b, scrx + c + 16 + 1, scry, colblack);
                                                nula_putpixel(b, scrx + c + 16 + 2, scry, colblack);
                                                nula_putpixel(b, scrx + c + 16 + 3, scry, colblack);
                                        }
                                        if (vid_linedbl)
                                        {
                                                for (c = 0; c < 16; c += 4)
                                                {
                                                        // *((uint32_t *)&b->line[scry + 1][scrx + c + 16]) = col0;
                                                        nula_putpixel(b, scrx + c + 16, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 16 + 1, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 16 + 2, scry + 1, colblack);
                                                        nula_putpixel(b, scrx + c + 16 + 3, scry + 1, colblack);
                                                }
                                        }
                                }
                        }
                        if (cdraw && scrx < 1280)
                        {
                                if (cursoron && (ula_ctrl & cursorlook[cdraw]))
                                {
                                        for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c++)
                                        {
                                                // b->line[scry][scrx + c] = inverttbl[b->line[scry][scrx + c]];
                                                nula_putpixel(b, scrx + c, scry, getpixel(b, scrx + c, scry) ^ colwhite);
                                        }
                                        if (vid_linedbl)
                                        {
                                                for (c = 0; c < ((ula_ctrl & 0x10) ? 8 : 16); c++)
                                                {
                                                        // b->line[scry + 1][scrx + c] = inverttbl[b->line[scry + 1][scrx + c]];
                                                        nula_putpixel(b, scrx + c, scry + 1, getpixel(b, scrx + c, scry + 1) ^ colwhite);
                                                }
                                        }
                                }
                                cdraw++;
                                if (cdraw == 7) cdraw = 0;
                        }
                }

                if (vid_linedbl)   scry >>= 1;
                if (vid_interlace) scry >>= 1;

                if (hvblcount)
                {
                        hvblcount--;
                        if (!hvblcount && timer_enable)
                                sysvia_set_ca1(0);
                }

                if (interline && hc == (crtc[0] >> 1))
                {
                        hc = interline = 0;
                        lasthc0 = 1;

                        if (ula_ctrl & 0x10) scrx = 128 - ((crtc[3] & 15) * 4);
                        else                 scrx = 128 - ((crtc[3] & 15) * 8);
                }
                else if (hc == crtc[0])
                {
                        mode7_col = 7;
                        mode7_bg = 0;
                        mode7_holdchar = 0;
                        mode7_heldchar = 0x20;
                        mode7_p[0] = mode7_chars;
                        mode7_p[1] = mode7_charsi;
                        mode7_flash = 0;
                        mode7_sep = 0;
                        mode7_gfx = 0;
                        mode7_heldp[0] = mode7_p[0];
                        mode7_heldp[1] = mode7_p[1];

                        hc = 0;

                        if (crtc_mode)
                        {
                            // NULA left edge
                            nula_left_edge = scrx + crtc_mode * 8;
 
                            // NULA left cut
                            nula_left_cut = nula_left_edge + nula_left_blank * crtc_mode * 8;

                            // NULA horizontal offset - "delay" the pixel clock
                            for (c = 0; c < nula_horizontal_offset * crtc_mode; c++, scrx++)
                            {
                                putpixel(b, scrx + crtc_mode * 8, scry, colblack);
                            }
                        }

                        if (sc == (crtc[11] & 31) || ((crtc[8] & 3) == 3 && sc == ((crtc[11] & 31) >> 1))) { con = 0; coff = 1; }
                        if (vadj)
                        {
                                sc++;
                                sc &= 31;
                                ma = maback;
                                vadj--;
                                if (!vadj)
                                {
                                        vdispen = 1;
                                        ma = maback = (crtc[13] | (crtc[12] << 8)) & 0x3FFF;
                                        sc = 0;
                                }
                        }
                        else if (sc == crtc[9] || ((crtc[8] & 3) == 3 && sc == (crtc[9] >> 1)))
                        {
                                maback = ma;
                                sc = 0;
                                con = 0;
                                coff = 0;
                                if (mode7_nextdbl) mode7_nextdbl = 0;
                                else               mode7_nextdbl = mode7_wasdbl;
                                oldvc = vc;
                                vc++;
                                vc &= 127;
                                if (vc == crtc[6]) vdispen = 0;
                                if (oldvc == crtc[4])
                                {
                                        vc = 0;
                                        vadj = crtc[5];
                                        if (!vadj) vdispen = 1;
                                        if (!vadj) ma = maback = (crtc[13] | (crtc[12] << 8)) & 0x3FFF;

                                        frcount++;
                                        if (!(crtc[10] & 0x60)) cursoron = 1;
                                        else                    cursoron = frcount & cmask[(crtc[10] & 0x60) >> 5];
                                }
                                if (vc == crtc[7])
                                {
                                        if (!(crtc[8] & 1) && oldr8)
                                        {
                                                clear_to_color(b, colblack);
                                                clear(b32);
                                        }
                                        frameodd ^= 1;
                                        if (frameodd) interline = (crtc[8] & 1);
                                        interlline = frameodd && (crtc[8] & 1);
                                        oldr8 = crtc[8] & 1;
                                        if (vidclocks > 1024 && !ccount)
                                        {
                                                video_doblit();
                                                vid_cleared = 0;
                                        }
                                        else if (vidclocks <= 1024 && !vid_cleared)
                                        {
                                                vid_cleared = 1;
                                                clear_to_color(b, colblack);
                                                video_doblit();
                                        }
                                        ccount++;
                                        if (ccount == 10 || ((!motor || !fasttape) && !key[KEY_PGUP])) ccount = 0;
                                        scry = 0;
                                        if (timer_enable)
                                                sysvia_set_ca1(1);

                                        vsynctime = (crtc[3] >> 4) + 1;
                                        if (!(crtc[3] >> 4)) vsynctime = 17;

                                        mode7_flashtime++;
                                        if ((mode7_flashon && mode7_flashtime == 32) || (!mode7_flashon && mode7_flashtime == 16))
                                        {
                                                mode7_flashon = !mode7_flashon;
                                                mode7_flashtime = 0;
                                        }

                                        vidclocks = vidbytes = 0;
                                }
                        }
                        else
                        {
                                sc++;
                                sc &= 31;
                                ma = maback;
                        }

                        mode7_dbl = mode7_wasdbl = 0;
                        if ((sc == (crtc[10] & 31) || ((crtc[8] & 3) == 3 && sc == ((crtc[10] & 31) >> 1))) && !coff) con = 1;

                        if (vsynctime)
                        {
                                vsynctime--;
                                if (!vsynctime)
                                {
                                        hvblcount = 1;
                                        if (frameodd) interline = (crtc[8] & 1);
                                }
                        }

                        dispen = vdispen;
                        if (dispen || vadj)
                        {
                                if (scry < firsty)      firsty = scry;
                                if ((scry + 1) > lasty) lasty  = scry;
                        }

                        firstdispen = 1;
                        lasthc0 = 1;
                }
                else
                {
                        hc++;
                        hc &= 255;
                }
                lasthc = hc;
        }
}

void video_savestate(FILE *f)
{
        putc(scrx,f); putc(scrx>>8,f);
        putc(scry,f); putc(scry>>8,f);
        putc(oddclock,f);
        putc(vidclocks,f); putc(vidclocks>>8,f); putc(vidclocks>>16,f); putc(vidclocks>>24,f);
}

void video_loadstate(FILE *f)
{
        scrx=getc(f); scrx|=getc(f)<<8;
        scry=getc(f); scry|=getc(f)<<8;
        oddclock=getc(f);
        vidclocks=getc(f); vidclocks=getc(f)<<8; vidclocks=getc(f)<<16; vidclocks=getc(f)<<24;
}
