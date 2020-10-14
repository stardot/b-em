/* -*- mode: c; c-basic-offset: 4 -*- */

#include "b-em.h"
#include "6502debug.h"
#include "6502.h"
#include "65816.h"

const char *dbg6502_reg_names[] = { "A", "X", "Y", "S", "P", "PC", NULL };

typedef enum {
    IMP,    // Implied.
    IMPA,   // Implied with A as the implied operand.
    IMM,    // Immediate, 8 bit
    IMV,    // Immediate, 8 or 16 bit depending accumulator mode.
    IMX,    // Immediate, 8 or 16 bit depending on index register mode.
    ZP,     // Zero page, known as Direct Page on the 65816.
    ZPX,    // Zero (direct) page indexed by X.
    ZPY,    // Zero (direct) page indexed by Y (for LDX).
    INDX,   // Zero (direct) page indexed (by X) indirect.
    INDY,   // Zero (direct) page indirect indexed (by Y).
    INDYL,  // Direct page indirect long indexed (by Y).  65816 only.
    IND,    // Zero (direct) page indirect.
    INDL,   // Direct page indirect long, 24 bit (65816 only)
    ABS,    // Absolute.
    ABSL,   // Absolute long, 24 bit (65816 only)
    ABSX,   // Absolute indexed by X
    ABSXL,  // Absolute indexed by X, long
    ABSY,   // Absolute indexed by Y
    IND16,  // Indirect 16bit (for JMP).
    IND1X,  // Indexed (by X) indirect (for JMP)
    PCR,    // PC-relative.  8bit signed offset from PC for branch instructions.
    PCRL,   // PC-relative.  16bit signed offset from PC.
    SR,     // Stack relative (65816 only)
    SRY,    // Stack relative indirect indexed (by Y).
    BM,     // Block moves (65816 only)
} addr_mode_t;

typedef enum {
    UND,   ADC,   ANC,   AND,   ANE,   ARR,   ASL,   ASR,   BCC,   BCS,   BEQ,
    BIT,   BMI,   BNE,   BPL,   BRA,   BRK,   BRL,   BVC,   BVS,   CLC,   CLD,
    CLI,   CLV,   CMP,   COP,   CPX,   CPY,   DCP,   DEC,   DEX,   DEY,   EOR,
    HLT,   INC,   INX,   INY,   ISB,   JML,   JMP,   JSL,   JSR,   LAS,   LAX,
    LDA,   LDX,   LDY,   LSR,   LXA,   MVN,   MVP,   NOP,   ORA,   PEA,   PEI,
    PER,   PHA,   PHB,   PHD,   PHK,   PHP,   PHX,   PHY,   PLA,   PLB,   PLD,
    PLP,   PLX,   PLY,   REP,   RLA,   ROL,   ROR,   RRA,   RTI,   RTL,   RTS,
    SAX,   SBC,   SBX,   SEC,   SED,   SEI,   SEP,   SHA,   SHS,   SHX,   SHY,
    SLO,   SRE,   STA,   STP,   STX,   STY,   STZ,   TAX,   TAY,   TCD,   TCS,
    TDC,   TRB,   TSB,   TSC,   TSX,   TXA,   TXS,   TXY,   TYA,   TYX,   WAI,
    WDM,   XBA,   XCE
} op_t;

static const char op_names[113][4] = {
    "---", "ADC", "ANC", "AND", "ANE", "ARR", "ASL", "ASR", "BCC", "BCS", "BEQ",
    "BIT", "BMI", "BNE", "BPL", "BRA", "BRK", "BRL", "BVC", "BVS", "CLC", "CLD",
    "CLI", "CLV", "CMP", "COP", "CPX", "CPY", "DCP", "DEC", "DEX", "DEY", "EOR",
    "HLT", "INC", "INX", "INY", "ISB", "JML", "JMP", "JSL", "JSR", "LAS", "LAX",
    "LDA", "LDX", "LDY", "LSR", "LXA", "MVN", "MVP", "NOP", "ORA", "PEA", "PEI",
    "PER", "PHA", "PHB", "PHD", "PHK", "PHP", "PHX", "PHY", "PLA", "PLB", "PLD",
    "PLP", "PLX", "PLY", "REP", "RLA", "ROL", "ROR", "RRA", "RTI", "RTL", "RTS",
    "SAX", "SBC", "SBX", "SEC", "SED", "SEI", "SEP", "SHA", "SHS", "SHX", "SHY",
    "SLO", "SRE", "STA", "STP", "STX", "STY", "STZ", "TAX", "TAY", "TCD", "TCS",
    "TDC", "TRB", "TSB", "TSC", "TSX", "TXA", "TXS", "TXY", "TYA", "TYX", "WAI",
    "WDM", "XBA", "XCE"
};

