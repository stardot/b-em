/*B-em v2.2 by Tom Walker
  CSW cassette support*/
  
/* - overhaul by Diminished */
  
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "b-em.h"
#include "sysacia.h"
#include "csw.h"
#include "tape.h"

/* #define CSW_DEBUG_PRINT */

#define CSW_BODY_MAXLEN_RAW          (8 * 1024 * 1024)
#define CSW_MAXLEN_OVERALL           CSW_BODY_MAXLEN_RAW

#define CSW_HEADER_LEN 0x34

static char pulse_get_bit_value (uint32_t pulse, uint32_t thresh_smps, uint32_t max_smps);

#define CSW_RATE_MIN 8000
#define CSW_RATE_MAX 192000

#define CSW_MAX_PULSES (CSW_BODY_MAXLEN_RAW) /* good enough */

#define MAGIC "Compressed Square Wave\x1a"


#ifdef CSW_DEBUG_PRINT
static void debug_csw_print (csw_state_t *csw);

static void debug_csw_print (csw_state_t *csw) {
    csw_header_t *hdr;
    hdr = &(csw->header);
    printf("csw:\n");
    printf("  version      =   %u.%u\n", hdr->version_maj, hdr->version_min);
    printf("  rate         =   %u\n", hdr->rate);
    printf("  num_pulses   =   %u\n", hdr->num_pulses);
    printf("  compressed   =   %u\n", hdr->compressed);
    printf("  flags        =   %u\n", hdr->flags);
    printf("  ext_len      =   0x%x\n", hdr->ext_len);
}
#endif /*CSW_DEBUG_PRINT*/


