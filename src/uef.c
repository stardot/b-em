/*B-em v2.2 by Tom Walker
  UEF/HQ-UEF tape support*/
  
/* - overhaul by Diminished */

#include <zlib.h>
#include <stdio.h>
#include <ctype.h>
#include "b-em.h"
#include "sysacia.h"
#include "csw.h"
#include "uef.h"
#include "tape.h"

#define UEF_REJECT_IF_TRUNCATED
#define UEF_REJECT_IF_UNKNOWN_CHUNK

static int chunks_decode (uint8_t *buf,
                          uint32_t len,
                          uef_state_t *uef,
                          uint8_t reject_if_truncated,
                          uint8_t reject_if_unknown_chunk);
                              
static int uef_store_chunk (uint8_t *buf,
                            uint16_t type,
                            uint32_t len,
                            int32_t chunknum,
                            uef_chunk_t *chunk);
                            
static int uef_parse_global_chunks (uef_state_t *u) ;
static int chunk_verify_length (uef_chunk_t *c) ;
static int chunks_verify_lengths (uef_chunk_t *chunks, int32_t num_chunks);
static int compute_chunk_102_data_len (uint32_t chunk_len,
                                       uint8_t data0,
                                       uint32_t *len_bytes_out,
                                       uint32_t *len_bits_out);
static uint8_t compute_parity_bit (uint8_t data, uint8_t num_data_bits, char parity);
static uint8_t valid_chunktype (uint16_t t);
static int chunk_114_next_cyc (uef_bitsource_t *src,
                               uint32_t chunk_len,
                               uint8_t *data,
                               uint8_t *cyc_out);
static int reload_reservoir (uef_state_t *u, uint8_t baud300);
static float uef_read_float (uint8_t b[4]);
static uint64_t reservoir_make_value (uint16_t v, uint8_t baud300);
static void init_bitsource (uef_bitsource_t *src);
static int
pre_parse_metadata_chunk (uef_chunk_t *chunk,
                          /* zero means no prior chunk 130: */
                          uint8_t num_tapes_from_prior_chunk_130,
                          /* likewise: */
                          uint8_t num_channels_from_prior_chunk_130,
                          uef_meta_t *meta_out);
                          
static int uef_verify_meta_chunks (uef_state_t *u);
static uint8_t is_chunk_spent (uef_chunk_t *c, uef_bitsource_t *src);

int uef_toneon = 0;
int uef_intone = 0;

#define UEF_MAX_FLOAT_GAP 36000.0f  /* ten hours */
#define UEF_BASE_FREQ 1201.9f

static void init_bitsource (uef_bitsource_t *src) {
    memset(src, 0, sizeof(uef_bitsource_t));
    src->framing.num_data_bits   = 8;
    src->framing.parity          = 'N';
    src->framing.num_stop_bits   = 1;
    src->chunk_114_pulsewaves[0] = '?';
    src->chunk_114_pulsewaves[1] = '?';
}

static void reset_bitsource (uef_bitsource_t *src) {
    uint8_t baud300;
    /* IMPORTANT: this is the ONLY difference between calling
     * init_bitsource() and calling reset_bitsource();
     * reset_bitsource will preserve the 300 baud setting. This is
     * the ONLY bitsource field which needs to persist in-between
     * chunks. Everything else is reset. */
    baud300 = src->framing.baud300;
    init_bitsource(src);
    src->framing.baud300 = baud300;
}

static int consider_chunk (uef_state_t *u,
                           uint8_t *contains_1200ths_out) {

    uint8_t cb;
    uef_chunk_t *c;
    uint16_t type;
    float f;
    uef_bitsource_t *src;
    
    c = u->chunks + u->cur_chunk;
    type = c->type;
    src = &(u->bitsrc);
    
    /*printf("\nconsider_chunk(%u), type &%x\n", u->cur_chunk, type);*/

    /* New chunk, so reset the bit source. IMPORTANT: This preserves
     * the 300 baud setting; everything else is reset. */
    reset_bitsource(src);
    
            /* data chunks: len must be > 0: */
    cb =    (((0x100==type)||(0x102==type)||(0x104==type)) && (c->len > 0))
            /* carrier tone: num cycles must be > 0: */
         || ((0x110==type) && (tape_read_u16(c->data) != 0))
            /* dummy byte in &111 means there is always data, so: */
         || (0x111==type)
            /* integer gap: must have length > 0 */
         || ((0x112==type) && (tape_read_u16(c->data) != 0))
            /* overhaul v2: enforce (gap > 1/1200) */
         || ((0x116==type) && (uef_read_float (c->data) > (1.0/1200.0)))
         || ((0x114==type) && (tape_read_u24(c->data) != 0));
    
    /* chunk lengths were validated by chunk_verify_length() earlier,
     * so we should be fine to go ahead and access the data without
     * further length checks */

    if (0x102 == type) {
        u->bitsrc.src_byte_pos = 1; /* skip first byte */
    } else if (0x104 == type) {
        /* This is a data chunk with arbitrary framing, so establish
         * the framing parameters before we start on the data;
         * reminder once again that these are parameters for
         * *decoding the UEF*. We do NOT program the ACIA with them.
         * Only an idiot would do that.
         */
        /* validate the framing spec */
        if ((c->data[0] != 8) && (c->data[0] != 7)) {
            log_warn("uef: chunk &104: illegal number of data bits (%u, should be 7 or 8)", c->data[0]);
            return TAPE_E_UEF_0104_NUM_BITS;
        } else if ((c->data[1] != 'N') && (c->data[1] != 'O') && (c->data[1] != 'E')) {
            log_warn("uef: chunk &104: illegal parity (&%x, should be &45, &4e or &4f)", c->data[1]);
            return TAPE_E_UEF_0104_NUM_BITS;
        } else if ((c->data[2] != 1) && (c->data[2] != 2)) {
            log_warn("uef: chunk &104: illegal number of stop bits (%u, should be 1 or 2)", c->data[2]);
            return TAPE_E_UEF_0104_NUM_STOPS;
        }
        /* use this framing for this chunk: */
        u->bitsrc.framing.num_data_bits = c->data[0];
        u->bitsrc.framing.parity        = c->data[1];
        /* MakeUEF < 2.4 has E and O mixed up */
        if (u->reverse_even_and_odd_parity) {
            if ('O'==u->bitsrc.framing.parity) {
              u->bitsrc.framing.parity = 'E';
            } else if ('E' == u->bitsrc.framing.parity) {
              u->bitsrc.framing.parity = 'O';
            }
        }
        u->bitsrc.framing.num_stop_bits = c->data[2];
        u->bitsrc.src_byte_pos = 3; /* skip header */
    } else if (0x114 == type) {
        if (('P' != c->data[3]) && ('W' != c->data[3])) {
            log_warn("uef: chunk &114: illegal pulse/wave char (%x): wanted P or W", c->data[3]);
            return TAPE_E_UEF_0114_BAD_PULSEWAVE_1;
        }
        if (('P' != c->data[4]) && ('W' != c->data[4])) {
            log_warn("uef: chunk &114: illegal pulse/wave char (%x): wanted P or W", c->data[4]);
            return TAPE_E_UEF_0114_BAD_PULSEWAVE_2;
        }
        if (('P' == c->data[3]) && ('P' == c->data[4])) {
            log_warn("uef: chunk &114: illegal pulse/wave char combination %c, %c", c->data[3], c->data[4]);
            return TAPE_E_UEF_0114_BAD_PULSEWAVE_COMBO;
        }
        u->bitsrc.chunk_114_total_cycs    = tape_read_u24(c->data);
        u->bitsrc.chunk_114_pulsewaves[0] = c->data[3];
        u->bitsrc.chunk_114_pulsewaves[1] = c->data[4];
        u->bitsrc.src_byte_pos = 5; /* skip header */
    } else if (0x110 == type) {
        /* carrier tone */
        u->bitsrc.nodata_total_pre_cycs = tape_read_u16(c->data);
        /*
printf("Carrier tone: Setting nodata_total_pre_cycs = %u (%x)\n", u->bitsrc.nodata_total_pre_cycs, u->bitsrc.nodata_total_pre_cycs);
*/
    } else if (0x111==type) {
        /* carrier tone + &AA + carrier tone */
        u->bitsrc.nodata_total_pre_cycs = tape_read_u16(c->data);
        u->bitsrc.nodata_total_post_cycs = tape_read_u16(c->data + 2);
    } else if (0x112==type) {
        /* integer gap; UEF spec is wrong */
        u->bitsrc.nodata_total_pre_cycs = tape_read_u16(c->data);
    } else if (0x116 == type) {
        /* float gap; make sure it isn't negative! */
        f = uef_read_float (c->data);
        if (f < 0.0f) {
            log_warn("uef: chunk &116 contains negative float gap!");
            return TAPE_E_UEF_0116_NEGATIVE_GAP;
        }
        if (f > UEF_MAX_FLOAT_GAP) {
            log_warn("uef: chunk &116 contains excessive float gap!");
            return TAPE_E_UEF_0116_HUGE_GAP;
        }
        u->bitsrc.nodata_total_pre_cycs = (uint32_t) (0.5f + (f * UEF_BASE_FREQ * 2.0f));
    }
    
    if (cb && is_chunk_spent(c, src)) {
        /* sanity check: we will also call is_chunk_spent()
           on the new chunk. It is possible to end up
           with a discrepancy where this function claims that
           a chunk can provide some tapetime, but the call to
           is_chunk_spent() in reload_reservoir() returns TRUE.
           One situation where this arose was where a chunk
           116 (float gap) had a gap length of zero, so did
           not actually resolve to any number of 1200ths.
           This condition is checked separately now (above)
           but even so this sanity check has been added
           in case it happens some other way.
        */
        log_warn("uef: chunk &%x that should have contained tapetime "
                 "is somehow empty; skipping", type);
        cb = 0; /* as you were */
    }
    
    *contains_1200ths_out = cb;
    
    return TAPE_E_OK;
    
}