static const uint8_t op_cmos[256] =
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  BRK,  ORA,  UND,  UND,  TSB,  ORA,  ASL,  UND,  PHP,  ORA,  ASL,  UND,  TSB,  ORA,  ASL,  UND,
/*10*/  BPL,  ORA,  ORA,  UND,  TRB,  ORA,  ASL,  UND,  CLC,  ORA,  INC,  UND,  TRB,  ORA,  ASL,  UND,
/*20*/  JSR,  AND,  UND,  UND,  BIT,  AND,  ROL,  UND,  PLP,  AND,  ROL,  UND,  BIT,  AND,  ROL,  UND,
/*30*/  BMI,  AND,  AND,  UND,  BIT,  AND,  ROL,  UND,  SEC,  AND,  DEC,  UND,  BIT,  AND,  ROL,  UND,
/*40*/  RTI,  EOR,  UND,  UND,  UND,  EOR,  LSR,  UND,  PHA,  EOR,  LSR,  UND,  JMP,  EOR,  LSR,  UND,
/*50*/  BVC,  EOR,  EOR,  UND,  UND,  EOR,  LSR,  UND,  CLI,  EOR,  PHY,  UND,  UND,  EOR,  LSR,  UND,
/*60*/  RTS,  ADC,  UND,  UND,  STZ,  ADC,  ROR,  UND,  PLA,  ADC,  ROR,  UND,  JMP,  ADC,  ROR,  UND,
/*70*/  BVS,  ADC,  ADC,  UND,  STZ,  ADC,  ROR,  UND,  SEI,  ADC,  PLY,  UND,  JMP,  ADC,  ROR,  UND,
/*80*/  BRA,  STA,  UND,  UND,  STY,  STA,  STX,  UND,  DEY,  BIT,  TXA,  UND,  STY,  STA,  STX,  UND,
/*90*/  BCC,  STA,  STA,  UND,  STY,  STA,  STX,  UND,  TYA,  STA,  TXS,  UND,  STZ,  STA,  STZ,  UND,
/*A0*/  LDY,  LDA,  LDX,  UND,  LDY,  LDA,  LDX,  UND,  TAY,  LDA,  TAX,  UND,  LDY,  LDA,  LDX,  UND,
/*B0*/  BCS,  LDA,  LDA,  UND,  LDY,  LDA,  LDX,  UND,  CLV,  LDA,  TSX,  UND,  LDY,  LDA,  LDX,  UND,
/*C0*/  CPY,  CMP,  UND,  UND,  CPY,  CMP,  DEC,  UND,  INY,  CMP,  DEX,  WAI,  CPY,  CMP,  DEC,  UND,
/*D0*/  BNE,  CMP,  CMP,  UND,  UND,  CMP,  DEC,  UND,  CLD,  CMP,  PHX,  STP,  UND,  CMP,  DEC,  UND,
/*E0*/  CPX,  SBC,  UND,  UND,  CPX,  SBC,  INC,  UND,  INX,  SBC,  NOP,  UND,  CPX,  SBC,  INC,  UND,
/*F0*/  BEQ,  SBC,  SBC,  UND,  UND,  SBC,  INC,  UND,  SED,  SBC,  PLX,  UND,  UND,  SBC,  INC,  UND,
 };

