#include "b-em.h"
#include "textsave.h"
#include "mem.h"
#include "via.h"
#include "sysvia.h"
#include "video.h"

/*
 * Save text screen to file.
 *
 * This can be called from the GUI to save the contents of the screen
 * memory to a file on the host.  This works similarly to calling
 * OSBYTE &87 for each location on the screen and writing the result
 * to a file, except that trailing spaces and trailing black lines
 * are omitted.
 */

/*
 * Mode 7 (teletext).
 *
 * This is easy as the characters are stored in memory as ASCII codes.
 * The only complexity is the way memory wrap-around works when the
 * screen is hardware scrolled.
 */

static void textsave_teletext(const char *filename, FILE *fp, uint_least16_t mem_addr)
{
    int cols = crtc[1];
    int rows = crtc[6];
    uint_least8_t newlines = 0;
    for (int row = 0; row < rows; ++row) {
        uint_least8_t spaces = 0;
        for (int col = 0; col < cols; ++col) {
            uint_least32_t ram_addr;
            if (mem_addr & 0x2000)
                ram_addr = ttxbank | (mem_addr & 0x3FF) | vidbank;
            else
                ram_addr = (mem_addr << 3) & 0x7fff;
            unsigned ch = ram[ram_addr];
            if (ch == 0 || ch == ' ')
                ++spaces;
            else {
                while (newlines) {
                    putc('\n', fp);
                    --newlines;
                }
                while (spaces) {
                    putc(' ', fp);
                    --spaces;
                }
                putc(ch, fp);
            }
            ++mem_addr;
        }
        ++newlines;
    }
    if (newlines)
        putc('\n', fp);
}

/*
 * Character definitions for working with the bitmap modes copied
 * from the MOS 1.20 ROM.  As each characters is an 8x8 grid, the
 * whole of one characters will fit in a 64-bit integer.
 */

