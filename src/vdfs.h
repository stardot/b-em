#ifndef __INCLUDE_VDFS_H__
#define __INCLUDE_VDFS_H__

extern void vdfs_init(const char *root, const char *dir);
extern void vdfs_reset(void);
extern void vdfs_close(void);
extern uint8_t vdfs_read(uint16_t addr);
extern void vdfs_write(uint16_t addr, uint8_t value);

extern bool vdfs_enabled;
extern const char *vdfs_get_root(void);
extern void vdfs_set_root(const char *dir);

extern void vdfs_loadstate(FILE *f);
extern void vdfs_savestate(FILE *f);

extern void vdfs_error(const char *msg);
extern bool vdfs_wildmat(const char *pattern, unsigned pat_len, const char *candidate, unsigned can_len);
extern uint8_t *vdfs_split_addr(void);
extern void vdfs_split_go(unsigned after);

extern const char *vdfs_cfg_root;

#endif
