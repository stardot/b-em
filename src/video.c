/*B-em v2.2 by Tom Walker
  Video emulation
  Incorporates 6845 CRTC, Video ULA and SAA5050*/

#include <allegro5/allegro_native_dialog.h>
#include <allegro5/allegro_primitives.h>
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

static int scrx, scry;
int interlline = 0;

static int colblack;
static int colwhite;

/*6845 CRTC*/
uint8_t crtc[32];
static const uint8_t crtc_mask[32] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x7F, 0x7F, 0xF3, 0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF };

int crtc_i;

int hc, vc, sc;
static int vadj;
uint16_t ma;
static uint16_t maback;
static int vdispen, dispen;

void crtc_reset()
{
    hc = vc = sc = vadj = 0;
    crtc[9] = 10;
}

void crtc_write(uint16_t addr, uint8_t val)
{
//        log_debug("Write CRTC %04X %02X %04X\n",addr,val,pc);
    if (!(addr & 1))
        crtc_i = val & 31;
    else {
        val &= crtc_mask[crtc_i];
        crtc[crtc_i] = val;
        if (crtc_i == 6 && vc == val)
            vdispen = 0;
    }
}

uint8_t crtc_read(uint16_t addr)
{
    if (!(addr & 1))
        return crtc_i;
    return crtc[crtc_i];
}

void crtc_latchpen()
{
    crtc[0x10] = (ma >> 8) & 0x3F;
    crtc[0x11] = ma & 0xFF;
}

void crtc_savestate(FILE * f)
{
    int c;
    for (c = 0; c < 18; c++)
        putc(crtc[c], f);
    putc(vc, f);
    putc(sc, f);
    putc(hc, f);
    putc(ma, f);
    putc(ma >> 8, f);
    putc(maback, f);
    putc(maback >> 8, f);
}

void crtc_loadstate(FILE * f)
{
    int c;
    for (c = 0; c < 18; c++)
        crtc[c] = getc(f);
    vc = getc(f);
    sc = getc(f);
    hc = getc(f);
    ma = getc(f);
    ma |= getc(f) << 8;
    maback = getc(f);
    maback |= getc(f) << 8;
}


/*Video ULA (VIDPROC)*/
uint8_t ula_ctrl;
static int ula_pal[16];         // maps from actual physical colour to bitmap display
uint8_t ula_palbak[16];         // palette RAM in orginal ULA maps actual colour to logical colour
static int ula_mode;
static int crtc_mode;
int nula_collook[16];           // maps palette (logical) colours to 12-bit RGB

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

static inline uint32_t makecol(int red, int green, int blue)
{
    return 0xff000000 | (red << 16) | (green << 8) | blue;
}

static inline int get_pixel(ALLEGRO_LOCKED_REGION *region, int x, int y)
{
    return *((uint32_t *)(region->data + region->pitch * y + x * region->pixel_size));
}

static inline void put_pixel(ALLEGRO_LOCKED_REGION *region, int x, int y, uint32_t colour)
{
    *((uint32_t *)(region->data + region->pitch * y + x * region->pixel_size)) = colour;
}

static inline void put_pixels(ALLEGRO_LOCKED_REGION *region, int x, int y, int count, uint32_t colour)
{
    void *ptr = region->data + region->pitch * y + x * region->pixel_size;
    while (count--) {
        *(uint32_t *)ptr = colour;
        ptr += region->pixel_size;
    }
}

static inline void nula_putpixel(ALLEGRO_LOCKED_REGION *region, int x, int y, uint32_t colour)
{
    if (crtc_mode && (nula_horizontal_offset || nula_left_blank) && (x < nula_left_cut || x >= nula_left_edge + (crtc[1] * crtc_mode * 8)))
        put_pixel(region, x, y, colblack);
    else if (x < 1280)
        put_pixel(region, x, y, colour);
}

void nula_default_palette(void)
{
    nula_collook[0]  = 0xff000000; // black
    nula_collook[1]  = 0xffff0000; // red
    nula_collook[2]  = 0xff00ff00; // green
    nula_collook[3]  = 0xffffff00; // yellow
    nula_collook[4]  = 0xff0000ff; // blue
    nula_collook[5]  = 0xffff00ff; // magenta
    nula_collook[6]  = 0xff00ffff; // cyan
    nula_collook[7]  = 0xffffffff; // white
    nula_collook[8]  = 0xff000000; // black
    nula_collook[9]  = 0xffff0000; // red
    nula_collook[10] = 0xff00ff00; // green
    nula_collook[11] = 0xffffff00; // yellow
    nula_collook[12] = 0xff0000ff; // blue
    nula_collook[13] = 0xffff00ff; // magenta
    nula_collook[14] = 0xff00ffff; // cyan
    nula_collook[15] = 0xffffffff; // white

    mode7_need_new_lookup = 1;
}

