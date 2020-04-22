/* Tables to enable disassembling the CINTCODE used as an intermediate
 * code for BCPL on the RCP implementation for the BC Micro.
 */

#include "cintcode_tabs.h"

/* CINTCODE opcodes */

const cintcode_op cintcode_ops[256] = {
    /* 00 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* 01 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* 02 */ { "BRK",   CAM_IMP,  CIT_OTHR }, /* 826A */
    /* 03 */ { "K3",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 04 */ { "K4",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 05 */ { "K5",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 06 */ { "K6",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 07 */ { "K7",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 08 */ { "K8",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 09 */ { "K9",    CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 0A */ { "K10",   CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 0B */ { "K11",   CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 0C */ { "K12",   CAM_IMP,  CIT_CALL }, /* 82F2 */
    /* 0D */ { "CODE1", CAM_IMP,  CIT_OTHR }, /* 871A */
    /* 0E */ { "LM",    CAM_BYTE, CIT_OTHR }, /* 8678 */
    /* 0F */ { "LM1",   CAM_IMP,  CIT_OTHR }, /* 83D3 */
    /* 10 */ { "L0",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 11 */ { "L1",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 12 */ { "L2",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 13 */ { "L3",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 14 */ { "L4",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 15 */ { "L5",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 16 */ { "L6",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 17 */ { "L7",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 18 */ { "L8",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 19 */ { "L9",    CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 1A */ { "L10",   CAM_IMP,  CIT_OTHR }, /* 831E */
    /* 1B */ { "FHOP",  CAM_IMP,  CIT_OTHR }, /* 8317 */
    /* 1C */ { "JEQ",   CAM_BREL, CIT_CJMP }, /* 8234 */
    /* 1D */ { "JEQ$",  CAM_BIND, CIT_CJMP }, /* 8234 */
    /* 1E */ { "JEQ0",  CAM_BREL, CIT_CJMP }, /* 824D */
    /* 1F */ { "JEQ0$", CAM_BIND, CIT_CJMP }, /* 824D */
    /* 20 */ { "K",     CAM_BYTE, CIT_CALL }, /* 82FA */
    /* 21 */ { "KW",    CAM_WORD, CIT_CALL }, /* 8303 */
    /* 22 */ { "S0G",   CAM_GLB0, CIT_OTHR }, /* 8597 */
    /* 23 */ { "K3G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 24 */ { "K4G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 25 */ { "K5G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 26 */ { "K6G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 27 */ { "K7G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 28 */ { "K8G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 29 */ { "K9G",   CAM_GLB0, CIT_CALL }, /* 830E */
    /* 2A */ { "K10G",  CAM_GLB0, CIT_CALL }, /* 830E */
    /* 2B */ { "K11G",  CAM_GLB0, CIT_CALL }, /* 830E */
    /* 2C */ { "K12G",  CAM_GLB0, CIT_CALL }, /* 830E */
    /* 2D */ { "L0G",   CAM_GLB0, CIT_OTHR }, /* 833E */
    /* 2E */ { "L1G",   CAM_GLB0, CIT_OTHR }, /* 833E */
    /* 2F */ { "L2G",   CAM_GLB0, CIT_OTHR }, /* 833E */
    /* 30 */ { "LG",    CAM_GLB0, CIT_OTHR }, /* 8352 */
    /* 31 */ { "SG",    CAM_GLB0, CIT_OTHR }, /* 8358 */
    /* 32 */ { "LLG",   CAM_GLB0, CIT_OTHR }, /* 8363 */
    /* 33 */ { "AG",    CAM_GLB0, CIT_OTHR }, /* 836C */
    /* 34 */ { "MUL",   CAM_IMP,  CIT_OTHR }, /* 8277 */
    /* 35 */ { "DIV",   CAM_IMP,  CIT_OTHR }, /* 82A0 */
    /* 36 */ { "REM",   CAM_IMP,  CIT_OTHR }, /* 82A6 */
    /* 37 */ { "XOR",   CAM_IMP,  CIT_OTHR }, /* 8331 */
    /* 38 */ { "SL",    CAM_BREL, CIT_OTHR }, /* 82DB */
    /* 39 */ { "SL$",   CAM_BIND, CIT_OTHR }, /* 82DB */
    /* 3A */ { "LL",    CAM_BREL, CIT_OTHR }, /* 82E6 */
    /* 3B */ { "LL$",   CAM_BIND, CIT_OTHR }, /* 82E6 */
    /* 3C */ { "JNE",   CAM_BREL, CIT_CJMP }, /* 8238 */
    /* 3D */ { "JNE$",  CAM_BIND, CIT_CJMP }, /* 8238 */
    /* 3E */ { "JNE0",  CAM_BREL, CIT_CJMP }, /* 8251 */
    /* 3F */ { "JNE0$", CAM_BIND, CIT_CJMP }, /* 8251 */
    /* 40 */ { "LLP",   CAM_BYTE, CIT_OTHR }, /* 83B3 */
    /* 41 */ { "LLPW",  CAM_WORD, CIT_OTHR }, /* 83BF */
    /* 42 */ { "S0G1",  CAM_GLB1, CIT_OTHR }, /* 8473 */
    /* 43 */ { "K3G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 44 */ { "K4G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 45 */ { "K5G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 46 */ { "K6G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 47 */ { "K7G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 48 */ { "K8G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 49 */ { "K9G1",  CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 4A */ { "K10G1", CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 4B */ { "K11G1", CAM_GLB1, CIT_CALL }, /* 8375 */
    /* 4C */ { "K12G1", CAM_GLB1, CIT_OTHR }, /* 8375 */
    /* 4D */ { "L0G1",  CAM_GLB1, CIT_OTHR }, /* 847F */
    /* 4E */ { "L1G1",  CAM_GLB1, CIT_OTHR }, /* 847F */
    /* 4F */ { "L2G1",  CAM_GLB1, CIT_OTHR }, /* 847F */
    /* 50 */ { "LG1",   CAM_GLB1, CIT_OTHR }, /* 8493 */
    /* 51 */ { "SG1",   CAM_GLB1, CIT_OTHR }, /* 849F */
    /* 52 */ { "LLG1",  CAM_GLB1, CIT_OTHR }, /* 84AB */
    /* 53 */ { "AG1",   CAM_GLB1, CIT_OTHR }, /* 84B7 */
    /* 54 */ { "ADD",   CAM_IMP,  CIT_OTHR }, /* 83DC */
    /* 55 */ { "SUB",   CAM_IMP,  CIT_OTHR }, /* 83EA */
    /* 56 */ { "LSH",   CAM_IMP,  CIT_OTHR }, /* 8412 */
    /* 57 */ { "RSH",   CAM_IMP,  CIT_OTHR }, /* 841E */
    /* 58 */ { "AND",   CAM_IMP,  CIT_OTHR }, /* 83F8 */
    /* 59 */ { "OR",    CAM_IMP,  CIT_OTHR }, /* 8405 */
    /* 5A */ { "LLL",   CAM_BREL, CIT_OTHR }, /* 8448 */
    /* 5B */ { "LLL$",  CAM_BIND, CIT_OTHR }, /* 8448 */
    /* 5C */ { "JLS",   CAM_BREL, CIT_CJMP }, /* 823C */
    /* 5D */ { "JLS$",  CAM_BIND, CIT_CJMP }, /* 823C */
    /* 5E */ { "JLS0",  CAM_BREL, CIT_CJMP }, /* 8255 */
    /* 5F */ { "JLS0$", CAM_BIND, CIT_CJMP }, /* 8255 */
    /* 60 */ { "L",     CAM_BYTE, CIT_OTHR }, /* 8461 */
    /* 61 */ { "LW",    CAM_WORD, CIT_OTHR }, /* 846A */
    /* 62 */ { "S0G2",  CAM_GLB2, CIT_OTHR }, /* 8479 */
    /* 62 */ { "K3G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 64 */ { "K4G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 65 */ { "K5G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 66 */ { "K6G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 67 */ { "K7G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 68 */ { "K8G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 69 */ { "K9G2",  CAM_GLB2, CIT_CALL }, /* 837E */
    /* 6A */ { "K10G2", CAM_GLB2, CIT_CALL }, /* 837E */
    /* 6B */ { "K11G2", CAM_GLB2, CIT_CALL }, /* 837E */
    /* 6C */ { "K12G2", CAM_GLB2, CIT_OTHR }, /* 837E */
    /* 6D */ { "L0G2",  CAM_GLB2, CIT_OTHR }, /* 8489 */
    /* 6E */ { "L1G2",  CAM_GLB2, CIT_OTHR }, /* 8489 */
    /* 6F */ { "L2G2",  CAM_GLB2, CIT_OTHR }, /* 8489 */
    /* 70 */ { "LG2",   CAM_GLB2, CIT_OTHR }, /* 8499 */
    /* 71 */ { "SG2",   CAM_GLB2, CIT_OTHR }, /* 84A5 */
    /* 72 */ { "LLG2",  CAM_GLB2, CIT_OTHR }, /* 84B1 */
    /* 73 */ { "AG2",   CAM_GLB2, CIT_OTHR }, /* 84BD */
    /* 74 */ { "RV",    CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 75 */ { "RV1",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 76 */ { "RV2",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 77 */ { "RV3",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 78 */ { "RV4",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 79 */ { "RV5",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 7A */ { "RV6",   CAM_IMP,  CIT_OTHR }, /* 8458 */
    /* 7B */ { "RTN",   CAM_IMP,  CIT_RETN }, /* 8387 */
    /* 7C */ { "JGR",   CAM_BREL, CIT_CJMP }, /* 8240 */
    /* 7D */ { "JGR$",  CAM_BIND, CIT_CJMP }, /* 8240 */
    /* 7E */ { "JGR0",  CAM_BREL, CIT_CJMP }, /* 8259 */
    /* 7F */ { "JGR0$", CAM_BIND, CIT_CJMP }, /* 8259 */
    /* 80 */ { "LP",    CAM_BYTE, CIT_OTHR }, /* 85FA */
    /* 81 */ { "LPW",   CAM_WORD, CIT_OTHR }, /* 861F */
    /* 82 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* 83 */ { "LP3",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 84 */ { "LP4",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 85 */ { "LP5",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 86 */ { "LP6",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 87 */ { "LP7",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 88 */ { "LP8",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 89 */ { "LP9",   CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8A */ { "LP10",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8B */ { "LP11",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8C */ { "LP12",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8D */ { "LP13",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8E */ { "LP14",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 8F */ { "LP15",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 90 */ { "LP16",  CAM_IMP,  CIT_OTHR }, /* 8600 */
    /* 91 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 865E */
    /* 92 */ { "SWB",   CAM_IMP,  CIT_OTHR }, /* 84C3 */
    /* 93 */ { "SWL",   CAM_IMP,  CIT_OTHR }, /* 84F5 */
    /* 94 */ { "ST",    CAM_IMP,  CIT_OTHR }, /* 85A1 */
    /* 95 */ { "ST1",   CAM_IMP,  CIT_OTHR }, /* 85A1 */
    /* 96 */ { "ST2",   CAM_IMP,  CIT_OTHR }, /* 85A1 */
    /* 97 */ { "ST3",   CAM_IMP,  CIT_OTHR }, /* 85A1 */
    /* 98 */ { "STP3",  CAM_IMP,  CIT_OTHR }, /* 864F */
    /* 99 */ { "STP4",  CAM_IMP,  CIT_OTHR }, /* 864F */
    /* 9A */ { "STP5",  CAM_IMP,  CIT_OTHR }, /* 864F */
    /* 9B */ { "GOTO",  CAM_IMP,  CIT_UJMP }, /* 8559 */
    /* 9C */ { "JLE",   CAM_BREL, CIT_CJMP }, /* 8244 */
    /* 9D */ { "JLE$",  CAM_BIND, CIT_CJMP }, /* 8244 */
    /* 9E */ { "JLE0",  CAM_BREL, CIT_CJMP }, /* 825D */
    /* 9F */ { "JLE0$", CAM_BIND, CIT_CJMP }, /* 825D */
    /* A0 */ { "SP",    CAM_BYTE, CIT_OTHR }, /* 8638 */
    /* A1 */ { "SPW",   CAM_WORD, CIT_OTHR }, /* 8646 */
    /* A2 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* A3 */ { "SP3",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A4 */ { "SP4",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A5 */ { "SP5",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A6 */ { "SP6",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A7 */ { "SP7",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A8 */ { "SP8",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* A9 */ { "SP9",   CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AA */ { "SP10",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AB */ { "SP11",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AC */ { "SP12",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AD */ { "SP13",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AE */ { "SP14",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* AF */ { "SP15",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* B0 */ { "SP16",  CAM_IMP,  CIT_OTHR }, /* 862B */
    /* B1 */ { "S1",    CAM_IMP,  CIT_OTHR }, /* 85E9 */
    /* B2 */ { "S2",    CAM_IMP,  CIT_OTHR }, /* 85E9 */
    /* B3 */ { "S3",    CAM_IMP,  CIT_OTHR }, /* 85E9 */
    /* B4 */ { "S4",    CAM_IMP,  CIT_OTHR }, /* 85E9 */
    /* B5 */ { "XCH",   CAM_IMP,  CIT_OTHR }, /* 85C9 */
    /* B6 */ { "GBYT",  CAM_IMP,  CIT_OTHR }, /* 856D */
    /* B7 */ { "PBYT",  CAM_IMP,  CIT_OTHR }, /* 8577 */
    /* B8 */ { "ATC",   CAM_IMP,  CIT_OTHR }, /* 85DE */
    /* B9 */ { "ATB",   CAM_IMP,  CIT_OTHR }, /* 85D8 */
    /* BA */ { "J",     CAM_BREL, CIT_UJMP }, /* 8266 */
    /* BB */ { "J$",    CAM_BIND, CIT_UJMP }, /* 8266 */
    /* BC */ { "JGE",   CAM_BREL, CIT_CJMP }, /* 8248 */
    /* BD */ { "JGE$",  CAM_BIND, CIT_CJMP }, /* 8248 */
    /* BE */ { "JGE0",  CAM_BREL, CIT_CJMP }, /* 8261 */
    /* BF */ { "JGE0$", CAM_BIND, CIT_CJMP }, /* 8261 */
    /* C0 */ { "AP",    CAM_BYTE, CIT_OTHR }, /* 86B0 */
    /* C1 */ { "APW",   CAM_WORD, CIT_OTHR }, /* 86A7 */
    /* C2 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* C3 */ { "AP3",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C4 */ { "AP4",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C5 */ { "AP5",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C6 */ { "AP6",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C7 */ { "AP7",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C8 */ { "AP8",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* C9 */ { "AP9",   CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* CA */ { "AP10",  CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* CB */ { "AP11",  CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* CC */ { "AP12",  CAM_IMP,  CIT_OTHR }, /* 86B6 */
    /* CD */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* CE */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* CF */ { "CODE2", CAM_IMP,  CIT_OTHR }, /* 8701 */
    /* D0 */ { "NOP",   CAM_IMP,  CIT_OTHR }, /* 8697 */
    /* D1 */ { "A1",    CAM_IMP,  CIT_OTHR }, /* 868C */
    /* D2 */ { "A2",    CAM_IMP,  CIT_OTHR }, /* 868C */
    /* D3 */ { "A3",    CAM_IMP,  CIT_OTHR }, /* 868C */
    /* D4 */ { "A4",    CAM_IMP,  CIT_OTHR }, /* 868C */
    /* D5 */ { "A5",    CAM_IMP,  CIT_OTHR }, /* 868C */
    /* D6 */ { "RVP3",  CAM_IMP,  CIT_OTHR }, /* 86C1 */
    /* D7 */ { "RVP4",  CAM_IMP,  CIT_OTHR }, /* 86C1 */
    /* D8 */ { "RVP5",  CAM_IMP,  CIT_OTHR }, /* 86C1 */
    /* D9 */ { "RVP6",  CAM_IMP,  CIT_OTHR }, /* 86C1 */
    /* DA */ { "RVP7",  CAM_IMP,  CIT_OTHR }, /* 86C1 */
    /* DB */ { "ST0P3", CAM_IMP,  CIT_OTHR }, /* 85AE */
    /* DC */ { "ST0P4", CAM_IMP,  CIT_OTHR }, /* 85AE */
    /* DD */ { "ST1P3", CAM_IMP,  CIT_OTHR }, /* 85B4 */
    /* DE */ { "ST1P4", CAM_IMP,  CIT_OTHR }, /* 85B4 */
    /* DF */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* E0 */ { "A",     CAM_BYTE, CIT_OTHR }, /* 8661 */
    /* E1 */ { "AW",    CAM_WORD, CIT_OTHR }, /* 8667 */
    /* E2 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* E3 */ { "L0P3",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E4 */ { "L0P4",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E5 */ { "L0P5",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E6 */ { "L0P6",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E7 */ { "L0P7",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E8 */ { "L0P8",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* E9 */ { "L0P9",  CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* EA */ { "L0P10", CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* EB */ { "L0P11", CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* EC */ { "L0P12", CAM_IMP,  CIT_OTHR }, /* 86D0 */
    /* ED */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* EE */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* EF */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* F0 */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* F1 */ { "NEG",   CAM_IMP,  CIT_OTHR }, /* 8684 */
    /* F2 */ { "NOT",   CAM_IMP,  CIT_OTHR }, /* 869A */
    /* F3 */ { "L1P3",  CAM_IMP,  CIT_OTHR }, /* 86D6 */
    /* F4 */ { "L1P4",  CAM_IMP,  CIT_OTHR }, /* 86D6 */
    /* F5 */ { "L1P5",  CAM_IMP,  CIT_OTHR }, /* 86D6 */
    /* F6 */ { "L1P6",  CAM_IMP,  CIT_OTHR }, /* 86D6 */
    /* F7 */ { "L2P3",  CAM_IMP,  CIT_OTHR }, /* 86DC */
    /* F8 */ { "L2P4",  CAM_IMP,  CIT_OTHR }, /* 86DC */
    /* F9 */ { "L2P5",  CAM_IMP,  CIT_OTHR }, /* 86DC */
    /* FA */ { "L3P3",  CAM_IMP,  CIT_OTHR }, /* 86E3 */
    /* FB */ { "L3P4",  CAM_IMP,  CIT_OTHR }, /* 86E3 */
    /* FC */ { "L4P3",  CAM_IMP,  CIT_OTHR }, /* 86EA */
    /* FD */ { "L4P4",  CAM_IMP,  CIT_OTHR }, /* 86EA */
    /* FE */ { "--",    CAM_IMP,  CIT_OTHR }, /* 8776 */
    /* FF */ { "--",    CAM_IMP,  CIT_OTHR }  /* 8776 */
};

/* Globals */

const char cintocde_globs[CINTCODE_NGLOB][13] = {
    /* 000 */ "MAXGLOB",
    /* 001 */ "START",
    /* 002 */ "STOP",
    /* 003 */ "STARTINIT",
    /* 004 */ "ABORTCODE",
    /* 005 */ "ABORTLABEL",
    /* 006 */ "ABORTLEVEL",
    /* 007 */ "G007",
    /* 008 */ "G008",
    /* 009 */ "G009",
    /* 010 */ "G010",
    /* 011 */ "MCRESULT",
    /* 012 */ "G012",
    /* 013 */ "G013",
    /* 014 */ "G014",
    /* 015 */ "RESULT2",
    /* 016 */ "STOREFILES",
    /* 017 */ "STREAMCHAIN",
    /* 018 */ "SYSINDEX",
    /* 019 */ "TRAPSTACK",
    /* 020 */ "MAINSTACK",
    /* 021 */ "LASTERROR",
    /* 022 */ "CURCO",
    /* 023 */ "ABORT",
    /* 024 */ "NDPROG",
    /* 025 */ "FREEVEC",
    /* 026 */ "GETVEC",
    /* 027 */ "LEVEL",
    /* 028 */ "LONGJUMP",
    /* 029 */ "MAXVEC",
    /* 030 */ "STACKSIZE",
    /* 031 */ "CALL",
    /* 032 */ "G032",
    /* 033 */ "CLIINSTR",
    /* 034 */ "ENDTRAP",
    /* 035 */ "OPSYS",
    /* 036 */ "MOVE",
    /* 037 */ "MULDIV",
    /* 038 */ "TRAP",
    /* 039 */ "FSTYPE",
    /* 040 */ "WRITET",
    /* 041 */ "WRITEU",
    /* 042 */ "COMPCH",
    /* 043 */ "CALLCO",
    /* 044 */ "COWAIT",
    /* 045 */ "CREATECO",
    /* 046 */ "DELETECO",
    /* 047 */ "RESUMECO",
    /* 048 */ "HEAP",
    /* 049 */ "G049",
    /* 050 */ "G050",
    /* 051 */ "G051",
    /* 052 */ "CNSLINSTR",
    /* 053 */ "CNSLOUTSTR",
    /* 054 */ "HEAPEND",
    /* 055 */ "G055",
    /* 056 */ "G056",
    /* 057 */ "G057",
    /* 058 */ "ERRORSTREAM",
    /* 059 */ "CAPCH",
    /* 060 */ "G060",
    /* 061 */ "G061",
    /* 062 */ "COMMON2",
    /* 063 */ "COMMON3",
    /* 064 */ "DELFILE",
    /* 065 */ "DELXFILE",
    /* 066 */ "FINDINPUT",
    /* 067 */ "FINDOUTPUT",
    /* 068 */ "FINDXINPUT",
    /* 069 */ "FINDXOUTPUT",
    /* 070 */ "RENAME",
    /* 071 */ "ENDREAD",
    /* 072 */ "ENDWRITE",
    /* 073 */ "INPUT",
    /* 074 */ "OUTPUT",
    /* 075 */ "RDBIN",
    /* 076 */ "RDCH",
    /* 077 */ "TESTSTR",
    /* 078 */ "G078",
    /* 079 */ "SELECTINPUT",
    /* 080 */ "SELECTOUTPUT",
    /* 081 */ "WRBIN",
    /* 082 */ "WRCH",
    /* 083 */ "UNRDCH",
    /* 084 */ "NEWLINE",
    /* 085 */ "NEWPAGE",
    /* 086 */ "WRITEA",
    /* 087 */ "WRITEBA",
    /* 088 */ "WRITED",
    /* 089 */ "G089",
    /* 090 */ "WRITEF",
    /* 091 */ "WRITEHEX",
    /* 092 */ "WRITEN",
    /* 093 */ "WRITEOCT",
    /* 094 */ "WRITES",
    /* 095 */ "COMPSTRING",
    /* 096 */ "FINDARG",
    /* 097 */ "RDARGS",
    /* 098 */ "RDITEM",
    /* 099 */ "SPLIT",
    /* 100 */ "GLOBIN",
    /* 101 */ "GLOBUNIN",
    /* 102 */ "EXTSFILE",
    /* 103 */ "G103",
    /* 104 */ "LOADSEG",
    /* 105 */ "UNLOADSEG",
    /* 106 */ "READWORDS",
    /* 107 */ "G107",
    /* 108 */ "TESTFLAGS",
    /* 109 */ "WRITEWORDS",
    /* 110 */ "G110",
    /* 111 */ "RANDOM",
    /* 112 */ "TIME",
    /* 113 */ "G113",
    /* 114 */ "LINKEDFILES",
    /* 115 */ "MODE",
    /* 116 */ "VDUINFO",
    /* 117 */ "LIBBASE",
    /* 118 */ "RUNPROG",
    /* 119 */ "G119",
    /* 120 */ "G120",
    /* 121 */ "CALLBYTE",
    /* 122 */ "TRAPSTART",
    /* 123 */ "SAVE",
    /* 124 */ "G124",
    /* 125 */ "FILETOVEC",
    /* 126 */ "VECTOFILE",
    /* 127 */ "APTOVEC",
    /* 128 */ "SOUND",
    /* 129 */ "ENVELOPE",
    /* 130 */ "G130",
    /* 131 */ "G131",
    /* 132 */ "G132",
    /* 133 */ "G133",
    /* 134 */ "G134",
    /* 135 */ "G135",
    /* 136 */ "G136",
    /* 137 */ "G137",
    /* 138 */ "G138",
    /* 139 */ "G139",
    /* 140 */ "G140",
    /* 141 */ "G141",
    /* 142 */ "G142",
    /* 143 */ "G143",
    /* 144 */ "G144",
    /* 145 */ "G145",
    /* 146 */ "G146",
    /* 147 */ "G147",
    /* 148 */ "G148",
    /* 149 */ "CONTPRG",
    /* 150 */ "G150",
    /* 151 */ "G151",
    /* 152 */ "G152",
    /* 153 */ "G153",
    /* 154 */ "G154",
    /* 155 */ "G155",
    /* 156 */ "G156",
    /* 157 */ "G157",
    /* 158 */ "G158",
    /* 159 */ "G159",
    /* 160 */ "G160",
    /* 161 */ "G161",
    /* 162 */ "G162",
    /* 163 */ "G163",
    /* 164 */ "G164",
    /* 165 */ "G165",
    /* 166 */ "COLIST",
    /* 167 */ "G167",
    /* 168 */ "G168",
    /* 169 */ "READ",
    /* 170 */ "G170",
    /* 171 */ "G171",
    /* 172 */ "G172",
    /* 173 */ "G173",
    /* 174 */ "G174",
    /* 175 */ "G175",
    /* 176 */ "G176",
    /* 177 */ "G177",
    /* 178 */ "G178",
    /* 179 */ "G179",
    /* 180 */ "G180",
    /* 181 */ "G181",
    /* 182 */ "G182",
    /* 183 */ "G183",
    /* 184 */ "G184",
    /* 185 */ "G185",
    /* 186 */ "G186",
    /* 187 */ "G187",
    /* 188 */ "G188",
    /* 189 */ "G189",
    /* 190 */ "G190",
    /* 191 */ "G191",
    /* 192 */ "G192",
    /* 193 */ "G193",
    /* 194 */ "G194",
    /* 195 */ "G195",
    /* 196 */ "G196",
    /* 197 */ "G197",
    /* 198 */ "G198",
    /* 199 */ "G199",
    /* 200 */ "VDU",
    /* 201 */ "ADVAL",
    /* 202 */ "ERRORMESG",
    /* 203 */ "SHUFFLE",
    /* 204 */ "SAVEVEC",
    /* 205 */ "FAULTROUTINE",
    /* 206 */ "G206",
    /* 207 */ "G207",
    /* 208 */ "TIDYSTATE",
    /* 209 */ "G209",
    /* 210 */ "G210",
    /* 211 */ "G211",
    /* 212 */ "G212",
    /* 213 */ "MOVEBYTE",
    /* 214 */ "G214",
    /* 215 */ "G215",
    /* 216 */ "G216",
    /* 217 */ "G217",
    /* 218 */ "BACKMVBY",
    /* 219 */ "BACKMOVE"
};
