#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static inline uint32_t read24(const unsigned char *base)
{
    return base[0] | (base[1] << 8) | (base[2] << 16);
}

static inline void write24(uint8_t *base, uint32_t value)
{
    base[0] = value & 0xff;
    base[1] = (value >> 8) & 0xff;
    base[2] = (value >> 16) & 0xff;
}

static uint8_t checksum(uint8_t *base)
{
    int i = 255, c = 0;
    unsigned sum = 255;
    while (--i >= 0) {
        sum += base[i] + c;
        c = 0;
        if (sum >= 256) {
            sum &= 0xff;
            c = 1;
        }
    }
    return sum;
}

int main(int argc, char **argv)
{
    int status;

    if (argc == 3) {
        int size = parse_size(argv[2]);
        if (size > 0) {
            const char *fn = argv[1];
            size_t len = strlen(fn) + 5;
            char *dat_fn = alloca(len);
            snprintf(dat_fn, len, "%s.dat", fn);
            FILE *dat_fp = fopen(dat_fn, "rb+");
            if (dat_fp) {
                unsigned char fsmap[512];
                if (fread(fsmap, sizeof fsmap, 1, dat_fp) == 1) {
                    if (fsmap[0x1FE] <= 3) {
                        status = 0;
                        char *dsc_fn = alloca(len);
                        snprintf(dsc_fn, len, "%s.dsc", fn);
                        FILE *dsc_fp = fopen(dsc_fn, "rb+");
                        if (dsc_fp) {
                            unsigned char geom[22];
                            if (fread(geom, sizeof geom, 1, dsc_fp) == 1) {
                                int new_sects = size / 256;
                                int old_sects = read24(fsmap + 0x0FC);
                                printf("hdresize: old sectors=%d, new_sects=%d\n", old_sects, new_sects);
                                if (new_sects > old_sects) {
                                    write24(fsmap + 0x0FC, new_sects);
                                    fsmap[0x0FF] = checksum(fsmap);
                                    unsigned char *ptr = fsmap + 0x100;
                                    write24(ptr, read24(ptr) + (new_sects - old_sects));
                                    fsmap[0x1ff] = checksum(ptr);
                                    int cyl = 1 + ((new_sects - 1) / (33 * 255));
                                    geom[13] = cyl % 256;
                                    geom[14] = cyl / 256;
                                    geom[15] = 255;
                                    if (fseek(dsc_fp, 0, SEEK_SET) == 0 && fwrite(geom, sizeof geom, 1, dsc_fp) == 1) {
                                        if (fseek(dat_fp, 0, SEEK_SET) == 0 && fwrite(fsmap, sizeof fsmap, 1, dat_fp) == 1) {
                                            status = 0;
                                            printf("hdresize: %s grown\n", fn);
                                        }
                                        else
                                        {
                                            fprintf(stderr, "hdresize: unable to write fsmap to %s: %s\n", dat_fn, strerror(errno));
                                            status = 8;
                                        }
                                    }
                                    else {
                                        fprintf(stderr, "hdresize: unable to write geometry to %s: %s\n", dsc_fn, strerror(errno));
                                        status = 7;
                                    }
                                }
                                else {
                                    fputs("hdresize: shrinking not implemented\n", stderr);
                                    status = 6;
                                }
                            }
                            else {
                                fprintf(stderr, "hdresize: unable to read geometry from %s: %s\n", dsc_fn, strerror(errno));
                                status = 5;
                            }
                        }
                        else {
                            fprintf(stderr, "hdresize: unable to open dsc file %s: %s\n", dsc_fn, strerror(errno));
                            status = 2;
                        }
                    }
                    else {
                        fprintf(stderr, "hdresize: %s has more than one free entry, compact before resizing\n", fn);
                        status = 4;
                    }
                }
                else {
                    fprintf(stderr, "hdresize: unable to read fsmap from %s: %s\n", dat_fn, strerror(errno));
                    status = 3;
                }
                fclose(dat_fp);
            }
            else {
                fprintf(stderr, "hdresize: unable to open data file %s: %s\n", dat_fn, strerror(errno));
                status = 2;
            }
        }
        else {
            fprintf(stderr, "hdresize: %s is not a valid size\n", argv[2]);
            status = 1;
        }
    }
    else {
        fputs("usage: hdresize <dat-file> <size>\n", stderr);
        status = 1;
    }
    return status;
}







