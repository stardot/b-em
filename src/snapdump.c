#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <zlib.h>

#define GROUP_SIZE  8
#define BLOCK_SIZE (GROUP_SIZE * 2)
#define OUT_SIZE (14+4*BLOCK_SIZE)

static const char xdigs[] = "0123456789ABCDEF";

static char *hex_group(const unsigned char *raw, unsigned bytes, char *out)
{
    if (bytes) {
        do {
            int ch = *raw++;
            *out++ = xdigs[(ch & 0xf0) >> 4];
            *out++ = xdigs[ch & 0x0f];
            out++;
        } while (--bytes);
    }
    return out;
}

static void rest_out(long offset, const unsigned char *raw, size_t bytes, char *out, size_t out_size)
{
    // Offset into data.

    char *outp = out + 7;
    while (outp >= out) {
        *outp-- = xdigs[offset & 0x0f];
        offset >>= 4;
    }

    // ASCII

    outp = out + 13 + BLOCK_SIZE*3;
    while (bytes--) {
        int ch = *raw++;
        if (ch < 0x20 || ch > 0x7e)
            ch = '.';
        *outp++ = ch;
    }

    // Output

    fwrite(out, out_size, 1, stdout);
}

static char *star_pad(size_t count, char *out)
{
    while (count--) {
        *out++ = '*';
        *out++ = '*';
        out++;
    }
    return out;
}

static void dump_hex(char *out, const char *fn, FILE *fp, size_t size)
{
    unsigned char raw[BLOCK_SIZE];

    // Whole blocks.

    long offset = 0;
    while (size >= BLOCK_SIZE) {
        if (fread(raw, BLOCK_SIZE, 1, fp) != 1) {
            fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
            return;
        }
        char *outp = hex_group(raw, GROUP_SIZE, out + 11);
        hex_group(raw + GROUP_SIZE, GROUP_SIZE, outp+1);
        rest_out(offset, raw, BLOCK_SIZE, out, OUT_SIZE);
        offset += BLOCK_SIZE;
        size -= BLOCK_SIZE;
    }

    // Partial end block.

    if (size > 0) {
        if (fread(raw, size, 1, fp) != 1) {
            fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
            return;
        }
        size_t out_size = 14 + 3*BLOCK_SIZE + size;
        out[out_size-1] = '\n';
        if (size >= GROUP_SIZE) {
            char *outp = hex_group(raw, GROUP_SIZE, out + 11);
            outp = hex_group(raw + GROUP_SIZE, size - GROUP_SIZE, outp+1);
            star_pad(BLOCK_SIZE - size, outp);
        }
        else {
            char *outp = hex_group(raw, size, out + 11);
            outp = star_pad(GROUP_SIZE - size, outp);
            star_pad(GROUP_SIZE, outp+1);
        }
        rest_out(offset, raw, size, out, out_size);
    }
}

static void dump_compressed(char *hexout, const char *fn, FILE *fp, long size)
{
    if (size > 0) {
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        inflateInit(&zs);
        zs.next_in = Z_NULL;
        zs.avail_in = 0;
        int res, flush;
        unsigned long offset = 0;
        unsigned char cbuf[BUFSIZ];
        unsigned char ubuf[BUFSIZ];
        zs.next_out = ubuf;
        zs.avail_out = BUFSIZ;

        do {
            size_t cchunk;
            if (size > BUFSIZ) {
                flush = Z_NO_FLUSH;
                cchunk = BUFSIZ;
            }
            else {
                flush = Z_FINISH;
                cchunk = size;
            }
            if (fread(cbuf, cchunk, 1, fp) != 1) {
                fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
                return;
            }
            zs.next_in = cbuf;
            zs.avail_in = cchunk;
            size -= cchunk;
            while ((res = inflate(&zs, flush)) == Z_OK || res == Z_STREAM_END) {
                size_t uchunk = BUFSIZ - zs.avail_out;
                const unsigned char *raw = ubuf;
                while (uchunk >= BLOCK_SIZE) {
                    char *outp = hex_group(raw, GROUP_SIZE, hexout + 11);
                    hex_group(raw + GROUP_SIZE, GROUP_SIZE, outp+1);
                    rest_out(offset, raw, BLOCK_SIZE, hexout, OUT_SIZE);
                    raw += BLOCK_SIZE;
                    offset += BLOCK_SIZE;
                    uchunk -= BLOCK_SIZE;
                }
                if (uchunk > 0)
                    memmove(ubuf, raw, uchunk);
                zs.next_out = ubuf + uchunk;
                zs.avail_out = BUFSIZ - uchunk;
                if (zs.avail_in == 0)
                    break;
            }
        } while (res == Z_OK && size);

        fprintf(stderr, "snapdump: finished main loop, res=%d\n", res);

        if (res == Z_STREAM_END) {
            size_t uchunk = BUFSIZ - zs.avail_out;
            fprintf(stderr, "snapdump: %ld bytes of uncompressed data at end\n", uchunk);
            if (uchunk > 0) {
                size_t out_size = 14 + 3*BLOCK_SIZE + uchunk;
                hexout[out_size-1] = '\n';
                if (uchunk >= GROUP_SIZE) {
                    char *outp = hex_group(ubuf, GROUP_SIZE, hexout + 11);
                    outp = hex_group(ubuf + GROUP_SIZE, uchunk - GROUP_SIZE, outp+1);
                    star_pad(BLOCK_SIZE - uchunk, outp);
                }
                else {
                    char *outp = hex_group(ubuf, uchunk, hexout + 11);
                    outp = star_pad(GROUP_SIZE - uchunk, outp);
                    star_pad(GROUP_SIZE, outp+1);
                }
                rest_out(offset, ubuf, uchunk, hexout, out_size);
            }
        }
    }
}

