extern void vdfs_init();
extern void vdfs_reset();
extern void vdfs_close();
extern uint8_t vdfs_read(uint16_t addr);
void vdfs_write(uint16_t addr, uint8_t value);

extern int vdfs_enabled;
extern const char *vdfs_get_root();
extern void vdfs_set_root(const char *dir);