static const uint64_t charset[] = {
    /* SPC */ 0x0000000000000000, /*  !  */ 0x1818181818001800,
    /*  "  */ 0x6c6c6c0000000000, /*  #  */ 0x36367f367f363600,
    /*  $  */ 0x0c3f683e0b7e1800, /*  %  */ 0x60660c1830660600,
    /*  &  */ 0x386c6c386d663b00, /*  '  */ 0x0c18300000000000,
    /*  (  */ 0x0c18303030180c00, /*  )  */ 0x30180c0c0c183000,
    /*  *  */ 0x00187e3c7e180000, /*  +  */ 0x0018187e18180000,
    /*  ,  */ 0x0000000000181830, /*  -  */ 0x0000007e00000000,
    /*  .  */ 0x0000000000181800, /*  /  */ 0x00060c1830600000,
    /*  0  */ 0x3c666e7e76663c00, /*  1  */ 0x1838181818187e00,
    /*  2  */ 0x3c66060c18307e00, /*  3  */ 0x3c66061c06663c00,
    /*  4  */ 0x0c1c3c6c7e0c0c00, /*  5  */ 0x7e607c0606663c00,
    /*  6  */ 0x1c30607c66663c00, /*  7  */ 0x7e060c1830303000,
    /*  8  */ 0x3c66663c66663c00, /*  9  */ 0x3c66663e060c3800,
    /*  :  */ 0x0000181800181800, /*  ;  */ 0x0000181800181830,
    /*  <  */ 0x0c18306030180c00, /*  =  */ 0x00007e007e000000,
    /*  >  */ 0x30180c060c183000, /*  ?  */ 0x3c660c1818001800,
    /*  @  */ 0x3c666e6a6e603c00, /*  A  */ 0x3c66667e66666600,
    /*  B  */ 0x7c66667c66667c00, /*  C  */ 0x3c66606060663c00,
    /*  D  */ 0x786c6666666c7800, /*  E  */ 0x7e60607c60607e00,
    /*  F  */ 0x7e60607c60606000, /*  G  */ 0x3c66606e66663c00,
    /*  H  */ 0x6666667e66666600, /*  I  */ 0x7e18181818187e00,
    /*  J  */ 0x3e0c0c0c0c6c3800, /*  K  */ 0x666c7870786c6600,
    /*  L  */ 0x6060606060607e00, /*  M  */ 0x63777f6b6b636300,
    /*  N  */ 0x6666767e6e666600, /*  O  */ 0x3c66666666663c00,
    /*  P  */ 0x7c66667c60606000, /*  Q  */ 0x3c6666666a6c3600,
    /*  R  */ 0x7c66667c6c666600, /*  S  */ 0x3c66603c06663c00,
    /*  T  */ 0x7e18181818181800, /*  U  */ 0x6666666666663c00,
    /*  V  */ 0x66666666663c1800, /*  W  */ 0x63636b6b7f776300,
    /*  X  */ 0x66663c183c666600, /*  Y  */ 0x6666663c18181800,
    /*  Z  */ 0x7e060c1830607e00, /*  [  */ 0x7c60606060607c00,
    /*  \  */ 0x006030180c060000, /*  ]  */ 0x3e06060606063e00,
    /*  ^  */ 0x183c664200000000, /*  _  */ 0x00000000000000ff,
    /*  `  */ 0x1c36307c30307e00, /*  a  */ 0x00003c063e663e00,
    /*  b  */ 0x60607c6666667c00, /*  c  */ 0x00003c6660663c00,
    /*  d  */ 0x06063e6666663e00, /*  e  */ 0x00003c667e603c00,
    /*  f  */ 0x1c30307c30303000, /*  g  */ 0x00003e66663e063c,
    /*  h  */ 0x60607c6666666600, /*  i  */ 0x1800381818183c00,
    /*  j  */ 0x1800381818181870, /*  k  */ 0x6060666c786c6600,
    /*  l  */ 0x3818181818183c00, /*  m  */ 0x0000367f6b6b6300,
    /*  n  */ 0x00007c6666666600, /*  o  */ 0x00003c6666663c00,
    /*  p  */ 0x00007c66667c6060, /*  q  */ 0x00003e66663e0607,
    /*  r  */ 0x00006c7660606000, /*  s  */ 0x00003e603c067c00,
    /*  t  */ 0x30307c3030301c00, /*  u  */ 0x0000666666663e00,
    /*  v  */ 0x00006666663c1800, /*  w  */ 0x0000636b6b7f3600,
    /*  x  */ 0x0000663c183c6600, /*  y  */ 0x00006666663e063c,
    /*  z  */ 0x00007e0c18307e00, /*  {  */ 0x0c18187018180c00,
    /*  |  */ 0x1818180018181800, /*  }  */ 0x3018180e18183000,
    /*  ~  */ 0x316b460000000000, /* DEL */ 0xffffffffffffffff,
};

/*
 * Information about the screen modes.  This includes row, columns
 * and the pixel format.
 *
 * 0 = 1-bit  per pixel,  2 colours, modea 0, 3, 4, 6
 * 1 = 2-bits per pixel,  4 colours, modes 1 and 5
 * 2 = 4-bits per pixel, 16 colours, modes 2
 */

static const uint8_t mode_rows[8] = { 32, 32, 32, 25, 32, 32, 25, 25 };
static const uint8_t mode_cols[8] = { 80, 40, 20, 80, 40, 20, 40, 40 };
static const uint8_t mode_pfmt[8] = {  0,  1,  2,  0,  0,  1,  0,  0 };

/*
 * Function to process one byte of memory in a 2bbp, four colour mode
 * working out the equivalent monochrome pixels and shifting into the
 * set being accumulated for the whole character.
 */

static uint64_t textsave_fcbits(uint64_t chbits, uint_least8_t bgmask, uint_least32_t cell_addr)
{
    uint_least8_t byte = (ram[(cell_addr & 0x7FFF) | vidbank]) ^ bgmask;
    chbits = (chbits << 1) | ((byte & 0x88) ? 1 : 0);
    chbits = (chbits << 1) | ((byte & 0x44) ? 1 : 0);
    chbits = (chbits << 1) | ((byte & 0x22) ? 1 : 0);
    chbits = (chbits << 1) | ((byte & 0x11) ? 1 : 0);
    return chbits;
}

