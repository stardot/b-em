#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
	IMP, IMPA, IMM, ZP, ZPX, ZPY, INDX, INDY, IND, ABS, ABSX, ABSY, IND16,
	IND1X, BRA
};

static char     dopname[256][6] = {
/*00*/ "BRK", "ORA", "---", "---", "TSB", "ORA", "ASL", "---", "PHP",
	"ORA", "ASL", "---", "TSB", "ORA", "ASL", "---",
/*10*/ "BPL", "ORA", "ORA", "---", "TRB", "ORA", "ASL", "---", "CLC",
	"ORA", "INC", "---", "TRB", "ORA", "ASL", "---",
/*20*/ "JSR", "AND", "---", "---", "BIT", "AND", "ROL", "---", "PLP",
	"AND", "ROL", "---", "BIT", "AND", "ROL", "---",
/*30*/ "BMI", "AND", "AND", "---", "BIT", "AND", "ROL", "---", "SEC",
	"AND", "DEC", "---", "BIT", "AND", "ROL", "---",
/*40*/ "RTI", "EOR", "---", "---", "---", "EOR", "LSR", "---", "PHA",
	"EOR", "LSR", "---", "JMP", "EOR", "LSR", "---",
/*50*/ "BVC", "EOR", "EOR", "---", "---", "EOR", "LSR", "---", "CLI",
	"EOR", "PHY", "---", "---", "EOR", "LSR", "---",
/*60*/ "RTS", "ADC", "---", "---", "STZ", "ADC", "ROR", "---", "PLA",
	"ADC", "ROR", "---", "JMP", "ADC", "ROR", "---",
/*70*/ "BVS", "ADC", "ADC", "---", "STZ", "ADC", "ROR", "---", "SEI",
	"ADC", "PLY", "---", "JMP", "ADC", "ROR", "---",
/*80*/ "BRA", "STA", "---", "---", "STY", "STA", "STX", "---", "DEY",
	"BIT", "TXA", "---", "STY", "STA", "STX", "---",
/*90*/ "BCC", "STA", "STA", "---", "STY", "STA", "STX", "---", "TYA",
	"STA", "TXS", "---", "STZ", "STA", "STZ", "---",
/*A0*/ "LDY", "LDA", "LDX", "---", "LDY", "LDA", "LDX", "---", "TAY",
	"LDA", "TAX", "---", "LDY", "LDA", "LDX", "---",
/*B0*/ "BCS", "LDA", "LDA", "---", "LDY", "LDA", "LDX", "---", "CLV",
	"LDA", "TSX", "---", "LDY", "LDA", "LDX", "---",
/*C0*/ "CPY", "CMP", "---", "---", "CPY", "CMP", "DEC", "---", "INY",
	"CMP", "DEX", "WAI", "CPY", "CMP", "DEC", "---",
/*D0*/ "BNE", "CMP", "CMP", "---", "---", "CMP", "DEC", "---", "CLD",
	"CMP", "PHX", "STP", "---", "CMP", "DEC", "---",
/*E0*/ "CPX", "SBC", "---", "---", "CPX", "SBC", "INC", "---", "INX",
	"SBC", "NOP", "---", "CPX", "SBC", "INC", "---",
/*F0*/ "BEQ", "SBC", "SBC", "---", "---", "SBC", "INC", "---", "SED",
	"SBC", "PLX", "---", "---", "SBC", "INC", "---",
};

static int      dopaddr[256] = {
/*00*/ IMP, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMPA, IMP, ABS,
	ABS, ABS, IMP,
/*10*/ BRA, INDY, IND, IMP, ZP, ZPX, ZPX, IMP, IMP, ABSY, IMPA, IMP,
	ABS, ABSX, ABSX, IMP,
/*20*/ ABS, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMPA, IMP, ABS,
	ABS, ABS, IMP,
/*30*/ BRA, INDY, IND, IMP, ZPX, ZPX, ZPX, IMP, IMP, ABSY, IMPA, IMP,
	ABSX, ABSX, ABSX, IMP,
/*40*/ IMP, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMPA, IMP, ABS,
	ABS, ABS, IMP,
/*50*/ BRA, INDY, IND, IMP, ZP, ZPX, ZPX, IMP, IMP, ABSY, IMP, IMP,
	ABS, ABSX, ABSX, IMP,
/*60*/ IMP, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMPA, IMP,
	IND16, ABS, ABS, IMP,
/*70*/ BRA, INDY, IND, IMP, ZPX, ZPX, ZPX, IMP, IMP, ABSY, IMP, IMP,
	IND1X, ABSX, ABSX, IMP,
/*80*/ BRA, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMP, IMP, ABS,
	ABS, ABS, IMP,
/*90*/ BRA, INDY, IND, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABSY, IMP, IMP,
	ABS, ABSX, ABSX, IMP,
/*A0*/ IMM, INDX, IMM, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMP, IMP, ABS,
	ABS, ABS, IMP,
/*B0*/ BRA, INDY, IND, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABSY, IMP, IMP,
	ABSX, ABSX, ABSY, IMP,
/*C0*/ IMM, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMP, IMP, ABS,
	ABS, ABS, IMP,
/*D0*/ BRA, INDY, IND, IMP, ZP, ZPX, ZPX, IMP, IMP, ABSY, IMP, IMP,
	ABS, ABSX, ABSX, IMP,
/*E0*/ IMM, INDX, IMP, IMP, ZP, ZP, ZP, IMP, IMP, IMM, IMP, IMP, ABS,
	ABS, ABS, IMP,
/*F0*/ BRA, INDY, IND, IMP, ZP, ZPX, ZPX, IMP, IMP, ABSY, IMP, IMP,
	ABS, ABSX, ABSX, IMP,
};