static const uint8_t am_cmos[256]=
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*10*/  PCR,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABS,  ABSX, ABSX, IMP,
/*20*/  ABS,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*30*/  PCR,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABSX, ABSX, ABSX, IMP,
/*40*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*50*/  PCR,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*60*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  IND16,ABS,  ABS,  IMP,
/*70*/  PCR,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  IND1X,ABSX, ABSX, IMP,
/*80*/  PCR,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*90*/  PCR,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*A0*/  IMM,  INDX, IMM,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*B0*/  PCR,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABSX, ABSX, ABSY, IMP,
/*C0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*D0*/  PCR,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*E0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*F0*/  PCR,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
};

static const uint8_t op_nmos[256] =
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  BRK,  ORA,  HLT,  SLO,  NOP,  ORA,  ASL,  SLO,  PHP,  ORA,  ASL,  ANC,  NOP,  ORA,  ASL,  SLO,
/*10*/  BPL,  ORA,  HLT,  SLO,  NOP,  ORA,  ASL,  SLO,  CLC,  ORA,  NOP,  SLO,  NOP,  ORA,  ASL,  SLO,
/*20*/  JSR,  AND,  HLT,  RLA,  BIT,  AND,  ROL,  RLA,  PLP,  AND,  ROL,  ANC,  BIT,  AND,  ROL,  RLA,
/*30*/  BMI,  AND,  HLT,  RLA,  NOP,  AND,  ROL,  RLA,  SEC,  AND,  NOP,  RLA,  NOP,  AND,  ROL,  RLA,
/*40*/  RTI,  EOR,  HLT,  SRE,  NOP,  EOR,  LSR,  SRE,  PHA,  EOR,  LSR,  ASR,  JMP,  EOR,  LSR,  SRE,
/*50*/  BVC,  EOR,  HLT,  SRE,  NOP,  EOR,  LSR,  SRE,  CLI,  EOR,  NOP,  SRE,  NOP,  EOR,  LSR,  SRE,
/*60*/  RTS,  ADC,  HLT,  RRA,  NOP,  ADC,  ROR,  RRA,  PLA,  ADC,  ROR,  ARR,  JMP,  ADC,  ROR,  RRA,
/*70*/  BVS,  ADC,  HLT,  RRA,  NOP,  ADC,  ROR,  RRA,  SEI,  ADC,  NOP,  RRA,  NOP,  ADC,  ROR,  RRA,
/*80*/  BRA,  STA,  NOP,  SAX,  STY,  STA,  STX,  SAX,  DEY,  NOP,  TXA,  ANE,  STY,  STA,  STX,  SAX,
/*90*/  BCC,  STA,  HLT,  SHA,  STY,  STA,  STX,  SAX,  TYA,  STA,  TXS,  SHS,  SHY,  STA,  SHX,  SHA,
/*A0*/  LDY,  LDA,  LDX,  LAX,  LDY,  LDA,  LDX,  LAX,  TAY,  LDA,  TAX,  LXA,  LDY,  LDA,  LDX,  LAX,
/*B0*/  BCS,  LDA,  HLT,  LAX,  LDY,  LDA,  LDX,  LAX,  CLV,  LDA,  TSX,  LAS,  LDY,  LDA,  LDX,  LAX,
/*C0*/  CPY,  CMP,  NOP,  DCP,  CPY,  CMP,  DEC,  DCP,  INY,  CMP,  DEX,  SBX,  CPY,  CMP,  DEC,  DCP,
/*D0*/  BNE,  CMP,  HLT,  DCP,  NOP,  CMP,  DEC,  DCP,  CLD,  CMP,  NOP,  DCP,  NOP,  CMP,  DEC,  DCP,
/*E0*/  CPX,  SBC,  NOP,  ISB,  CPX,  SBC,  INC,  ISB,  INX,  SBC,  NOP,  SBC,  CPX,  SBC,  INC,  ISB,
/*F0*/  BEQ,  SBC,  HLT,  ISB,  NOP,  SBC,  INC,  ISB,  SED,  SBC,  NOP,  ISB,  NOP,  SBC,  INC,  ISB
};

static const uint8_t am_nmos[256] =
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*10*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*20*/  ABS,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*30*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*40*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*50*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*60*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  IND16,ABS,  ABS,  ABS,
/*70*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*80*/  PCR,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*90*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*A0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*B0*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSY, ABSX,
/*C0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*D0*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*E0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*F0*/  PCR,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
};