unsigned load_var(FILE *fp)
{
    unsigned var, lshift;
    int      ch;

    var = lshift = 0;
    while ((ch = getc(fp)) != EOF) {
        if (ch & 0x80) {
            var |= ((ch & 0x7f) << lshift);
            break;
        }
        var |= ch << lshift;
        lshift += 7;
    }
    return var;
}

void print_vstr(const char *fn, FILE *fp, const char *prompt)
{
    size_t len = load_var(fp);
    if (len) {
        printf("  %-11s ", prompt);
        char buf[256];
        while (len > sizeof(buf)) {
            if (fread(buf, sizeof(buf), 1, fp) != 1) {
                fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
                return;
            }
            fwrite(buf, sizeof(buf), 1, stdout);
            len -= sizeof(buf);
        }
        if (fread(buf, len, 1, fp) != 1) {
            fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
            return;
        }
        buf[len++] = '\n';
        fwrite(buf, len, 1, stdout);
    }
    else
        printf("  %-11s <null>\n", prompt);
}

static void print_bool(int value, const char *label)
{
    printf("  %-11s %s\n", label, value ? "Yes" : "No");
}

static void dump_model(const char *fn, FILE *fp)
{
    printf("Model information\n  Model num:  %d\n", load_var(fp));
    print_vstr(fn, fp, "Model name:");
    print_vstr(fn, fp, "OS");
    print_vstr(fn, fp, "CMOS");
    print_vstr(fn, fp, "ROM setup");
    print_vstr(fn, fp, "FDC type");
    unsigned char bytes[7];
    fread(bytes, sizeof(bytes), 1, fp);
    print_bool(bytes[0] & 0x01, "65C02:");
    print_bool(bytes[0] & 0x80, "Integra:");
    print_bool(bytes[1], "B+:");
    print_bool(bytes[2], "Master:");
    print_bool(bytes[3], "Model A:");
    print_bool(bytes[4], "OS 0.1:");
    print_bool(bytes[5], "Compact:");
    if (bytes[6] == 1 || bytes[6] == 2)
        print_vstr(fn, fp, "Tube:");
}

static void dump_6502(const unsigned char *data)
{
    uint_least16_t pc = data[5] | (data[6] << 8);
    uint_least32_t cycles = data[9] | (data[10] << 8) | (data[11] << 16) | (data[12] << 24);
    unsigned flags = data[3];
    printf("6502 state:\n  PC=%04X A=%02X X=%02X Y=%02X S=%02X Flags=%c%c%c%c%c%c%c NMI=%d IRQ=%d cycles=%d\n",
           pc, data[0], data[1], data[2], data[4], flags & 0x80 ? 'N' : '-',
           flags & 0x40 ? 'V' : '-', flags & 0x10 ? 'B' : '-',
           flags & 0x08 ? 'D' : '-', flags & 0x04 ? 'I' : '-',
           flags & 0x02 ? 'Z' : '-', flags & 0x01 ? 'C' : '-',
           data[7], data[8], cycles);
}