int csw_load_file (const char *fn, csw_state_t *csw)
{
    int e;
    uint8_t *buf;
    uint32_t len;
    uint8_t *body;
    
    buf = NULL;
    body = NULL;
    
    memset(csw, 0, sizeof(csw_state_t));

    e = tape_load_file (fn, 0, &buf, &len);
    if (TAPE_E_OK != e) { return e; }
        
    do {

        double d;
        uint32_t header_ext_len;
        uint32_t body_len, raw_body_len;
        uint32_t start_of_data;
        uint8_t pass;
        
        body = NULL;
        body_len = 0;
        raw_body_len = 0;
        start_of_data = 0;
        e = 0;

        
        /* file needs to be at least HEADER_LEN plus one pulse long */
        if (len < (1 + CSW_HEADER_LEN)) {
            log_warn("csw: header is truncated: '%s'", fn);
            e = TAPE_E_CSW_HEADER_TRUNCATED;
            break;
        }
        
        if (0 != memcmp (MAGIC, buf, strlen(MAGIC))) {
            log_warn("csw: bad magic: '%s'", fn);
            e = TAPE_E_CSW_BAD_MAGIC;
            break;
        }
        
        //csw->header.version = tape_read_u16 (buf + 0x17);
        csw->header.version_maj = buf[0x17];
        csw->header.version_min = buf[0x18];
        if (csw->header.version_maj != 2) {
            log_warn("csw: unknown major version %u: '%s'", csw->header.version_maj, fn);
            e = TAPE_E_CSW_BAD_VERSION;
            break;
        }
        if ((csw->header.version_min != 0) && (csw->header.version_min != 1)) {
            log_warn("csw: unknown minor version %u: '%s'", csw->header.version_min, fn);
            e = TAPE_E_CSW_BAD_VERSION;
            break;
        }
        if (1 == csw->header.version_min) {
            log_warn("csw: minor version 1: CSW violates spec: '%s'", fn);
        }
        
        csw->header.rate = tape_read_u32 (buf + 0x19);
        if ((csw->header.rate < CSW_RATE_MIN) || (csw->header.rate > CSW_RATE_MAX)) {
            log_warn("csw: bad sample rate %d: '%s'", csw->header.rate, fn);
            e = TAPE_E_CSW_BAD_RATE;
            break;
        }
        
        csw->header.num_pulses = tape_read_u32(buf + 0x1d);
        if ((csw->header.num_pulses >= CSW_MAX_PULSES) || (0 == csw->header.num_pulses)) {
            log_warn("csw: bad number of pulses in header (%d): '%s'", csw->header.num_pulses, fn);
            e = TAPE_E_CSW_HEADER_NUM_PULSES;
            break;
        }
        
        if ((buf[0x21] != 1) && (buf[0x21] != 2)) {
            log_warn("csw: bad compression value (%d): '%s'", buf[0x21], fn);
            e = TAPE_E_CSW_COMP_VALUE;
            break;
        }
        csw->header.compressed = (buf[0x21] == 2);
        
        if (buf[0x22] & 0xf8) {
            log_warn("csw: bad flags value (0x%x): '%s'", buf[0x22], fn);
            e = TAPE_E_CSW_BAD_FLAGS;
            break;
        }
        if ((buf[0x22] != 0) && (buf[0x22] != 1)) {
            log_warn("csw: illegal flags (&%x): CSW violates spec: '%s'", buf[0x22], fn);
        }
        csw->header.flags = buf[0x22];
        
        /* consume remaining header */
        csw->header.ext_len = buf[0x23];
        header_ext_len = csw->header.ext_len; /* 32-bit edition */
        if (header_ext_len != 0) {
            log_warn("csw: hdr. ext. len. is nonzero (&%x); "
                     "may be an illegal CSW that abuses hdr. ext. for "
                     "purposes of graffiti", header_ext_len);
            if ((0x23 + header_ext_len + 1) >= len) { /* +1 for a single CSW pulse */
                log_warn("csw: file truncated during hdr. ext., or no pulses (&%x >= len (&%x)) : '%s'",
                         (0x23 + header_ext_len + 1), len, fn);
                e = TAPE_E_CSW_HEADER_TRUNCATED;
                break;
            }
        }
        
        start_of_data = (CSW_HEADER_LEN + header_ext_len);

        raw_body_len = len - start_of_data;

        
#ifdef CSW_DEBUG_PRINT
        debug_csw_print(csw);
#endif
        
        /* compressed? */
        if (csw->header.compressed) {

            e = tape_decompress (&body, &body_len, buf + start_of_data, raw_body_len);
            if (TAPE_E_OK != e) { break; }
                        
        } else {
            /* uncompressed; just copy */
            if (raw_body_len >= CSW_BODY_MAXLEN_RAW) {
                log_warn("csw: raw body is too large (%u): '%s'", raw_body_len, fn);
                e = TAPE_E_CSW_BODY_LARGE;
                break;
            }
            body = buf + start_of_data;
            body_len = raw_body_len;
        }
        
        /* parse pulses */
        for (pass=0; pass < 2; pass++) {
        
            int trunc;
            uint32_t np, n;
            
            for (n=0, np=0, trunc=0; n < body_len; n++, np++) {
                uint32_t pulse;
                if (0 == body[n]) {
                    if ((n + 4) >= body_len) {
                        log_warn("csw: truncated? '%s'", fn);
                        trunc = 1;
                        break;
                    }
                    pulse = tape_read_u32(body + n + 1);
                    n+=4;
                } else {
                    pulse = body[n];
                }
                if (1 == pass) {
                    csw->pulses[np] = pulse;
                }
            }
            
            if (np != csw->header.num_pulses) {
                log_warn("csw: pulses in body (%u) does not match value in header (%u): '%s'",
                         np, csw->header.num_pulses, fn);
                e = TAPE_E_CSW_PULSES_MISMATCH;
                break;
            }
            
            if (trunc) { break; }
            
            if (0==pass) {
                csw->pulses = malloc(sizeof(uint32_t) * np);
                if (NULL == csw->pulses)  {
                    log_warn("csw: out of memory allocating pulses: '%s'", fn);
                    e = TAPE_E_MALLOC;
                    break;
                }
            }
        }
        
        if (e != TAPE_E_OK) { break; }
        
        /* "three-halves" threshold: */
        d = ((((double) csw->header.rate) / 1200.0) + (((double) csw->header.rate) / 2400.0)) / 4.0;
        csw->thresh_smps = (uint32_t) d; /* round down */
        d = (((double) csw->header.rate) / 1200.0) + 0.5;
        csw->len_1200th_smps = (uint32_t) d;
        csw_ena     = 1;
    
    } while (0);
    
    free(buf);
    if (csw->header.compressed) { free(body); }
    
    return e;
    
}