static const uint8_t op_816[256] =
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  BRK,  ORA,  COP,  ORA,  TSB,  ORA,  ASL,  ORA,  PHP,  ORA,  ASL,  PHD,  TSB,  ORA,  ASL,  ORA,
/*10*/  BPL,  ORA,  ORA,  ORA,  TRB,  ORA,  ASL,  ORA,  CLC,  ORA,  INC,  TCS,  TRB,  ORA,  ASL,  ORA,
/*20*/  JSR,  AND,  JSL,  AND,  BIT,  AND,  ROL,  AND,  PLP,  AND,  ROL,  PLD,  BIT,  AND,  ROL,  AND,
/*30*/  BMI,  AND,  AND,  AND,  BIT,  AND,  ROL,  AND,  SEC,  AND,  DEC,  TSC,  BIT,  AND,  ROL,  AND,
/*40*/  RTI,  EOR,  WDM,  EOR,  MVP,  EOR,  LSR,  EOR,  PHA,  EOR,  LSR,  PHK,  JMP,  EOR,  LSR,  EOR,
/*50*/  BVC,  EOR,  EOR,  EOR,  MVN,  EOR,  LSR,  EOR,  CLI,  EOR,  PHY,  TCD,  JMP,  EOR,  LSR,  EOR,
/*60*/  RTS,  ADC,  PER,  ADC,  STZ,  ADC,  ROR,  ADC,  PLA,  ADC,  ROR,  RTL,  JMP,  ADC,  ROR,  ADC,
/*70*/  BVS,  ADC,  ADC,  ADC,  STZ,  ADC,  ROR,  ADC,  SEI,  ADC,  PLY,  TDC,  JMP,  ADC,  ROR,  ADC,
/*80*/  BRA,  STA,  BRL,  STA,  STY,  STA,  STX,  STA,  DEY,  BIT,  TXA,  PHB,  STY,  STA,  STX,  STA,
/*90*/  BCC,  STA,  STA,  STA,  STY,  STA,  STX,  STA,  TYA,  STA,  TXS,  TXY,  STZ,  STA,  STZ,  STA,
/*A0*/  LDY,  LDA,  LDX,  LDA,  LDY,  LDA,  LDX,  LDA,  TAY,  LDA,  TAX,  PLB,  LDY,  LDA,  LDX,  LDA,
/*B0*/  BCS,  LDA,  LDA,  LDA,  LDY,  LDA,  LDX,  LDA,  CLV,  LDA,  TSX,  TYX,  LDY,  LDA,  LDX,  LDA,
/*C0*/  CPY,  CMP,  REP,  CMP,  CPY,  CMP,  DEC,  CMP,  INY,  CMP,  DEX,  WAI,  CPY,  CMP,  DEC,  CMP,
/*D0*/  BNE,  CMP,  CMP,  CMP,  PEI,  CMP,  DEC,  CMP,  CLD,  CMP,  PHX,  STP,  JML,  CMP,  DEC,  CMP,
/*E0*/  CPX,  SBC,  SEP,  SBC,  CPX,  SBC,  INC,  SBC,  INX,  SBC,  NOP,  XBA,  CPX,  SBC,  INC,  SBC,
/*F0*/  BEQ,  SBC,  SBC,  SBC,  PEA,  SBC,  INC,  SBC,  SED,  SBC,  PLX,  XCE,  JSR,  SBC,  INC,  SBC
};

