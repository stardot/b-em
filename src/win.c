/* B-em by Tom Walker, Allegro 5 port.
 *
 * Previously containing a Windows-specific main function this file now
 * B-Em specific functions whose definition varies between Windows and
 * Linux/Linux-like systems.
 */

#ifdef WIN32
#include "b-em.h"

static bool win_file_exists(const char *szPath) {
    DWORD dwAttrib = GetFileAttributes(szPath);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
            !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool find_dat_file(char *path, size_t psize, const char *subdir, const char *name, const char *ext) {
    ALLEGRO_PATH *respath = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    const char *cpath = al_path_cstr(respath, ALLEGRO_NATIVE_PATH_SEP);
    snprintf(path, psize, "%s/%s/%s.%s", cpath, subdir, name, ext);
    return win_file_exists(path);
}

bool find_cfg_file(char *path, size_t psize, const char *name, const char *ext) {
    ALLEGRO_PATH *setpath = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH);
    const char *cpath = al_path_cstr(setpath, ALLEGRO_NATIVE_PATH_SEP);
    snprintf(path, psize, "%s/%s.%s", cpath, name, ext);
    return win_file_exists(path);
}

bool find_cfg_dest(char *path, size_t psize, const char *name, const char *ext) {
    ALLEGRO_PATH *setpath = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH);
    const char *cpath = al_path_cstr(setpath, ALLEGRO_NATIVE_PATH_SEP);
    CreateDirectory(cpath, NULL);
    snprintf(path, psize, "%s/%s.%s", cpath, name, ext);
    return true;
}

#endif
