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

static bool try_dat_file(ALLEGRO_PATH *path, const char *subdir1, const char *subdir2, const char *name, const char *ext)
{
    const char *cpath;

    al_append_path_component(path, "b-em");
    if (subdir1)
        al_append_path_component(path, subdir1);
    if (subdir2)
        al_append_path_component(path, subdir2);
    al_set_path_filename(path, name);
    al_set_path_extension(path, ext);
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    log_debug("linux: looking for dat file %s at %s", name, cpath);
    return access(cpath, R_OK) == 0;
}

ALLEGRO_PATH *find_dat_file(const char *subdir1, const char *subdir2, const char *name, const char *ext)
{
    ALLEGRO_PATH *path;
    const char *var;
    char *cpy, *ptr, *sep;

    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
        if (try_dat_file(path, subdir1, subdir2, name, ext))
            return path;
        al_destroy_path(path);
    }
    if ((var = getenv("XDG_DATA_HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            if (try_dat_file(path, subdir1, subdir2, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            al_append_path_component(path, ".local");
            al_append_path_component(path, "share");
            if (try_dat_file(path, subdir1, subdir2, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("XDG_DATA_DIRS")) == NULL)
        var = "/usr/local/share:/usr/share";
    if ((cpy = strdup(var))) {
        for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
            *sep++ = '\0';
            if ((path = al_create_path_for_directory(ptr))) {
                if (try_dat_file(path, subdir1, subdir2, name, ext)) {
                    free(cpy);
                    return path;
                }
                al_destroy_path(path);
            }
        }
        path = al_create_path_for_directory(ptr);
        free(cpy);
        if (path) {
            if (try_dat_file(path, subdir1, subdir2, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    return NULL;
}

static bool try_cfg_file(ALLEGRO_PATH *path, const char *name, const char *ext)
{
    const char *cpath;

    al_append_path_component(path, "b-em");
    al_set_path_filename(path, name);
    al_set_path_extension(path, ext);
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    log_debug("linux: looking for cfg file %s at %s", name, cpath);
    return access(cpath, R_OK) == 0;
}

ALLEGRO_PATH *find_cfg_file(const char *name, const char *ext) {
    ALLEGRO_PATH *path;
    const char *var;
    char *cpy, *ptr, *sep;

    if ((var = getenv("XDG_CONFIG_HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            if (try_cfg_file(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            al_append_path_component(path, ".config");
            if (try_cfg_file(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("XDG_CONFIG_DIRS"))) {
        if ((cpy = strdup(var))) {
            for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
                *sep++ = '\0';
                if ((path = al_create_path_for_directory(ptr))) {
                    if (try_cfg_file(path, name, ext)) {
                        free(cpy);
                        return path;
                    }
                    al_destroy_path(path);
                }
            }
            path = al_create_path_for_directory(ptr);
            free(cpy);
            if (path) {
                if (try_cfg_file(path, name, ext))
                    return path;
                al_destroy_path(path);
            }
        }
    }
    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
        if (try_cfg_file(path, name, ext))
            return path;
        al_destroy_path(path);
    }
    if ((var = getenv("XDG_DATA_HOME"))) {
        al_append_path_component(path, ".config");
        if ((path = al_create_path_for_directory(var))) {
            if (try_cfg_file(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("XDG_DATA_DIRS")) == NULL)
        var = "/usr/local/share:/usr/share";
    if ((cpy = strdup(var))) {
        for (ptr = cpy; (sep = strchr(ptr, ':')); ptr = sep) {
            *sep++ = '\0';
            if ((path = al_create_path_for_directory(ptr))) {
                if (try_cfg_file(path, name, ext)) {
                    free(cpy);
                    return path;
                }
                al_destroy_path(path);
            }
        }
        path = al_create_path_for_directory(ptr);
        free(cpy);
        if (path) {
            if (try_cfg_file(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    return NULL;
}

static bool try_cfg_dest(ALLEGRO_PATH *path, const char *name, const char *ext)
{
    const char *cpath;
    struct stat stb;

    al_append_path_component(path, "b-em");
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    log_debug("linux: trying cfg dest dir %s", cpath);
    if (stat(cpath, &stb) == 0) {
        if ((stb.st_mode & S_IFMT) == S_IFDIR) {
            al_set_path_filename(path, name);
            al_set_path_extension(path, ext);
            return true;
        }
        else if (errno == ENOENT) {
            if (mkdir(cpath, 0777) == 0) {
                al_set_path_filename(path, name);
                al_set_path_extension(path, ext);
                return true;
            }
        }
    }
    return false;
}

ALLEGRO_PATH *find_cfg_dest(const char *name, const char *ext)
{
    ALLEGRO_PATH *path;
    const char *var;

    if ((var = getenv("XDG_CONFIG_HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            if (try_cfg_dest(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    if ((var = getenv("HOME"))) {
        if ((path = al_create_path_for_directory(var))) {
            al_append_path_component(path, ".config");
            if (try_cfg_dest(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }
    return false;
}

#endif