static const uint8_t am_816[256]=
{
/*       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*00*/  IMP,  INDX, IMM,  SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMPA, IMP,  ABS,  ABS,  ABS,  ABSL,
/*10*/  PCR,  INDY, IND,  SRY,  ZP,   ZPX,  ZPX,  INDYL,IMP,  ABSY, IMPA, IMP,  ABS,  ABSX, ABSX, ABSXL,
/*20*/  ABS,  INDX, ABSL, SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMPA, IMP,  ABS,  ABS,  ABS,  ABSL,
/*30*/  PCR,  INDY, IND,  SRY,  ZPX,  ZPX,  ZPX,  INDYL,IMP,  ABSY, IMPA, IMP,  ABSX, ABSX, ABSX, ABSXL,
/*40*/  IMP,  INDX, IMP,  SR,   BM,   ZP,   ZP,   INDL, IMP,  IMV,  IMPA, IMP,  ABS,  ABS,  ABS,  ABSL,
/*50*/  PCR,  INDY, IND,  SRY,  BM,   ZPX,  ZPX,  INDYL,IMP,  ABSY, IMP,  IMP,  ABSL, ABSX, ABSX, ABSXL,
/*60*/  IMP,  INDX, PCRL, SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMPA, IMP,  IND16,ABS,  ABS,  ABSL,
/*70*/  PCR,  INDY, IND,  SRY,  ZPX,  ZPX,  ZPX,  INDYL,IMP,  ABSY, IMP,  IMP,  IND1X,ABSX, ABSX, ABSXL,
/*80*/  PCR,  INDX, PCRL, SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMP,  IMP,  ABS,  ABS,  ABS,  ABSL,
/*90*/  PCR,  INDY, IND,  SRY,  ZPX,  ZPX,  ZPY,  INDYL,IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, ABSXL,
/*A0*/  IMX,  INDX, IMX,  SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMP,  IMP,  ABS,  ABS,  ABS,  ABSL,
/*B0*/  PCR,  INDY, IND,  SRY,  ZPX,  ZPX,  ZPY,  INDYL,IMP,  ABSY, IMP,  IMP,  ABSX, ABSX, ABSY, ABSXL,
/*C0*/  IMX,  INDX, IMV,  SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMP,  IMP,  ABS,  ABS,  ABS,  ABSL,
/*D0*/  PCR,  INDY, IND,  SRY,  IMP,  ZPX,  ZPX,  INDYL,IMP,  ABSY, IMP,  IMP,  ABSL, IND16,ABSX, ABSXL,
/*E0*/  IMX,  INDX, IMV,  SR,   ZP,   ZP,   ZP,   INDL, IMP,  IMV,  IMP,  IMP,  ABS,  ABS,  ABS,  ABSL,
/*F0*/  PCR,  INDY, IND,  SRY,  IMP,  ZPX,  ZPX,  INDYL,IMP,  ABSY, IMP,  IMP,  ABSX, ABSX, ABSX, ABSXL
};

