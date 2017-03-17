/* -*- mode: c; c-basic-offset: 4 -*- */

#include "b-em.h"
#include "6502debug.h"
#include "6502.h"

const char *dbg6502_reg_names[] = { "A", "X", "Y", "S", "P", "PC", NULL };

enum
{
    IMP,IMPA,IMM,ZP,ZPX,ZPY,INDX,INDY,IND,ABS,ABSX,ABSY,IND16,IND1X,BRA
};

static char dopname[256][6]=
{
/*00*/  "BRK","ORA","---","---","TSB","ORA","ASL","---","PHP","ORA","ASL","---","TSB","ORA","ASL","---",
/*10*/  "BPL","ORA","ORA","---","TRB","ORA","ASL","---","CLC","ORA","INC","---","TRB","ORA","ASL","---",
/*20*/  "JSR","AND","---","---","BIT","AND","ROL","---","PLP","AND","ROL","---","BIT","AND","ROL","---",
/*30*/  "BMI","AND","AND","---","BIT","AND","ROL","---","SEC","AND","DEC","---","BIT","AND","ROL","---",
/*40*/  "RTI","EOR","---","---","---","EOR","LSR","---","PHA","EOR","LSR","---","JMP","EOR","LSR","---",
/*50*/  "BVC","EOR","EOR","---","---","EOR","LSR","---","CLI","EOR","PHY","---","---","EOR","LSR","---",
/*60*/  "RTS","ADC","---","---","STZ","ADC","ROR","---","PLA","ADC","ROR","---","JMP","ADC","ROR","---",
/*70*/  "BVS","ADC","ADC","---","STZ","ADC","ROR","---","SEI","ADC","PLY","---","JMP","ADC","ROR","---",
/*80*/  "BRA","STA","---","---","STY","STA","STX","---","DEY","BIT","TXA","---","STY","STA","STX","---",
/*90*/  "BCC","STA","STA","---","STY","STA","STX","---","TYA","STA","TXS","---","STZ","STA","STZ","---",
/*A0*/  "LDY","LDA","LDX","---","LDY","LDA","LDX","---","TAY","LDA","TAX","---","LDY","LDA","LDX","---",
/*B0*/  "BCS","LDA","LDA","---","LDY","LDA","LDX","---","CLV","LDA","TSX","---","LDY","LDA","LDX","---",
/*C0*/  "CPY","CMP","---","---","CPY","CMP","DEC","---","INY","CMP","DEX","WAI","CPY","CMP","DEC","---",
/*D0*/  "BNE","CMP","CMP","---","---","CMP","DEC","---","CLD","CMP","PHX","STP","---","CMP","DEC","---",
/*E0*/  "CPX","SBC","---","---","CPX","SBC","INC","---","INX","SBC","NOP","---","CPX","SBC","INC","---",
/*F0*/  "BEQ","SBC","SBC","---","---","SBC","INC","---","SED","SBC","PLX","---","---","SBC","INC","---",
};

static int dopaddr[256]=
{
/*00*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*10*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABS,  ABSX, ABSX, IMP,
/*20*/  ABS,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*30*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMPA, IMP,  ABSX, ABSX, ABSX, IMP,
/*40*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  ABS,  ABS,  ABS,  IMP,
/*50*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*60*/  IMP,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMPA, IMP,  IND16,ABS,  ABS,  IMP,
/*70*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  IND1X,ABSX, ABSX, IMP,
/*80*/  BRA,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*90*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*A0*/  IMM,  INDX, IMM,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*B0*/  BRA,  INDY, IND,  IMP,  ZPX,  ZPX,  ZPY,  IMP,  IMP,  ABSY, IMP,  IMP,  ABSX, ABSX, ABSY, IMP,
/*C0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*D0*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
/*E0*/  IMM,  INDX, IMP,  IMP,  ZP,   ZP,   ZP,   IMP,  IMP,  IMM,  IMP,  IMP,  ABS,  ABS,  ABS,  IMP,
/*F0*/  BRA,  INDY, IND,  IMP,  ZP,   ZPX,  ZPX,  IMP,  IMP,  ABSY, IMP,  IMP,  ABS,  ABSX, ABSX, IMP,
};

