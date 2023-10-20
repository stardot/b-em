#ifndef __INC_TAPE_H
#define __INC_TAPE_H

#include "b-em.h"
#include "acia.h"
#include "tibet.h"
#include "csw.h"
#include "uef.h"

#define SERIAL_PRINT_FRAMING_CHANGES

#define TAPE_E_OK                             0
#define TAPE_E_MALLOC                         1
#define TAPE_E_EOF                            2
#define TAPE_E_BUG                            3
#define TAPE_E_FOPEN                          4
#define TAPE_E_FTELL                          5
#define TAPE_E_FREAD                          6
#define TAPE_E_FILE_TOO_LARGE                 7
#define TAPE_E_ZLIB_INIT                      8
#define TAPE_E_ZLIB_DECOMPRESS                9
#define TAPE_E_DECOMPRESSED_TOO_LARGE         10

#define TAPE_E_CSW_BAD_MAGIC                  101
#define TAPE_E_CSW_BAD_VERSION                102
#define TAPE_E_CSW_BAD_RATE                   103
#define TAPE_E_CSW_HEADER_NUM_PULSES          104
#define TAPE_E_CSW_READ_BODY                  105
#define TAPE_E_CSW_BODY_LARGE                 106
#define TAPE_E_CSW_COMP_VALUE                 107
#define TAPE_E_CSW_BAD_FLAGS                  108
#define TAPE_E_CSW_PULSES_MISMATCH            109
#define TAPE_E_CSW_HEADER_TRUNCATED           110

#define TAPE_E_TIBET_BADCHAR                  200
#define TAPE_E_TIBET_VERSION                  201
#define TAPE_E_TIBET_VERSION_LINE             202
#define TAPE_E_TIBET_VERSION_LINE_NOSPC       203
#define TAPE_E_TIBET_UNK_WORD                 204
#define TAPE_E_TIBET_VERSION_MISMATCH         205
#define TAPE_E_TIBET_TOO_MANY_SPANS           206
#define TAPE_E_TIBET_FIELD_INCOMPAT           207
#define TAPE_E_TIBET_MULTI_DECIMAL_POINT      208
#define TAPE_E_TIBET_POINT_ENDS_DECIMAL       209
#define TAPE_E_TIBET_DECIMAL_BAD_CHAR         210
#define TAPE_E_TIBET_DECIMAL_TOO_LONG         211
#define TAPE_E_TIBET_DECIMAL_PARSE            212
#define TAPE_E_TIBET_SHORT_SILENCE            213
#define TAPE_E_TIBET_LONG_SILENCE             214
#define TAPE_E_TIBET_INT_TOO_LONG             215
#define TAPE_E_TIBET_INT_PARSE                216
#define TAPE_E_TIBET_INT_BAD_CHAR             217
#define TAPE_E_TIBET_LONG_LEADER              218
#define TAPE_E_TIBET_DUP_BAUD                 219
#define TAPE_E_TIBET_BAD_FRAMING              220
#define TAPE_E_TIBET_DUP_FRAMING              221
#define TAPE_E_TIBET_DUP_TIME                 222
#define TAPE_E_TIBET_TIME_HINT_LONG           223
#define TAPE_E_TIBET_BAD_BAUD                 224
#define TAPE_E_TIBET_DUP_PHASE                225
#define TAPE_E_TIBET_BAD_PHASE                226
#define TAPE_E_TIBET_DUP_SPEED                227
#define TAPE_E_TIBET_SPEED_HINT_HIGH          228
#define TAPE_E_TIBET_SPEED_HINT_LOW           229
#define TAPE_E_TIBET_DATA_JUNK_FOLLOWS        230
#define TAPE_E_TIBET_DATA_ILLEGAL_CHAR        231
#define TAPE_E_TIBET_DATA_DOUBLE_PULSE        232
#define TAPE_E_TIBET_DATA_EXCESSIVE_TONES     233
#define TAPE_E_TIBET_DANGLING_TIME            234
#define TAPE_E_TIBET_DANGLING_PHASE           235
#define TAPE_E_TIBET_DANGLING_SPEED           236
#define TAPE_E_TIBET_DANGLING_BAUD            237
#define TAPE_E_TIBET_DANGLING_FRAMING         238
#define TAPE_E_TIBET_NO_DECODE                239

