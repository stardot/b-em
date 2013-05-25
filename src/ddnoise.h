void ddnoise_init();
void ddnoise_close();
void ddnoise_seek(int len);
void ddnoise_mix();
extern int ddnoise_vol;
extern int ddnoise_type;
SAMPLE *safe_load_wav(char *fn);