static float uef_read_float (uint8_t b[4]) {
    /* FIXME: implement FLOAT-reading properly
       (i.e. platform-independently) */
    /* avoid type-punning */
    union { uint8_t b[4]; float f; } u;
    memcpy(u.b, b, 4);
    return u.f;
}


void uef_metadata_list_finish (uef_meta_t metadata_list[UEF_MAX_METADATA],
                               uint32_t fill) {
    uint32_t i;
    for (i=0; i < fill; i++) {
        uef_meta_u_t *u;
        uint16_t type;
        u = &(metadata_list[i].data);
        type = metadata_list[i].type;
        if ((0x120 == type) && (u->position_marker != NULL)) {
            free(u->position_marker);
        } else if ((0x131 == type) && (u->start_of_tape_side.description != NULL)) {
            free(u->start_of_tape_side.description);
        }
    }
    memset(metadata_list, 0, sizeof(uef_meta_t) * UEF_MAX_METADATA);
}

/* called from uef_load_file(), to make an initial
 * verification pass across all chunks and find out
 * if it's worth persisting with this UEF file */
static int uef_verify_meta_chunks (uef_state_t *u) {
    uint32_t i;
    uef_tape_set_info_t tsi;
    memset (&tsi, 0, sizeof(uef_tape_set_info_t));
    for (i=0; i < u->num_chunks; i++) {
        uef_chunk_t *c;
        uef_meta_t m;
        int e;
        c = u->chunks + i;
        e = pre_parse_metadata_chunk (c, tsi.num_tapes, tsi.num_channels, &m);
        if (TAPE_E_OK != e) { return e; }
        if (m.is_valid && (0x130 == m.type)) {
            /* keep current working chunk 130 specification */
            tsi = m.data.tape_set_info;
        }
    }
    return TAPE_E_OK;
}


/* we will let the caller clean up metadata_list on error;
 * it's a tiny bit neater;
 * 
 * if chunk was not a metadata-type chunk, then meta_out->is_valid
 * will be 0; otherwise 1 */
static int
pre_parse_metadata_chunk (uef_chunk_t *chunk,
                          /* zero means no prior chunk 130: */
                          uint8_t num_tapes_from_prior_chunk_130,
                          /* likewise: */
                          uint8_t num_channels_from_prior_chunk_130,
                          uef_meta_t *meta_out) {
                      
    size_t desclen;
    
    memset(meta_out, 0, sizeof(uef_meta_t));
        
    /* once again, chunk lengths should have been verified by
     * chunk_verify_length() earlier */
    if (0x115 == chunk->type) { /* phase change */
        meta_out->data.phase = tape_read_u16(chunk->data);
        if (meta_out->data.phase > 360) {
            log_warn("uef: phase change: illegal value %u", meta_out->data.phase);
            return TAPE_E_UEF_0115_ILLEGAL;
        }
        meta_out->is_valid = 1;
    } else if (0x120 == chunk->type) { /* position marker text */
        meta_out->data.position_marker = malloc(1 + chunk->len);
        if (NULL == meta_out->data.position_marker) {
            log_warn("uef: could not allocate position marker metadata");
            //~ metadata_finish(metadata_list, *fill_inout);
            return TAPE_E_MALLOC;
        }
        /* null-terminate: */
        meta_out->data.position_marker[chunk->len] = '\0';
        memcpy(meta_out->data.position_marker,
               chunk->data,
               chunk->len);
        meta_out->is_valid = 1;
    } else if (0x130 == chunk->type) { /* tape set info */
        if (chunk->data[0] > 4) {
            log_warn("uef: tape set info: illegal vocabulary (max. 4): %u", chunk->data[0]);
            return TAPE_E_UEF_0130_VOCAB;
        }
        meta_out->data.tape_set_info.vocabulary   = chunk->data[0];
        if ((chunk->data[1] > 127) || (0 == chunk->data[1])) {
            log_warn("uef: tape set info: illegal number of tapes (1<=nt<=127): %u", chunk->data[1]);
            return TAPE_E_UEF_0130_NUM_TAPES;
        }
        /* this acts as a limit for future chunk &131s (will be
         * passed back into this function on future calls to it): */
        meta_out->data.tape_set_info.num_tapes    = chunk->data[1];
        if (0 == chunk->data[2]) {
            log_warn("uef: tape set info: illegal (zero) number of channels");
            return TAPE_E_UEF_0130_NUM_CHANNELS;
        }
        /* this acts as a limit for future chunk &131s (will be
         * passed back into this function on future calls to it): */
        meta_out->data.tape_set_info.num_channels = chunk->data[2];
        meta_out->is_valid = 1;
    } else if (0x131 == chunk->type) { /* start of tape side */
        if (127 == (chunk->data[0] & 0x7f)) {
            log_warn("uef: tape set info: bad tape ID %u", (chunk->data[0] & 0x7f));
            return TAPE_E_UEF_0131_TAPE_ID;
        } else if (    (num_tapes_from_prior_chunk_130 > 0)
                    && ((chunk->data[0] & 0x7f) >= num_tapes_from_prior_chunk_130)) {
            log_warn("uef: tape set info: tape ID exceeds prior max.: %u vs. %u",
                     (chunk->data[0] & 0x7f), num_tapes_from_prior_chunk_130 - 1);
            return TAPE_E_UEF_0131_TAPE_ID_130_LIMIT;
        }
        meta_out->data.start_of_tape_side.tape_id     = chunk->data[0] & 0x7f;
        meta_out->data.start_of_tape_side.is_side_B   = (chunk->data[0] & 0x80) ? 1 : 0;
        if (0xff == chunk->data[1]) {
            log_warn("uef: tape set info: bad channel ID 255");
            return TAPE_E_UEF_0131_CHANNEL_ID;
        } else if (    (num_channels_from_prior_chunk_130 > 0)
                    && (chunk->data[1] >= num_channels_from_prior_chunk_130)) {
            log_warn("uef: tape set info: tape ID exceeds prior max.: %u vs. %u",
                     chunk->data[1], num_channels_from_prior_chunk_130 - 1);
            return TAPE_E_UEF_0131_CHANNEL_ID_130_LIMIT;
        }
        meta_out->data.start_of_tape_side.channel_id  = chunk->data[1];
        meta_out->data.start_of_tape_side.description = malloc(1 + (chunk->len-2));
        if (NULL == meta_out->data.start_of_tape_side.description) {
            return TAPE_E_MALLOC;
        }
        meta_out->data.start_of_tape_side.description[chunk->len-2] = '\0';
        memcpy (meta_out->data.start_of_tape_side.description,
                chunk->data + 2,
                chunk->len  - 2);
        desclen = strlen(meta_out->data.start_of_tape_side.description);
        /* UEF spec constrains len to 255 chars max */
        if (desclen > 255) {
            log_warn("uef: tape set info: description exceeds 255 chars (%zu)",
                     desclen);
            return TAPE_E_UEF_0131_DESCRIPTION_LONG;
        }
        meta_out->is_valid = 1;
    } else if (0x117 == chunk->type) { /* NEW: baud rate */
        meta_out->data.baud = tape_read_u16(chunk->data);
        if ((meta_out->data.baud != 300) && (meta_out->data.baud != 1200)) {
            log_warn("uef: data encoding format change: bad baud %u",
                     meta_out->data.baud);
            return TAPE_E_UEF_0117_BAD_RATE;
        }
        meta_out->is_valid = 1;
    }
    
    if (meta_out->is_valid) {
        meta_out->type = chunk->type;
    }
    return TAPE_E_OK;
}



