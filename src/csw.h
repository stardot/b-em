#ifndef __INC__CSW_H
#define __INC__CSW_H

typedef struct csw_header_s {
    uint32_t num_pulses;
    uint8_t compressed;
    uint8_t ext_len;
    uint8_t version_maj;
    uint8_t version_min;
    uint32_t rate;
    uint8_t flags;
} csw_header_t;

typedef struct csw_state_s {

    csw_header_t header;
    uint32_t *pulses; /* length is in header */
    
    /* comparison to this pulse length threshold must be
       p <= thresh : short pulse
       p >  thresh : long pulse */
    uint32_t thresh_smps;
    uint32_t len_1200th_smps;
    
    /* reader state: */
    uint32_t cur_pulse;
    uint32_t sub_pulse;
    
} csw_state_t;

int csw_load_file (const char *fn, csw_state_t *csw);
void csw_load(const char *fn);
void csw_close(void);
void csw_poll(void);
void csw_findfilenames(void);
int csw_clone (csw_state_t *out, csw_state_t *in);
void csw_rewind (csw_state_t *csw);
int csw_read_1200th (csw_state_t *csw, char *out_1200th);
uint8_t csw_peek_eof (csw_state_t *csw);
void csw_finish (csw_state_t *csw);

#endif
