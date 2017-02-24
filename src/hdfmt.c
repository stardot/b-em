#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIR_SECTORS     5
#define INITIAL_SECTORS (DIR_SECTORS+2)

static int parse_size(const char *size)
{
        int value;
        char *end;

        value = strtol(size, &end, 10);
        switch (*end) {
                case 'k':
                case 'K':
                        value *= 1024;
                        break;
                case 'm':
                case 'M':
                        value *= 1024 * 1024;
                        break;
                case 'g':
                case 'G':
                        value *= 1024 * 1024 * 1024;
                        break;
        }
        return value;
}

static inline void write16(uint8_t *base, uint32_t value)
{
        base[0] = value & 0xff;
        base[1] = (value >> 8) & 0xff;
}

static inline void write24(uint8_t *base, uint32_t value)
{
        write16(base, value);
        base[2] = (value >> 16) & 0xff;
}

static uint8_t checksum(uint8_t *base)
{
        int i = 255, c = 0;
        unsigned sum = 255;
        while (--i >= 0)
        {
                sum += base[i] + c;
                c = 0;
                if (sum >= 256) {
                        sum &= 0xff;
                        c = 1;
                }
        }
        return sum;
}

static void setup_freemap(uint8_t *base, int sectors)
{
        int free_sect = sectors - INITIAL_SECTORS;
        base[0x000] = INITIAL_SECTORS; // position of first free space.
        write24(base + 0x0fc, sectors);
        base[0x0ff] = checksum(base);
        putchar('\n');
        write24(base + 0x100, free_sect);
        write16(base + 0x1fb, rand()); // disc ID
        base[0x1fe] = 3;
        base[0x1ff] = checksum(base + 0x100);
}

static void setup_root_dir(uint8_t *base)
{
        strcpy((char *)(base+0x001), "Hugo");
        base[0x4cc] = '$';
        base[0x4d6] = 2;
        base[0x4d9] = '$';
        strcpy((char *)(base+0x4fb), "Hugo");
}

static int adfs_format(const char *fn, FILE *fp, int sectors)
{
        int status = 0;
        uint8_t *data = calloc(INITIAL_SECTORS, 256);
        if (data)
        {
                setup_freemap(data, sectors);
                setup_root_dir(data + 0x200);
                if (fwrite(data, 256, INITIAL_SECTORS, fp) != INITIAL_SECTORS)
                {
                       fprintf(stderr, "hdfmt: error writing to %s: %s\n", fn, strerror(errno));
                       status = 2;
                }
        }
        return status;
}

int main(int argc, char **argv)
{
        const char *fn;
        int status, len, size, sectors, cyl;
        char *dat_fn, *dsc_fn, geom[22];
        FILE *dat_fp, *dsc_fp;

        if (argc == 3)
        {
                size = parse_size(argv[2]);
                if (size > 0)
                {
                        fn = argv[1];
                        len = strlen(fn) + 5;
                        dat_fn = alloca(len);
                        snprintf(dat_fn, len, "%s.dat", fn);
                        dsc_fn = alloca(len);
                        snprintf(dsc_fn, len, "%s.dsc", fn);

                        if ((dat_fp = fopen(dat_fn, "w")))
                        {
                                if ((dsc_fp = fopen(dsc_fn, "w")))
                                {
                                        sectors = size / 256;
                                        cyl = 1 + ((sectors - 1) / (33 * 255));
                                        printf("size=%d, sectors=%d, cyl=%d\n", size, sectors, cyl);
                                        geom[13] = cyl % 256;
                                        geom[14] = cyl / 256;
                                        geom[15] = 255;
                                        if (fwrite(geom, sizeof geom, 1, dsc_fp) != 1)
                                        {
                                                fprintf(stderr, "hdfmt: unable to write to dsc file %s: %s\n", dsc_fn, strerror(errno));
                                                status = 1;
                                        }
                                        fclose(dsc_fp);

                                        status = adfs_format(dat_fn, dat_fp, sectors);
                                }
                                else
                                {
                                        fprintf(stderr, "hdfmt: unable to create dsc file %s: %s\n", dsc_fn, strerror(errno));
                                        status = 2;
                                }
                                fclose(dat_fp);
                        }
                        else
                        {
                                fprintf(stderr, "hdfmt: unable to create data file %s: %s\n", dat_fn, strerror(errno));
                                status = 2;
                        }
                }
                else
                {
                        fprintf(stderr, "hdfmt: %s is not a valid size\n", argv[1]);
                        status = 1;
                }
        }
        else
        {
                fputs("usage: hdfmt <dat-file> <size>\n", stderr);
                status = 1;
        }
        return status;
}