/*
 * Function to process one byte of memory in a 4bbp, sixteen colour
 * mode working out the equivalent monochrome pixels and shifting into
 * the set being accumulated for the whole character.
 */

static uint64_t textsave_scbits(uint64_t chbits, uint_least8_t bgmask, uint_least32_t cell_addr)
{
    uint_least8_t byte = (ram[(cell_addr & 0x7FFF) | vidbank]) ^ bgmask;
    chbits = (chbits << 1) | ((byte & 0xaa) ? 1 : 0);
    chbits = (chbits << 1) | ((byte & 0x55) ? 1 : 0);
    return chbits;
}

/* Non-teletext modes.
 *
 * This does the same scan over rows and columns as for teletext but
 * instead of reading the character from the screen memmory a bitmap
 * is built up representing the character cell on screen which is then
 * compared with the character set in the table above.
 */

static void textsave_bitmap(const char *filename, FILE *fp, uint_least16_t mem_addr)
{
    uint_least32_t cell_addr = (mem_addr << 3);
    uint_least8_t bpc = ram[0x34f];
    uint_least8_t mode = ram[0x355];
    uint_least8_t bgmask = ram[0x358];
    uint_least8_t rows = mode_rows[mode];
    uint_least8_t cols = mode_cols[mode];
    uint_least8_t pfmt = mode_pfmt[mode];
    log_debug("textsave: bpc=%d, mode=%d, bgmask=%02X, rows=%d, cols=%d, pfmt=%d\n", bpc, mode, bgmask, rows, cols, pfmt);

    uint_least8_t newlines = 0;
    for (int row = 0; row < rows; ++row) {
        uint_least8_t spaces = 0;
        for (int col = 0; col < cols; ++col) {
            uint_least32_t line_addr = cell_addr;
            if (line_addr & 0x8000)
                line_addr -= screenlen[scrsize];
            log_debug("textsave: col=%d, call_addr=%08X", col, line_addr);
            uint64_t chbits = 0;
            if (pfmt == 0) {
                for (int line = 0; line < 8; ++line) {
                    chbits = (chbits << 8) | ((ram[(line_addr & 0x7FFF) | vidbank]) ^ bgmask);
                    ++line_addr;
                }
            }
            else if (pfmt == 1) {
                for (int line = 0; line < 8; ++line) {
                    chbits = textsave_fcbits(chbits, bgmask, line_addr);
                    chbits = textsave_fcbits(chbits, bgmask, line_addr + 8);
                    ++line_addr;
                }
            }
            else {
                for (int line = 0; line < 8; ++line) {
                    chbits = textsave_scbits(chbits, bgmask, line_addr);
                    chbits = textsave_scbits(chbits, bgmask, line_addr + 8);
                    chbits = textsave_scbits(chbits, bgmask, line_addr + 16);
                    chbits = textsave_scbits(chbits, bgmask, line_addr + 24);
                    ++line_addr;
                }
            }
            log_debug("textsave: chbits=%016lx", chbits);
            int ch = ' ';
            for (int i = 0; i < 96; ++i) {
                if (chbits == charset[i]) {
                    ch = i + 32;
                    break;
                }
            }
            log_debug("textsave: ch=%02X '%c'", ch, ch);
            if (ch == ' ')
                ++spaces;
            else {
                while (newlines) {
                    putc('\n', fp);
                    --newlines;
                }
                while (spaces) {
                    putc(' ', fp);
                    --spaces;
                }
                putc(ch, fp);
            }
            cell_addr += bpc;
        }
        ++newlines;
    }
    if (newlines)
        putc('\n', fp);
}

/*
 * Main function.  This opens the file and then calls the
 * appropriate teletext or non-teletext function based on
 * the teletext bit in the Video ULA.
 */

void textsave(const char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (fp) {
        uint_least16_t mem_addr = crtc[13] | (crtc[12] << 8);            
        if (ula_ctrl & 2)
            textsave_teletext(filename, fp, mem_addr);
        else
            textsave_bitmap(filename, fp, mem_addr);
        fclose(fp);
    }
    else {
        log_error("unable to open file %s: %s\n", filename, strerror(errno));
    }
}
