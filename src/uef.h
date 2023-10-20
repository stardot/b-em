#ifndef __INC_UEF_H
#define __INC_UEF_H

#include "tape2.h"

/* maximum number of metadata chunks that will be stored in between
 * actual bit-containing chunks */
#define UEF_MAX_METADATA 128

void uef_load(const char *fn);
void uef_close(void);
void uef_poll(void);
void uef_findfilenames(void);

extern int uef_toneon;

typedef struct uef_chunk_s {
    uint16_t type;
    uint8_t *data;
    uint32_t len;
} uef_chunk_t;

typedef struct uef_rom_hint_s {
    uint8_t *data;
    uint32_t len;
} uef_rom_hint_t;

typedef struct uef_origin_s {
    /* careful: may not be null-terminated */
    char *data;
    uint32_t len;
} uef_origin_t;

typedef struct uef_tape_set_info_s {
    /* chunk &130 */
    uint8_t vocabulary;   /* 0-4 */
    uint8_t num_tapes;    /* 1-127 */
    uint8_t num_channels; /* 1-255 */
} uef_tape_set_info_t;

typedef struct uef_start_of_tape_side_s {
    /* chunk &131; a prior chunk &130 sets the limits for this */
    /* 0-126 */
    uint8_t tape_id;
    uint8_t is_side_B;
    /* 0-254, although expect 0 and 1 to be L and R: */
    uint8_t channel_id; 
    /* NOTE: properly malloced and null-terminated;
     * doesn't just point into uef data, so it MUST BE FREED
     * once done with: */
    char *description; 
} uef_start_of_tape_side_t;

/* UNION */
typedef union uef_meta_u {
    uint16_t phase; /* chunk &115 */
    /* NOTE: properly malloced and null-terminated;
     * doesn't just point into uef data, so it MUST BE FREED
     * once done with: */
    char *position_marker; /* chunk &120 */
    uef_tape_set_info_t tape_set_info; /* chunk &130 */
    /* similarly, this must be destroyed once it's done with: */
    uef_start_of_tape_side_t start_of_tape_side; /* chunk &131 */
    uint16_t baud; /* NEW: chunk &117 */
} uef_meta_u_t;

typedef struct uef_meta_s {
    /* the generating chunk type.
     * currently supported:
     * &115 -- phase change
     * &117 -- baud rate
     * &120 -- position marker
     * &130 -- tape set info 
     * &131 -- start of tape side */
    uint16_t type;
    uef_meta_u_t data;
    uint8_t is_valid;
} uef_meta_t;

typedef struct uef_instructions_s {
    /* careful: may not be null-terminated: */
    char *data;
    uint32_t len;
} uef_instructions_t;

typedef struct uef_inlay_scan_s {
    char *data;
    uint32_t len;
} uef_inlay_scan_t;

typedef struct uef_globals_s {

    /* Note: Fields on globals just point into uef->data. Some
     * chunk types are only allowed once, in which case they have
     * a "have_..." flag to indicate that they have been found;
     * there is then a pointer for that chunk type that points into
     * uef->data. If these chunks occur multiple times, our strategy
     * will just be to ignore later ones.
     * 
     * Many global chunk types, though, may validly occur multiple
     * times. In this case, there will be a malloced list whose
     * elements point into multiple points in uef->data.
     * 
     * These lists of multiple entries will need to be freed when
     * the UEF is done. (None of the data that the lists or fields
     * pointed to should be freed.)
     */
     
    /* careful with these: they may not be null-terminated: */
    uef_origin_t *origins; /* &0 */
    uint32_t num_origins;
    
    /* careful with this: it may not be null-terminated: */
    uint32_t num_instructions; /* &1 */
    uef_instructions_t *instructions;
    
    /* raw chunk 3, no parsing done on it yet: */
    uint32_t num_inlay_scans;  /* &3 */ 
    uef_inlay_scan_t *inlay_scans;
    
    uint32_t num_target_machines; /* &5 */
    /* values; one byte; not pointers into UEF data;
     * note two independent nybbles (see UEF spec) */
    uint8_t *target_machines;
    
    uint8_t have_bit_mux; /* &6 */
    uint8_t bit_mux_info; /* value; one byte; not a pointer */
    
    /* raw chunks, no parsing done yet */
    uint8_t have_extra_palette;   /* &7 */
    uint8_t *extra_palette;
    uint32_t extra_palette_len;
    
    /* raw chunk, no parsing done yet */
    uef_rom_hint_t *rom_hints; /* &8 */
    uint32_t num_rom_hints;
    
    /* careful with this: it may not be null-terminated: */
    uint8_t have_short_title; 
    char *short_title;  /* &9 */
    uint32_t short_title_len;
    
    uint8_t have_visible_area;
    uint8_t *visible_area; /* &A */
    
    /* pulled out of origin chunk: */
    uint8_t have_makeuef_version;
    int makeuef_version_major;
    int makeuef_version_minor;
    
} uef_globals_t;