static char     dopnamenmos[256][6] = {
/*00*/ "BRK", "ORA", "HLT", "SLO", "NOP", "ORA", "ASL", "SLO", "PHP",
	"ORA", "ASL", "ANC", "NOP", "ORA", "ASL", "SLO",
/*10*/ "BPL", "ORA", "HLT", "SLO", "NOP", "ORA", "ASL", "SLO", "CLC",
	"ORA", "NOP", "SLO", "NOP", "ORA", "ASL", "SLO",
/*20*/ "JSR", "AND", "HLT", "RLA", "NOP", "AND", "ROL", "RLA", "PLP",
	"AND", "ROL", "ANC", "BIT", "AND", "ROL", "RLA",
/*30*/ "BMI", "AND", "HLT", "RLA", "NOP", "AND", "ROL", "RLA", "SEC",
	"AND", "NOP", "RLA", "NOP", "AND", "ROL", "RLA",
/*40*/ "RTI", "EOR", "HLT", "SRE", "NOP", "EOR", "LSR", "SRE", "PHA",
	"EOR", "LSR", "ASR", "JMP", "EOR", "LSR", "SRE",
/*50*/ "BVC", "EOR", "HLT", "SRE", "NOP", "EOR", "LSR", "SRE", "CLI",
	"EOR", "NOP", "SRE", "NOP", "EOR", "LSR", "SRE",
/*60*/ "RTS", "ADC", "HLT", "RRA", "NOP", "ADC", "ROR", "RRA", "PLA",
	"ADC", "ROR", "ARR", "JMP", "ADC", "ROR", "RRA",
/*70*/ "BVS", "ADC", "HLT", "RRA", "NOP", "ADC", "ROR", "RRA", "SEI",
	"ADC", "NOP", "RRA", "NOP", "ADC", "ROR", "RRA",
/*80*/ "BRA", "STA", "NOP", "SAX", "STY", "STA", "STX", "SAX", "DEY",
	"NOP", "TXA", "ANE", "STY", "STA", "STX", "SAX",
/*90*/ "BCC", "STA", "HLT", "SHA", "STY", "STA", "STX", "SAX", "TYA",
	"STA", "TXS", "SHS", "SHY", "STA", "SHX", "SHA",
/*A0*/ "LDY", "LDA", "LDX", "LAX", "LDY", "LDA", "LDX", "LAX", "TAY",
	"LDA", "TAX", "LXA", "LDY", "LDA", "LDX", "LAX",
/*B0*/ "BCS", "LDA", "HLT", "LAX", "LDY", "LDA", "LDX", "LAX", "CLV",
	"LDA", "TSX", "LAS", "LDY", "LDA", "LDX", "LAX",
/*C0*/ "CPY", "CMP", "NOP", "DCP", "CPY", "CMP", "DEC", "DCP", "INY",
	"CMP", "DEX", "SBX", "CPY", "CMP", "DEC", "DCP",
/*D0*/ "BNE", "CMP", "HLT", "DCP", "NOP", "CMP", "DEC", "DCP", "CLD",
	"CMP", "NOP", "DCP", "NOP", "CMP", "DEC", "DCP",
/*E0*/ "CPX", "SBC", "NOP", "ISB", "CPX", "SBC", "INC", "ISB", "INX",
	"SBC", "NOP", "SBC", "CPX", "SBC", "INC", "ISB",
/*F0*/ "BEQ", "SBC", "HLT", "ISB", "NOP", "SBC", "INC", "ISB", "SED",
	"SBC", "NOP", "ISB", "NOP", "SBC", "INC", "ISB",
};

