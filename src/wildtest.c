#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <fnmatch.h>

#define MAX_FILE_NAME 12

static const char files[][MAX_FILE_NAME] = {
    "bbcclp",       "bbczip",       "education",    "ELIPSE",
    "FILE1",        "file101",      "FILE11",       "file121",
    "FILE2",        "FILE21",       "file3",        "file4",
    "INTEGRA",      "kermit",       "mu18",         "music",
    "pascal",       "roms",         "shakespeare",  "tst1lst",
    "tube",         "view",         "welcome",      "x86basic",
    "xedit",        "z80"
};

#define MAX_PATTERN 8

static const char patterns[][MAX_PATTERN] = {
    "*",            "bbc*",         "*1",           "ed*t*",
    "*8*",          "file#",        "file##",       "###clp",
    "file1#1",      "kermit",       "file2%"
};

static bool debug = false;

static void log_debug(const char *fmt, ...)
{
    if (debug) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        putc('\n', stderr);
    }
}

static bool vdfs_wildmat(const char *pattern, const char *candidate)
{
    log_debug("vdfs: vdfs_wildmat, pattern=%s, candidate=%s", pattern, candidate);
    for (;;) {
        int pat_ch = *pattern++;
        if (pat_ch == '*') {
            if (!*pattern) {
                log_debug("vdfs: vdfs_wildmat return#1, true, * matches nothing");
                return true;
            }
            do {
                if (vdfs_wildmat(pattern, candidate++)) {
                    log_debug("vdfs: vdfs_wildmat return#2, true, * recursive");
                    return true;
                }
            } while (*candidate);
            log_debug("vdfs: vdfs_wildmat return#3, false, * mismatch");
            return false;
        }
        int can_ch = *candidate++;
        if (can_ch) {
            if (pat_ch != can_ch && pat_ch != '#') {
                if (pat_ch >= 'a' && pat_ch <= 'z')
                    pat_ch = pat_ch - 'a' + 'A';
                if (can_ch >= 'a' && can_ch <= 'z')
                    can_ch = can_ch - 'a' + 'A';
                if (can_ch != pat_ch) {
                    log_debug("vdfs: vdfs_wildmat return#4, character mismatch");
                    return false;
                }
            }
        }
        else if (pat_ch) {
            log_debug("vdfs: vdfs_wildmat return#5, false, candidate too short");
            return false;
        }
        else {
            log_debug("vdfs: vdfs_wildmat return#6, true, NUL on both");
            return true;
        }
    }
    log_debug("vdfs: vdfs_wildmat return#7, true, reached max length");
    return true;
}

static void patxfm(const char pattern[MAX_PATTERN], char dest[MAX_PATTERN])
{
    for (int c = 0; c < MAX_PATTERN; ++c) {
        int ch = pattern[c];
        if (!ch) {
            dest[c] = 0;
            break;
        }
        if (ch == '#')
            ch = '?';
        else if (ch == '?')
            ch = '#';
        else if (ch >= 'a' && ch <= 'z')
            ch = ch + 'A' - 'a';
        dest[c] = ch;
    }
}

static void filexfm(const char file[MAX_FILE_NAME], char dest[MAX_FILE_NAME])
{
    for (int f = 0; f < MAX_FILE_NAME; ++f) {
        int ch = file[f];
        if (!ch) {
            dest[f] = 0;
            break;
        }
        if (ch >= 'a' && ch <= 'z')
            ch = ch + 'A' - 'a';
        dest[f] = ch;
    }
}

int main(int argc, char **argv)
{
    const char *arg = argv[1];
    if (arg && arg[0] == '-' && arg[1] == 'd')
        debug = true;
    for (int p = 0; p < sizeof(patterns)/sizeof(patterns[0]); ++p) {
        for (int f = 0; f < sizeof(files)/sizeof(files[0]); ++f) {
            bool res = vdfs_wildmat(patterns[p], files[f]);
            char xpat[MAX_PATTERN], xfile[MAX_FILE_NAME];
            patxfm(patterns[p], xpat);
            filexfm(files[f], xfile);
            if (!fnmatch(xpat, xfile, 0)) {
                if (!res)
                    printf("unexpected mis-match of %s against %s\n", patterns[p], files[f]);
            }
            else {
                if (res)
                    printf("unexpected match of %s against %s\n", patterns[p], files[f]);
            }
        }
    }
}
