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

int winsizex, winsizey;
int videoresize = 0;

int mousecapture = 0;

void setejecttext(int drive, const char *fn) {};

int windx, windy;
void updatewindowsize(int x, int y)
{
    x=(x+3)&~3; y=(y+3)&~3;
    if (x<128) x=128;
    if (y<64)  y=64;
    if (windx!=x || windy!=y)
    {
        ALLEGRO_DISPLAY *display = al_get_current_display();
        windx=winsizex=x; windy=winsizey=y;
        al_resize_display(display, x, y);
        scr_x_start = 0;
        scr_y_start = 0;
        scr_x_size = x;
        scr_y_size = y;
    }
}

void setquit()
{
    quitting=1;
}
//#undef print

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

int main(int argc, char *argv[])
{
    if (!al_init()) {
        fputs("Failed to initialise Allegro!\n", stderr);
        exit(1);
    }
    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);

    config_load();
    main_init(argc, argv);
    main_run();
#if 0
        al_get_keyboard_state(&keystate);
        if (al_key_down(&keystate, ALLEGRO_KEY_ALT) && al_key_down(&keystate, ALLEGRO_KEY_ENTER) && fullscreen && !oldf) {
            fullscreen = 0;
            video_leavefullscreen();
        }
        else if (al_key_down(&keystate, ALLEGRO_KEY_ALT) && al_key_down(&keystate, ALLEGRO_KEY_ENTER) && !fullscreen && !oldf) {
            fullscreen = 1;
            video_enterfullscreen();
        }
        oldf = al_key_down(&keystate, ALLEGRO_KEY_ALT) && al_key_down(&keystate, ALLEGRO_KEY_ENTER);
#endif
	main_close();
	return 0;
}

void log_msgbox(const char *level, char *msg)
{
    const int max_len = 80;
    char *max_ptr, *new_split, *cur_split;
    ALLEGRO_DISPLAY *display;

    display = al_get_current_display();
    if (strlen(msg) < max_len)
        al_show_native_message_box(display, level, msg, "", NULL, 0);
    else
    {
        max_ptr = msg + max_len;
        cur_split = msg;
        while ((new_split = strchr(cur_split+1, ' ')) && new_split < max_ptr)
            cur_split = new_split;
        
        if (cur_split > msg)
        {
            *cur_split = '\0';
            al_show_native_message_box(display, level, msg, cur_split+1, NULL, 0);
            *cur_split = ' ';
        }
        else
            al_show_native_message_box(display, level, msg, "", NULL, 0);
    }
}

#endif