static int      dopaddrnmos[256] = {
/*00*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS,
	ABS, ABS, ABS,
/*10*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*20*/ ABS, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS,
	ABS, ABS, ABS,
/*30*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*40*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS,
	ABS, ABS, ABS,
/*50*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*60*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM,
	IND16, ABS, ABS, ABS,
/*70*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*80*/ BRA, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS,
	ABS, ABS, ABS,
/*90*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPY, ZPY, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*A0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS,
	ABS, ABS, ABS,
/*B0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPY, ZPY, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSY, ABSX,
/*C0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS,
	ABS, ABS, ABS,
/*D0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
/*E0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS,
	ABS, ABS, ABS,
/*F0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY,
	ABSX, ABSX, ABSX, ABSX,
};

static inline void disassemble(int cmos, uint16_t addr, uint8_t op,
    uint8_t p1, uint8_t p2, FILE *out)
{
	unsigned        temp;
	const char     *opname = cmos ? dopname[op] : dopnamenmos[op];
	fprintf(out, "%04X : %02X ", addr, op);
	switch (cmos ? dopaddr[op] : dopaddrnmos[op]) {
	case IMP:
		fprintf(out, "      %s         ", opname);
		break;
	case IMPA:
		fprintf(out, "      %s A       ", opname);
		break;
	case IMM:
		fprintf(out, "%02X    %s #%02X     ", p1, opname, p1);
		break;
	case ZP:
		fprintf(out, "%02X    %s %02X      ", p1, opname, p1);
		break;
	case ZPX:
		fprintf(out, "%02X    %s %02X,X    ", p1, opname, p1);
		break;
	case ZPY:
		fprintf(out, "%02X    %s %02X,Y    ", p1, opname, p1);
		break;
	case IND:
		fprintf(out, "%02X    %s (%02X)    ", p1, opname, p1);
		break;
	case INDX:
		fprintf(out, "%02X    %s (%02X,X)  ", p1, opname, p1);
		break;
	case INDY:
		fprintf(out, "%02X    %s (%02X),Y  ", p1, opname, p1);
		break;
	case ABS:
		fprintf(out, "%02X %02X %s %02X%02X    ", p1, p2, opname, p2,
		    p1);
		break;
	case ABSX:
		fprintf(out, "%02X %02X %s %02X%02X,X  ", p1, p2, opname, p2,
		    p1);
		break;
	case ABSY:
		fprintf(out, "%02X %02X %s %02X%02X,Y  ", p1, p2, opname, p2,
		    p1);
		break;
	case IND16:
		fprintf(out, "%02X %02X %s (%02X%02X)  ", p1, p2, opname, p2,
		    p1);
		break;
	case IND1X:
		fprintf(out, "%02X %02X %s (%02X%02X,X)", p1, p2, opname, p2,
		    p1);
		break;
	case BRA:
		temp = addr + 2 + (signed char) p1;
		fprintf(out, "%02X    %s %04X    ", p1, opname, temp);
		break;
	}
}


static void display_trace(const char *filename, FILE *fp)
{
	char            magic[4];
	time_t          secs;
	int             nmos, cmos, tickcount;
	char            tmstr[20];
	uint16_t        pc, ppc = 0;
	uint8_t         op, p1, p2, a, x, y, s, f;

	nmos = cmos = 0;
	if (fread(magic, 8, 1, fp) == 1) {
		if (strncmp(magic, "6502NMOS", 8) == 0)
			nmos = 1;
		else if (strncmp(magic, "6502CMOS", 8) == 0)
			cmos = 1;
	}
	if (nmos || cmos) {
		if (fread(&secs, sizeof(secs), 1, fp) == 1) {
			strftime(tmstr, sizeof tmstr, "%d/%m/%Y %H:%M:%S",
			    localtime(&secs));
			printf("6502 trace starts %s\n", tmstr);
			while ((tickcount = getc_unlocked(fp)) != EOF) {
				pc = getc_unlocked(fp);
				pc |= getc_unlocked(fp) << 8;
				pc--;
				op = getc_unlocked(fp);
				p1 = getc_unlocked(fp);
				p2 = getc_unlocked(fp);
				a = getc_unlocked(fp);
				x = getc_unlocked(fp);
				y = getc_unlocked(fp);
				s = getc_unlocked(fp);
				f = getc_unlocked(fp);
				if (pc >= 0xf800 && ppc < 0xf800)
					puts("call to OS ROM");
				else if (pc < 0xf800) {
					printf("%02d ", tickcount);
					disassemble(cmos, pc, op, p1, p2,
					    stdout);
					printf(" %02X %02X %02X %02X ", a, x,
					    y, s);
					putc_unlocked((f & 0x80) ? 'N' : '.',
					    stdout);
					putc_unlocked((f & 0x40) ? 'V' : '.',
					    stdout);
					putc_unlocked((f & 0x08) ? 'D' : '.',
					    stdout);
					putc_unlocked((f & 0x04) ? 'I' : '.',
					    stdout);
					putc_unlocked((f & 0x02) ? 'Z' : '.',
					    stdout);
					putc_unlocked((f & 0x01) ? 'C' : '.',
					    stdout);
					putc_unlocked('\n', stdout);
				}
				ppc = pc;
			}
		} else
			fprintf(stderr,
			    "disptrace: unexpected end of file on %s\n",
			    filename);
	} else
		fprintf(stderr, "disptrace: %s not a 6502 trace file\n",
		    filename);
}

int main(int argc, char **argv)
{
	int             status = 0;
	const char     *filename;
	FILE           *fp;

	if (argc == 1)
		display_trace("<stdin>", stdin);
	else {
		while (--argc) {
			filename = *++argv;
			if ((fp = fopen(filename, "rb"))) {
				display_trace(filename, fp);
				fclose(fp);
			} else {
				fprintf(stderr,
				    "disptrace: unable to open %s: %m\n",
				    filename);
				status++;
			}
		}
	}
	return status;
}