void videoula_write(uint16_t addr, uint8_t val)
{
    int c;
    //log_debug("ULA write %04X %02X %i %i\n",addr,val,hc,vc);
    if (nula_disable)
        addr &= ~2;             // nuke additional NULA addresses

    switch (addr & 3) {
    case 0:
        {
            //        printf("ULA write %04X %02X\n",addr,val);
            if ((ula_ctrl ^ val) & 1) {
                if (val & 1) {
                    for (c = 0; c < 16; c++) {
                        if ((ula_palbak[c] & 8) && nula_flash[(ula_palbak[c] & 7) ^ 7])
                            ula_pal[c] = nula_collook[ula_palbak[c] & 15];
                        else
                            ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                    }
                } else {
                    for (c = 0; c < 16; c++)
                        ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                }
            }
            ula_ctrl = val;
            ula_mode = (ula_ctrl >> 2) & 3;
            if (val & 2)
                crtc_mode = 0;  // Teletext
            else if (val & 0x10)
                crtc_mode = 1;  // High frequency
            else
                crtc_mode = 2;  // Low frequency
            //                printf("ULAmode %i\n",ulamode);
        }
        break;

    case 1:
        {
            // log_debug("ULA write %04X %02X\n",addr,val);
            c = ula_palbak[val >> 4];
            ula_palbak[val >> 4] = val & 15;
            ula_pal[val >> 4] = nula_collook[(val & 15) ^ 7];
            if ((val & 8) && (ula_ctrl & 1) && nula_flash[val & 7])
                ula_pal[val >> 4] = nula_collook[val & 15];
        }
        break;

    case 2:                     // &FE22 = NULA CONTROL REG
        {
            uint8_t code = val >> 4;
            uint8_t param = val & 0xf;

            switch (code) {
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
                nula_default_palette();

                // Reset flash
                for (c = 0; c < 8; c++) {
                    nula_flash[c] = 1;
                }
                break;

            case 5:
                nula_disable = 1;
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

    case 3:                     // &FE23 = NULA PALETTE REG
        {
            if (nula_pal_write_flag) {
                // Commit the write to palette
                int c = (nula_pal_first_byte >> 4);
                int r = nula_pal_first_byte & 0x0f;
                int g = (val & 0xf0) >> 4;
                int b = val & 0x0f;
                nula_collook[c] = makecol(r | r << 4, g | g << 4, b | b << 4);
                // Manual states colours 8-15 are set solid by default
                if (c & 8)
                    nula_flash[c - 8] = 0;
                // Reset all colour lookups
                for (c = 0; c < 16; c++) {
                    ula_pal[c] = nula_collook[(ula_palbak[c] & 15) ^ 7];
                    if ((ula_palbak[c] & 8) && (ula_ctrl & 1) && nula_flash[(ula_palbak[c] & 7) ^ 7])
                        ula_pal[c] = nula_collook[ula_palbak[c] & 15];
                }
                mode7_need_new_lookup = 1;
            } else {
                // Remember the first byte
                nula_pal_first_byte = val;
            }

            nula_pal_write_flag = !nula_pal_write_flag;
        }
        break;

    }
}

void videoula_savestate(FILE * f)
{
    int c;
    uint32_t v;

    putc(ula_ctrl, f);
    for (c = 0; c < 16; c++)
        putc(ula_palbak[c], f);
    for (c = 0; c < 16; c++) {
        v = nula_collook[c];
        putc(((v >> 16) & 0xff), f); // red
        putc(((v >> 8) & 0xff), f);  // green
        putc((v & 0xff), f);         // blue
        putc(((v >> 24) & 0xff), f); // alpha
    }
    putc(nula_pal_write_flag, f);
    putc(nula_pal_first_byte, f);
    for (c = 0; c < 8; c++)
        putc(nula_flash[c], f);
    putc(nula_palette_mode, f);
    putc(nula_horizontal_offset, f);
    putc(nula_left_blank, f);
    putc(nula_disable, f);
    putc(nula_attribute_mode, f);
    putc(nula_attribute_text, f);
}

void videoula_loadstate(FILE * f)
{
    int c;
    uint8_t red, grn, blu, alp;
    videoula_write(0, getc(f));
    for (c = 0; c < 16; c++)
        videoula_write(1, getc(f) | (c << 4));
    for (c = 0; c < 16; c++) {
        red = getc(f);
        blu = getc(f);
        grn = getc(f);
        alp = getc(f);
        nula_collook[c] = (alp << 24) | (red << 16) | (grn << 8) | blu;
    }
    nula_pal_write_flag = getc(f);
    nula_pal_first_byte = getc(f);
    for (c = 0; c < 8; c++)
        nula_flash[c] = getc(f);
    nula_palette_mode = getc(f);
    nula_horizontal_offset = getc(f);
    nula_left_blank = getc(f);
    nula_disable = getc(f);
    nula_attribute_mode = getc(f);
    nula_attribute_text = getc(f);
}

/*Mode 7 (SAA5050)*/
static uint8_t mode7_chars[96 * 160], mode7_charsi[96 * 160], mode7_graph[96 * 160], mode7_graphi[96 * 160], mode7_sepgraph[96 * 160], mode7_sepgraphi[96 * 160], mode7_tempi[96 * 120], mode7_tempi2[96 * 120];
static int mode7_lookup[8][8][16];

static int mode7_col = 7, mode7_bg = 0;
static int mode7_sep = 0;
static int mode7_dbl, mode7_nextdbl, mode7_wasdbl;
static int mode7_gfx;
static int mode7_flash, mode7_flashon = 0, mode7_flashtime = 0;
static uint8_t mode7_buf[2];
static uint8_t *mode7_p[2] = { mode7_chars, mode7_charsi };

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
    for (c = 0; c < (96 * 60); c++)
        teletext_characters[c] *= 15;
    for (c = 0; c < (96 * 60); c++)
        teletext_graphics[c] *= 15;
    for (c = 0; c < (96 * 60); c++)
        teletext_separated_graphics[c] *= 15;
    for (c = 0; c < (96 * 120); c++)
        mode7_tempi2[c] = teletext_characters[c >> 1];
    for (c = 0; c < 960; c++) {
        x = 0;
        x2 = 0;
        for (d = 0; d < 16; d++) {
            mode7_graph[offs2 + d] = (int) (((float) teletext_graphics[offs1 + x2] * (1.0 - x)) + ((float) teletext_graphics[offs1 + x2 + 1] * x));
            mode7_sepgraph[offs2 + d] = (int) (((float) teletext_separated_graphics[offs1 + x2] * (1.0 - x)) + ((float) teletext_separated_graphics[offs1 + x2 + 1] * x));
            if (!d) {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1];
            } else if (d == 15) {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1 + 5];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + 5];
            } else {
                mode7_graph[offs2 + d] = mode7_graphi[offs2 + d] = teletext_graphics[offs1 + x2];
                mode7_sepgraph[offs2 + d] = mode7_sepgraphi[offs2 + d] = teletext_separated_graphics[offs1 + x2];
            }
            x += (5.0 / 15.0);
            if (x >= 1.0) {
                x2++;
                x -= 1.0;
            }
            mode7_charsi[offs2 + d] = 0;
        }

        offs1 += 6;
        offs2 += 16;
    }
    for (c = 0; c < 96; c++) {
        for (y = 0; y < 10; y++) {
            for (d = 0; d < 6; d++) {
                stat = 0;
                if (y < 9 && p[(y * 6) + d] && p[(y * 6) + d + 6])
                    stat |= 3;  /*Above + below - set both */
                if (y < 9 && d > 0 && p[(y * 6) + d] && p[(y * 6) + d + 5] && !p[(y * 6) + d - 1])
                    stat |= 1;  /*Above + left  - set left */
                if (y < 9 && d > 0 && p[(y * 6) + d + 6] && p[(y * 6) + d - 1] && !p[(y * 6) + d + 5])
                    stat |= 1;  /*Below + left  - set left */
                if (y < 9 && d < 5 && p[(y * 6) + d] && p[(y * 6) + d + 7] && !p[(y * 6) + d + 1])
                    stat |= 2;  /*Above + right - set right */
                if (y < 9 && d < 5 && p[(y * 6) + d + 6] && p[(y * 6) + d + 1] && !p[(y * 6) + d + 7])
                    stat |= 2;  /*Below + right - set right */

                p2[0] = (stat & 1) ? 15 : 0;
                p2[1] = (stat & 2) ? 15 : 0;
                p2 += 2;
            }
        }
        p += 60;
    }
    offs1 = offs2 = 0;
    for (c = 0; c < 960; c++) {
        x = 0;
        x2 = 0;
        for (d = 0; d < 16; d++) {
            mode7_chars[offs2 + d] = (int) (((float) mode7_tempi2[offs1 + x2] * (1.0 - x)) + ((float) mode7_tempi2[offs1 + x2 + 1] * x));
            mode7_charsi[offs2 + d] = (int) (((float) mode7_tempi[offs1 + x2] * (1.0 - x)) + ((float) mode7_tempi[offs1 + x2 + 1] * x));
            x += (11.0 / 15.0);
            if (x >= 1.0) {
                x2++;
                x -= 1.0;
            }
            if (c >= 320 && c < 640) {
                mode7_graph[offs2 + d] = mode7_sepgraph[offs2 + d] = mode7_chars[offs2 + d];
                mode7_graphi[offs2 + d] = mode7_sepgraphi[offs2 + d] = mode7_charsi[offs2 + d];
            }
        }
        offs1 += 12;
        offs2 += 16;
    }
}

