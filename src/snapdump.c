#include <errno.h>
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

static void dump_one(char *hexout, const char *fn, FILE *fp)
{
    printf("Version 1 dump, model = %d\n", getc(fp));
    puts("6502 state");
    dump_hex(hexout, fn, fp, 13);
    puts("I/O processor memory");
    dump_hex(hexout, fn, fp, 327682);
    puts("System VIA state");
    dump_hex(hexout, fn, fp, 34);
    puts("User VIA state");
    dump_hex(hexout, fn, fp, 33);
    puts("Video ULA state");
    dump_hex(hexout, fn, fp, 97);
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
    bool compressed = false;
    switch(key) {
        case 'm':
            desc = "Model information";
            break;
        case '6':
            desc = "6502 state";
            break;
        case 'M':
            desc = "I/O processor memory";
            compressed = true;
            break;
        case 'S':
            desc = "System VIA state";
            break;
        case 'U':
            desc = "User VIA state";
            break;
        case 'V':
            desc = "Video ULA state";
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
    puts(desc);
    if (compressed)
        dump_compressed(hexout, fn, fp, size);
    else
        dump_hex(hexout, fn, fp, size);
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