static uint8_t compute_parity_bit (uint8_t data, uint8_t num_data_bits, char parity) {

    uint8_t n, num_ones;
    
    for (n=0, num_ones = 0; n < num_data_bits; n++) {
        num_ones += (data & 1);
        data = (data >> 1) & 0x7f;
    }
    
    if (num_ones & 1) {
        /* have odd */
        if ('E' == parity) { /* want even */
            return 1;
        }
    } else {
        /* have even */
        if ('O' == parity) { /* want odd */
            return 1;
        }
    }
    
    return 0;
    
}


static int chunk_114_next_cyc (uef_bitsource_t *src,
                               uint32_t chunk_len,
                               uint8_t *data,
                               uint8_t *cyc_out) {
                            
    uint8_t v, b;

    /* Chunk is finished when all the cycles in the 24-bit value
     * from bytes 0-2 have been consumed. */
    if (src->chunk_114_consumed_cycs >= src->chunk_114_total_cycs) {
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    /* This shouldn't happen, unless the 24-bit value was full of
     * lies and sin. */
    if (src->src_byte_pos >= chunk_len) {
        log_warn("uef: Chunk &114 number-of-cycles field is wrong (&%x)",
                 src->chunk_114_total_cycs);
        return TAPE_E_UEF_0114_BAD_NUM_CYCS;
    }
    
    v = data[src->src_byte_pos];
    
    /* get next cycle-bit; cycles are MSB first */
    b = (v >> (7 - src->chunk_114_src_bit_pos)) & 1;
    
    /* cycle is consumed: */
    (src->chunk_114_consumed_cycs)++;
    (src->chunk_114_src_bit_pos)++;
    
    *cyc_out = b;
    
    if (src->chunk_114_src_bit_pos >= 8) {
        (src->src_byte_pos)++;
        src->chunk_114_src_bit_pos = 0;
    }
    
    return TAPE_E_OK;
    
}


static uint8_t is_chunk_spent (uef_chunk_t *c, uef_bitsource_t *src) {
    uint16_t type;
    uint32_t bits102, bytes102;
    int e;
    type = c->type;
    if ((0x100==type)||(0x104==type)) {
        return (src->src_byte_pos >= c->len);
    } else if (0x102==type) {
        e = compute_chunk_102_data_len (c->len, c->data[0], &bytes102, &bits102);
        if (TAPE_E_OK != e) { return 1; }
        return (src->src_byte_pos - 1) >= bytes102;
    } else if (0x114==type) {
        return (src->chunk_114_consumed_cycs >= src->chunk_114_total_cycs);
    } else if (0x111==type) {
        return (src->nodata_consumed_post_cycs >= src->nodata_total_post_cycs);
    } else if ((0x110==type)||(0x112==type)||(0x116==type)) {
        return (src->nodata_consumed_pre_cycs >= src->nodata_total_pre_cycs);
    }
    /* other chunk types don't contain any bits */
    return 1;
}


/* Take a set of bits and turn them into a set of 1/1200s tones.
 * At 1200 baud, these will be the same thing, but at 300 baud,
 * one bit becomes four 1/1200s tones, represented by four
 * bits in the output uint64_t. */
static uint64_t reservoir_make_value (uint16_t v, uint8_t baud300) {
    uint64_t x, four;
    uint8_t n;
    if ( ! baud300 ) { return v; }
    for (n=0, x=0; n < 16; n++) {
        four = (((uint64_t) 15) << (n*4));
        x |= ((v>>n)&1) ? four : 0;
    }
    return x;
}


static int reload_reservoir (uef_state_t *u, uint8_t baud300) {

    int e;
    uef_bitsource_t *src;
    uint8_t v;
    uef_chunk_t *chunk;
    uint16_t pb;
    uint8_t cycs[8];
    uint32_t saved_consumed_cycs;
    uint32_t saved_src_bit_pos;
    uint32_t saved_src_byte_pos;
    uint16_t type;
    uint16_t nbits;
    uint32_t bits102, bytes102;
    
    /* cur_chunk starts at -1; force consideration of chunk 0 */
    if (u->cur_chunk < 0) {
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    e = TAPE_E_OK;
    src = &(u->bitsrc);
    
    chunk = u->chunks + u->cur_chunk;
    
/*printf("reload_reservoir: cur_chunk = %d, type &%x\n", u->cur_chunk, chunk->type);*/
            
    if (u->cur_chunk >= u->num_chunks) {
        log_info("uef: EOF");
        return TAPE_E_EOF;
    }
    
    /* Empty the reservoir. */
    src->reservoir_pos = 0;
    src->reservoir_len = 0;
    
    if (is_chunk_spent (chunk, src)) {
/*printf("reload_reservoir: chunk spent\n");*/
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    type = chunk->type;
    
//~ printf("reload_reservoir: baud300 = %u\n", baud300);

    /* Chunks &100, &102 and &104 are easy -- we'll just move one
     * more byte through the source data. This nets us one frame
     * for &100 and &104, and eight bits for chunk &102 (for 102, these
     * are explicit bits, so not a frame -- it's just a fragment of
     * the bitstream, which may include start, stop and parity
     * bits). */
    if ( (0x100 == type) || (0x104 == type) ) {
        /* get byte from input chunk and frame it */
        v = chunk->data[src->src_byte_pos];
        nbits = 1; /* start bit */
        src->reservoir = (((uint16_t) v) << 1);
        nbits += src->framing.num_data_bits; /* data bits */
        /* parity */
        if (src->framing.parity != 'N') {
            pb = compute_parity_bit (v,
                                     src->framing.num_data_bits,
                                     src->framing.parity);
            pb <<= nbits;
            src->reservoir |= pb; /* install parity bit */
            nbits++;
        }
        /* stop 1 */
        src->reservoir |= (1 << nbits);
        nbits++;
        if (2 == src->framing.num_stop_bits) {
            nbits++;
            src->reservoir |= (1 << nbits);
        }
        src->reservoir_len = nbits;
        (src->src_byte_pos)++;
        
        if (baud300) {
            /* quadruple up the reservoir */
            src->reservoir = reservoir_make_value (src->reservoir, 1);
            src->reservoir_len *= 4;
        }
        
    } else if (0x102==type) {
        /* explicit bits */
        /* get byte from input chunk and don't frame it */
        e = compute_chunk_102_data_len (chunk->len, chunk->data[0], &bytes102, &bits102);
        if (TAPE_E_OK != e) { return e; }
        if ((src->src_byte_pos - 1) == (bytes102 - 1)) {
            /* final byte; may be incomplete */
            bits102 -= ((src->src_byte_pos - 1) * 8);
/*
printf("final byte: rem bits %u (value &%x)", bits102, chunk->data[src->src_byte_pos]);
*/
            src->reservoir_len = bits102;
        } else {
            src->reservoir_len = 8;
        }
        v = chunk->data[src->src_byte_pos];
        src->reservoir = v & 0xff;
        (src->src_byte_pos)++;
        
        if (baud300) {
            /* quadruple up the reservoir */
            src->reservoir = reservoir_make_value (src->reservoir, 1);
            src->reservoir_len *= 4;
        }
        
    } else if (0x114==type) {
    
        /* Squawk, or maybe actually data. */
    
        do {

            /* Get the first cycle. It determines what we do next.
             * (src is updated with advanced bit/byte positions) */
            e = chunk_114_next_cyc (src, chunk->len, chunk->data, cycs + 0);
            if (TAPE_E_OK != e) { return e; }

            /* Look at the cycle and figure out how many more bits
             * we need to pull from the chunk to complete one output
             * bit. Remember, a 0-bit means one 1200 Hz cycle; a 1-bit
             * means one 2400 Hz cycle.
             * 
             * first cycle   baud rate   extra cycs needed   total cycs
             *   0             1200        0                   1
             *   1             1200        1                   2
             *   0              300        3                   4
             *   1              300        7                   8
             */
            
            /* Before pulling any extra cycle, back up the bitsource
             * state. We might need to revert it, if the output bit 
             * turns out to be ambiguous and we need to re-synchronise.
             */
             
            saved_consumed_cycs = src->chunk_114_consumed_cycs;
            saved_src_bit_pos   = src->chunk_114_src_bit_pos;
            saved_src_byte_pos  = src->src_byte_pos;
            
            /* Pull any cycle from the chunk that makes up the rest of
             * this output bit. If cycs[0] is a 2400 Hz cycle, then
             * we need to pull another cycle to make up the full
             * 1/1200. */
            if (cycs[0]) {
                e = chunk_114_next_cyc (src, chunk->len, chunk->data, cycs + 1);
                if (TAPE_E_OK != e) { return e; }
            }
            
            /* Now we have 1-2 cycles in cycs[]. Examine this data
             * and see how fidelitous the bit is. */
            if ((cycs[0]) && ( ! cycs[1])) {
                /* Bit is ambiguous. Restore the source state, throw out
                 * this cycle, and resynchronise on the next cycle. */
                src->chunk_114_consumed_cycs = saved_consumed_cycs;
                src->chunk_114_src_bit_pos   = saved_src_bit_pos;
                src->src_byte_pos            = saved_src_byte_pos;
            } else {
              break;
            }
            
        } while (1);
        
        /* For &114, we only do one 1200th at a time; place a single 1200th
         * into the reservoir. */
        src->reservoir     = cycs[0] ? 1 : 0;
        src->reservoir_len = 1;
        
        /* 114 is explicit; so we don't do the quadrupling with 300 baud. */
        
    } else if (0x110 == type) {
    
        /* leader -- send one 1200th only */
    
        src->reservoir = 0x1;
        src->reservoir_len = 1;
        (src->nodata_consumed_pre_cycs) += 2;
        
        /* leader chunks count cycles, not bits or anything, so
         * they are unaffected by 300 baud. */
        
    } else if (0x111 == type) {
    
        /* leader + &AA + leader */
        if (0 == src->chunk_111_state) {
            /* pre-&AA leader */
            if (src->nodata_consumed_pre_cycs >= src->nodata_total_pre_cycs) {
                src->chunk_111_state = 1;
            } else {
                src->reservoir = 0x1;
                src->reservoir_len = 1;
                (src->nodata_consumed_pre_cycs)+=2; /* 2 cycs, 1 bit */
            }
        } else if (1 == src->chunk_111_state) {
            /* dummy byte; framed as 8N1 */
            src->reservoir = 0x354;
            src->reservoir_len = 10;
            src->chunk_111_state = 2;
        } else {
            /* post-&AA leader */
            src->reservoir = 0x1;
            src->reservoir_len = 1;
            (src->nodata_consumed_post_cycs) += 2; /* 2 cycs, 1 bit */
        }
        
        /* As above, leader chunks count cycles, not bits;
         * they are unaffected by 300 baud. The dummy byte is always
         * 1200 baud, because MOS always writes it that way. */
        
    } else if ((0x112==type)||(0x116==type)) {
        src->silence = 1; /* overrides reservoir */
        src->reservoir_len = 1;
        (src->nodata_consumed_pre_cycs)++;
    }

    return TAPE_E_OK;
    
}

uint8_t uef_peek_eof (uef_state_t *uef) {
    return (uef->cur_chunk >= uef->num_chunks);
}


/* Caller must offload all the metadata every time this function is
 * called. 
 * The next call to this function that advances the chunk will
 * wipe the contents of metadata_list.
 * Caller must also call metadata_finish() on metadata_list,
 * regardless of whether this function succeeds or fails.
 */
int uef_read_1200th (uef_state_t *u,
                     char *out_1200th,
                     uef_meta_t metadata_list[UEF_MAX_METADATA], /* caller must call uef_metadata_list_finish() */
                     uint32_t *metadata_fill_out) {

    int e;
    uint8_t chunk_contains_bits;
    uef_bitsource_t *src;
    uint16_t shift;
    
/*printf("uef_read_bit: cur_chunk = %d, reservoir_pos = %u, reservoir_len = %u\n",
       u->cur_chunk, u->bitsrc.reservoir_pos, u->bitsrc.reservoir_len);*/
    
    *out_1200th = '?';
    *metadata_fill_out = 0;
    
    if (u->cur_chunk >= u->num_chunks) {
        log_info("uef: EOF");
        return TAPE_E_EOF; /* on EOF, metadata is NOT freed */
    }
    
    src = &(u->bitsrc);

    if (src->reservoir_pos >= src->reservoir_len) {
        /* reservoir empty, refill it */
        e = reload_reservoir (u,
                              /* NOT the 300 baud setting in the ACIA!
                               * It's the 300 baud setting for the UEF!
                               * Not the same thing at all! */
                              u->bitsrc.framing.baud300);
        if ((TAPE_E_OK != e) && (TAPE_E_UEF_CHUNK_SPENT != e)) { return e; }
        /* reload_reservoir() may fail with "chunk spent",
         * in which case we need to get a new chunk and try again: */
        if (TAPE_E_UEF_CHUNK_SPENT == e) {
            chunk_contains_bits = 0;
            e = TAPE_E_OK;
            /* chunk spent! free any metadata from previous chunk */
            uef_metadata_list_finish (metadata_list, *metadata_fill_out);
            /* loop to find a chunk that actually contains some bits,
               collecting up any intervening metadata chunks in the process */
            while ( (TAPE_E_OK == e) && ! chunk_contains_bits ) {
                uef_meta_t meta;
                (u->cur_chunk)++; /* initially goes -1 to 0 */
                if (u->cur_chunk >= u->num_chunks) {
                    log_info("uef: EOF"); /* on EOF, metadata is NOT freed */
                    return TAPE_E_EOF;
                }
                chunk_contains_bits     = 0;
                /* this ought to receive the tape set info from the previous
                 * chunk &130 for validation -- but it ought to have been
                 * checked already on load by uef_verify_meta_chunks(),
                 * so I don't think it's really necessary to check it again;
                 * => just pass 0,0 for the tape set info */
                e = pre_parse_metadata_chunk (u->chunks + u->cur_chunk, 0, 0, &meta);
                if (TAPE_E_OK != e) { return e; }
                if (meta.is_valid) { /* chunk is a metadata chunk, => no bits */
                    /* we handle baud rate changes here and now */
                    if (0x117 == meta.type) {
                        u->bitsrc.framing.baud300 = (300==meta.data.baud);
                        log_info("uef: baud change: %u\n", meta.data.baud);
                    }
                    /* there is some interesting metadata on this chunk,
                     * so bag it up and return it */
                    if ( *metadata_fill_out >= UEF_MAX_METADATA)  {
                        log_warn ("uef: too many metadata chunks between data chunks");
                        return TAPE_E_UEF_TOO_MANY_METADATA_CHUNKS;
                    }
                    metadata_list[*metadata_fill_out] = meta;
                    (*metadata_fill_out)++;
                } else {
                    e = consider_chunk (u, &chunk_contains_bits);
                    if (TAPE_E_OK != e) { return e; }
                }
            } /* keep trying chunks until we get some bits */
            if (TAPE_E_OK != e) { return e; }
            /* OK, new chunk with more bits, so try refilling the
             * reservoir again: */
            e = reload_reservoir (u,
                                  /* NOT the 300 baud setting in the ACIA!
                                   * It's the 300 baud setting for the UEF!
                                   * Not the same thing at all! */
                                  u->bitsrc.framing.baud300);
            if (TAPE_E_OK != e) { return e; }
        }
/* printf("reservoir reloaded: reservoir_pos = %u, value %x\n", src->reservoir_pos, src->reservoir); */
    }

    /* Reservoir contains something; emit 1/1200th of a second
     * from the reservoir */
    
    /* src->silence overrides the reservoir contents */
    if ( ! src->silence ) {
        shift = src->reservoir_pos;
        *out_1200th = (0x1 & (src->reservoir >> shift)) ? '1' : '0';
    } else {
        *out_1200th = 'S';
    }
    
    (src->reservoir_pos)++;
    
    return TAPE_E_OK;

}

#define MAGIC "UEF File!"

int uef_load_file (const char *fn, uef_state_t *uef) {

    uint32_t magic_len;
    uint32_t len;
    uint8_t *buf;
    int e;
    uint8_t reject_if_truncated;
    uint8_t reject_if_unknown_chunk;
    uint32_t n;

#ifdef UEF_REJECT_IF_TRUNCATED
    reject_if_truncated = 1;
#else
    reject_if_truncated = 0;
#endif

#ifdef UEF_REJECT_IF_UNKNOWN_CHUNK
    reject_if_unknown_chunk = 1;
#else
    reject_if_unknown_chunk = 0;
#endif

    uef_finish(uef);
    
    /* FIXME: stupid; it reads the file twice if unrecognised,
     * trying to decompress it the second time around,
     * whereas it should just read it once and then attempt
     * decompression on existing memory buffer */
    e = tape_load_file(fn, 1, &buf, &len);
    if (TAPE_E_OK != e) {
        log_info("uef: retrying load without compression");
        /* try again without compression */
        e = tape_load_file(fn, 0, &buf, &len);
    }
    if (TAPE_E_OK != e) {
        log_warn("uef: could not load file: '%s'", fn);
        return e;
    }
    
    magic_len = 0xff & (1 + strlen(MAGIC)); /* size_t -> uint32_t */
    if (len < (magic_len + 2)) {
        log_warn("uef: file is too short: '%s'", fn);
        free(buf);
        return TAPE_E_UEF_BAD_HEADER;
    }
    if ( 0 != memcmp (MAGIC, buf, magic_len) ) {
        log_warn("uef: bad magic: '%s'", fn);
        free(buf);
        return TAPE_E_UEF_BAD_MAGIC;
    }
    
    uef->version_minor = buf[0 + magic_len];
    uef->version_major = buf[1 + magic_len];
    log_info("uef: header OK: version %u.%u: '%s'",
             uef->version_major, uef->version_minor, fn);

    do {
        e = chunks_decode(buf + magic_len + 2,
                          len - (magic_len + 2),
                          uef,
                          reject_if_truncated,
                          reject_if_unknown_chunk);
        free(buf);
        if (TAPE_E_OK != e) { break; }
        
        e = chunks_verify_lengths (uef->chunks, uef->num_chunks);
        if (TAPE_E_OK != e) { break; }
        
        e = uef_parse_global_chunks(uef);
        if (TAPE_E_OK != e) { break; }
        
        e = uef_verify_meta_chunks(uef);
        if (TAPE_E_OK != e) { break; }
        
    } while (0);
    
    if (TAPE_E_OK != e) {
        uef_finish(uef);
        return e;
    }
        
    /*
     * Today's haiku:
     * 
     * log origin chunks
     * text may lack termination
     * we'll need a copy.
     */
    for (n=0; n < uef->globals.num_origins; n++) {
    
        char *min, *maj;
        char *s;
        uef_origin_t *o;
        size_t z, dp, a;
        int maj_i, min_i;
                
        o = uef->globals.origins + n;
        
//~ memcpy(o->data, "MakeUEF V10.10.", o->len = 15);

        s = malloc(1 + o->len);
        if (NULL == s) { continue; }
        
        s[o->len] = '\0';
        memcpy(s, o->data, o->len);
        
        len = strlen(s);
        
        /* dispel curses */
        for (z=0; z < len; z++) {
            if (!isprint(s[z])) {
                s[z]='?';
            }
        }
        
        log_info("uef: origins[%u]: \"%s\"", n, s);
        
        /* hunt for MakeUEF < 2.4, which has even and odd
         * parity mixed up for chunk &104. NOTE: we only record
         * the FIRST instance of any MakeUEF version origin chunk; if
         * multiples show up, later ones are ignored. */
        if ( (len >= 12) && (0 == strncmp (s, "MakeUEF V", 9) ) ) {
             
            if (uef->globals.have_makeuef_version) {
                log_warn("uef: multiple MakeUEF version origin chunks; ignoring later ones");
            } else {
                /* found MakeUEF version origin chunk */
                for (z=9, dp=0; z < len; z++) {
                    if ('.'==s[z]) {
                        dp = z;
                        break;
                    }
                }
                if ((dp > 9) && (dp < (len-1)))  { /* found decimal point */
                    /* null terminate DP */
                    s[dp] = '\0';
                    maj = s + 9;
                    min = s + dp + 1;
                    for (a=0; (a < strlen(maj)) && isdigit(maj[a]); a++);
                    if (a!=strlen(maj)) { continue; }
                    for (a=0; (a < strlen(min)) && isdigit(min[a]); a++);
                    if (0==a) { continue; }
                    maj_i = atoi(maj);
                    min_i = atoi(min);
                    uef->globals.have_makeuef_version = 1;
                    uef->globals.makeuef_version_major = maj_i;
                    uef->globals.makeuef_version_minor = min_i;
                    log_info("uef: MakeUEF detected: version %d.%d", maj_i, min_i);
                    uef->reverse_even_and_odd_parity = ((maj_i < 2) || ((2==maj_i)&&(min_i<4)));
                    if (uef->reverse_even_and_odd_parity) {
                        log_warn("uef: MakeUEF < 2.4 detected (%d.%d); reversing E&O parity for chunk &104", maj_i, min_i);
                    }
                }
            } /* endif first makeuef origin chunk */
        } /* endif makeuef origin chunk*/
        
        free(s);
        
    } /* next origin chunk */
    
    return e;
    
}

#define UEF_CHUNK_DELTA 10

static int chunks_decode (uint8_t *buf,
                          uint32_t len,
                          uef_state_t *uef,
                          uint8_t reject_if_truncated,
                          uint8_t reject_if_unknown_chunk) {

    uint32_t pos;
    int32_t alloc;
    uint32_t chunklen;
    
    chunklen = 0;
    
    for (pos=0, uef->num_chunks=0, alloc=0;
         pos < len;
         pos += (6 + chunklen)) {
         
        uint16_t type;
        uef_chunk_t *p;
        int32_t newsize;
        int e;
        
        if (uef->num_chunks >= alloc) {
            newsize = alloc + UEF_CHUNK_DELTA;
            p = realloc(uef->chunks, newsize * sizeof(uef_chunk_t));
            if (NULL == p) {
                log_warn("uef: could not reallocate chunks\n");
                uef_finish(uef);
                return TAPE_E_MALLOC;
            }
            uef->chunks = p;
            alloc = newsize;
        }
        
        /* ensure type and chunklen are in-bounds */
        if ((pos + 6) >= len) {
            log_warn("uef: chunk #%d: truncated chunk header\n", uef->num_chunks+1);
            if (reject_if_truncated) {
                uef_finish(uef);
                return TAPE_E_UEF_TRUNCATED;
            } else {
                break;
            }
        }
       
        type = tape_read_u16(buf + pos);
        chunklen = tape_read_u32(buf + pos + 2);
        
//~ printf("type %x, len %u\n", type, chunklen);

        if ( ! valid_chunktype(type) ) {
            log_warn("uef: unknown chunk type &%x", type);
            if (reject_if_unknown_chunk) {
                uef_finish(uef);
                return TAPE_E_UEF_UNKNOWN_CHUNK;
            } else {
                /* prevent this chunk from making it into the list */
                continue;
            }
        }
        
        /* ensure data is in-bounds */
        if ((pos + 6 + chunklen) > len) {
            log_warn("uef: chunk #%d: truncated chunk body (pos + 6 = &%x, chunklen = &%x, buflen = &%x)\n",
                     uef->num_chunks+1, pos + 6, chunklen, len);
            if (reject_if_truncated) {
                uef_finish(uef);
                return TAPE_E_UEF_TRUNCATED;
            } else {
                break;
            }
        }
        
        e = uef_store_chunk (buf + pos + 6,
                             type,
                             chunklen,
                             uef->num_chunks,
                             uef->chunks + uef->num_chunks);
        if (TAPE_E_OK != e) { return e; }
        
        (uef->num_chunks)++;
        
    }
    
    /*
    int x;
    for (x=0; x < uef->num_chunks; x++) {
        int z;
        uef_chunk_t *c;
        c = uef->chunks + x;
        printf("\n\ntype &%x len &%x\n\n", c->type, c->len);
        for (z=0; z < c->len; z++) {
            printf("%02x", c->data[z]);
        }
    }
    */
    return TAPE_E_OK;
    
}


static uint8_t valid_chunktype (uint16_t t) {
    return    (0==t)||(1==t)||(3==t)||(5==t)||(6==t)||(7==t)||(8==t)||(9==t)||(10==t)
           || (0x100==t)||(0x101==t)||(0x102==t)||(0x104==t)||(0x110==t)||(0x111==t)
           || (0x112==t)||(0x116==t)||(0x113==t)||(0x114==t)||(0x115==t)||(0x117==t)
           || (0x120==t)||(0x130==t)||(0x131==t);
}

#define UEF_CHUNKLEN_MAX 0xffffff /* shrug */

static int uef_store_chunk (uint8_t *buf,
                            uint16_t type,
                            uint32_t len,
                            int32_t chunknum,
                            uef_chunk_t *chunk) {
                            
    chunk->type = type;
    chunk->len  = len;
    chunk->data = NULL;
    
    /*printf("uef_store_chunk(%d): type &%x, len %u\n", chunknum, type, len);*/
    
    if (0 == len) { return TAPE_E_OK; }
    
    /* enforce some arbitrary maximum chunk length */
    if (len > UEF_CHUNKLEN_MAX) {
        log_warn("uef: oversized chunk #%d, at &%x bytes", chunknum+1, len);
        return TAPE_E_UEF_OVERSIZED_CHUNK;
    }
    
    chunk->data = malloc (len + 1); /* get an extra byte, for strings */
    chunk->data[len] = '\0';

    if (NULL == chunk->data) {
        log_warn("uef: could not allocate chunk data for chunk #%d", chunknum+1);
        return TAPE_E_MALLOC;
    }
    
    memcpy(chunk->data, buf, len);
    return TAPE_E_OK;
    
}


#define UEF_LOG_GLOBAL_CHUNKS

static int uef_parse_global_chunks (uef_state_t *u) {

    uint32_t nrh, no, ni, nis, ntm;
    int32_t n;
    int e;
    uef_globals_t *g;
    
    e = TAPE_E_OK;
    
    /* deal with &00xx chunks, which apply to the entire file,
     * and copy properties onto the uef_state_t struct.
     * 
     * UEF doesn't always specify what to do if several instances of
     * these chunks are present.
     * 
     * We allow chunks 0, 1, 3, 5, 8 in multiplicity.
     * 6, 7, 9 and &A will be singletons.
     */
     
    g = &(u->globals); /* save some typing */
    memset (g, 0, sizeof(uef_globals_t));
     
    /* perform an initial pass, so we know how many of each
     * multichunk exists. */
    for (n=0; n < u->num_chunks; n++) {
        if (0x0 == u->chunks[n].type) { (g->num_origins)++;         }
        if (0x1 == u->chunks[n].type) { (g->num_instructions)++;    }
        if (0x3 == u->chunks[n].type) { (g->num_inlay_scans)++;     }
        if (0x5 == u->chunks[n].type) { (g->num_target_machines)++; }
        if (0x8 == u->chunks[n].type) { (g->num_rom_hints)++;       }
    }

    if ((TAPE_E_OK == e) && (g->num_origins > 0)) {
        g->origins = malloc(sizeof(uef_origin_t) * g->num_origins);
        if (NULL == g->origins) {
            log_warn("uef: could not allocate origins list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_instructions > 0)) {
        g->instructions = malloc(sizeof(uef_instructions_t) * g->num_instructions);
        if (NULL == g->instructions) {
            log_warn("uef: could not allocate instructions list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_inlay_scans > 0)) {
        g->inlay_scans = malloc(sizeof(uef_inlay_scan_t) * g->num_inlay_scans);
        if (NULL == g->inlay_scans) {
            log_warn("uef: could not allocate inlay_scans list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_target_machines > 0)) {
        g->target_machines = malloc(g->num_target_machines); /* one byte each */
        if (NULL == g->target_machines) {
            log_warn("uef: could not allocate target_machines list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_rom_hints > 0)) {
        g->rom_hints = malloc(sizeof(uef_rom_hint_t) * g->num_rom_hints);
        if (NULL == g->rom_hints) {
            log_warn("uef: could not allocate rom hints");
            e = TAPE_E_MALLOC;
        }
    }
    
    if (TAPE_E_OK == e) {
        log_info("uef: multichunk counts: &0/%u; &1/%u; &3/%u; &5/%u; &8/%u\n",
                 g->num_origins, g->num_instructions, g->num_inlay_scans,
                 g->num_target_machines, g->num_rom_hints);
    }
    
    for (n=0, no=0, ni=0, nis=0, ntm=0, nrh=0;
         (TAPE_E_OK == e) && (n < u->num_chunks);
         n++) {
        uef_chunk_t *c;
        uint8_t log_it;
        uint8_t nyb_hi, nyb_lo;
        c = u->chunks + n;
        log_it = 0;
        if ( 0x0 == c->type ) {
            g->origins[no].data = (char *) c->data;
            g->origins[no].len  = c->len;
            no++;
            log_it = 1;
        } else if ( 0x1 == c->type )  {
            g->instructions[ni].data = (char *) c->data;
            g->instructions[ni].len  = c->len;
            ni++;
            log_it = 1;
        } else if ( 0x3 == c->type ) {
            /* no parsing or validation is done on this yet
             * NOTE: some header parsing is done independently
             * by chunk_verify_length(), can leverage that code */
            g->inlay_scans[nis].data = (char *) c->data;
            g->inlay_scans[nis].len  = c->len;
            nis++;
            log_it = 1;
        } else if ( 0x5 == c->type ) {
            /* validate this */
            if (c->len != 1) {
                log_warn("uef: target machine chunk is wrong length %u", c->len);
                e = TAPE_E_UEF_CHUNKLEN_0005;
                break;
            }
            nyb_hi = (c->data[0]>>4)&0xf;
            nyb_lo = c->data[0]&0xf;
            if ((nyb_hi>0x4) || (nyb_lo>0x2)) {
                log_warn("uef: invalid target machine chunk");
                e = TAPE_E_UEF_CHUNKDAT_0005;
                break;
            }
            g->target_machines[ntm] = c->data[0]; /* value not pointer */
            ntm++;
            log_it = 1;
        } else if ( 0x6 == c->type ) {
            if (g->have_bit_mux) {
                log_warn("uef: multiple bit multiplexing information chunks; ignoring later ones");
            } else {
                /* not sure quite how to validate this, but it
                 * probably shouldn't be larger than 4, or smaller than 1 */
                if ((c->data[0] > 4) || (c->data[0] < 1)) {
                    log_warn("uef: invalid bit multiplexing information chunk");
                    e = TAPE_E_UEF_CHUNKDAT_0006;
                    break;
                }
                g->bit_mux_info = c->data[0];
                g->have_bit_mux = 1;
                log_it = 1;
            }
        } else if ( 0x7 == c->type ) {
            if (g->have_extra_palette) {
                log_warn("uef: multiple extra palette chunks; ignoring later ones");
            } else {
                /* again, not parsed yet */
                g->extra_palette = c->data;
                g->extra_palette_len = c->len;
                g->have_extra_palette = 1;
                log_it = 1;
            }
        } else if ( 0x8 == c->type ) {
            g->rom_hints[nrh].data = c->data;
            g->rom_hints[nrh].len  = c->len;
            log_it = 1;
            nrh++;
        } else if ( 0x9 == c->type ) {
            if (g->have_short_title) {
                log_warn("uef: multiple short title chunks; ignoring later ones");
            } else {
                g->short_title = (char *) c->data;
                g->short_title_len = c->len;
                g->have_short_title = 1;
                log_it = 1;
            }
        } else if ( 0xa == c->type ) {
            if (g->have_visible_area) {
                log_warn("uef: multiple visible area chunks; ignoring later ones");
            } else {
                g->visible_area = c->data;
                g->have_visible_area = 1;
                log_it = 1;
            }
        }
#ifdef UEF_LOG_GLOBAL_CHUNKS
        if (log_it) {
            log_info("uef: \"global\" chunk type &%x, len %u", c->type, c->len);
        }
#endif
        
/* arbitrary hard sanity limit on multi chunk repeats */
#define MAX_METADATA_MULTICHUNKS 10000
        
        if (no >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of origin chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0000;
        } else if (ni >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of instructions chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0001;
        } else if (nis >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of inlay scan chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0003;
        } else if (ntm >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of target machine chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0005;
        } else if (nrh >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of rom hint chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0008;
        }
    } /* next chunk */

    if (TAPE_E_OK != e) {
        if (NULL != g->origins)         { free(g->origins);         }
        if (NULL != g->instructions)    { free(g->instructions);    }
        if (NULL != g->inlay_scans)     { free(g->inlay_scans);     }
        if (NULL != g->target_machines) { free(g->target_machines); }
        if (NULL != g->rom_hints)       { free(g->rom_hints);       }
        g->origins         = NULL;
        g->instructions    = NULL;
        g->inlay_scans     = NULL;
        g->target_machines = NULL;
        g->rom_hints       = NULL;
    }
    
    return e;
    
}

/* ------------------------- */

static int chunk_verify_length (uef_chunk_t *c) {

    uint32_t i; /* j;*/
    uint32_t len_bytes, len_bits;
    uint8_t bpp, grey; /* chunk 3 */
    int e;
    
    /* let's set a catch-all sanity-based 5MB upper limit on
     * all chunk types for now */
    if (c->len > 5000000) {
        log_warn("uef: chunk &%x length exceeds universal upper limit: %u bytes",
                 c->type, c->len);
        return TAPE_E_UEF_LONG_CHUNK;
    }
    
    if (0x0 == c->type) { /* origin information chunk */
        if (c->len < 1) {
            log_warn("uef: chunk type &0 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0000;
        }
    } else if (0x1 == c->type) { /* game instructions / manual or URL */
        if (c->len < 1) {
            log_warn("uef: chunk type &1 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0001;
        }
    } else if (0x3 == c->type) { /* inlay scan */
        if (c->len < 5) {
            log_warn("uef: chunk type &3 has bad length (%u, want >=5)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0003;
        }
        /* do some parsing of the header to predict the length
         * TODO: abstract this out into a separate function,
         * so it can be called separately if someone writes a
         * proper parser for chunk 3 */
        bpp  = 0x7f & c->data[4]; /* BPP */
        grey = 0x80 & c->data[4]; /* greyscale? */
        /* keep this simple for now and enforce BPP = 8, 16, 24 or 32 */
        if ((bpp!=8)&&(bpp!=16)&&(bpp!=24)&&(bpp!=32)) {
            log_warn("uef: chunk type &3 has bizarre BPP value &%u", bpp);
            return TAPE_E_UEF_INLAY_SCAN_BPP;
        }
        /* compute predicted size */
        i =   ((uint32_t)tape_read_u16(c->data+0))
            * ((uint32_t)tape_read_u16(c->data+2))
            * (bpp/8);
        if (0==i) {
            log_warn("uef: chunk type &3 has a pixel size of 0");
            return TAPE_E_UEF_INLAY_SCAN_ZERO;
        }
        i += 5;
        if ((8==bpp) && ! grey) {
            i += 768; /* palette */
        }
        if (c->len != i) {
            log_warn("uef: chunk type &3 has bad length (%u, want %u)", c->len, i);
            return TAPE_E_UEF_CHUNKLEN_0003;
        }
    } else if (0x5 == c->type) { /* target machine chunk */
        if (c->len != 1) {
            log_warn("uef: chunk type &5 has bad length (%u, want 1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0005;
        }
    } else if (0x6 == c->type) { /* bit multiplexing information */
        if (c->len != 1) {
            log_warn("uef: chunk type &6 has bad length (%u, want 1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0006;
        }
    } else if (0x7 == c->type) { /* extra palette */
        if (c->len < 3) {
            log_warn("uef: chunk type &7 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0007;
        }
    } else if (0x8 == c->type) { /* ROM hint */
        if (c->len < 3) {
            log_warn("uef: chunk type &8 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0008;
        }
    } else if (0x9 == c->type) { /* short title */
        if (c->len < 1) {
            log_warn("uef: chunk type &9 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0009;
        }
#define UEF_SHORT_TITLE_MAX_LEN 255 /* shrug */
        if (c->len > UEF_SHORT_TITLE_MAX_LEN) {
            log_warn("uef: short title chunk &9 is too long (%u bytes)",
                     c->len);
            return TAPE_E_UEF_CHUNKLEN_0009;
        }
    } else if (0xa == c->type) { /* visible area */
        if (c->len != 8) {
            log_warn("uef: chunk type &a has bad length (%u, want 8)", c->len);
            return TAPE_E_UEF_CHUNKLEN_000A;
        }
    } else if (0x100 == c->type) { /* 8N1 chunk */
        if (c->len < 1) {
            log_warn("uef: chunk type &100 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0100;
        }
    //~ } else if (0x101 == c->type) {
        //~ /* FIXME: multiplexed nonsense */
        //~ return TAPE_E_OK;
    } else if (0x102 == c->type) { /* raw bits */
        if (c->len < 1) {
            log_warn("uef: chunk type &102 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0102;
        }
        e = compute_chunk_102_data_len (c->len, c->data[0], &len_bytes, &len_bits);
        if (TAPE_E_OK != e) { return e; }
        /* chunk len is data len plus one: */
        if (c->len != (len_bytes + 1)) {
            log_warn("uef: chunk type &102 has bad length (%u, expect %u)",
                     c->len, len_bytes + 1);
            return TAPE_E_UEF_CHUNKLEN_0102;
        }
    } else if (0x104 == c->type) { /* programmable framing */
        if (c->len < 3) {
            log_warn("uef: chunk type &104 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0104;
        }
    } else if (0x110 == c->type) { /* leader */
        if (c->len != 2) {
            log_warn("uef: chunk type &110 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0110;
        }
    } else if (0x111 == c->type) { /* leader + &AA + leader */
        if (c->len != 4) {
            log_warn("uef: chunk type &111 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0111;
        }
    } else if (0x112 == c->type) { /* integer gap */
        if (c->len != 2) {
            log_warn("uef: chunk type &112 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0112;
        }
    } else if (0x116 == c->type) { /* float gap */
        if (c->len != 4) {
            log_warn("uef: chunk type &116 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0116;
        }
    } else if (0x113 == c->type) { /* baud (float) */
        if (c->len != 4) {
            log_warn("uef: chunk type &113 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0113;
        }
    } else if (0x114 == c->type) { /* arbitrary cycles */
        if (c->len < 6) {
            log_warn("uef: chunk type &114 has bad length (%u, want >=6)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0114;
        }
    } else if (0x115 == c->type) { /* phase change */
        if (c->len != 2) {
            log_warn("uef: chunk type &115 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0115;
        }
    } else if (0x117 == c->type) { /* baud */
        if (c->len != 2) {
            log_warn("uef: chunk type &117 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0117;
        }
    } else if (0x120 == c->type) { /* position marker text */
        if (c->len < 1) {
            log_warn("uef: chunk type &120 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0120;
        }
    } else if (0x130 == c->type) {
        if (c->len != 3) {
            log_warn("uef: chunk type &130 has bad length (%u, want 3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0130;
        }
    } else if (0x131 == c->type) { /* start of tape side */
        if ((c->len < 3) || (c->len > 258)) {
            log_warn("uef: chunk type &131 has bad length (%u, want 3-258)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0131;
        }
    }
    
    return TAPE_E_OK;
    
}


/* -------------------------- */

int uef_clone (uef_state_t *out, uef_state_t *in) {
    /* globals poses a problem here, because it's mostly a list of
     * pointers into in->chunks[]->data; we need globals fields to point
     * into out->chunks[]->data instead. The cleanest way to fix this is
     * probably just to zero out->globals, then run uef_parse_global_chunks()
     * on out. rom_hints will also be allocated and populated separately
     * for out. */
    int32_t n;
    int e;
    memcpy(out, in, sizeof(uef_state_t));
    memset(&(out->globals), 0, sizeof(uef_globals_t));
    out->chunks = malloc(sizeof(uef_chunk_t) * in->num_chunks);
    if (NULL == out->chunks) {
        log_warn("uef: could not allocate clone UEF chunks\n");
        return TAPE_E_MALLOC;
    }
    for (n=0; n < out->num_chunks; n++) {
        memcpy(out->chunks + n, in->chunks + n, sizeof(uef_chunk_t));
        out->chunks[n].data = NULL; /* no, that doesn't belong to you */
    }
    for (n=0; n < out->num_chunks; n++) {
        /* remember, we allocated one extra in case of strings */
        out->chunks[n].data = malloc(out->chunks[n].len + 1);
        if (NULL == out->chunks[n].data) {
            log_warn("uef: could not allocate clone UEF chunk data\n");
            uef_finish(out);
            return TAPE_E_MALLOC;
        }
        /* again, one extra */
        memcpy(out->chunks[n].data, in->chunks[n].data, out->chunks[n].len + 1);
    }
    e = uef_parse_global_chunks(out);
    if (TAPE_E_OK != e) {
        uef_finish(out);
    }
    return e;
}

void uef_rewind (uef_state_t *u) {
    u->cur_chunk = -1;
    init_bitsource(&(u->bitsrc));
}

void uef_finish (uef_state_t *u) {
    int32_t n;
    if (NULL == u) { return; }
    /* The fields on u->globals just point into u->chunks[].data,
     * but these ones are multi and require lists: */
    if (NULL != u->globals.origins) {
        free(u->globals.origins);
    }
    if (NULL != u->globals.instructions) {
        free(u->globals.instructions);
    }
    if (NULL != u->globals.inlay_scans) {
        free(u->globals.inlay_scans);
    }
    if (NULL != u->globals.target_machines) {
        free(u->globals.target_machines);
    }
    if (NULL != u->globals.rom_hints) {
        free(u->globals.rom_hints);
    }
    for (n=0; n < u->num_chunks; n++) {
        if (NULL != u->chunks[n].data) {
            free(u->chunks[n].data);
        }
    }
    free(u->chunks);
    memset(u, 0, sizeof(uef_state_t));
}


static int chunks_verify_lengths (uef_chunk_t *chunks, int32_t num_chunks) {
    int32_t n;
    int e;
    e = TAPE_E_OK;
    for (n=0; (TAPE_E_OK == e) && (n < num_chunks); n++) {
        e = chunk_verify_length(chunks + n);
    }
    return e;
}


#define REJECT_WEIRD_CHUNK_102_FIRST_BYTE

static int compute_chunk_102_data_len (uint32_t chunk_len,
                                       uint8_t data0,
                                       uint32_t *len_bytes_out,
                                       uint32_t *len_bits_out) {
    uint32_t i;
    *len_bits_out  = 0;
    *len_bytes_out = 0;
    if (data0 < 8) {
        /*log_info ("uef: chunk &102: interpretation A");*/
        data0 += 8;
    } else if (data0 < 16) {
        /*log_info ("uef: chunk &102: interpretation B");*/
    } else {
        log_warn ("uef: chunk &102: data[0] is weird (&%x, expect < &10)", data0);
#ifdef REJECT_WEIRD_CHUNK_102_FIRST_BYTE
        return TAPE_E_UEF_0102_WEIRD_DATA_0;
#endif
    }
    /* length of data in bits: */
    i = (chunk_len * 8) - data0;
    *len_bits_out = i;
    /* length of data in bytes: */
    if ((i % 8) == 0) {
        i = (i / 8);
    } else {
        i = (i / 8) + 1;
    }
    *len_bytes_out = i;
    return TAPE_E_OK;
}



