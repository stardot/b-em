/*
 * B-EM SDF - Simple Disk Formats - Testing
 *
 * This B-Em module is part of the handling of simple disc formats,
 * i.e. those where the sectors that comprise the disk image are
 * stored in the file in a logical order and without ID headers.
 *
 * This module contains a test harness which applies the geometry
 * recognition to a series of disc images so the results can be
 * compared with those expected.
 */

#include <allegro5/allegro.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdf.h"

void log_debug(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("DEBUG ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("WARN  ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
    va_end(ap);
}

int main(int argc, char **argv)
{
    int status;
    const char *fn, *ext;
    FILE *fp;
    const struct sdf_geometry  *geo;

    if (argc <= 1) {
        fputs("Usage: sdf-gtest <img-file> [ <img-file> ...]\n", stderr);
        status = 1;
    }
    else {
        status = 0;
        while (--argc) {
            fn = *++argv;
            if ((ext = strrchr(fn, '.')))
                ext++;
            if ((fp = fopen(fn, "rb"))) {
                if ((geo = sdf_find_geo(fn, ext, fp)))
                    printf("%s: %s, %s, %d tracks, %s, %d %d byte sectors/track\n",
                           fn, geo->name, sdf_desc_sides(geo), geo->tracks,
                           sdf_desc_dens(geo), geo->sectors_per_track, geo->sector_size);
                else
                    printf("%s: unknown geometry\n", fn);
                fclose(fp);
            }
            else {
                fprintf(stderr, "sdf-gtest: unable to open %s for reading: %s\n", fn, strerror(errno));
                status++;
            }
        }
    }
    return status;
}