static void mode7_gen_nula_lookup(void)
{
    int fg_ix, fg_pix, fg_red, fg_grn, fg_blu;
    int bg_ix, bg_pix, bg_red, bg_grn, bg_blu;
    int weight, lu_red, lu_grn, lu_blu;

    for (fg_ix = 0; fg_ix < 8; fg_ix++) {
        fg_pix = nula_collook[fg_ix];
        fg_red = (fg_pix >> 16) & 0xff;
        fg_grn = (fg_pix >> 8) & 0xff;
        fg_blu = fg_pix & 0xff;
        for (bg_ix = 0; bg_ix < 8; bg_ix++) {
            bg_pix = nula_collook[bg_ix];
            bg_red = (bg_pix >> 16) & 0xff;
            bg_grn = (bg_pix >> 8) & 0xff;
            bg_blu = bg_pix & 0xff;
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

static inline void mode7_render(ALLEGRO_LOCKED_REGION *region, uint8_t dat)
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

    if (!mode7_dbl && mode7_nextdbl)
        on = mode7_lookup[mode7_bg & 7][mode7_bg & 7];
    if (dat == 255) {
        for (c = 0; c < 16; c++)
            put_pixel(region, scrx + c + 16, scry, colblack);
        if (vid_linedbl) {
            for (c = 0; c < 16; c++)
                put_pixel(region, scrx + c + 16, scry + 1, colblack);
        }
        return;
    }

    if (dat < 0x20) {
        switch (dat) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            mode7_gfx = 0;
            mode7_col = dat;
            mode7_p[0] = mode7_chars;
            mode7_p[1] = mode7_charsi;
            holdclear = 1;
            break;
        case 8:
            mode7_flash = 1;
            break;
        case 9:
            mode7_flash = 0;
            break;
        case 12:
        case 13:
            mode7_dbl = dat & 1;
            if (mode7_dbl)
                mode7_wasdbl = 1;
            break;
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
            mode7_gfx = 1;
            mode7_col = dat & 7;
            if (mode7_sep) {
                mode7_p[0] = mode7_sepgraph;
                mode7_p[1] = mode7_sepgraphi;
            } else {
                mode7_p[0] = mode7_graph;
                mode7_p[1] = mode7_graphi;
            }
            break;
        case 24:
            mode7_col = mcolx = mode7_bg;
            break;
        case 25:
            if (mode7_gfx) {
                mode7_p[0] = mode7_graph;
                mode7_p[1] = mode7_graphi;
            }
            mode7_sep = 0;
            break;
        case 26:
            if (mode7_gfx) {
                mode7_p[0] = mode7_sepgraph;
                mode7_p[1] = mode7_sepgraphi;
            }
            mode7_sep = 1;
            break;
        case 28:
            mode7_bg = 0;
            break;
        case 29:
            mode7_bg = mode7_col;
            break;
        case 30:
            mode7_holdchar = 1;
            break;
        case 31:
            holdoff = 1;
            break;
        }
        if (mode7_holdchar) {
            dat = mode7_heldchar;
            if (dat >= 0x40 && dat < 0x60)
                dat = 32;
            mode7_px[0] = mode7_heldp[0];
            mode7_px[1] = mode7_heldp[1];
        } else
            dat = 0x20;
        if (mode7_dblx != mode7_dbl)
            dat = 32;           /*Double height doesn't respect held characters */
    } else if (mode7_p[0] != mode7_chars) {
        mode7_heldchar = dat;
        mode7_heldp[0] = mode7_px[0];
        mode7_heldp[1] = mode7_px[1];
    }

    if (mode7_dblx && !mode7_nextdbl)
        t = ((dat - 0x20) * 160) + ((sc >> 1) * 16);
    else if (mode7_dblx)
        t = ((dat - 0x20) * 160) + ((sc >> 1) * 16) + (5 * 16);
    else
        t = ((dat - 0x20) * 160) + (sc * 16);

    off = mode7_lookup[0][mode7_bg & 7][0];
    if (!mode7_dbl && mode7_nextdbl)
        on = mode7_lookup[mode7_bg & 7][mode7_bg & 7];
    else
        on = mode7_lookup[mcolx & 7][mode7_bg & 7];

    for (c = 0; c < 16; c++) {
        if (mode7_flashx && !mode7_flashon)
            put_pixel(region, scrx + c + 16, scry, off);
        else if (mode7_dblx)
            put_pixel(region, scrx + c + 16, scry, on[mode7_px[sc & 1][t] & 15]);
        else
            put_pixel(region, scrx + c + 16, scry, on[mode7_px[vid_interlace & interlline][t] & 15]);
        t++;
    }

    if (vid_linedbl) {
        t -= 16;
        for (c = 0; c < 16; c++) {
            if (mode7_flashx && !mode7_flashon)
                put_pixel(region, scrx + c + 16, scry + 1, off);
            else if (mode7_dblx)
                put_pixel(region, scrx + c + 16, scry + 1, on[mode7_px[sc & 1][t] & 15]);
            else
                put_pixel(region, scrx + c + 16, scry + 1, on[mode7_px[1][t] & 15]);
            t++;
        }
    }

    if ((scrx + 16) < firstx)
        firstx = scrx + 16;
    if ((scrx + 32) > lastx)
        lastx = scrx + 32;

    if (holdoff) {
        mode7_holdchar = 0;
        mode7_heldchar = 32;
    }
    if (holdclear)
        mode7_heldchar = 32;
}

uint16_t vidbank;
static const int screenlen[4] = { 0x4000, 0x5000, 0x2000, 0x2800 };

static int vsynctime;
static int interline;
static int hvblcount;
static int frameodd;
static int con, cdraw, coff;
static int cursoron;
static int frcount;
static int charsleft;

static int vidclocks = 0;
static int oddclock = 0;
static int vidbytes = 0;

static int oldr8;

int firstx, firsty, lastx, lasty;

int desktop_width, desktop_height;

static ALLEGRO_DISPLAY *display;
ALLEGRO_BITMAP *b, *b16, *b32;

ALLEGRO_LOCKED_REGION *region;

ALLEGRO_DISPLAY *video_init(void)
{
    int c;
    int temp, temp2, left;

#ifdef ALLEGRO_GTK_TOPLEVEL
    al_set_new_display_flags(ALLEGRO_WINDOWED | ALLEGRO_GTK_TOPLEVEL | ALLEGRO_RESIZABLE);
#else
    al_set_new_display_flags(ALLEGRO_WINDOWED | ALLEGRO_RESIZABLE);
#endif
    video_set_window_size();
    if ((display = al_create_display(winsizex, winsizey)) == NULL) {
        log_fatal("video: unable to create display");
        exit(1);
    }

    al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);
    b16 = al_create_bitmap(832, 614);
    b32 = al_create_bitmap(1536, 800);

    colblack = 0xff000000;
    colwhite = 0xffffffff;

    nula_default_palette();

    for (c = 0; c < 8; c++)
        nula_flash[c] = 1;
    for (temp = 0; temp < 256; temp++) {
        temp2 = temp;
        for (c = 0; c < 16; c++) {
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
            temp2 <<= 1;
            temp2 |= 1;
        }
        for (c = 0; c < 16; c++) {
            table4bpp[2][temp][c] = table4bpp[3][temp][c >> 1];
            table4bpp[1][temp][c] = table4bpp[3][temp][c >> 2];
            table4bpp[0][temp][c] = table4bpp[3][temp][c >> 3];
        }
    }
    b = al_create_bitmap(1280, 800);
    al_set_target_bitmap(b);
    al_clear_to_color(al_map_rgb(0, 0,0));
    region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
    return display;
}


