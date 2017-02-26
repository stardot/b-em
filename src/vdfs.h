#ifndef __INCLUDE_VDFS_H__
#define __INCLUDE_VDFS_H__

extern void     vdfs_init(void);
extern void     vdfs_reset(void);
extern void     vdfs_close(void);
extern uint8_t  vdfs_read(uint16_t addr);
void            vdfs_write(uint16_t addr, uint8_t value);

extern int      vdfs_enabled;
extern const char *vdfs_get_root(void);
extern void     vdfs_set_root(const char *dir);
#endif
