#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define alloca _alloca
#endif

#define DIR_SECTORS             5
#define INITIAL_SECTORS         (DIR_SECTORS+2)

#define DSC_LEN                 22

#define SECTOR_SIZE             256
#define SECTORS_PER_TRACK       33
#define HEADS                   4

static int parse_size_and_adjust(const char *size)
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

        // adjust size of the created volume to match the expected geometry which is
        // a size evenly dividable as given below
        value = value - (value % (SECTOR_SIZE * SECTORS_PER_TRACK * HEADS));

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

                // fill the rest of the image with 0x00
                memset(data, 0, 256);
                for(int cs=INITIAL_SECTORS;cs<sectors;cs++)
                        fwrite(data,256,1,fp);
                free(data);
        }
        return status;
}

int main(int argc, char **argv)
{
        const char *fn;
        int status, len, size, sectors, cyl;
        char *dat_fn, *dsc_fn;
        unsigned char geom[DSC_LEN];
        FILE *dat_fp, *dsc_fp;

        // we don't want random stuff in the dsc-file
        memset(geom,0,DSC_LEN);

        if (argc == 3)
        {
                size = parse_size_and_adjust(argv[2]);
                if (size > 0)
                {
                        fn = argv[1];
                        len = strlen(fn) + 5;
                        dat_fn = alloca(len);
                        snprintf(dat_fn, len, "%s.dat", fn);
                        dsc_fn = alloca(len);
                        snprintf(dsc_fn, len, "%s.dsc", fn);

                        if ((dat_fp = fopen(dat_fn, "wb")))
                        {
                                if ((dsc_fp = fopen(dsc_fn, "wb")))
                                {
                                        sectors = size / SECTOR_SIZE;
                                        cyl = size / (SECTOR_SIZE * SECTORS_PER_TRACK * HEADS);
                                        geom[13] = (unsigned char) ((((unsigned short) cyl) & 0xFF00) >> 8);
                                        geom[14] = (unsigned char) (((unsigned short) cyl) & 0x00FF);
                                        geom[15] = (unsigned char) (((unsigned short) HEADS) & 0x00FF);

                                        fprintf(stdout,"hdfmt: size=%d sectors=%d sector_size=%d sectors_per_track=%d heads=%d cylinders=%d\n",
                                                size, sectors, SECTOR_SIZE, SECTORS_PER_TRACK, HEADS, cyl);

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
                        fprintf(stderr, "hdfmt: %s is not a valid size\n", argv[2]);
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