static void dump_via(const unsigned char *data)
{
    uint_least32_t t1l = data[13] | (data[14] << 8) | (data[15] << 15) | (data[16] << 24);
    uint_least32_t t2l = data[17] | (data[18] << 8) | (data[19] << 15) | (data[20] << 24);
    uint_least32_t t1c = data[21] | (data[22] << 8) | (data[23] << 15) | (data[24] << 24);
    uint_least32_t t2c = data[25] | (data[26] << 8) | (data[27] << 15) | (data[28] << 24);
    printf("  ORA=%02X IRA=%02X INA=%02X DDRA=%02X\n"
           "  ORB=%02X IRB=%02X INB=%02X DDRB=%02X\n"
           "  SR=%02X ACR=%02X PCR=%02X IFR=%02X IER=%02X\n"
           "  T1L=%04X (%d) T1C=%04X (%d)\n"
           "  T2L=%04X (%d) T2C=%04X (%d)\n"
           "  t1hit=%d t2hit=%d ca1=%d ca2=%d\n",
           data[0], data[2], data[4], data[6], data[1], data[3], data[5],
           data[7], data[8], data[9], data[10], data[11], data[12],
           t1l, t1l, t1c, t1c, t2l, t2l, t2c, t2c,
           data[29], data[30], data[31], data[32]);
}

static void dump_sysvia(const unsigned char *data)
{
    fputs("System VIA state:\n", stdout);
    dump_via(data);
    printf("  IC32=%02X\n", data[33]);
}

static void dump_uservia(const unsigned char *data)
{
    fputs("User VIA state:\n", stdout);
    dump_via(data);
}

static const char *nula_names[] = {
    "palette mode",
    "horizontal offset",
    "left blank",
    "disable",
    "attribute mode",
    "attribute text"
};

static void dump_vula(const unsigned char *data)
{
    printf("Video ULA state:\n  ULA CTRL=%02X\n  Original Palette:\n", data[0]);
    for (int c = 0; c < 4; ++c) {
        unsigned v1 = data[c+1];
        unsigned v2 = data[c+5];
        unsigned v3 = data[c+9];
        unsigned v4 = data[c+13];
        printf("    %2d: %02X (%3d)  %2d: %02X (%3d)  %2d: %02X (%3d)  %2d: %02X (%3d)\n", c, v1, v1, c+4, v2, v2, c+8, v3, v3, c+12, v4, v4);
    }
    fputs("  NuLA palette (RGBA):\n", stdout);
    const unsigned char *ptr = data+14;
    for (int c= 0; c < 16; ++c) {
        uint_least32_t v = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        printf("    %2d: %08X %4d,%4d,%4d,%4d\n", c, v, ptr[0], ptr[1], ptr[2], ptr[3]);
        ptr += 4;
    }
    printf("  NuLA palette write flag: %02X\n  NuLA palette first byte: %02X\n  NuLA flash:", ptr[0], ptr[1]);
    ptr += 2;
    for (int c = 0; c < 8; ++c)
        printf(" %02X", *ptr++);
    putchar('\n');
    for (int c = 0; c < 6; ++c) {
        unsigned v = *ptr++;
        printf("  NuLA %s: %02X (%u)\n", nula_names[c], v, v);
    }
}

static void small_section(const char *fn, FILE *fp, size_t size, void (*func)(const unsigned char *data))
{
    unsigned char data[256];
    if (size > sizeof(data))
        fprintf(stderr, "snapdump: in %s, section of %ld bytes too big\n", fn, size);
    else if (fread(data, size, 1, fp) == 1)
        func(data);
    else
        fprintf(stderr, "snapdump: unexpected EOF on %s\n", fn);
}

static void dump_one(char *hexout, const char *fn, FILE *fp)
{
    printf("Version 1 dump, model = %d\n", getc(fp));
    small_section(fn, fp, 13, dump_6502);
    puts("I/O processor memory");
    dump_hex(hexout, fn, fp, 327682);
    small_section(fn, fp, 34, dump_sysvia);
    small_section(fn, fp, 33, dump_uservia);
    small_section(fn, fp, 97, dump_vula);
    puts("CRTC state");
    dump_hex(hexout, fn, fp, 25);
    puts("Other video state");
    dump_hex(hexout, fn, fp, 9);
    puts("Sound ship state");
    dump_hex(hexout, fn, fp, 55);
    puts("ADC state");
    dump_hex(hexout, fn, fp, 5);
    puts("ACIA state");
    dump_hex(hexout, fn, fp, 2);
    puts("Serial state");
    dump_hex(hexout, fn, fp, 1);
/*
    vdfs_loadstate(fp);
    music5000_loadstate(fp);}*/
}

