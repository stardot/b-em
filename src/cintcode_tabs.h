/* Tables to enable disassembling the CINTCODE used as an intermediate
 * code for BCPL on the RCP implementation for the BC Micro.
 */

#ifndef CINTCODE_TABS
#define CINTCODE_TABS

#include <stdint.h>

/* An entry in the table of CINTCODE opcodes. */

typedef struct {
    char mnemonic[6];
    uint8_t cc_am;
    uint8_t cc_it;
} cintcode_op;

/* CINTCODE addressing mode */

typedef enum {
    CAM_IMP,    // implied - any offsets are coded within the opcode
    CAM_BYTE,   // a single byte follows the opcode
    CAM_WORD,   // a word (16bit) follows the opcode.
    CAM_BREL,   // one byte follows, it is relative to the PC
    CAM_BIND,   // one byte follows, relative to PC, indirect
    CAM_GLB0,   // one byte global number follows, globals 0-255
    CAM_GLB1,   // one byte global number follows, globals 256-511
    CAM_GLB2    // one byte global number follows, globals 512-767
} cintcode_am;

/* CINTCODE instruction types.  Only the ones needed for tracing
 * control flow are distinguished.
 */

typedef enum {
    CIT_CJMP,   // conditional jump.
    CIT_UJMP,   // unconitional jump.
    CIT_CALL,   // call.
    CIT_RETN,   // return.
    CIT_OTHR    // anything that does not affect control flow.
} cintocde_it;

/* Table of CINTCODE opcodes */

extern const cintcode_op cintcode_ops[256];

/* Table of CINTCODE globals (Global Vector) */

#define CINTCODE_NGLOB 220

extern const char cintocde_globs[CINTCODE_NGLOB][13];

#endif
