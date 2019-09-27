#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int status;

    if (argc > 1) {
        status = 0;
        while (--argc) {
            const char *fn = *++argv;
            FILE *fp = fopen(fn, "rb");
            if (fp) {
                unsigned char fsmap[0x200];
                if (fread(fsmap, sizeof fsmap, 1, fp) == 1) {
                    puts(fn);
                    unsigned char *sptr = fsmap;
                    unsigned char *eptr = fsmap + fsmap[0x1FE];
                    unsigned char *lptr = fsmap + 0x100;
                    while (sptr < eptr) {
                        uint32_t start = *sptr++;
                        start |= (*sptr++ << 8);
                        start |= (*sptr++ << 16);
                        uint32_t len = *lptr++;
                        len |= (*lptr++ << 8);
                        len |= (*lptr++ << 16);
                        if (len > 0)
                            printf("%06X %06X (%d)\n", start, len, len);
                    }
                }
                else {
                    fprintf(stderr, "fsmap: unable to read fsmap from %s: %s\n", fn, strerror(errno));
                    status = 3;
                }
            }
            else {
                fprintf(stderr, "fsmap: unable to open %s for reading: %s\n", fn, strerror(errno));
                status = 2;
            }
        }
    }
    else {
        fputs("Usage: fsmap <dat-file> [ ... ]\n", stderr);
        status = 1;
    }
    return status;
}
