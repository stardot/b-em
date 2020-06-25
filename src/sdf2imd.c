#include <allegro5/allegro.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "sdf.h"

#if __GNUC__
#define printflike __attribute__((format (printf, 1, 2)))
#else
#define printflike
#endif

void log_error(const char *fmt, ...) printflike;
void log_warn(const char *fmt, ...)  printflike;
void log_debug(const char *fmt, ...) printflike;

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("sdf2imd: ERROR ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("sdf2imd: WARN  ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
    va_end(ap);
}

void log_debug(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("sdf2imd: WARN  ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
    va_end(ap);
}

static void imd_hdr(const char *sdf_fn, const struct sdf_geometry *geo, FILE *ifp)
{
    time_t now;
    time(&now);
    struct tm *tp = gmtime(&now);
    fprintf(ifp, "IMD 1.18: %02u/%02u/%04u %02u:%02u:%02u\r\n", tp->tm_mday, tp->tm_mon+1, tp->tm_year + 1900, tp->tm_hour, tp->tm_min, tp->tm_sec);
    fprintf(ifp, "Converted from %s, format %s\r\n%s, %d tracks, %s, %d %d byte sectors/track\r\n\x1a",
            sdf_fn, geo->name, sdf_desc_sides(geo), geo->tracks,
            sdf_desc_dens(geo), geo->sectors_per_track, geo->sector_size);
}

static const uint8_t modes[4] = { 0x02, 0x02, 0x05, 0x03 };

static bool copy_track(FILE *sfp, const struct sdf_geometry *geo, FILE *ifp, unsigned cyclinder, unsigned head, bool eof)
{
    log_debug("copy track cyl=%u, head=%u, eof=%u", cyclinder, head, eof);
    uint8_t buf[1025];
    buf[0] = modes[geo->density];
    buf[1] = cyclinder;
    buf[2] = head;
    buf[3] = geo->sectors_per_track;
    unsigned size = geo->sector_size;
    unsigned szcode = 0;
    while (size > 128) {
        size >>= 1;
        szcode++;
    }
    buf[4] = szcode;
    uint8_t *ptr = buf+5;
    unsigned base = (geo->sector_size > 256) ? 1 : 0;
    for (unsigned sect = 0; sect < geo->sectors_per_track; sect++)
        *ptr++ = base++;
    fwrite(buf, ptr-buf, 1, ifp);
    for (unsigned sect = 0; sect < geo->sectors_per_track; sect++) {
        if (!eof && fread(buf+1, geo->sector_size, 1, sfp) == 1) {
            bool compress = true;
            unsigned v = buf[1];
            for (ptr = buf+2; ptr < buf+geo->sector_size+1; ptr++) {
                if (*ptr != v) {
                    compress = false;
                    break;
                }
            }
            if (compress) {
                log_debug("writing compressed sector %u", sect);
                buf[0] = 0x02;
                fwrite(buf, 2, 1, ifp);
            }
            else {
                log_debug("writing full sector %u", sect);
                buf[0] = 0x01;
                fwrite(buf, geo->sector_size+1, 1, ifp);
            }
        }
        else {
            eof = true;
            putc(0, ifp);
        }
    }
    return eof;
}

static void sdf2imd(FILE *sfp, const struct sdf_geometry *geo, FILE *ifp)
{
    bool eof = false;
    fseek(sfp, 0, SEEK_SET);
    switch(geo->sides) {
        case SDF_SIDES_SINGLE:
            for (unsigned track = 0; track < geo->tracks; track++)
                eof = copy_track(sfp, geo, ifp, track, 0, eof);
            break;
        case SDF_SIDES_SEQUENTIAL:
            for (unsigned track = 0; track < geo->tracks; track++)
                eof = copy_track(sfp, geo, ifp, track, 0, eof);
            for (unsigned track = 0; track < geo->tracks; track++)
                eof = copy_track(sfp, geo, ifp, track, 1, eof);
            break;
        case SDF_SIDES_INTERLEAVED:
            for (unsigned track = 0; track < geo->tracks; track++) {
                eof = copy_track(sfp, geo, ifp, track, 0, eof);
                eof = copy_track(sfp, geo, ifp, track, 1, eof);
            }
    }
}

int main(int argc, char **argv)
{
    int status;

    if (argc > 1 && argc % 2) {
        status = 0;
        do {
            const char *sdf_fn = *++argv;
            const char *imd_fn = *++argv;
            const char *ext = strrchr(sdf_fn, '.');
            if (ext)
                ext++;
            FILE *sfp = fopen(sdf_fn, "rb");
            if (sfp) {
                const struct sdf_geometry *geo = sdf_find_geo(sdf_fn, ext, sfp);
                if (geo) {
                    FILE *ifp = fopen(imd_fn, "wb");
                    if (ifp) {
                        imd_hdr(sdf_fn, geo, ifp);
                        sdf2imd(sfp, geo, ifp);
                        fclose(ifp);
                    }
                    else {
                        log_error("unable to open '%s' for writing: %s", imd_fn, strerror(errno));
                        status = 4;
                    }
                }
                else {
                    log_error("unable to determine geometry of '%s'", sdf_fn);
                    status = 3;
                }
                fclose(sfp);
            }
            else {
                log_error("unable to open '%s' for reading: %s", sdf_fn, strerror(errno));
                status = 2;
            }
            argc -= 2;
        } while (argc > 1);
    }
    else {
        fputs("Usage: ssd2imd <ssd-file> <imd-file> [ ... ]\n", stderr);
        status = 1;
    }
    return status;
}
