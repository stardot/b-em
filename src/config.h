#ifndef __INC_CONFIG_H
#define __INC_CONFIG_H

extern ALLEGRO_CONFIG *bem_cfg;

void config_load(void);
void config_save(void);

int get_config_int(const char *sect, const char *key, int idefault);
double get_config_float(const char *sect, const char *key, double fdefault);
bool get_config_bool(const char *sect, const char *key, bool bdefault);
const char *get_config_string(const char *sect, const char *key, const char *sdefault);
ALLEGRO_COLOR get_config_colour(const char *sect, const char *key, ALLEGRO_COLOR cdefault);
void set_config_int(const char *sect, const char *key, int value);
void set_config_hex(const char *sect, const char *key, unsigned value);
void set_config_bool(const char *sect, const char *key, bool value);
void set_config_string(const char *sect, const char *key, const char *value);

extern int curmodel;
extern int selecttube;

#endif
