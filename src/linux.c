/*B-em v2.2 by Tom Walker
  Linux main*/

#ifndef WIN32
#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "config.h"
#include "linux-gui.h"
#include "main.h"
#include "video_render.h"

#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

void setejecttext(int drive, const char *fn) {};

static int try_file(char *path, size_t psize, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(path, psize, fmt, ap);
    va_end(ap);
    log_debug("linux: trying file %s", path);
    return access(path, R_OK);
}

int find_dat_file(char *path, size_t psize, const char *subdir, const char *name, const char *ext) {
    const char *var, *ptr, *sep;

    if (!try_file(path, psize, "%s/%s.%s", subdir, name, ext))
        return 0;
    if ((var = getenv("XDG_DATA_HOME"))) {
        if (!try_file(path, psize, "%s/b-em/%s/%s.%s", var, subdir, name, ext))
            return 0;
    } else if ((var = getenv("HOME"))) {
        if (!try_file(path, psize, "%s/.local/share/b-em/%s/%s.%s", var, subdir, name, ext))
            return 0;
    }
    if ((var = getenv("XDG_DATA_DIRS")) == NULL)
        var = "/usr/local/share:/usr/share";
    for (ptr = var; (sep = strchr(ptr, ':')); ptr = sep+1)
        if (!try_file(path, psize, "%.*s/b-em/%s/%s.%s", (int)(sep-ptr), ptr, subdir, name, ext))
            return 0;
    return try_file(path, psize, "%s/b-em/%s/%s.%s", ptr, subdir, name, ext);
}

int find_cfg_file(char *path, size_t psize, const char *name, const char *ext) {
    const char *var;

    if ((var = getenv("XDG_CONFIG_HOME")))
        if (!try_file(path, psize, "%s/b-em/%s.%s", var, name, ext))
            return 0;
    if ((var = getenv("HOME")))
        if (!try_file(path, psize, "%s/.config/b-em/%s.%s", var, name, ext))
            return 0;
    return 1;
}

static int try_dest(char *path, size_t psize, const char *name, const char *ext, const char *fmt, ...) {
    va_list ap;
    size_t len;
    struct stat stb;
    char *npath;

    va_start(ap, fmt);
    len = vsnprintf(path, psize, fmt, ap);
    va_end(ap);
    if (len < psize) {
        log_debug("linux: trying dest dir %s", path);
        psize -= len;
        npath = path + len;
        if (stat(path, &stb) == 0) {
            if ((stb.st_mode & S_IFMT) == S_IFDIR) {
                snprintf(npath, psize, "/%s.%s", name, ext);
                return 0;
            }
        } else if (errno == ENOENT) {
            if (mkdir(path, 0777) == 0) {
                snprintf(npath, psize, "/%s.%s", name, ext);
                return 0;
            }
        }
    }
    return 1;
}

int find_cfg_dest(char *path, size_t psize, const char *name, const char *ext) {
    const char *var;

    if ((var = getenv("XDG_CONFIG_HOME")))
        if (!try_dest(path, psize, name, ext, "%s/b-em", var))
            return 0;
    if ((var = getenv("HOME")))
        if (!try_dest(path, psize, name, ext, "%s/.config/b-em", var))
            return 0;
    return 1;
}

#endif
