#include "cpu_debug.h"
#include "z80.h"
#include "z80dis.h"

#include <string.h>
#include <stdio.h>

static const char *reg[8]   = { "B", "C", "D", "E", "H", "L", "(HL)", "A"};
static const char *dreg[4]  = { "BC", "DE", "HL", "SP"};
static const char *cond[8]  = { "NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
static const char *arith[8] = { "ADD  A,", "ADC  A,", "SUB  ", "SBC  A,", "AND  ", "XOR  ", "OR   ","CP   "};

static const char *ins1[8]  = { "RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"};
static const char *ins2[4]  = { "RET", "EXX", "JP  (HL)", "LD  SP,HL" };
static const char *ins3[8]  = { "RLC","RRC","RL ","RR ","SLA","SRA","???","SRL"};
static const char *ins4[8]  = { "NEG","???","???","???","???","???","???","???"};
static const char *ins5[8]  = { "RETN","RETI","???","???","???","???","???","???"};
static const char *ins6[8]  = { "LD   I,A","???","LD   A,I","???","RRD","RLD","???","???"};
static const char *ins7[32] = { "LDI","CPI","INI","OUTI","???","???","???","???",
				"LDD","CPD","IND","OUTD","???","???","???","???",
				"LDIR","CPIR","INIR","OTIR","???","???","???","???",
				"LDDR","CPDR","INDR","OTDR","???","???","???","???"};
static const char *ins8[8]  = { "RLC","RRC","RL","RR","SLA","SRA","???","SRL"};

static uint32_t disassemble(uint32_t addr, char *buf, size_t bufsize) {
    uint8_t a = tube_z80_readmem(addr++);
    uint8_t d = (a >> 3) & 7;
    uint8_t e = a & 7;
    uint16_t opaddr, opadd2;
    const char *ireg;

    switch(a & 0xC0) {
        case 0x00:
            switch(e) {
                case 0x00:
                    switch(d) {
                        case 0x00:
                            strncpy(buf, "NOP", bufsize);
                            break;
                        case 0x01:
                            strncpy(buf, "EX   AF,AF'", bufsize);
                            break;
                        case 0x02:
                            opaddr = addr + 1;
			    opaddr += (signed char)tube_z80_readmem(addr++);
                            snprintf(buf, bufsize, "DJNZ %4.4Xh", opaddr);
                            break;
                        case 0x03:
                            opaddr = addr + 1;
			    opaddr += (signed char)tube_z80_readmem(addr++);
                            snprintf(buf, bufsize, "JR   %4.4Xh", opaddr);
                            break;
                        default:
                            opaddr = addr + 1;
			    opaddr += (signed char)tube_z80_readmem(addr++);
                            snprintf(buf, bufsize, "JR   %s,%4.4Xh", cond[d & 3], opaddr);
                            break;
                    }
                    break;
                case 0x01:
                    if (a & 0x08) {
                        snprintf(buf, bufsize, "ADD  HL,%s", dreg[d >> 1]);
                    } else {
                        opaddr = tube_z80_readmem(addr++);
			opaddr |= (tube_z80_readmem(addr++)<<8);
                        snprintf(buf, bufsize, "LD   %s,%4.4Xh", dreg[d >> 1], opaddr);
                    }
                    break;
                case 0x02:
                    switch(d) {
                        case 0x00:
                            strncpy(buf,"LD   (BC),A", bufsize);
                            break;
                        case 0x01:
                            strncpy(buf,"LD   A,(BC)", bufsize);
                            break;
                        case 0x02:
                            strncpy(buf,"LD   (DE),A", bufsize);
                            break;
                        case 0x03:
                            strncpy(buf,"LD   A,(DE)", bufsize);
                            break;
                        case 0x04:
			    opaddr = tube_z80_readmem(addr++);
			    opaddr |= (tube_z80_readmem(addr++)<<8);
                            snprintf(buf, bufsize, "LD   (%4.4Xh),HL", opaddr);
                            break;
                        case 0x05:
			    opaddr = tube_z80_readmem(addr++);
			    opaddr |= (tube_z80_readmem(addr++)<<8);
                            snprintf(buf, bufsize, "LD   HL,(%4.4Xh)", opaddr);
                            break;
                        case 0x06:
			    opaddr = tube_z80_readmem(addr++);
			    opaddr |= (tube_z80_readmem(addr++)<<8);
                            snprintf(buf, bufsize, "LD   (%4.4Xh),A", opaddr);
                            break;
                        case 0x07:
			    opaddr = tube_z80_readmem(addr++);
			    opaddr |= (tube_z80_readmem(addr++)<<8);
                            snprintf(buf, bufsize, "LD   A,(%4.4Xh)", opaddr);
                            break;
                    }
                    break;
                case 0x03:
                    if(a & 0x08)
                        snprintf(buf, bufsize, "DEC  %s", dreg[d >> 1]);
                    else
                        snprintf(buf, bufsize, "INC  %s", dreg[d >> 1]);
                    break;
                case 0x04:
                    snprintf(buf, bufsize, "INC  %s", reg[d]);
                    break;
                case 0x05:
                    snprintf(buf, bufsize, "DEC  %s", reg[d]);
                    break;
                case 0x06:                              // LD   d,n
                    opaddr = tube_z80_readmem(addr++);
                    snprintf(buf, bufsize, "LD   %s,%2.2Xh", reg[d], opaddr);
                    break;
                case 0x07:
                    strncpy(buf, ins1[d], bufsize);
                    break;
            }
            break;
        case 0x40: // LD d,s
            if (d == e) {
                strncpy(buf, "HALT", bufsize);
            } else {
                snprintf(buf, bufsize, "LD   %s,%s", reg[d], reg[e]);
            }
            break;
        case 0x80:
            snprintf(buf, bufsize, "%s%s", arith[d], reg[e]);
            break;
        case 0xC0:
            switch(e) {
                case 0x00:
                    snprintf(buf, bufsize, "RET  %s", cond[d]);
                    break;
                case 0x01:
                    if(d & 1)
                        strncpy(buf, ins2[d >> 1], bufsize);
		    else {
                        if ((d >> 1) == 3)
                            strncpy(buf, "POP  AF", bufsize);
                        else
                            snprintf(buf, bufsize, "POP  %s", dreg[d >> 1]);
                    }
                    break;
                case 0x02:
		    opaddr = tube_z80_readmem(addr++);
		    opaddr |= (tube_z80_readmem(addr++)<<8);
                    snprintf(buf, bufsize, "JP   %s,%4.4Xh", cond[d], opaddr);
                    break;
                case 0x03:
		    switch(d) {
                        case 0x00:
			    opaddr = tube_z80_readmem(addr++);
			    opaddr |= (tube_z80_readmem(addr++)<<8);
			    snprintf(buf, bufsize, "JP   %4.4Xh", opaddr);
			    break;
		        case 0x01: // 0xCB
			    a = tube_z80_readmem(addr++);
			    d = (a >> 3) & 7;
			    e = a & 7;
			    switch(a & 0xC0) {
			        case 0x00:
				    snprintf(buf, bufsize, "%s  %s", ins3[d], reg[e]);
				    break;
                                case 0x40:
				    snprintf(buf, bufsize, "BIT  %d,%s", d, reg[e]);
				    break;
                                case 0x80:
				    snprintf(buf, bufsize, "RES  %d,%s", d, reg[e]);
				    break;
                                case 0xC0:
				    snprintf(buf, bufsize, "SET  %d,%s", d, reg[e]);
				    break;
			    }
			    break;
                        case 0x02:
			    opaddr = tube_z80_readmem(addr++);
			    snprintf(buf, bufsize, "OUT  (%2.2Xh),A", opaddr);
			    break;
                        case 0x03:
			    opaddr = tube_z80_readmem(addr++);
			    snprintf(buf, bufsize, "IN   A,(%2.2Xh)", opaddr);
			    break;
                        case 0x04:
			    strncpy(buf, "EX   (SP),HL", bufsize);
			    break;
                        case 0x05:
			    strncpy(buf, "EX   DE,HL", bufsize);
			    break;
                        case 0x06:
			    strncpy(buf, "DI", bufsize);
			    break;
                        case 0x07:
			    strncpy(buf, "EI", bufsize);
			    break;
		    }
		    break;
                case 0x04:
		    opaddr = tube_z80_readmem(addr++);
		    opaddr |= (tube_z80_readmem(addr++)<<8);
		    snprintf(buf, bufsize, "CALL %s,%4.4Xh", cond[d], opaddr);
		    break;
                case 0x05:
		    if (d & 1) {
			switch(d >> 1) {
			    case 0x00:
				opaddr = tube_z80_readmem(addr++);
				opaddr |= (tube_z80_readmem(addr++)<<8);
				snprintf(buf, bufsize, "CALL %4.4Xh", opaddr);
				break;
			case 0x02: // 0xED
			    a = tube_z80_readmem(addr++);
			    d = (a >> 3) & 7;
			    e = a & 7;
			    switch (a & 0xC0) {
			        case 0x40:
				    switch (e) {
				        case 0x00:
					    snprintf(buf, bufsize, "IN   %s,(C)", reg[d]);
					    break;
				        case 0x01:
					    snprintf(buf, bufsize, "OUT  (C),%s", reg[d]);
					    break;
				        case 0x02:
					    if (d & 1)
						snprintf(buf, bufsize, "ADC  HL,%s", dreg[d >> 1]);
					    else
						snprintf(buf, bufsize, "SBC  HL,%s", dreg[d >> 1]);
					    break;
				        case 0x03:
					    opaddr = tube_z80_readmem(addr++);
					    opaddr |= (tube_z80_readmem(addr++)<<8);
					    if (d & 1)
						snprintf(buf, bufsize, "LD   %s,(%4.4Xh)", dreg[d >> 1], opaddr);
					    else
						snprintf(buf, bufsize, "LD   (%4.4Xh),%s", opaddr, dreg[d >> 1]);
					    break;
				        case 0x04:
					    strncpy(buf, ins4[d], bufsize);
					    break;
				        case 0x05:
					    strncpy(buf, ins5[d], bufsize);
					    break;
				        case 0x06:
					    switch(d) {
					    case 0:
						snprintf(buf, bufsize, "IM   0", d-1);
						break;
					    case 1:
						snprintf(buf, bufsize, "IM   0/1", d-1);
						break;
					    default:
						snprintf(buf, bufsize, "IM   %d", d-1);
					    }
					    break;
				        case 0x07:
					    strncpy(buf, ins6[d], bufsize);
					    break;
				    }
				    break;
			        case 0x80:
				    strncpy(buf, ins7[a & 0x1F], bufsize);
				    break;
			    }
			    break;
			default: // 0x01 (0xDD) = IX, 0x03 (0xFD) = IY
			    ireg = (a & 0x20) ? "IY" : "IX";
			    a = tube_z80_readmem(addr++);
			    switch(a) {
			        case 0x09:
				    snprintf(buf, bufsize, "ADD  %s,BC", ireg);
				    break;
			        case 0x19:
				    snprintf(buf, bufsize, "ADD  %s,DE", ireg);
				    break;
			        case 0x21:
				    opaddr = tube_z80_readmem(addr++);
				    opaddr |= (tube_z80_readmem(addr++)<<8);
				    snprintf(buf, bufsize, "LD   %s,%4.4Xh", ireg, opaddr);
				    break;
			        case 0x22:
				    opaddr = tube_z80_readmem(addr++);
				    opaddr |= (tube_z80_readmem(addr++)<<8);
				    snprintf(buf, bufsize, "LD   (%4.4Xh),%s", opaddr, ireg);
				    break;
			        case 0x23:
				    snprintf(buf, bufsize, "INC  %s", ireg);
				    break;
			        case 0x29:
				    snprintf(buf, bufsize, "ADD  %s,%s", ireg, ireg);
				    break;
			        case 0x2A:
				    opaddr = tube_z80_readmem(addr++);
				    opaddr |= (tube_z80_readmem(addr++)<<8);
				    snprintf(buf, bufsize, "LD   %s,(%4.4Xh)", ireg, opaddr);
				    break;
			        case 0x2B:
				    snprintf(buf, bufsize, "DEC  %s", ireg);
				    break;
			        case 0x34:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "INC  (%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0x35:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "DEC  (%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0x36:
				    opaddr = tube_z80_readmem(addr++);
				    opadd2 = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "LD   (%s+%2.2Xh),%2.2Xh", ireg, opaddr, opadd2);
				    break;
			        case 0x39:
				    snprintf(buf, bufsize, "ADD  %s,SP", ireg);
				    break;
			        case 0x46:
			        case 0x4E:
			        case 0x56:
			        case 0x5E:
			        case 0x66:
			        case 0x6E:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "LD   %s,(%s+%2.2Xh)", reg[(a>>3)&7], ireg, opaddr);
				    break;
			        case 0x70:
			        case 0x71:
			        case 0x72:
			        case 0x73:
			        case 0x74:
			        case 0x75:
			        case 0x77:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "LD   (%s+%2.2Xh),%s", ireg, opaddr, reg[a & 7]);
				    break;
			        case 0x7E:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "LD   A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0x86:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "ADD  A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0x8E:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "ADC  A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0x96:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "SUB  (%s+%2.2Xh)", ireg, opaddr);
				    break;
			       case 0x9E:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "SBC  A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			       case 0xA6:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "AND  A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			       case 0xAE:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "XOR  A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0xB6:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "OR   A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0xBE:
				    opaddr = tube_z80_readmem(addr++);
				    snprintf(buf, bufsize, "CP   A,(%s+%2.2Xh)", ireg, opaddr);
				    break;
			        case 0xE1:
				    snprintf(buf, bufsize, "POP  %s", ireg);
				    break;
			        case 0xE3:
				    snprintf(buf, bufsize, "EX   (SP),%s", ireg);
				    break;
			        case 0xE5:
				    snprintf(buf, bufsize, "PUSH %s", ireg);
				    break;
			        case 0xE9:
				    snprintf(buf, bufsize, "JP   (%s)", ireg);
				    break;
			        case 0xF9:
				    snprintf(buf, bufsize, "LD   SP,%s", ireg);
				    break;
			        case 0xCB:
				    opaddr = tube_z80_readmem(addr++);
				    a = tube_z80_readmem(addr++);
				    d = (a >> 3) & 7;
				    switch(a & 0xC0) {
				        case 0x00:
					    snprintf(buf, bufsize, "%s  (%s+%2.2Xh)", ins8[d], ireg, opaddr);
				        case 0x40:
					    snprintf(buf, bufsize, "BIT  %d,(%s+%2.2Xh)", d, ireg, opaddr);
					    break;
				        case 0x80:
					    snprintf(buf, bufsize, "RES  %d,(%s+%2.2Xh)", d, ireg, opaddr);
					    break;
				        case 0xC0:
					    snprintf(buf, bufsize, "SET  %d,(%s+%2.2Xh)", d, ireg, opaddr);
					    break;
				    }
				    break;
			    }
			    break;
			}
		    } else {
			if ((d >> 1) == 3)
			    strncpy(buf, "PUSH AF", bufsize);
			else
			    snprintf(buf, bufsize, "PUSH %s", dreg[d >> 1]);
		    }
		    break;
	        case 0x06:
		    opaddr = tube_z80_readmem(addr++);
		    snprintf(buf, bufsize, "%s%2.2Xh", arith[d], opaddr);
		    break;
	        case 0x07:
		    snprintf(buf, bufsize, "RST  %2.2Xh", a & 0x38);
		    break;
	    }
	    break;
    }
    return addr;
}

uint32_t z80_disassemble(uint32_t addr, char *buf, size_t bufsize) {
    const size_t width=18;
    size_t len;
    char *ptr;

    if (bufsize >= width) {
	len = snprintf(buf, bufsize, "%04X ", addr);
	addr = disassemble(addr, buf+len, bufsize-len);
	len = strlen(buf);
	if (len < width) {
	    ptr = buf + len;
	    do *ptr++ = ' '; while (++len < width);
	}
    }
    return addr;
}
