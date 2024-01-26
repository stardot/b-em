/*
 * B-EM MMB - Support for MMB files - interface.
 */

#ifndef MMB_INC
#define MMB_INC

extern FILE *mmb_fp;
extern char *mmb_fn;
extern unsigned mmb_ndisc;
extern off_t mmb_offset[NUM_DRIVES][2];

// Functions for MMB files.
void mmb_load(char *fn);
void mmb_eject(void);
void mmb_pick(unsigned drive, unsigned side, unsigned disc);
void mmb_reset(void);
int mmb_find(const char *name);

#endif