void csw_finish (csw_state_t *csw) {
    if (NULL != csw->pulses) {
        free(csw->pulses);
    }
    memset(csw, 0, sizeof(csw_state_t));
}



uint8_t csw_peek_eof (csw_state_t *csw) {
    return (csw->cur_pulse >= csw->header.num_pulses);
}



int csw_read_1200th (csw_state_t *csw, char *out_1200th) {

    char b;
    
    b = '?';
    
    do {
    
        /* buffer for lookahead pulse values;
           - 2  elements used for a 0-tone (1/1200s)
           - 4  elements used for a 1-tone (1/1200s) */
           
        char v[4] = { 'X','X','X','X' };
        
        uint8_t n;
        uint32_t k;
        uint8_t lookahead;
        char q;
        int wins;
    
        if (csw->cur_pulse >= csw->header.num_pulses) {
            return TAPE_E_EOF;
        }
        
        /* convert the next 4 pulses into
           bit values that correspond to their length; so at 44.1K
           ~9 becomes '1', and ~18 becomes '0': */
        for (n=0, k = csw->cur_pulse;
             (n < 4) && (k < csw->header.num_pulses);
             n++, k++) {
            v[n] = pulse_get_bit_value(csw->pulses[k], csw->thresh_smps, csw->len_1200th_smps);
        }
        
        lookahead = 4;

        /* test for '1' first, and then '0' afterwards;
           lookahead is halved after testing for '1': */
        for (k=0, q='1', wins=0; (k < 2); k++, lookahead >>= 1, q='0') {
            if (q == v[0]) { /* first value matches the wanted value, proceed ... */
                for (n=0; (n < lookahead); n++) {
                    if (q == v[n]) { wins++; }
                }
                if (wins < lookahead) {
                    break; /* insufficient correct-length pulses found in lookahead; failure */
                }
                csw->cur_pulse += lookahead;
                csw->sub_pulse = 0;
                b = q; /* got it */
                break;
            }
        }
        if (b==q) { break; }
        
        if ('S' == v[0]) {
            if (csw->sub_pulse >= csw->pulses[csw->cur_pulse]) {
                csw->sub_pulse = 0;
                (csw->cur_pulse)++;
            } else {
                csw->sub_pulse += csw->len_1200th_smps;
            }
            b = 'S';
            break;
        }
        
        /* give up; advance pulse and try again: */
        (csw->cur_pulse)++;
        
    } while (b == '?');
    
    *out_1200th = b;
    return TAPE_E_OK;
    
}


static char pulse_get_bit_value (uint32_t pulse, uint32_t thresh_smps, uint32_t max_smps) {
    if (pulse <= thresh_smps) {
        return '1';
    } else if (pulse < max_smps) {
        return '0';
    }
    return 'S';
}


int csw_clone (csw_state_t *out, csw_state_t *in) {
    memcpy(out, in, sizeof(csw_state_t));
    out->pulses = malloc(sizeof(uint32_t) * in->header.num_pulses);
    if (NULL == out->pulses) {
        log_warn("TAPE: out of memory allocating CSW pulses clone\n");
        return TAPE_E_MALLOC;
    }
    memcpy(out->pulses, in->pulses, sizeof(uint32_t) * in->header.num_pulses);
    return TAPE_E_OK;
}


void csw_rewind (csw_state_t *csw) {
    csw->cur_pulse = 0;
    csw->sub_pulse = 0;
}