static void dump_section(char *hexout, const char *fn, FILE *fp, int key, long size)
{
    long start = ftell(fp);
    const char *desc = "unknown section";
    bool compressed = false, done = false;
    switch(key) {
        case 'm':
            dump_model(fn, fp);
            done = true;
            break;
        case '6':
            small_section(fn, fp, size, dump_6502);
            done = true;
            break;
        case 'M':
            desc = "I/O processor memory";
            compressed = true;
            break;
        case 'S':
            small_section(fn, fp, size, dump_sysvia);
            done = true;
            break;
        case 'U':
            small_section(fn, fp, size, dump_uservia);
            done = true;
            break;
        case 'V':
            small_section(fn, fp, size, dump_vula);
            done = true;
            break;
        case 'C':
            desc = "CRTC state";
            break;
        case 'v':
            desc = "Other video state";
            break;
        case 's':
            desc = "Sound ship state";
            break;
        case 'A':
            desc = "ADC state";
            break;
        case 'a':
            desc = "ACIA state";
            break;
        case 'r':
            desc = "Serial state";
            break;
        case 'F':
            desc = "VDFS filing system state";
            break;
        case '5':
            desc = "Music 5000";
            break;
        case 'T':
            desc = "Tube ULA";
            break;
        case 'P':
            desc = "Tube Processor";
            compressed = true;
            break;
        case 'p':
            desc = "Paula Sound";
            break;
        case 'j':
            desc = "JIM memory";
            compressed = true;
    }
    if (!done) {
        puts(desc);
        if (compressed)
            dump_compressed(hexout, fn, fp, size);
        else
            dump_hex(hexout, fn, fp, size);
    }
    fseek(fp, start+size, SEEK_SET);
}

static void dump_two(char *hexout, const char *fn, FILE *fp)
{
    unsigned char hdr[4];

    while (fread(hdr, sizeof hdr, 1, fp) == 1) {
        long size = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16);
        dump_section(hexout, fn, fp, hdr[0], size);
    }
}

static void dump_three(char *hexout, const char *fn, FILE *fp)
{
    unsigned char hdr[3];

    while (fread(hdr, sizeof hdr, 1, fp) == 1) {
        int key = hdr[0];
        long size = hdr[1] | (hdr[2] << 8);
        if (key & 0x80) {
            if (fread(hdr, 2, 1, fp) != 1) {
                fprintf(stderr, "snapdump: unexpected EOF on file %s", fn);
                return;
            }
            size |= (hdr[0] << 16) | (hdr[1] << 24);
            key &= 0x7f;
        }
        dump_section(hexout, fn, fp, key, size);
    }
}

static bool snapdump(char *hexout, const char *fn)
{
    FILE *fp = fopen(fn, "rb");
    if (fp) {
        char magic[8];
        if (fread(magic, 8, 1, fp) == 1 && memcmp(magic, "BEMSNAP", 7) == 0) {
            switch(magic[7]) {
                case '1':
                    dump_one(hexout, fn, fp);
                    break;
                case '2':
                    dump_two(hexout, fn, fp);
                    break;
                case '3':
                    dump_three(hexout, fn, fp);
                    break;
                default:
                    fprintf(stderr, "snapdump: file %s: unrecognised B-Em snapshot file version %c\n", fn, magic[7]);
            }
        }
        else
            fprintf(stderr, "snapdump: file %s is not a B-Em snapshot file", fn);
        fclose(fp);
        return true;
    }
    fprintf(stderr, "snapdump: unable to open file %s for reading: %s\n", fn, strerror(errno));
    return false;
}

int main(int argc, char **argv)
{
    if (--argc) {
        int status = 1;
        char hexout[OUT_SIZE];
        memset(hexout, ' ', OUT_SIZE);
        hexout[9] = '-';
        hexout[OUT_SIZE-1] = '\n';

        while (argc--) {
            if (!snapdump(hexout, *++argv))
                status = 2;
        }
        return status;
    }
    else {
        fputs("Usage: snapdump [file] ...\n", stderr);
        return 1;
    }
}