#define TAPE_E_UEF_BAD_MAGIC                  301
#define TAPE_E_UEF_BAD_HEADER                 302
#define TAPE_E_UEF_TRUNCATED                  303
#define TAPE_E_UEF_UNKNOWN_CHUNK              304
#define TAPE_E_UEF_OVERSIZED_CHUNK            305
#define TAPE_E_UEF_TOO_MANY_METADATA_CHUNKS   306
#define TAPE_E_UEF_0104_NUM_BITS              307
#define TAPE_E_UEF_0104_NUM_STOPS             308
#define TAPE_E_UEF_CHUNKLEN_0000              309
#define TAPE_E_UEF_CHUNKLEN_0001              310
#define TAPE_E_UEF_CHUNKLEN_0003              311
#define TAPE_E_UEF_CHUNKLEN_0005              312
#define TAPE_E_UEF_CHUNKLEN_0006              313
#define TAPE_E_UEF_CHUNKLEN_0007              314
#define TAPE_E_UEF_CHUNKLEN_0008              315
#define TAPE_E_UEF_CHUNKLEN_0009              316
#define TAPE_E_UEF_CHUNKLEN_000A              317
#define TAPE_E_UEF_CHUNKLEN_0100              318
#define TAPE_E_UEF_CHUNKLEN_0102              319
#define TAPE_E_UEF_CHUNKLEN_0104              320
#define TAPE_E_UEF_CHUNKLEN_0110              321
#define TAPE_E_UEF_CHUNKLEN_0111              322
#define TAPE_E_UEF_CHUNKLEN_0112              323
#define TAPE_E_UEF_CHUNKLEN_0115              324
#define TAPE_E_UEF_CHUNKLEN_0116              325
#define TAPE_E_UEF_CHUNKLEN_0113              326
#define TAPE_E_UEF_CHUNKLEN_0114              327
#define TAPE_E_UEF_CHUNKLEN_0117              328
#define TAPE_E_UEF_CHUNKLEN_0120              329
#define TAPE_E_UEF_CHUNKLEN_0130              330
#define TAPE_E_UEF_CHUNKLEN_0131              331
#define TAPE_E_UEF_CHUNKDAT_0005              332
#define TAPE_E_UEF_CHUNKDAT_0006              333
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_1       334
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_2       335
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_COMBO   336
#define TAPE_E_UEF_CHUNK_SPENT                337
#define TAPE_E_UEF_0114_BAD_NUM_CYCS          338
#define TAPE_E_UEF_0116_NEGATIVE_GAP          339
#define TAPE_E_UEF_0116_HUGE_GAP              340
#define TAPE_E_UEF_0102_WEIRD_DATA_0          341
#define TAPE_E_UEF_EXCESS_0000                342
#define TAPE_E_UEF_EXCESS_0001                343
#define TAPE_E_UEF_EXCESS_0003                344
#define TAPE_E_UEF_EXCESS_0005                345
#define TAPE_E_UEF_EXCESS_0008                346
#define TAPE_E_UEF_0130_VOCAB                 347
#define TAPE_E_UEF_0130_NUM_TAPES             348
#define TAPE_E_UEF_0130_NUM_CHANNELS          349
#define TAPE_E_UEF_0131_TAPE_ID               350
#define TAPE_E_UEF_0131_CHANNEL_ID            351
#define TAPE_E_UEF_0131_TAPE_ID_130_LIMIT     352
#define TAPE_E_UEF_0131_CHANNEL_ID_130_LIMIT  353
#define TAPE_E_UEF_0131_DESCRIPTION_LONG      354
#define TAPE_E_UEF_LONG_CHUNK                 355 /* exceeds generic length limit */
#define TAPE_E_UEF_INLAY_SCAN_BPP             356
#define TAPE_E_UEF_INLAY_SCAN_ZERO            357
#define TAPE_E_UEF_0115_ILLEGAL               358
#define TAPE_E_UEF_0117_BAD_RATE              359

#define TAPE_FILETYPE_INVALID 0
#define TAPE_FILETYPE_UEF     1
#define TAPE_FILETYPE_CSW     2
#define TAPE_FILETYPE_TIBET   3

typedef struct serial_state_s {
       
    uint8_t poll_phase;
    /*uint8_t dcd_blipped;*/ /* removed in overhaul v2: */

    uint8_t filetype;

    /* format-specific state: */
    union {
        tibet_t tibet;
        uef_state_t uef;
        csw_state_t csw;
    } fmt;

    // vars for local byte decoding:
    uint8_t state;
    uint32_t num_1200ths_since_silence;
    uint32_t start_wait_count_1200ths;
    uint8_t bits_count;

    uint8_t frame;
    uint8_t parity_error;
    serial_framing_t framing;
#ifdef SERIAL_PRINT_FRAMING_CHANGES
    serial_framing_t prev_framing;
#endif

    uint8_t bit_phase;     /* needed for 300 baud, count to 4 */
    uint8_t bit_cycles[4]; /* for 300 baud, hold previous cycle values from tape */
    
    uint8_t tapenoise_no_emsgs;     /* prevent "ring buffer full" spam */
    uint8_t tape_finished_no_emsgs; /* overhaul v2: prevent "tape finished" spam */

} serial_state_t;



extern ALLEGRO_PATH *tape_fn;
extern bool tape_loaded;
extern serial_state_t tape_serial;
extern bool tibet_ena;
extern bool csw_ena;

void tape_rewind(void);
void tape_load(ALLEGRO_PATH *fn);
void tape_close(void);
void tape_poll(void);
void tape_receive(ACIA *acia, uint8_t data);
void findfilenames_new (void);
void tape_serial_init (serial_state_t *ser, uint8_t filetype);
void tape_serial_finish (serial_state_t *ser);
int tape_serial_clone_and_rewind (serial_state_t *out, serial_state_t *in);
int tape_load_file (const char *fn, uint8_t decompress, uint8_t **buf_out, uint32_t *len_out);
int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in);
void csw_close(void);
void tibet_close(void);
void uef_close(void);
uint32_t tape_read_u32 (uint8_t *in);
uint16_t tape_read_u16 (uint8_t *in);
uint32_t tape_read_u24 (uint8_t *in);

extern int tapelcount,tapellatch,tapeledcount;
extern bool fasttape;

#endif