typedef struct uef_bitsource_s {

    /* This is going to work by having a local, pre-framed buffer which
     * contains some 1/1200s tones. When a tone is needed, we'll shift
     * a bit out of the buffer and send it to the ACIA.
     * 
     * When the buffer is empty,
     * we get more data from the source chunk and re-fill the pre-framed
     * buffer.
     * 
     * For chunks &100 and &104, we take 7 or 8 bits out of the source
     * chunk, frame them with start, stop and parity bits, and place
     * either 10 or 11 1200th-tones into the pre-framed buffer (a.k.a.
     * "reservoir"). If 300 baud is selected with chunk &117, this
     * becomes 40 or 44 1200th-tones. Note that the 300 baud setting
     * of the ACIA has absolutely no effect on this; it is purely
     * decided by UEF chunks.
     * 
     * For chunk &102, the framing is explicit in the chunk, so we can
     * just take 8 bits out of the chunk and place them directly into
     * the reservoir (or quadruple those up to 32 bits, if at 300 baud).
     * 
     * All the above chunk types can take data from the source chunk
     * one byte at a time -- no sub-byte resolution is needed at source.
     * 
     * This is not true of chunk &114. Chunk &114 contains cycles, not
     * bits. At 1200 baud, an output 1-bit is two source bits. An
     * output 0-bit is one source bit. At 300 baud, an output 1-bit
     * is eight source bits, and a 0-bit is four source bits. For this
     * chunk type, we will take 1 to 8 bits from the source and place
     * just a single 1200th-tone into the reservoir. This will
     * necessitate a source sub-byte position, which we don't need for
     * the other chunk types. Chunk &114 uniquely does not care about
     * the current selected UEF baud rate.
     * 
     * We also have the other chunk types that contain either leader,
     * gap, or <leader + &AA + leader>. These (&110, &111, &112, &116)
     * don't have any source data. We will mostly just be supplying
     * a single bit (i.e. two 2400 Hz cycles) to the reservoir in these
     * cases. Chunk &111 (leader + dummy byte) will send a single
     * 1200th-tone to the reservoir during the pre-leader and
     * post-leader sections; the actual dummy byte will be placed as
     * 8N1 into the reservoir, so that part only will load 10 bits.
     * Since MOS always writes the dummy byte at 1200 baud, we ignore
     * the current baud setting for the dummy byte. */
     
     /* Complicated, yes? This is why you should use TIBET instead. */
     
    uint8_t silence;       /* for gaps; one bit's worth of silence */

    uint64_t reservoir;     /* atom pairs on their way to the ACIA */
    int8_t reservoir_len;  /* total atom pairs in value */
    int8_t reservoir_pos;  /* num atom pairs already sent to ACIA */

    uint32_t src_byte_pos;      /* chunk 100, 102, 104, 114 */
    
    /* lengths of current leader or gap: */
    uint32_t nodata_total_pre_cycs;     /* chunk 110, 111, 112, 116 */
    uint32_t nodata_consumed_pre_cycs;  /* chunk 110, 111, 112, 116 */
    uint32_t nodata_total_post_cycs;    /* chunk 111 only */ 
    uint32_t nodata_consumed_post_cycs; /* chunk 111 only */ 
    
    uint8_t chunk_111_state;    /* 0 = pre-leader, 1 = &AA, 2 = post-leader */
    
    uint32_t chunk_114_src_bit_pos;   /* chunk 114 only, 0 to 7 */
    uint32_t chunk_114_total_cycs;    /* 24-bit value, &114's first 3 bytes */
    uint32_t chunk_114_consumed_cycs; /* count of total consumed cycles */
    char chunk_114_pulsewaves[2];     /* &114 bytes 4 & 5, 'P' / 'W' */
    
    /*
     * IMPORTANT: here follows the framing for the *UEF decoder*.
     * 
     * This is *not* the same thing as the framing that is currently
     * programmed into the ACIA. We *do not* allow metadata in a UEF
     * file to go around merrily reprogramming the ACIA. That's just
     * nonsense.
     * 
     * 8N1-type framing values here are derived from the start of &104
     * chunks, and used to form the correct bitstream to send to the
     * ACIA for that chunk type only. There is also the baud300 flag,
     * which will be set by an instance of chunk &117. This one is used
     * for all data chunks.
     * 
     * Chunk &102 encodes bits, not 1/1200-second pairs of atoms. At
     * 1200 baud, these are the same thing, but at 300 baud, they are
     * not.
     * 
     * This means that even if there were widespread implementation of
     * UEF chunk &102, there is still no guaranteed way to take an
     * arbitrary stream from a tape and encode it into a UEF file
     * without first having to perform some reverse-engineering of its
     * various framings. Viper's Ultron is an example; it has four
     * blocks at 8N1/300. The agent that builds the UEF needs to be
     * aware that these blocks are at 300 baud, or it will incorrectly
     * populate the &102 chunk with quadrupled bits, i.e. "1111"
     * instead of "1", and "0000" instead of "0". The UEF file would
     * also need to contain some &117 chunks to inform the UEF decoder
     * that 300 baud needs to be selected for the decoding of that &102
     * chunk.
     * 
     * It is stupid: The tape contains "1111"; chunk &102 contains "1";
     * chunk &117 tells the UEF decoder that 300 baud is selected;
     * the UEF decoder sends "1111" to the ACIA, and we are finally
     * back where we started!
     * 
     * Only chunk &114 permits
     * a direct representation of *cycles* on the tape, not *bits*, and
     * it (very generously) uses a 24-bit number to denote the number
     * of cycles in the squawk, making it easily expansive enough to
     * contain any sensible amount of data. You can get about 800K of
     * Beeb data into a single &114 chunk:
     * 
     * 2^24 cycles / 2 cycs/bit (worst case) = 2^23 bits;
     * 2^23 bits / 10 bits/frame = 839K.
     */
    serial_framing_t framing;

} uef_bitsource_t;


typedef struct uef_state_s {

    /* global data */
    uint8_t version_major;
    uint8_t version_minor;
    
    /* origin chunks, etc., which apply to entire file: */
    uef_globals_t globals;
    
    int32_t cur_chunk;
    uef_bitsource_t bitsrc;
    
    uef_chunk_t *chunks;
    int32_t num_chunks;
    
    uint8_t reverse_even_and_odd_parity;
    
} uef_state_t;

int uef_read_1200th (uef_state_t *u,
                     char *out_1200th,
                     uef_meta_t metadata_list[UEF_MAX_METADATA], /* caller must call metadata_finish() */
                     uint32_t *metadata_fill_out);
                     
                  
void uef_finish (uef_state_t *u);
int uef_clone (uef_state_t *out, uef_state_t *in);
void uef_rewind (uef_state_t *u);
int uef_load_file (const char *fn, uef_state_t *uef);
uint8_t uef_peek_eof (uef_state_t *uef);
void uef_metadata_list_finish (uef_meta_t metadata_list[UEF_MAX_METADATA],
                               uint32_t fill);

#endif
