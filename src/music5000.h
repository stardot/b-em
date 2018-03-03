void music5000_init(ALLEGRO_EVENT_QUEUE *queue);
void music5000_loadstate(FILE *f);
void music5000_savestate(FILE *f);
void music5000_fillbuf(int16_t *buffer, int len);
void music5000_streamfrag(void);
void music5000_write(uint16_t addr, uint8_t val);
void music5000_reset(void);