uint32_t dbg6502_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize, m6502_t model)
{
    uint8_t op, ni, p1, p2, p3;
    uint16_t temp;
    const char *op_name;
    size_t len;
    addr_mode_t addr_mode;

    char addr_buf[SYM_MAX + 10];
    char op_buf[10];

    op = cpu->memread(addr);
    switch (model)
    {
    case M6502:
        ni = op_nmos[op];
        addr_mode = am_nmos[op];
        break;
    case M65C02:
        ni = op_cmos[op];
        addr_mode = am_cmos[op];
        break;
    case W65816:
        ni = op_816[op];
        addr_mode = am_816[op];
        break;
    default:
        log_fatal("6502debug: unkown 6502 model %d", model);
        exit(-1);
    }
    op_name = op_names[ni];
    cpu->print_addr(cpu, addr, addr_buf, sizeof(addr_buf), false);
    debug_print_8bit(op, op_buf, sizeof(op_buf));

    len = snprintf(buf, bufsize, "%s: %s", addr_buf, op_buf);

    buf += len;
    bufsize -= len;
    addr++;

    bool        lookforsym = false;
    uint32_t    symaddr = 0;

    switch (addr_mode)
    {
        case IMP:
            snprintf(buf, bufsize, "         %s         ", op_name);
            break;
        case IMPA:
            snprintf(buf, bufsize, "         %s A       ", op_name);
            break;
        case IMM:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s #%02X     ", p1, op_name, p1);
            break;
        case IMV:
            p1 = cpu->memread(addr++);
            if (w65816p.m)
                snprintf(buf, bufsize, "%02X       %s #%02X     ", p1, op_name, p1);
            else {
                p2 = cpu->memread(addr++);
                snprintf(buf, bufsize, "%02X %02X     %s #%02X%02X     ", p1, p2, op_name, p1, p2);
            }
            break;
        case IMX:
            p1 = cpu->memread(addr++);
            if (w65816p.ex)
                snprintf(buf, bufsize, "%02X       %s #%02X     ", p1, op_name, p1);
            else {
                p2 = cpu->memread(addr++);
                snprintf(buf, bufsize, "%02X %02X     %s #%02X%02X     ", p1, p2, op_name, p1, p2);
            }
            break;
        case ZP:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s %02X      ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case ZPX:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s %02X,X    ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case ZPY:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s %02X,Y    ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case IND:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s (%02X)    ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case INDL:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s [%02X]    ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case INDX:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s (%02X,X)  ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case INDY:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s (%02X),Y  ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case INDYL:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s [%02X],Y  ", p1, op_name, p1);
            lookforsym = true;
            symaddr = p1;
            break;
        case SR:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s (%02X,S)  ", p1, op_name, p1);
            break;
        case SRY:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X       %s (%02X,S),Y", p1, op_name, p1);
            break;
        case ABS:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s %02X%02X    ", p1, p2, op_name, p2, p1);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case ABSL:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            p3 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %02X %s %02X%02X%02X  ", p1, p2, p3, op_name, p2, p1, p3);
            lookforsym = true;
            symaddr = p1 + (p2 << 8) + (p3 << 16);
            break;
        case ABSX:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s %02X%02X,X  ", p1, p2, op_name, p2, p1);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case ABSY:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s %02X%02X,Y  ", p1, p2, op_name, p2, p1);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case ABSXL:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            p3 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %02X  %s %02X%02X%02X,X  ", p1, p2, p3, op_name, p2, p1, p3);
            lookforsym = true;
            symaddr = p1 + (p2 << 8) + (p3 << 16);
            break;
        case BM:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s %02X,%02X   ", p1, p2, op_name, p1, p2);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case IND16:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s (%02X%02X)  ", p1, p2, op_name, p2, p1);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case IND1X:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X    %s (%02X%02X,X)", p1, p2, op_name, p2, p1);
            lookforsym = true;
            symaddr = p1 + (p2 << 8);
            break;
        case PCR:
            p1 = cpu->memread(addr++);
            temp = (signed char)p1;
            temp += addr;
            snprintf(buf, bufsize, "%02X       %s %04X    ", p1, op_name, temp);
            lookforsym = true;
            symaddr = temp;
            break;
        case PCRL:

            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            temp = (int16_t)((uint16_t)p1 | (uint16_t)p2 <<8);
            temp += addr;
            snprintf(buf, bufsize, "%02X %02X     %s %04X    ", p1, p2, op_name, temp);
            lookforsym = true;
            symaddr = temp;
            break;
    }

    const char *sym = NULL;
    if (lookforsym)
    {
        if (symaddr >= 0x8000 && symaddr < 0xC000) {
            // add rom number, first see if we are disassembling a rom

            symaddr = symaddr | (addr & 0xF0000000);  // add in rom # if present
        }

        uint32_t symaddr_found;
        if (symbol_find_by_addr_near(cpu->symbols, symaddr, (symaddr <= 10)?0:symaddr-10, (symaddr <= 0xFFFFFFF5)?symaddr+10:0xFFFFFFFF, &symaddr_found, &sym))
        {
            int ll = strlen(buf);
            if (symaddr_found < symaddr)
                snprintf(buf + ll, bufsize - ll, "\\ (%s+%d)", sym, symaddr - symaddr_found);
            else if (symaddr_found < symaddr)
                snprintf(buf + ll, bufsize - ll, "\\ (%s-%d)", sym, symaddr_found - symaddr);
            else
                snprintf(buf + ll, bufsize - ll, "\\ (%s)", sym);
        }
    }

    return addr;
}

size_t dbg6502_print_flags(PREG *pp, char *buf, size_t bufsize) {
    if (bufsize >= 6) {
    *buf++ = pp->n ? 'N' : ' ';
    *buf++ = pp->v ? 'V' : ' ';
    *buf++ = pp->d ? 'D' : ' ';
    *buf++ = pp->i ? 'I' : ' ';
    *buf++ = pp->z ? 'Z' : ' ';
    *buf++ = pp->c ? 'C' : ' ';
    return 6;
    }
    return 0;
}