uint8_t cursorlook[7] = { 0, 0, 0, 0x80, 0x40, 0x20, 0x20 };
int cdrawlook[4] = { 3, 2, 1, 0 };

int cmask[4] = { 0, 0, 16, 32 };

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
    frameodd = 0;
    con = cdraw = 0;
    cursoron = 0;
    charsleft = 0;
    vidbank = 0;

    nula_left_cut = 0;
    nula_left_edge = 0;
    nula_left_blank = 0;
    nula_horizontal_offset = 0;

}

#if 0
static inline int is_free_run(void) {
    ALLEGRO_KEYBOARD_STATE keystate;

    al_get_keyboard_state(&keystate);
    return al_key_down(&keystate, ALLEGRO_KEY_PGUP);
}
#endif
static inline int is_free_run(void) {
    return 0;
}

void video_poll(int clocks, int timer_enable)
{
    int c, oldvc;
    uint16_t addr;
    uint8_t dat;

    while (clocks--) {
        scrx += 8;
        vidclocks++;
        oddclock = !oddclock;
        if (!(ula_ctrl & 0x10) && !oddclock)
            continue;

        if (hc == crtc[1]) { // reached horizontal displayed count.
            if (dispen && ula_ctrl & 2)
                charsleft = 3;
            else
                charsleft = 0;
            dispen = 0;
        }
        if (hc == crtc[2]) { // reached horizontal sync position.
            if (ula_ctrl & 0x10)
                scrx = 128 - ((crtc[3] & 15) * 4);
            else
                scrx = 128 - ((crtc[3] & 15) * 8);
            scry++;
            if (scry >= 384) {
                scry = 0;
                video_doblit(crtc_mode, crtc[4]);
            }
        }

        if (vid_interlace)
            scry = (scry << 1) + interlline;
        if (vid_linedbl)
            scry <<= 1;
        if (dispen) {
            if (!((ma ^ (crtc[15] | (crtc[14] << 8))) & 0x3FFF) && con)
                cdraw = cdrawlook[crtc[8] >> 6];

            if (ma & 0x2000)
                dat = ram[0x7C00 | (ma & 0x3FF) | vidbank];
            else {
                if ((crtc[8] & 3) == 3)
                    addr = (ma << 3) | ((sc & 3) << 1) | interlline;
                else
                    addr = (ma << 3) | (sc & 7);
                if (addr & 0x8000)
                    addr -= screenlen[scrsize];
                dat = ram[(addr & 0x7FFF) | vidbank];
            }

            if (scrx < 1280) {
                if ((crtc[8] & 0x30) == 0x30 || ((sc & 8) && !(ula_ctrl & 2))) {
                    // Gaps between lines in modes 3 & 6.
                    put_pixels(region, scrx, scry, (ula_ctrl & 0x10) ? 8 : 16, colblack);
                    if (vid_linedbl)
                        put_pixels(region, scrx, scry+1, (ula_ctrl & 0x10) ? 8 : 16, colblack);
                } else
                    switch (crtc_mode) {
                    case 0:
                        mode7_render(region, dat & 0x7F);
                        break;
                    case 1:
                        {
                            if (scrx < firstx)
                                firstx = scrx;
                            if ((scrx + 8) > lastx)
                                lastx = scrx + 8;
                            if (nula_attribute_mode && ula_mode > 1) {
                                if (ula_mode == 3) {
                                    // 1bpp
                                    if (nula_attribute_text) {
                                        int attribute = ((dat & 7) << 1);
                                        float pc = 0.0f;
                                        for (c = 0; c < 7; c++, pc += 0.75f) {
                                            int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                                            nula_putpixel(region, scrx + c, scry, output);
                                            if (vid_linedbl)
                                                nula_putpixel(region, scrx + c, scry + 1, output);
                                        }
                                        // Very loose approximation of the text attribute mode
                                        nula_putpixel(region, scrx + 7, scry, ula_pal[attribute]);
                                        if (vid_linedbl)
                                            nula_putpixel(region, scrx + 7, scry + 1, ula_pal[attribute]);
                                    } else {
                                        int attribute = ((dat & 3) << 2);
                                        float pc = 0.0f;
                                        for (c = 0; c < 8; c++, pc += 0.75f) {
                                            int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                                            nula_putpixel(region, scrx + c, scry, output);
                                            if (vid_linedbl)
                                                nula_putpixel(region, scrx + c, scry + 1, output);
                                        }
                                    }
                                } else {
                                    int attribute = (((dat & 16) >> 1) | ((dat & 1) << 2));
                                    float pc = 0.0f;
                                    for (c = 0; c < 8; c++, pc += 0.75f) {
                                        int a = 3 - ((int) pc) / 2;
                                        int output = ula_pal[attribute | ((dat >> (a + 3)) & 2) | ((dat >> a) & 1)];
                                        nula_putpixel(region, scrx + c, scry, output);
                                        if (vid_linedbl)
                                            nula_putpixel(region, scrx + c, scry + 1, output);
                                    }
                                }
                            } else {
                                for (c = 0; c < 8; c++) {
                                    nula_putpixel(region, scrx + c, scry, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                }
                                if (vid_linedbl) {
                                    for (c = 0; c < 8; c++) {
                                        nula_putpixel(region, scrx + c, scry + 1, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                    }
                                }
                            }
                        }
                        break;
                    case 2:
                        {
                            if (scrx < firstx)
                                firstx = scrx;
                            if ((scrx + 16) > lastx)
                                lastx = scrx + 16;
                            if (nula_attribute_mode && ula_mode > 1) {
                                // In low frequency clock can only have 1bpp modes
                                if (nula_attribute_text) {
                                    int attribute = ((dat & 7) << 1);
                                    float pc = 0.0f;
                                    for (c = 0; c < 14; c++, pc += 0.375f) {
                                        int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                                        nula_putpixel(region, scrx + c, scry, output);
                                        if (vid_linedbl)
                                            nula_putpixel(region, scrx + c, scry + 1, output);
                                    }

                                    // Very loose approximation of the text attribute mode
                                    nula_putpixel(region, scrx + 14, scry, ula_pal[attribute]);
                                    nula_putpixel(region, scrx + 15, scry, ula_pal[attribute]);

                                    if (vid_linedbl) {
                                        nula_putpixel(region, scrx + 14, scry + 1, ula_pal[attribute]);
                                        nula_putpixel(region, scrx + 15, scry + 1, ula_pal[attribute]);
                                    }
                                } else {
                                    int attribute = ((dat & 3) << 2);
                                    float pc = 0.0f;
                                    for (c = 0; c < 16; c++, pc += 0.375f) {
                                        int output = ula_pal[attribute | (dat >> (7 - (int) pc) & 1)];
                                        nula_putpixel(region, scrx + c, scry, output);
                                        if (vid_linedbl)
                                            nula_putpixel(region, scrx + c, scry + 1, output);
                                    }
                                }
                            } else {
                                for (c = 0; c < 16; c++) {
                                    nula_putpixel(region, scrx + c, scry, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                }
                                if (vid_linedbl) {
                                    for (c = 0; c < 16; c++) {
                                        nula_putpixel(region, scrx + c, scry + 1, nula_palette_mode ? nula_collook[table4bpp[ula_mode][dat][c]] : ula_pal[table4bpp[ula_mode][dat][c]]);
                                    }
                                }
                            }
                        }
                        break;
                    }
                if (cdraw) {
                    if (cursoron && (ula_ctrl & cursorlook[cdraw])) {
                        for (c = ((ula_ctrl & 0x10) ? 8 : 16); c >= 0; c--) {
                            nula_putpixel(region, scrx + c, scry, get_pixel(region, scrx + c, scry) ^ colwhite);
                        }
                        if (vid_linedbl) {
                            for (c = ((ula_ctrl & 0x10) ? 8 : 16); c >= 0; c--) {
                                nula_putpixel(region, scrx + c, scry + 1, get_pixel(region, scrx + c, scry + 1) ^ colwhite);
                            }
                        }
                    }
                    cdraw++;
                    if (cdraw == 7)
                        cdraw = 0;
                }
            }
            ma++;
            vidbytes++;
        } else {
            if (charsleft) {
                if (charsleft != 1)
                    mode7_render(region, 255);
                charsleft--;

            } else if (scrx < 1280) {
                put_pixels(region, scrx, scry, (ula_ctrl & 0x10) ? 8 : 16, colblack);
                if (vid_linedbl)
                    put_pixels(region, scrx, scry+1, (ula_ctrl & 0x10) ? 8 : 16, colblack);
                if (!crtc_mode) {
                    put_pixels(region, scrx + 16, scry, 16, colblack);
                    if (vid_linedbl)
                        put_pixels(region, scrx + 16, scry+1, 16, colblack);
                }
            }
            if (cdraw && scrx < 1280) {
                if (cursoron && (ula_ctrl & cursorlook[cdraw])) {
                    for (c = ((ula_ctrl & 0x10) ? 8 : 16); c >= 0; c--) {
                        nula_putpixel(region, scrx + c, scry, get_pixel(region, scrx + c, scry) ^ colwhite);
                    }
                    if (vid_linedbl) {
                        for (c = ((ula_ctrl & 0x10) ? 8 : 16); c >= 0; c--) {
                            nula_putpixel(region, scrx + c, scry + 1, get_pixel(region, scrx + c, scry + 1) ^ colwhite);
                        }
                    }
                }
                cdraw++;
                if (cdraw == 7)
                    cdraw = 0;
            }
        }

        if (vid_linedbl)
            scry >>= 1;
        if (vid_interlace)
            scry >>= 1;

        if (hvblcount) {
            hvblcount--;
            if (!hvblcount && timer_enable)
                sysvia_set_ca1(0);
        }

        if (interline && hc == (crtc[0] >> 1)) {
            hc = interline = 0;
            lasthc0 = 1;

            if (ula_ctrl & 0x10)
                scrx = 128 - ((crtc[3] & 15) * 4);
            else
                scrx = 128 - ((crtc[3] & 15) * 8);
        } else if (hc == crtc[0]) {
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

            if (crtc_mode) {
                // NULA left edge
                nula_left_edge = scrx + crtc_mode * 8;

                // NULA left cut
                nula_left_cut = nula_left_edge + nula_left_blank * crtc_mode * 8;

                // NULA horizontal offset - "delay" the pixel clock
                for (c = 0; c < nula_horizontal_offset * crtc_mode; c++, scrx++) {
                    put_pixel(region, scrx + crtc_mode * 8, scry, colblack);
                }
            }

            if (sc == (crtc[11] & 31) || ((crtc[8] & 3) == 3 && sc == ((crtc[11] & 31) >> 1))) {
                con = 0;
                coff = 1;
            }
            if (vadj) {
                sc++;
                sc &= 31;
                ma = maback;
                vadj--;
                if (!vadj) {
                    vdispen = 1;
                    ma = maback = (crtc[13] | (crtc[12] << 8)) & 0x3FFF;
                    sc = 0;
                }
            } else if (sc == crtc[9] || ((crtc[8] & 3) == 3 && sc == (crtc[9] >> 1))) {
                maback = ma;
                sc = 0;
                con = 0;
                coff = 0;
                if (mode7_nextdbl)
                    mode7_nextdbl = 0;
                else
                    mode7_nextdbl = mode7_wasdbl;
                oldvc = vc;
                vc++;
                vc &= 127;
                if (vc == crtc[6])
                    vdispen = 0;
                if (oldvc == crtc[4]) {
                    vc = 0;
                    vadj = crtc[5];
                    if (!vadj) {
                        vdispen = 1;
                        ma = maback = (crtc[13] | (crtc[12] << 8)) & 0x3FFF;
                    }
                    frcount++;
                    if (!(crtc[10] & 0x60))
                        cursoron = 1;
                    else
                        cursoron = frcount & cmask[(crtc[10] & 0x60) >> 5];
                }
                if (vc == crtc[7]) {
                    if (!(crtc[8] & 1) && oldr8) {
                        ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);
                        al_set_target_bitmap(b32);
                        al_clear_to_color(black);
                        al_unlock_bitmap(b);
                        al_set_target_bitmap(b);
                        al_clear_to_color(black);
                        region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
                    }
                    frameodd ^= 1;
                    interlline = frameodd && (crtc[8] & 1);
                    oldr8 = crtc[8] & 1;
                    if (vidclocks > 1024 && !ccount) {
                        video_doblit(crtc_mode, crtc[4]);
                        vid_cleared = 0;
                    } else if (vidclocks <= 1024 && !vid_cleared) {
                        vid_cleared = 1;
                        al_unlock_bitmap(b);
                        al_clear_to_color(al_map_rgb(0, 0, 0));
                        region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
                        video_doblit(crtc_mode, crtc[4]);
                    }
                    ccount++;
                    if (ccount == 10 || ((!motor || !fasttape) && !is_free_run()))
                        ccount = 0;
                    scry = 0;
                    if (timer_enable)
                        sysvia_set_ca1(1);

                    vsynctime = (crtc[3] >> 4) + 1;
                    if (!(crtc[3] >> 4))
                        vsynctime = 17;

                    mode7_flashtime++;
                    if ((mode7_flashon && mode7_flashtime == 32) || (!mode7_flashon && mode7_flashtime == 16)) {
                        mode7_flashon = !mode7_flashon;
                        mode7_flashtime = 0;
                    }

                    vidclocks = vidbytes = 0;
                }
            } else {
                sc++;
                sc &= 31;
                ma = maback;
            }

            mode7_dbl = mode7_wasdbl = 0;
            if ((sc == (crtc[10] & 31) || ((crtc[8] & 3) == 3 && sc == ((crtc[10] & 31) >> 1))) && !coff)
                con = 1;

            if (vsynctime) {
                vsynctime--;
                if (!vsynctime) {
                    hvblcount = 1;
                    if (frameodd)
                        interline = (crtc[8] & 1);
                }
            }

            dispen = vdispen;
            if (dispen || vadj) {
                if (scry < firsty)
                    firsty = scry;
                if ((scry + 1) > lasty)
                    lasty = scry;
            }

            firstdispen = 1;
            lasthc0 = 1;
        } else {
            hc++;
            hc &= 255;
        }
        lasthc = hc;
    }
}

void video_savestate(FILE * f)
{
    putc(scrx, f);
    putc(scrx >> 8, f);
    putc(scry, f);
    putc(scry >> 8, f);
    putc(oddclock, f);
    putc(vidclocks, f);
    putc(vidclocks >> 8, f);
    putc(vidclocks >> 16, f);
    putc(vidclocks >> 24, f);
}

void video_loadstate(FILE * f)
{
    scrx = getc(f);
    scrx |= getc(f) << 8;
    scry = getc(f);
    scry |= getc(f) << 8;
    oddclock = getc(f);
    vidclocks = getc(f);
    vidclocks = getc(f) << 8;
    vidclocks = getc(f) << 16;
    vidclocks = getc(f) << 24;
}
