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

void mmb_cmd_dabout(void);
void mmb_cmd_din(uint16_t addr);
void mmb_cmd_dcat_start(uint16_t addr);
void mmb_cmd_dcat_cont(void);

#endif
