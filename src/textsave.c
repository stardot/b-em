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
    0x0000000000000000, /*  32 20 ' ' */
    0x1818181818001800, /*  33 21 '!' */
    0x6c6c6c0000000000, /*  34 22 '"' */
    0x36367f367f363600, /*  35 23 '#' */
    0x0c3f683e0b7e1800, /*  36 24 '$' */
    0x60660c1830660600, /*  37 25 '%' */
    0x386c6c386d663b00, /*  38 26 '&' */
    0x0c18300000000000, /*  39 27 ''' */
    0x0c18303030180c00, /*  40 28 '(' */
    0x30180c0c0c183000, /*  41 29 ')' */
    0x00187e3c7e180000, /*  42 2A '*' */
    0x0018187e18180000, /*  43 2B '+' */
    0x0000000000181830, /*  44 2C ',' */
    0x0000007e00000000, /*  45 2D '-' */
    0x0000000000181800, /*  46 2E '.' */
    0x00060c1830600000, /*  47 2F '/' */
    0x3c666e7e76663c00, /*  48 30 '0' */
    0x1838181818187e00, /*  49 31 '1' */
    0x3c66060c18307e00, /*  50 32 '2' */
    0x3c66061c06663c00, /*  51 33 '3' */
    0x0c1c3c6c7e0c0c00, /*  52 34 '4' */
    0x7e607c0606663c00, /*  53 35 '5' */
    0x1c30607c66663c00, /*  54 36 '6' */
    0x7e060c1830303000, /*  55 37 '7' */
    0x3c66663c66663c00, /*  56 38 '8' */
    0x3c66663e060c3800, /*  57 39 '9' */
    0x0000181800181800, /*  58 3A ':' */
    0x0000181800181830, /*  59 3B ';' */
    0x0c18306030180c00, /*  60 3C '<' */
    0x00007e007e000000, /*  61 3D '=' */
    0x30180c060c183000, /*  62 3E '>' */
    0x3c660c1818001800, /*  63 3F '?' */
    0x3c666e6a6e603c00, /*  64 40 '@' */
    0x3c66667e66666600, /*  65 41 'A' */
    0x7c66667c66667c00, /*  66 42 'B' */
    0x3c66606060663c00, /*  67 43 'C' */
    0x786c6666666c7800, /*  68 44 'D' */
    0x7e60607c60607e00, /*  69 45 'E' */
    0x7e60607c60606000, /*  70 46 'F' */
    0x3c66606e66663c00, /*  71 47 'G' */
    0x6666667e66666600, /*  72 48 'H' */
    0x7e18181818187e00, /*  73 49 'I' */
    0x3e0c0c0c0c6c3800, /*  74 4A 'J' */
    0x666c7870786c6600, /*  75 4B 'K' */
    0x6060606060607e00, /*  76 4C 'L' */
    0x63777f6b6b636300, /*  77 4D 'M' */
    0x6666767e6e666600, /*  78 4E 'N' */
    0x3c66666666663c00, /*  79 4F 'O' */
    0x7c66667c60606000, /*  80 50 'P' */
    0x3c6666666a6c3600, /*  81 51 'Q' */
    0x7c66667c6c666600, /*  82 52 'R' */
    0x3c66603c06663c00, /*  83 53 'S' */
    0x7e18181818181800, /*  84 54 'T' */
    0x6666666666663c00, /*  85 55 'U' */
    0x66666666663c1800, /*  86 56 'V' */
    0x63636b6b7f776300, /*  87 57 'W' */
    0x66663c183c666600, /*  88 58 'X' */
    0x6666663c18181800, /*  89 59 'Y' */
    0x7e060c1830607e00, /*  90 5A 'Z' */
    0x7c60606060607c00, /*  91 5B '[' */
    0x006030180c060000, /*  92 5C '\' */
    0x3e06060606063e00, /*  93 5D ']' */
    0x183c664200000000, /*  94 5E '^' */
    0x00000000000000ff, /*  95 5F '_' */
    0x1c36307c30307e00, /*  96 60 '`' */
    0x00003c063e663e00, /*  97 61 'a' */
    0x60607c6666667c00, /*  98 62 'b' */
    0x00003c6660663c00, /*  99 63 'c' */
    0x06063e6666663e00, /* 100 64 'd' */
    0x00003c667e603c00, /* 101 65 'e' */
    0x1c30307c30303000, /* 102 66 'f' */
    0x00003e66663e063c, /* 103 67 'g' */
    0x60607c6666666600, /* 104 68 'h' */
    0x1800381818183c00, /* 105 69 'i' */
    0x1800381818181870, /* 106 6A 'j' */
    0x6060666c786c6600, /* 107 6B 'k' */
    0x3818181818183c00, /* 108 6C 'l' */
    0x0000367f6b6b6300, /* 109 6D 'm' */
    0x00007c6666666600, /* 110 6E 'n' */
    0x00003c6666663c00, /* 111 6F 'o' */
    0x00007c66667c6060, /* 112 70 'p' */
    0x00003e66663e0607, /* 113 71 'q' */
    0x00006c7660606000, /* 114 72 'r' */
    0x00003e603c067c00, /* 115 73 's' */
    0x30307c3030301c00, /* 116 74 't' */
    0x0000666666663e00, /* 117 75 'u' */
    0x00006666663c1800, /* 118 76 'v' */
    0x0000636b6b7f3600, /* 119 77 'w' */
    0x0000663c183c6600, /* 120 78 'x' */
    0x00006666663e063c, /* 121 79 'y' */
    0x00007e0c18307e00, /* 122 7A 'z' */
    0x0c18187018180c00, /* 123 7B '{' */
    0x1818180018181800, /* 124 7C '|' */
    0x3018180e18183000, /* 125 7D '}' */
    0x316b460000000000, /* 126 7E '~' */
    0xffffffffffffffff, /* 127 7F     */
    0x66003c667e666600, /* 128 80     */
    0x6666003c667e6600, /* 128 80     */
    0x3c663c667e666600, /* 129 81     */
    0x3c663c3c667e6600, /* 129 81     */
    0x3f66667f66666700, /* 130 82     */
    0x3c66606060663c60, /* 131 83     */
    0x3c666060663c3060, /* 131 83     */
    0x0c187e607c607e00, /* 132 84     */
    0x663c666666663c00, /* 133 85     */
    0x6600666666663c00, /* 134 86     */
    0x7ec39db19dc37e00, /* 135 87     */
    0x3c4299a1a199423c, /* 135 87     */
    0x0018387f38180000, /* 136 88     */
    0x00181cfe1c180000, /* 137 89     */
    0x181818187e3c1800, /* 138 8A     */
    0x00183c7e18181818, /* 139 8B     */
    0x30183c063e663e00, /* 140 8C     */
    0x30183c667e603c00, /* 141 8D     */
    0x66003c667e603c00, /* 142 8E     */
    0x3c663c667e603c00, /* 143 8F     */
    0x66003c063e663e00, /* 144 90     */
    0x3c663c063e663e00, /* 145 91     */
    0x00003f0d3f6c3f00, /* 146 92     */
    0x00003c6660663c60, /* 147 93     */
    0x0c183c667e603c00, /* 148 94     */
    0x6600003c66663c00, /* 149 95     */
    0x6600666666663e00, /* 150 96     */
    0x6600006666663e00, /* 150 96     */
    0x3018003818183c00, /* 151 97     */
    0x3c66003818183c00, /* 152 98     */
    0x3018003c66663c00, /* 153 99     */
    0x3c66003c66663c00, /* 154 9A     */
    0x3018006666663e00, /* 155 9B     */
    0x3c66006666663e00, /* 156 9C     */
    0x66006666663e063c, /* 157 9D     */
    0x00663c66663c6600, /* 158 9E     */
    0x3c603c663c063c00, /* 159 9F     */
    0x3c663c0000000000, /* 160 A0     */
    0x0000001818181818, /* 161 A1     */
    0x0000001f00000000, /* 162 A2     */
    0x0000001f18181818, /* 163 A3     */
    0x000000f800000000, /* 164 A4     */
    0x000000f818181818, /* 165 A5     */
    0x000000ff00000000, /* 166 A6     */
    0x000000ff18181818, /* 167 A7     */
    0x1818181800000000, /* 168 A8     */
    0x1818181818181818, /* 169 A9     */
    0x1818181f00000000, /* 170 AA     */
    0x1818181f18181818, /* 171 AB     */
    0x181818f800000000, /* 172 AC     */
    0x181818f818181818, /* 173 AD     */
    0x181818ff00000000, /* 174 AE     */
    0x181818ff18181818, /* 175 AF     */
    0x000000070c181818, /* 176 B0     */
    0x000000e030181818, /* 177 B1     */
    0x18180c0700000000, /* 178 B2     */
    0x181830e000000000, /* 179 B3     */
    0x1800181830663c00, /* 180 B4     */
    0x1800181818181800, /* 181 B5     */
    0x366c0066766e6600, /* 182 B6     */
    0x366c007c66666600, /* 183 B7     */
    0x187e181818181800, /* 184 B8     */
    0x187e1818187e1800, /* 185 B9     */
    0x1818180000000000, /* 186 BA     */
    0x1800000000000000, /* 186 BA     */
    0x30180c0000000000, /* 187 BB     */
    0x3018000000000000, /* 187 BB     */
    0x3f7b7b3b1b1b1f00, /* 188 BC     */
    0x033e767636363e00, /* 188 BC     */
    0x0000001818000000, /* 189 BD     */
    0x03030606761c0c00, /* 190 BE     */
    0xaa55aa55aa55aa55, /* 191 BF     */
    0x3e63676b73633e00, /* 192 C0     */
    0x1c3663637f636300, /* 193 C1     */
    0x7e33333e33337e00, /* 194 C2     */
    0x7f63606060606000, /* 195 C3     */
    0x1c1c363663637f00, /* 196 C4     */
    0x7f33303e30337f00, /* 197 C5     */
    0x7e660c1830667e00, /* 198 C6     */
    0x7733333f33337700, /* 199 C7     */
    0x3e63637f63633e00, /* 200 C8     */
    0x3c18181818183c00, /* 201 C9     */
    0x63666c786c666300, /* 202 CA     */
    0x1c1c363663636300, /* 203 CB     */
    0x63777f6b63636300, /* 204 CC     */
    0x63737b6f67636300, /* 205 CD     */
    0x7e00003c00007e00, /* 206 CE     */
    0x3e63636363633e00, /* 207 CF     */
    0x7f36363636363600, /* 208 D0     */
    0x7e33333e30307800, /* 209 D1     */
    0x7f63301830637f00, /* 210 D2     */
    0x7e5a181818181800, /* 211 D3     */
    0x6666663c18183c00, /* 212 D4     */
    0x3e083e6b3e083e00, /* 213 D5     */
    0x6363361c36636300, /* 214 D6     */
    0x3e086b6b3e083e00, /* 215 D7     */
    0x3e63636336366300, /* 216 D8     */
    0x7f636336361c1c00, /* 217 D9     */
    0x18187e1818007e00, /* 218 DA     */
    0x007e0018187e1818, /* 219 DB     */
    0x1818181818181800, /* 220 DC     */
    0x3636363636363600, /* 221 DD     */
    0x0066666666663c00, /* 222 DE     */
    0x003c666666666600, /* 223 DF     */
    0x00033e676b733e60, /* 224 E0     */
    0x00023c6e7666bc00, /* 224 E0     */
    0x00003b6e666e3b00, /* 225 E1     */
    0x1e33333e33333e60, /* 226 E2     */
    0x000066361c183030, /* 227 E3     */
    0x3c60303c66663c00, /* 228 E4     */
    0x00001e301c301e00, /* 229 E5     */
    0x3e0c183060603e06, /* 230 E6     */
    0x00007c6666660606, /* 231 E7     */
    0x3c66667e66663c00, /* 232 E8     */
    0x0000181818180c00, /* 233 E9     */
    0x0000666c786c6600, /* 234 EA     */
    0x6030181c36636300, /* 235 EB     */
    0x0000333333333e60, /* 236 EC     */
    0x000063331b1e1c00, /* 237 ED     */
    0x3c60603c60603e06, /* 238 EE     */
    0x0c3e603c603e060c, /* 238 EE     */
    0x00003e6363633e00, /* 239 EF     */
    0x00007f3636363600, /* 240 F0     */
    0x00003c66667c6060, /* 241 F1     */
    0x00003f6666663c00, /* 242 F2     */
    0x00007e1818180c00, /* 243 F3     */
    0x0000733333331e00, /* 244 F4     */
    0x00003e6b6b3e1818, /* 245 F5     */
    0x000066361c1c3633, /* 246 F6     */
    0x0000636b6b3e1818, /* 247 F7     */
    0x000036636b7f3600, /* 248 F8     */
    0x000063636b7f3600, /* 248 F8     */
    0x380c063e66663c00, /* 249 F9     */
    0x00316b46007f0000, /* 250 FA     */
    0x007e007e007e0000, /* 251 FB     */
    0x071c701c07007f00, /* 252 FC     */
    0x060c7e187e306000, /* 253 FD     */
    0x701c071c70007f00, /* 254 FE     */
    0xffffffffffffffff, /* 255 FF     */
};

static const unsigned char charcodes[] =
{
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x80, 0x81, 0x81, 0x82, 0x83, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x87, 0x88, 0x89, 0x8a, 0x8b,
    0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93,
    0x94, 0x95, 0x96, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2,
    0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
    0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xba, 0xbb, 0xbb, 0xbc, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee,
    0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
    0xf6, 0xf7, 0xf8, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc,
    0xfd, 0xfe, 0xff
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
            log_debug("textsave: chbits=%016lx", (unsigned long)chbits);
            int ch = 0;
            for (int i = 0; i < sizeof(charcodes); ++i) {
                if (chbits == charset[i]) {
                    ch = charcodes[i];
                    break;
                }
            }
            if (!ch) {
                if (chbits == 0x66003c6666663c00) {
                    /* This bit pattern is includes in both the
                     * MOS 3.20 and MOS 3.50 character definitions,
                     * but with a different code in each so we need
                     * to work out which version of MOS is running.
                     */
                    uint_least8_t byte = rom[15*ROM_SIZE+0x3c29];
                    log_debug("textsave: byte=%02X", byte);
                    ch = byte ? 0x95 : 0x85;
                }
                else
                    ch = ' ';
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

