#ifndef __INC_CONFIG_H
#define __INC_CONFIG_H

void config_load();
void config_save();

int get_config_int(const char *sect, const char *key, int idefault);
const char *get_config_string(const char *sect, const char *key, const char *sdefault);
void set_config_int(const char *sect, const char *key, int value);
void set_config_string(const char *sect, const char *key, const char *value);

extern int curmodel;
extern int selecttube;

#endif