static char dopnamenmos[256][6]=
{
/*00*/  "BRK","ORA","HLT","SLO","NOP","ORA","ASL","SLO","PHP","ORA","ASL","ANC","NOP","ORA","ASL","SLO",
/*10*/  "BPL","ORA","HLT","SLO","NOP","ORA","ASL","SLO","CLC","ORA","NOP","SLO","NOP","ORA","ASL","SLO",
/*20*/  "JSR","AND","HLT","RLA","NOP","AND","ROL","RLA","PLP","AND","ROL","ANC","BIT","AND","ROL","RLA",
/*30*/  "BMI","AND","HLT","RLA","NOP","AND","ROL","RLA","SEC","AND","NOP","RLA","NOP","AND","ROL","RLA",
/*40*/  "RTI","EOR","HLT","SRE","NOP","EOR","LSR","SRE","PHA","EOR","LSR","ASR","JMP","EOR","LSR","SRE",
/*50*/  "BVC","EOR","HLT","SRE","NOP","EOR","LSR","SRE","CLI","EOR","NOP","SRE","NOP","EOR","LSR","SRE",
/*60*/  "RTS","ADC","HLT","RRA","NOP","ADC","ROR","RRA","PLA","ADC","ROR","ARR","JMP","ADC","ROR","RRA",
/*70*/  "BVS","ADC","HLT","RRA","NOP","ADC","ROR","RRA","SEI","ADC","NOP","RRA","NOP","ADC","ROR","RRA",
/*80*/  "BRA","STA","NOP","SAX","STY","STA","STX","SAX","DEY","NOP","TXA","ANE","STY","STA","STX","SAX",
/*90*/  "BCC","STA","HLT","SHA","STY","STA","STX","SAX","TYA","STA","TXS","SHS","SHY","STA","SHX","SHA",
/*A0*/  "LDY","LDA","LDX","LAX","LDY","LDA","LDX","LAX","TAY","LDA","TAX","LXA","LDY","LDA","LDX","LAX",
/*B0*/  "BCS","LDA","HLT","LAX","LDY","LDA","LDX","LAX","CLV","LDA","TSX","LAS","LDY","LDA","LDX","LAX",
/*C0*/  "CPY","CMP","NOP","DCP","CPY","CMP","DEC","DCP","INY","CMP","DEX","SBX","CPY","CMP","DEC","DCP",
/*D0*/  "BNE","CMP","HLT","DCP","NOP","CMP","DEC","DCP","CLD","CMP","NOP","DCP","NOP","CMP","DEC","DCP",
/*E0*/  "CPX","SBC","NOP","ISB","CPX","SBC","INC","ISB","INX","SBC","NOP","SBC","CPX","SBC","INC","ISB",
/*F0*/  "BEQ","SBC","HLT","ISB","NOP","SBC","INC","ISB","SED","SBC","NOP","ISB","NOP","SBC","INC","ISB",
};

static int dopaddrnmos[256]=
{
/*00*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*10*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*20*/  ABS,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*30*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*40*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  ABS,  ABS,  ABS,  ABS,
/*50*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*60*/  IMP,  INDX, IMP,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMPA, IMM,  IND16,ABS,  ABS,  ABS,
/*70*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*80*/  BRA,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*90*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*A0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*B0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPY,  ZPY,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSY, ABSX,
/*C0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*D0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
/*E0*/  IMM,  INDX, IMM,  INDX, ZP,   ZP,   ZP,   ZP,   IMP,  IMM,  IMP,  IMM,  ABS,  ABS,  ABS,  ABS,
/*F0*/  BRA,  INDY, IMP,  INDY, ZPX,  ZPX,  ZPX,  ZPX,  IMP,  ABSY, IMP,  ABSY, ABSX, ABSX, ABSX, ABSX,
};

uint32_t dbg6502_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize, int cmos)
{
    uint8_t op, p1, p2;
    uint16_t temp;
    const char *op_name;
    size_t len;
    int addr_mode;

    op = cpu->memread(addr);
    if (cmos) {
        op_name = dopname[op];
        addr_mode = dopaddr[op];
    } else {
        op_name = dopnamenmos[op];
        addr_mode = dopaddrnmos[op];
    }
    len = snprintf(buf, bufsize, "%04X: %02X ", addr, op);
    if (len < bufsize) {
	buf += len;
	bufsize -= len;
	addr++;

	switch (addr_mode)
	{
        case IMP:
            snprintf(buf, bufsize, "      %s         ", op_name);
            break;
        case IMPA:
            snprintf(buf, bufsize, "      %s A       ", op_name);
            break;
        case IMM:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s #%02X     ", p1, op_name, p1);
            break;
        case ZP:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s %02X      ", p1, op_name, p1);
            break;
        case ZPX:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s %02X,X    ", p1, op_name, p1);
            break;
        case ZPY:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s %02X,Y    ", p1, op_name, p1);
            break;
        case IND:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s (%02X)    ", p1, op_name, p1);
            break;
        case INDX:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s (%02X,X)  ", p1, op_name, p1);
            break;
        case INDY:
            p1 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X    %s (%02X),Y  ", p1, op_name, p1);
            break;
        case ABS:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %s %02X%02X    ", p1, p2, op_name, p2, p1);
            break;
        case ABSX:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %s %02X%02X,X  ", p1, p2, op_name, p2, p1);
            break;
        case ABSY:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %s %02X%02X,Y  ", p1, p2, op_name, p2, p1);
            break;
        case IND16:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %s (%02X%02X)  ", p1, p2, op_name, p2, p1);
            break;
        case IND1X:
            p1 = cpu->memread(addr++);
            p2 = cpu->memread(addr++);
            snprintf(buf, bufsize, "%02X %02X %s (%02X%02X,X)", p1, p2, op_name, p2, p1);
            break;
        case BRA:
            p1 = cpu->memread(addr++);
            temp = addr + (signed char)p1;
            snprintf(buf, bufsize, "%02X    %s %04X    ", p1, op_name, temp);
            break;
	}
    }
    return addr;
}

size_t dbg6502_print_flags(PREG *pp, char *buf, size_t bufsize) {
    if (bufsize >= 6) {
	*buf++ = p.n ? 'N' : ' ';
	*buf++ = p.v ? 'V' : ' ';
	*buf++ = p.d ? 'D' : ' ';
	*buf++ = p.i ? 'I' : ' ';
	*buf++ = p.z ? 'Z' : ' ';
	*buf++ = p.c ? 'C' : ' ';
	return 6;
    }
    return 0;
}
