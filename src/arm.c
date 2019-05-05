/*B-em v2.2 by Tom Walker
  ARM1 parasite CPU emulation
  Originally from Arculator*/

#include <stdio.h>
#include <stdint.h>
#include "b-em.h"
#include "arm.h"
#include "tube.h"
#include "cpu_debug.h"
#include "darm/darm.h"
#include "ssinline.h"

#define ARM_ROM_SIZE   0x4000
#define ARM_RAM_SIZE 0x400000

static int arm_debug_enabled = 0;

uint32_t *usrregs[16],userregs[16],superregs[16],fiqregs[16],irqregs[16];
uint32_t armregs[16];

#define PC ((armregs[15])&0x3FFFFFC)

static uint32_t opcode2,opcode3;

static int armirq=0;
static int databort;
static uint32_t *armrom,*armram;
static uint8_t *armromb,*armramb;
#define USER       0
#define FIQ        1
#define IRQ        2
#define SUPERVISOR 3

#define NFSET ((armregs[15]&0x80000000)?1:0)
#define ZFSET ((armregs[15]&0x40000000)?1:0)
#define CFSET ((armregs[15]&0x20000000)?1:0)
#define VFSET ((armregs[15]&0x10000000)?1:0)

#define NFLAG 0x80000000
#define ZFLAG 0x40000000
#define CFLAG 0x20000000
#define VFLAG 0x10000000
#define IFLAG 0x08000000

#define RD ((opcode>>12)&0xF)
#define RN ((opcode>>16)&0xF)
#define RM (opcode&0xF)

#define MULRD ((opcode>>16)&0xF)
#define MULRN ((opcode>>12)&0xF)
#define MULRS ((opcode>>8)&0xF)
#define MULRM (opcode&0xF)

#define GETADDR(r) ((r==15)?(armregs[15]&0x3FFFFFC):armregs[r])
#define LOADREG(r,v) if (r==15) { armregs[15]=(armregs[15]&0xFC000003)|((v+4)&0x3FFFFFC); refillpipeline(); } else armregs[r]=v;
#define GETREG(r) ((r==15) ? armregs[15]+4 : armregs[r])
#define LDRRESULT(a,v) ((a&3)?(v>>((a&3)<<3))|(v<<(((a&3)^3)<<3)):v)

#define undefined()\
                                        templ=armregs[15]-4; \
                                        armregs[15]|=3;\
                                        updatemode(SUPERVISOR);\
                                        armregs[14]=templ;\
                                        armregs[15]&=0xFC000003;\
                                        armregs[15]|=0x08000008;\
                                        refillpipeline();\
                                        tubecycles--

static int mode;
static int osmode=0,memmode;

static void mode_store_regs(void)
{
    int c;

    switch (mode)
    {
        case USER:
            for (c=8; c<15; c++)
                userregs[c]=armregs[c];
            break;
        case IRQ:
            for (c=8; c<13; c++)
                userregs[c]=armregs[c];
            irqregs[0]=armregs[13];
            irqregs[1]=armregs[14];
            break;
        case FIQ:
            for (c=8; c<15; c++)
                fiqregs[c]=armregs[c];
            break;
        case SUPERVISOR:
            for (c=8; c<13; c++)
                userregs[c]=armregs[c];
            superregs[0]=armregs[13];
            superregs[1]=armregs[14];
            break;
    }
}

static void mode_load_regs(void)
{
    int c;

    switch (mode)
    {
        case USER:
            for (c=8; c<15; c++)
                armregs[c]=userregs[c];
            memmode=osmode;
            for (c=0; c<15; c++)
                usrregs[c]=&armregs[c];
            break;
        case IRQ:
            for (c=8; c<13; c++)
                armregs[c]=userregs[c];
            armregs[13]=irqregs[0];
            armregs[14]=irqregs[1];
            for (c=0; c<13; c++)
                usrregs[c]=&armregs[c];
            for (c=13; c<15; c++)
                usrregs[c]=&userregs[c];
            memmode=2;
            break;
        case FIQ:
            for (c=8; c<15; c++)
                armregs[c]=fiqregs[c];
            for (c=0; c<8; c++)
                usrregs[c]=&armregs[c];
            for (c=8; c<15; c++)
                usrregs[c]=&userregs[c];
            memmode=2;
            break;
        case SUPERVISOR:
            for (c=8; c<13; c++)
                armregs[c]=userregs[c];
            armregs[13]=superregs[0];
            armregs[14]=superregs[1];
            for (c=0; c<13; c++)
                usrregs[c]=&armregs[c];
            for (c=13; c<15; c++)
                usrregs[c]=&userregs[c];
            memmode=2;
            break;
    }
}

static void updatemode(int m)
{
    usrregs[15]=&armregs[15];
    mode_store_regs();
    mode=m;
    mode_load_regs();
}

static uint8_t flaglookup[16][16];
static uint32_t rotatelookup[4096];

static void refillpipeline2();

#define countbits(c) countbitstable[c]
static int countbitstable[65536];
void arm_reset()
{
        int c,d,exec,data;
        uint32_t rotval,rotamount;
        for (c=0;c<65536;c++)
        {
                countbitstable[c]=0;
                for (d=0;d<16;d++)
                {
                        if (c&(1<<d)) countbitstable[c]+=4;
                }
        }
        for (c=0;c<16;c++)
        {
                for (d=0;d<16;d++)
                {
                        armregs[15]=d<<28;
                        switch (c)
                        {
                                case 0:  /*EQ*/ exec=ZFSET; break;
                                case 1:  /*NE*/ exec=!ZFSET; break;
                                case 2:  /*CS*/ exec=CFSET; break;
                                case 3:  /*CC*/ exec=!CFSET; break;
                                case 4:  /*MI*/ exec=NFSET; break;
                                case 5:  /*PL*/ exec=!NFSET; break;
                                case 6:  /*VS*/ exec=VFSET; break;
                                case 7:  /*VC*/ exec=!VFSET; break;
                                case 8:  /*HI*/ exec=(CFSET && !ZFSET); break;
                                case 9:  /*LS*/ exec=(!CFSET || ZFSET); break;
                                case 10: /*GE*/ exec=(NFSET == VFSET); break;
                                case 11: /*LT*/ exec=(NFSET != VFSET); break;
                                case 12: /*GT*/ exec=(!ZFSET && (NFSET==VFSET)); break;
                                case 13: /*LE*/ exec=(ZFSET || (NFSET!=VFSET)); break;
                                case 14: /*AL*/ exec=1; break;
                                case 15: /*NV*/ exec=0; break;
                        }
                        flaglookup[c][d]=exec;
                }
        }

        for (data=0;data<4096;data++)
        {
                rotval=data&0xFF;
                rotamount=((data>>8)&0xF)<<1;
                rotval=(rotval>>rotamount)|(rotval<<(32-rotamount));
                rotatelookup[data]=rotval;
        }
        updatemode(SUPERVISOR);
        armregs[15]=0x0C00000B;
        mode=3;
        memmode=2;
        memcpy(armramb,armromb,ARM_ROM_SIZE);
        refillpipeline2();
}

void arm_dumpregs()
{
        FILE *f=x_fopen("armram.dmp","wb");
        fwrite(armram,0x10000,1,f);
        fclose(f);
        log_debug("R 0=%08X R 4=%08X R 8=%08X R12=%08X\n",armregs[0],armregs[4],armregs[8],armregs[12]);
        log_debug("R 1=%08X R 5=%08X R 9=%08X R13=%08X\n",armregs[1],armregs[5],armregs[9],armregs[13]);
        log_debug("R 2=%08X R 6=%08X R10=%08X R14=%08X\n",armregs[2],armregs[6],armregs[10],armregs[14]);
        log_debug("R 3=%08X R 7=%08X R11=%08X R15=%08X\n",armregs[3],armregs[7],armregs[11],armregs[15]);
        log_debug("f12=%08X  ",fiqregs[12]);
        log_debug("PC =%07X\n",PC);
}

static uint32_t *armread[64];
static uint32_t armmask[64];

bool arm_init(FILE *romf)
{
        int c;
        if (!armrom) armrom=(uint32_t *)malloc(ARM_ROM_SIZE);
        if (!armram) armram=(uint32_t *)malloc(ARM_RAM_SIZE);
        armromb=(uint8_t *)armrom;
        armramb=(uint8_t *)armram;
        if (fread(armromb, ARM_ROM_SIZE, 1, romf) != 1)
            return false;
        memcpy(armramb,armromb,ARM_ROM_SIZE);
        for (c=0;c<64;c++) armread[c]=0;
        for (c=0;c<4;c++) armread[c]=&armram[c*0x40000];
        armread[48]=armrom;
        for (c=0;c<64;c++) armmask[c]=0xFFFFF;
        armmask[48]=0x3FFF;
        return true;
}

void arm_close()
{
        if (armrom) free(armrom);
        if (armram) free(armram);
}

static unsigned char *save_regset(unsigned char *ptr, uint32_t *regs)
{
    for (int i = 0; i < 16; i++)
        ptr = save_uint32(ptr, regs[i]);
    return ptr;
}

void arm_savestate(ZFILE *zfp)
{
    unsigned char bytes[332], *ptr;

    ptr = bytes;
    *ptr++ = mode;
    *ptr++ = osmode;
    mode_store_regs();
    ptr = save_regset(ptr, armregs);
    ptr = save_regset(ptr, userregs);
    ptr = save_regset(ptr, superregs);
    ptr = save_regset(ptr, fiqregs);
    ptr = save_regset(ptr, irqregs);
    ptr = save_uint32(ptr, opcode2);
    ptr = save_uint32(ptr, opcode3);
    *ptr++ = armirq;
    *ptr++ = databort;
    savestate_zwrite(zfp, bytes, sizeof bytes);
    savestate_zwrite(zfp, armram, ARM_RAM_SIZE);
    savestate_zwrite(zfp, armrom, ARM_ROM_SIZE);
}

static unsigned char *load_regset(unsigned char *ptr, uint32_t *regs)
{
    for (int i = 0; i < 16; i++)
        ptr = load_uint32(ptr, &regs[i]);
    return ptr;
}

void arm_loadstate(ZFILE *zfp)
{
    unsigned char bytes[332], *ptr;

    savestate_zread(zfp, bytes, sizeof bytes);
    ptr = bytes;
    mode = *ptr++;
    osmode = *ptr++;
    ptr = load_regset(ptr, armregs);
    ptr = load_regset(ptr, userregs);
    ptr = load_regset(ptr, superregs);
    ptr = load_regset(ptr, fiqregs);
    ptr = load_regset(ptr, irqregs);
    mode_load_regs();
    ptr = load_uint32(ptr, &opcode2);
    ptr = load_uint32(ptr, &opcode3);
    armirq = *ptr++;
    databort = *ptr;
    savestate_zread(zfp, armram, ARM_RAM_SIZE);
    savestate_zread(zfp, armrom, ARM_ROM_SIZE);
}

int endtimeslice=0;

static inline uint32_t readarmfl(uint32_t addr)
{
        if (addr<0x400000) return armram[addr>>2];
        if (addr<0x400010) return 0xFFFFFFFF;
        if ((addr>=0x3000000) && (addr<0x3004000)) return armrom[(addr&0x3FFC)>>2];
        return 0xFFFFFFFF;
/*        log_debug("Bad ARM read long %08X\n",addr);
        dumparmregs();
        exit(-1);*/
}

extern cpu_debug_t tubearm_cpu_debug;

static inline uint32_t do_readarml(uint32_t a)
{
    if (armread[(a>>20)&63])
        return armread[(a>>20)&63][(a&0xFFFFF)>>2];
    return readarmfl(a);
}

static uint32_t readarml(uint32_t a)
{
    uint32_t v = do_readarml(a);

    if (arm_debug_enabled)
        debug_memread(&tubearm_cpu_debug, a, v, 4);
    return v;
}

static inline uint8_t do_readarmb(uint32_t addr)
{
        if (addr<0x400000) return armramb[addr];
        if ((addr&~0x1F)==0x1000000)
        {
                //log_debug("Read %08X\n",addr);
                return tube_parasite_read((addr&0x1C)>>2);
        }
        if ((addr>=0x3000000) && (addr<0x3004000)) return armromb[addr&0x3FFF];
        return 0xFF;
/*        log_debug("Bad ARM read byte %08X\n",addr);
        dumparmregs();
        exit(-1);*/
}

uint8_t readarmb(uint32_t addr)
{
    uint8_t v = do_readarmb(addr);
    if (arm_debug_enabled)
        debug_memread(&tubearm_cpu_debug, addr, v, 1);
    return v;
}

static inline void do_writearmb(uint32_t addr, uint8_t val)
{
        if (addr<0x400000)
        {
                armramb[addr]=val;
                return;
        }
        if ((addr&~0x1F)==0x1000000)
        {
//                log_debug("Write %08X %02X\n",addr,val);
                tube_parasite_write((addr&0x1C)>>2,val);
                endtimeslice=1;
                return;
        }
/*        log_debug("Bad ARM write byte %08X %02X\n",addr,val);
        dumparmregs();
        exit(-1);*/
}

void writearmb(uint32_t addr, uint8_t val)
{
    if (arm_debug_enabled)
        debug_memwrite(&tubearm_cpu_debug, addr, val, 1);
    do_writearmb(addr, val);
}

static void writearml(uint32_t addr, uint32_t val)
{
        if (arm_debug_enabled)
                debug_memwrite(&tubearm_cpu_debug, addr, val, 4);
        if (addr<0x400000)
        {
                armram[addr>>2]=val;
                return;
        }
/*        if (addr<0x400010) return;
        log_debug("Bad ARM write long %08X %08X\n",addr,val);
        dumparmregs();
        exit(-1);*/
}

/*****************************************************
 * CPU Debug Interface
 *****************************************************/

#define ADDRESS_MASK    ((uint32_t) 0x03fffffcu)

static darm_t dis;
static darm_str_t dis_str;

enum register_numbers {
    i_R0,
    i_R1,
    i_R2,
    i_R3,
    i_R4,
    i_R5,
    i_R6,
    i_R7,
    i_R8,
    i_R9,
    i_R10,
    i_R11,
    i_R12,
    i_SP,
    i_LR,
    i_PC,
    i_R8_fiq,
    i_R9_fiq,
    i_R10_fiq,
    i_R11_fiq,
    i_R12_fiq,
    i_SP_fiq,
    i_LR_fiq,
    i_SP_irq,
    i_LR_irq,
    i_SP_svc,
    i_LR_svc,
    i_PSR
};

// NULL pointer terminated list of register names.
static const char *arm_dbg_reg_names[] = {
    "R0",
    "R1",
    "R2",
    "R3",
    "R4",
    "R5",
    "R6",
    "R7",
    "R8",
    "R9",
    "R10",
    "R11",
    "R12",
    "SP",
    "LR",
    "PC",
    "R8_fiq",
    "R9_fiq",
    "R10_fiq",
    "R11_fiq",
    "R12_fiq",
    "SP_fiq",
    "LR_fiq",
    "SP_irq",
    "LR_irq",
    "SP_svc",
    "LR_svc",
    "PSR",
    NULL
};

static int arm_dbg_debug_enable(int newvalue) {
    int oldvalue = arm_debug_enabled;
    arm_debug_enabled = newvalue;
    return oldvalue;
};

static uint32_t arm_dbg_memread(uint32_t addr)
{
    return do_readarmb(addr);
}

static void arm_dbg_memwrite(uint32_t addr, uint32_t val)
{
    do_writearmb(addr, val);
}

static uint32_t arm_dbg_disassemble(uint32_t addr, char *buf, size_t bufsize) {
   uint32_t instr = do_readarml(addr);
   int len = snprintf(buf, bufsize, "%08"PRIx32" %08"PRIx32" ", addr, instr);
   buf += len;
   bufsize -= len;
   int ok = 0;
   if (darm_armv7_disasm(&dis, instr) == 0) {
      dis.addr = addr;
      dis.addr_mask = ADDRESS_MASK;
      if (darm_str2(&dis, &dis_str, 1) == 0) {
         strncpy(buf, dis_str.total, bufsize);
         ok = 1;
      }
   }
   if (!ok) {
      strncpy(buf, "???", bufsize);
   }
   return addr + 4;
};

static uint32_t arm_dbg_reg_get(int which) {
    if (which <= i_LR) {
        return *(usrregs[which]);
    } else if (which == i_PC) {
        return PC;
    } else if (which == i_PSR) {
        return ((armregs[i_PC] >> 24) & 0xFC) | (armregs[i_PC] & 0x03);
    } else if (which <= i_LR_fiq) {
        if (mode == FIQ)
            return armregs[8+(which-i_R8_fiq)];
        else
            return fiqregs[8+(which-i_R8_fiq)];
    } else if (which <= i_LR_irq) {
        if (mode == IRQ)
            return armregs[13+(which-i_LR_irq)];
        else
            return fiqregs[13+(which-i_LR_irq)];
    } else if (which <= i_LR_svc) {
        if (mode == SUPERVISOR)
            return armregs[13+(which-i_LR_svc)];
        else
            return superregs[13+(which-i_LR_svc)];
    } else {
        log_warn("arm: unrecognised register %d", which);
        return 0;
    }
};

// Set a register.
static void  arm_dbg_reg_set(int which, uint32_t value) {
    if (which <= i_LR) {
        *(usrregs[which]) = value;
    } else if (which == i_PC) {
        armregs[i_PC] &= ~ADDRESS_MASK;
        armregs[i_PC] |= (value & ADDRESS_MASK);
    } else if (which == i_PSR) {
        armregs[i_PC] &= ADDRESS_MASK;
        armregs[i_PC] |= ((value & 0xFC) << 24) | (value & 0x03);
    } else if (which <= i_LR_fiq) {
        if (mode == FIQ)
            armregs[8+(which-i_R8_fiq)] = value;
        else
            fiqregs[8+(which-i_R8_fiq)] = value;
    } else if (which <= i_LR_irq) {
        if (mode == IRQ)
            armregs[13+(which-i_LR_irq)] = value;
        else
            fiqregs[13+(which-i_LR_irq)] = value;
    } else if (which <= i_LR_svc) {
        if (mode == SUPERVISOR)
            armregs[13+(which-i_LR_svc)] = value;
        else
            superregs[13+(which-i_LR_svc)] = value;
    } else {
        log_warn("arm: unrecognised register %d", which);
    }
}

static const char* flagname = "N Z C V I F M1 M0 ";

// Print register value in CPU standard form.
static size_t arm_dbg_reg_print(int which, char *buf, size_t bufsize) {
    if (which == i_PSR) {
        int i;
        int bit;
        char c;
        const char *flagnameptr = flagname;
        int psr = arm_dbg_reg_get(i_PSR);

        if (bufsize < 40) {
            strncpy(buf, "buffer too small!!!", bufsize);
        }

        bit = 0x80;
        for (i = 0; i < 8; i++) {
            if (psr & bit) {
                c = '1';
            } else {
                c = '0';
            }
            do {
                *buf++ = *flagnameptr++;
            } while (*flagnameptr != ' ');
            flagnameptr++;
            *buf++ = ':';
            *buf++ = c;
            *buf++ = ' ';
            bit >>= 1;
        }
        switch (psr & 3) {
        case 0:
            sprintf(buf, "(USR)");
            break;
        case 1:
            sprintf(buf, "(FIQ)");
            break;
        case 2:
            sprintf(buf, "(IRQ)");
            break;
        case 3:
            sprintf(buf, "(SVC)");
            break;
        }
        return strlen(buf);
    } else {
        return snprintf(buf, bufsize, "%08"PRIx32, arm_dbg_reg_get(which));
    }
};

// Parse a value into a register.
static void arm_dbg_reg_parse(int which, char *strval) {
   uint32_t val = 0;
   sscanf(strval, "%"SCNx32, &val);
   arm_dbg_reg_set(which, val);
};

static uint32_t arm_dbg_get_instr_addr() {
    return PC;
}

static const char *arm_trap_names[] = { NULL };

cpu_debug_t tubearm_cpu_debug = {
   .cpu_name       = "ARM",
   .debug_enable   = arm_dbg_debug_enable,
   .memread        = arm_dbg_memread,
   .memwrite       = arm_dbg_memwrite,
   .disassemble    = arm_dbg_disassemble,
   .reg_names      = arm_dbg_reg_names,
   .reg_get        = arm_dbg_reg_get,
   .reg_set        = arm_dbg_reg_set,
   .reg_print      = arm_dbg_reg_print,
   .reg_parse      = arm_dbg_reg_parse,
   .get_instr_addr = arm_dbg_get_instr_addr,
   .trap_names     = arm_trap_names
};

/*****************************************************
 * CPU Implementation
 *****************************************************/

#define checkneg(v) (v&0x80000000)
#define checkpos(v) !(v&0x80000000)

static inline void setadd(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkneg(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkneg(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkneg(op2) && checkpos(res)) ||
            (checkpos(op1) && checkpos(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
        if (!res)                          armregs[15]|=ZFLAG;
        else if (checkneg(res))            armregs[15]|=NFLAG;
}

static inline void setsub(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkpos(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkpos(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if (!res)               armregs[15]|=ZFLAG;
        else if (checkneg(res)) armregs[15]|=NFLAG;
        if ((checkneg(op1) && checkpos(op2) && checkpos(res)) ||
            (checkpos(op1) && checkneg(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
}

static inline void setsbc(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if (!res)                           armregs[15]|=ZFLAG;
        else if (checkneg(res))             armregs[15]|=NFLAG;
        if ((checkneg(op1) && checkpos(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkpos(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkpos(op2) && checkpos(res)) ||
            (checkpos(op1) && checkneg(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
}

static inline void setadc(uint32_t op1, uint32_t op2, uint32_t res)
{
        armregs[15]&=0xFFFFFFF;
        if ((checkneg(op1) && checkneg(op2)) ||
            (checkneg(op1) && checkpos(res)) ||
            (checkneg(op2) && checkpos(res)))  armregs[15]|=CFLAG;
        if ((checkneg(op1) && checkneg(op2) && checkpos(res)) ||
            (checkpos(op1) && checkpos(op2) && checkneg(res)))
            armregs[15]|=VFLAG;
        if (!res)                          armregs[15]|=ZFLAG;
        else if (checkneg(res))            armregs[15]|=NFLAG;
}

static inline void setarmzn(uint32_t op)
{
        armregs[15]&=0x3FFFFFFF;
        if (!op)               armregs[15]|=ZFLAG;
        else if (checkneg(op)) armregs[15]|=NFLAG;
}

static inline uint32_t shift(uint32_t opcode)
{
        uint32_t shiftmode=(opcode>>5)&3;
        uint32_t shiftamount=(opcode>>7)&31;
        uint32_t temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
//                cycles--;
        }
        temp=armregs[RM];
//        if (RM==15)        temp+=4;
        if (opcode&0x100000 && shiftamount) armregs[15]&=~CFLAG;
        switch (shiftmode)
        {
                case 0: /*LSL*/
                if (!shiftamount) return temp;
                if (shiftamount==32)
                {
                        if (temp&1 && opcode&0x100000) armregs[15]|=CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp<<(shiftamount-1))&0x80000000) armregs[15]|=CFLAG;
                }
                return temp<<shiftamount;

                case 1: /*LSR*/
                if (!shiftamount && !(opcode&0x10))
                {
                        shiftamount=32;
                }
                if (!shiftamount) return temp;
                if (shiftamount==32)
                {
                        if (temp&0x80000000 && opcode&0x100000) armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               armregs[15]&=~CFLAG;
                        return 0;
                }
                if (shiftamount>32) return 0;
                if (opcode&0x100000)
                {
                        if ((temp>>(shiftamount-1))&1) armregs[15]|=CFLAG;
                }
                return temp>>shiftamount;

                case 2: /*ASR*/
                if (!shiftamount)
                {
                        if (opcode&0x10) return temp;
                }
                if (shiftamount>=32 || !shiftamount)
                {
                        if (temp&0x80000000 && opcode&0x100000) armregs[15]|=CFLAG;
                        else if (opcode&0x100000)               armregs[15]&=~CFLAG;
                        if (temp&0x80000000) return 0xFFFFFFFF;
                        return 0;
                }
                if (opcode&0x100000)
                {
                        if (((int)temp>>(shiftamount-1))&1) armregs[15]|=CFLAG;
                }
                return (int)temp>>shiftamount;

                case 3: /*ROR*/
                if (opcode&0x100000) armregs[15]&=~CFLAG;
                if (!shiftamount && !(opcode&0x10))
                {
                        if (opcode&0x100000 && temp&1) armregs[15]|=CFLAG;
                        return (((cflag)?1:0)<<31)|(temp>>1);
                }
                if (!shiftamount)
                {
                        if (opcode&0x100000) armregs[15]|=cflag;
                        return temp;
                }
                if (!(shiftamount&0x1F))
                {
                        if (opcode&0x100000 && temp&0x80000000) armregs[15]|=CFLAG;
                        return temp;
                }
                if (opcode&0x100000)
                {
                        if (((temp>>shiftamount)|(temp<<(32-shiftamount)))&0x80000000) armregs[15]|=CFLAG;
                }
                return (temp>>shiftamount)|(temp<<(32-shiftamount));
        }
        return 0; /*Should never reach here*/
}

static inline unsigned shift2(unsigned opcode)
{
        unsigned shiftmode=(opcode>>5)&3;
        unsigned shiftamount=(opcode>>7)&31;
        uint32_t temp;
        int cflag=CFSET;
        if (!(opcode&0xFF0)) return armregs[RM];
        if (opcode&0x10)
        {
                shiftamount=armregs[(opcode>>8)&15]&0xFF;
                if (shiftmode==3)
                   shiftamount&=0x1F;
        }
        temp=armregs[RM];
//        if (RM==15) temp+=4;
        switch (shiftmode)
        {
                case 0: /*LSL*/
                if (!shiftamount)    return temp;
                if (shiftamount>=32) return 0;
                return temp<<shiftamount;

                case 1: /*LSR*/
                if (!shiftamount && !(opcode&0x10))    return 0;
                if (shiftamount>=32) return 0;
                return temp>>shiftamount;

                case 2: /*ASR*/
                if (!shiftamount && !(opcode&0x10)) shiftamount=32;
                if (shiftamount>=32)
                {
                        if (temp&0x80000000)
                           return 0xFFFFFFFF;
                        return 0;
                }
                return (int)temp>>shiftamount;

                case 3: /*ROR*/
                if (!shiftamount && !(opcode&0x10)) return (((cflag)?1:0)<<31)|(temp>>1);
                if (!shiftamount)                   return temp;
                return (temp>>shiftamount)|(temp<<(32-shiftamount));
        }
        return 0; /*Should never reach here*/
}

static inline unsigned rotate(unsigned data)
{
        uint32_t rotval;
        rotval=rotatelookup[data&4095];
        if (data&0x100000 && data&0xF00)
        {
                if (rotval&0x80000000) armregs[15]|=CFLAG;
                else                   armregs[15]&=~CFLAG;
        }
        return rotval;
}

#define rotate2(v) rotatelookup[v&4095]

static int ldrlookup[4]={0,8,16,24};

#define ldrresult(v,a) ((v>>ldrlookup[addr&3])|(v<<(32-ldrlookup[addr&3])))

static void refillpipeline()
{
        opcode2=readarml(PC-4);
        opcode3=readarml(PC);
}

static void refillpipeline2()
{
        opcode2=readarml(PC-8);
        opcode3=readarml(PC-4);
}

int accc=0;
void arm_exec()
{
        uint32_t opcode,templ,templ2,mask,addr,addr2;
        int c;
        while (tubecycles>0)
        {
                opcode=opcode2;
                opcode2=opcode3;
                opcode3=readarml(PC);
                if (arm_debug_enabled)
                    debug_preexec(&tubearm_cpu_debug, PC);
                if (flaglookup[opcode>>28][armregs[15]>>28])
                        {
                                switch ((opcode>>20)&0xFF)
                                {
                                        case 0x00: /*AND reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MUL*/
//                                        {
//                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                cycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                }
                                                tubecycles--;
//                                        }
                                        break;
                                        case 0x01: /*ANDS reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MULS*/
//                                        {
//                                                armregs[MULRD]=(armregs[MULRM])*(armregs[MULRS]);
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                setarmzn(armregs[MULRD]);
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(GETADDR(RN)&templ)+4;
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)&templ;
                                                        setarmzn(armregs[RD]);
                                                }
                                                tubecycles--;
//                                        }
                                        break;

                                        case 0x02: /*EOR reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MLA*/
///                                        {
//                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[RD]=GETADDR(RN)^templ;
                                                }
                                                tubecycles--;
//                                        }
                                        break;
                                        case 0x03: /*EORS reg*/
//                                        if (((opcode&0xF0)==0x90)) /*MLAS*/
//                                        {
//                                                armregs[MULRD]=((armregs[MULRM])*(armregs[MULRS]))+armregs[MULRN];
//                                                if (MULRD==MULRM) armregs[MULRD]=0;
//                                                setarmzn(armregs[MULRD]);
//                                                tubecycles-=17;
//                                        }
//                                        else
//                                        {
                                                if (RD==15)
                                                {
                                                        templ=shift2(opcode);
                                                        armregs[15]=(GETADDR(RN)^templ)+4;
                                                        refillpipeline();
                                                }
                                                else
                                                {
                                                        templ=shift(opcode);
                                                        armregs[RD]=GETADDR(RN)^templ;
                                                        setarmzn(armregs[RD]);
                                                }
                                                tubecycles--;
//                                        }
                                        break;

                                        case 0x04: /*SUB reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x05: /*SUBS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)-templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x06: /*RSB reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x07: /*RSBS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(templ-GETADDR(RN))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x08: /*ADD reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x09: /*ADDS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x0A: /*ADC reg*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0B: /*ADCS reg*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+templ2+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=shift2(opcode);
                                                setadc(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x0C: /*SBC reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0D: /*SBCS reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsbc(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0E: /*RSC reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((templ-(GETADDR(RN)+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x0F: /*RSCS reg*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(templ-(GETADDR(RN)+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                setsbc(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x10: /*SWP word*/
                                        break;

                                        case 0x11: /*TST reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)&shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)&shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&shift(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x12:
                                        break;

                                        case 0x13: /*TEQ reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)^shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)^shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^shift(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x14: /*SWPB*/
                                        break;

                                        case 0x15: /*CMP reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)-shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)-shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setsub(GETADDR(RN),shift(opcode),GETADDR(RN)-shift2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x17: /*CMN reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)+shift2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)+shift2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setadd(GETADDR(RN),shift2(opcode),GETADDR(RN)+shift2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x18: /*ORR reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x19: /*ORRS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)|templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1A: /*MOV reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|((shift2(opcode)+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=shift2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x1B: /*MOVS reg*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3) armregs[15]=shift2(opcode)+4;
                                                else               armregs[15]=((shift2(opcode)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1C: /*BIC reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift2(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x1D: /*BICS reg*/
                                        if (RD==15)
                                        {
                                                templ=shift2(opcode);
                                                armregs[15]=(GETADDR(RN)&~templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=shift(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x1E: /*MVN reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~shift2(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~shift2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x1F: /*MVNS reg*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~shift2(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~shift(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x20: /*AND imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)&templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)&templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x21: /*ANDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)&templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x22: /*EOR imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)^templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)^templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x23: /*EORS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)^templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)^templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x24: /*SUB imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)-templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x25: /*SUBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)-templ)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsub(GETADDR(RN),templ,GETADDR(RN)-templ);
                                                armregs[RD]=GETADDR(RN)-templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x26: /*RSB imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((templ-GETADDR(RN))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x27: /*RSBS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(templ-GETADDR(RN))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsub(templ,GETADDR(RN),templ-GETADDR(RN));
                                                armregs[RD]=templ-GETADDR(RN);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x28: /*ADD imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)+templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x29: /*ADDS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setadd(GETADDR(RN),templ,GETADDR(RN)+templ);
                                                armregs[RD]=GETADDR(RN)+templ;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x2A: /*ADC imm*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[15]=((GETADDR(RN)+templ+templ2+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2B: /*ADCS imm*/
                                        if (RD==15)
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                armregs[15]=GETADDR(RN)+templ+templ2+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ2=CFSET;
                                                templ=rotate2(opcode);
                                                setadc(GETADDR(RN),templ,GETADDR(RN)+templ+templ2);
                                                armregs[RD]=GETADDR(RN)+templ+templ2;
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x2C: /*SBC imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)-(templ+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2D: /*SBCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(GETADDR(RN)-(templ+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsbc(GETADDR(RN),templ,GETADDR(RN)-(templ+templ2));
                                                armregs[RD]=GETADDR(RN)-(templ+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2E: /*RSC imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((templ-(GETADDR(RN)+templ2))+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x2F: /*RSCS imm*/
                                        templ2=(CFSET)?0:1;
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(templ-(GETADDR(RN)+templ2))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                setsbc(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
//                                                setsub(templ,GETADDR(RN),templ-(GETADDR(RN)+templ2));
                                                armregs[RD]=templ-(GETADDR(RN)+templ2);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x31: /*TST imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)&rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)&rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)&rotate(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x33: /*TEQ imm*/
                                        if (RD==15)
                                        {
                                                opcode&=~0x100000;
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)^rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                        {
                                                setarmzn(GETADDR(RN)^rotate(opcode));
//                                                log_debug("TEQ %08X %08X\n",GETADDR(RN),rotate(opcode));
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x34:
                                        break;
                                        case 0x35: /*CMP imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)-rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)-rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setsub(GETADDR(RN),rotate2(opcode),GETADDR(RN)-rotate2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x37: /*CMN imm*/
                                        if (RD==15)
                                        {
                                                if (armregs[15]&3)
                                                {
                                                        templ=armregs[15]&0x3FFFFFC;
                                                        armregs[15]=((GETADDR(RN)+rotate2(opcode))&0xFC000003)|templ;
                                                }
                                                else
                                                {
                                                        templ=armregs[15]&0x0FFFFFFF;
                                                        armregs[15]=((GETADDR(RN)+rotate2(opcode))&0xF0000000)|templ;
                                                }
                                        }
                                        else
                                           setadd(GETADDR(RN),rotate2(opcode),GETADDR(RN)+rotate2(opcode));
                                        tubecycles--;
                                        break;

                                        case 0x38: /*ORR imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)|templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x39: /*ORRS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                if (armregs[15]&3)
                                                   armregs[15]=(GETADDR(RN)|templ)+4;
                                                else
                                                   armregs[15]=(((GETADDR(RN)|templ)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)|templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3A: /*MOV imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|((rotate2(opcode)+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=rotate2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x3B: /*MOVS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=rotate2(opcode)+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3C: /*BIC imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                armregs[15]=(((GETADDR(RN)&~templ)+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate2(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                        }
                                        tubecycles--;
                                        break;
                                        case 0x3D: /*BICS imm*/
                                        if (RD==15)
                                        {
                                                templ=rotate2(opcode);
                                                if (armregs[15]&3) armregs[15]=(GETADDR(RN)&~templ)+4;
                                                else               armregs[15]=(((GETADDR(RN)&~templ)+4)&0xF3FFFFFC)|(armregs[15]&0xC000003);
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                templ=rotate(opcode);
                                                armregs[RD]=GETADDR(RN)&~templ;
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x3E: /*MVN imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(armregs[15]&0xFC000003)|(((~rotate2(opcode))+4)&0x3FFFFFC);
                                                refillpipeline();
                                        }
                                        else
                                           armregs[RD]=~rotate2(opcode);
                                        tubecycles--;
                                        break;
                                        case 0x3F: /*MVNS imm*/
                                        if (RD==15)
                                        {
                                                armregs[15]=(~rotate2(opcode))+4;
                                                refillpipeline();
                                        }
                                        else
                                        {
                                                armregs[RD]=~rotate(opcode);
                                                setarmzn(armregs[RD]);
                                        }
                                        tubecycles--;
                                        break;

                                        case 0x47: case 0x4F: /*LDRBT*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=memmode;
                                        memmode=0;
                                        templ2=readarmb(addr);
                                        memmode=templ;
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        tubecycles-=4;
                                        break;

                                        case 0x41: case 0x49: case 0x61: case 0x69: /*LDR Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        templ2=ldrresult(readarml(addr),addr);
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=4;
                                        break;
                                        case 0x43: case 0x4B: case 0x63: case 0x6B: /*LDRT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        templ=memmode; memmode=0;
                                        templ2=ldrresult(readarml(addr),addr);
                                        memmode=templ;
                                        if (databort) break;
                                        LOADREG(RD,templ2);
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=4;
                                        break;

                                        case 0x40: case 0x48: case 0x60: case 0x68: /*STR Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        if (databort) break;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x42: case 0x4A: case 0x62: case 0x6A: /*STRT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        templ=memmode; memmode=0;
                                        if (databort) break;
                                        memmode=templ;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x50: case 0x58: case 0x70: case 0x78: /*STR Rd,[Rn,offset]*/
                                        case 0x52: case 0x5A: case 0x72: case 0x7A: /*STR Rd,[Rn,offset]!*/
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (opcode&0x800000) addr=GETADDR(RN)+addr2;
                                        else                 addr=GETADDR(RN)-addr2;
                                        if (RD==15) { writearml(addr,armregs[RD]+4); }
                                        else        { writearml(addr,armregs[RD]); }
                                        if (databort) break;
                                        if (opcode&0x200000) armregs[RN]=addr;
                                        tubecycles-=3;
                                        break;

                                        case 0x44: case 0x4C: case 0x64: case 0x6C: /*STRB Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        writearmb(addr,armregs[RD]);
                                        if (databort) break;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x46: case 0x4E: case 0x66: case 0x6E: /*STRBT Rd,[Rn],offset*/
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        writearmb(addr,armregs[RD]);
                                        templ=memmode;
                                        memmode=0;
                                        if (databort) break;
                                        memmode=templ;
                                        if (opcode&0x800000) armregs[RN]+=addr2;
                                        else                 armregs[RN]-=addr2;
                                        tubecycles-=3;
                                        break;
                                        case 0x54: case 0x5C: case 0x74: case 0x7C: /*STRB Rd,[Rn,offset]*/
                                        case 0x56: case 0x5E: case 0x76: case 0x7E: /*STRB Rd,[Rn,offset]!*/
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (opcode&0x800000) addr=GETADDR(RN)+addr2;
                                        else                 addr=GETADDR(RN)-addr2;
                                        writearmb(addr,armregs[RD]);
                                        if (databort) break;
                                        if (opcode&0x200000) armregs[RN]=addr;
                                        tubecycles-=3;
                                        break;


//                                        case 0x41: case 0x49: /*LDR*/
                                        case 0x51: case 0x53: case 0x59: case 0x5B:
//                                        case 0x61: case 0x69:
                                        case 0x71: case 0x73: case 0x79: case 0x7B:
                                        if ((opcode&0x2000010)==0x2000010)
                                        {
                                                undefined();
                                                break;
                                        }
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=readarml(addr);
                                        templ=ldrresult(templ,addr);
                                        if (databort) break;
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        LOADREG(RD,templ);
//                                        if (RD==15) refillpipeline();
                                        tubecycles-=4;
/*                                        if (RD==7)
                                        {
                                                if (!olog) olog=x_fopen("armlog.txt","wt");
                                                slog_debug(s,"LDR R7 %08X,%07X\n",armregs[7],PC);
                                                fputs(s,olog);
                                        }*/
                                        break;

                                        case 0x65: case 0x6D:
                                        case 0x75: case 0x77: case 0x7D: case 0x7F:
                                        if (opcode&0x10)
                                        {
                                                undefined();
                                                break;
                                        }
                                        case 0x45: case 0x4D: /*LDRB*/
                                        case 0x55: case 0x57: case 0x5D: case 0x5F:
                                        addr=GETADDR(RN);
                                        if (opcode&0x2000000) addr2=shift2(opcode);
                                        else                  addr2=opcode&0xFFF;
                                        if (!(opcode&0x800000))  addr2=-addr2;
                                        if (opcode&0x1000000)
                                        {
                                                addr+=addr2;
                                        }
                                        templ=readarmb(addr);
                                        if (databort) break;
                                        if (!(opcode&0x1000000))
                                        {
                                                addr+=addr2;
                                                armregs[RN]=addr;
                                        }
                                        else
                                        {
                                                if (opcode&0x200000) armregs[RN]=addr;
                                        }
                                        armregs[RD]=templ;
                                        tubecycles-=4;
                                        break;

#define STMfirst()      mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        if (c==15) { writearml(addr,armregs[c]+4); } \
                                        else       { writearml(addr,armregs[c]); } \
                                        addr+=4; \
                                        tubecycles--; \
                                        break; \
                                } \
                                mask<<=1; \
                        } \
                        mask<<=1; c++;

#define STMall()        for (;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        writearml(addr,armregs[c]); \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                writearml(addr,armregs[15]+4); \
                                tubecycles--; \
                        }

#define STMfirstS()     mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        if (c==15) { writearml(addr,armregs[c]+4); } \
                                        else       { writearml(addr,*usrregs[c]); } \
                                        addr+=4; \
                                        tubecycles--; \
                                        break; \
                                } \
                                mask<<=1; \
                        } \
                        mask<<=1; c++;

#define STMallS()       for (;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        writearml(addr,*usrregs[c]); \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                writearml(addr,armregs[15]+4); \
                                tubecycles--; \
                        }

#define LDMall()        mask=1; \
                        for (c=0;c<15;c++) \
                        { \
                                if (opcode&mask) \
                                { \
                                        if (!(addr&0xC)) tubecycles--; \
                                        templ=readarml(addr); if (!databort) armregs[c]=templ; \
                                        addr+=4; \
                                        tubecycles--; \
                                } \
                                mask<<=1; \
                        } \
                        if (opcode&0x8000) \
                        { \
                                if (!(addr&0xC)) tubecycles--; \
                                templ=readarml(addr); \
                                if (!databort) armregs[15]=(armregs[15]&0xFC000003)|((templ+4)&0x3FFFFFC); \
                                tubecycles--; \
                                refillpipeline(); \
                        }

#define LDMallS()       mask=1; \
                        if (opcode&0x8000) \
                        { \
                                for (c=0;c<15;c++) \
                                { \
                                        if (opcode&mask) \
                                        { \
                                                if (!(addr&0xC)) tubecycles--; \
                                                templ=readarml(addr); if (!databort) armregs[c]=templ; \
                                                addr+=4; \
                                                tubecycles--; \
                                        } \
                                        mask<<=1; \
                                } \
                                if (!(addr&0xC)) tubecycles--; \
                                templ=readarml(addr); \
                                if (!databort) \
                                { \
                                        if (armregs[15]&3) armregs[15]=(templ+4); \
                                        else               armregs[15]=(armregs[15]&0x0C000003)|((templ+4)&0xF3FFFFFC); \
                                } \
                                tubecycles--; \
                                refillpipeline(); \
                        } \
                        else \
                        { \
                                for (c=0;c<15;c++) \
                                { \
                                        if (opcode&mask) \
                                        { \
                                                if (!(addr&0xC)) tubecycles--; \
                                                templ=readarml(addr); if (!databort) *usrregs[c]=templ; \
                                                addr+=4; \
                                                tubecycles--; \
                                        } \
                                        mask<<=1; \
                                } \
                        }

                                        case 0x80: /*STMDA*/
                                        case 0x82: /*STMDA !*/
                                        case 0x90: /*STMDB*/
                                        case 0x92: /*STMDB !*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        STMfirst();
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        STMall()
                                        tubecycles--;
                                        break;
                                        case 0x88: /*STMIA*/
                                        case 0x8A: /*STMIA !*/
                                        case 0x98: /*STMIB*/
                                        case 0x9A: /*STMIB !*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        STMfirst();
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        STMall();
                                        tubecycles--;
                                        break;
                                        case 0x84: /*STMDA ^*/
                                        case 0x86: /*STMDA ^!*/
                                        case 0x94: /*STMDB ^*/
                                        case 0x96: /*STMDB ^!*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        STMfirstS();
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        STMallS()
                                        tubecycles--;
                                        break;
                                        case 0x8C: /*STMIA ^*/
                                        case 0x8E: /*STMIA ^!*/
                                        case 0x9C: /*STMIB ^*/
                                        case 0x9E: /*STMIB ^!*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        STMfirstS();
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        STMallS();
                                        tubecycles--;
                                        break;

                                        case 0x81: /*LDMDA*/
                                        case 0x83: /*LDMDA !*/
                                        case 0x91: /*LDMDB*/
                                        case 0x93: /*LDMDB !*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
//                                        log_debug("LDMDB %08X\n",addr);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        LDMall();
                                        tubecycles-=2;
                                        break;
                                        case 0x89: /*LDMIA*/
                                        case 0x8B: /*LDMIA !*/
                                        case 0x99: /*LDMIB*/
                                        case 0x9B: /*LDMIB !*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        LDMall();
                                        tubecycles-=2;
                                        break;
                                        case 0x85: /*LDMDA ^*/
                                        case 0x87: /*LDMDA ^!*/
                                        case 0x95: /*LDMDB ^*/
                                        case 0x97: /*LDMDB ^!*/
                                        addr=armregs[RN]-countbits(opcode&0xFFFF);
                                        if (!(opcode&0x1000000)) addr+=4;
                                        if (opcode&0x200000) armregs[RN]-=countbits(opcode&0xFFFF);
                                        LDMallS();
                                        tubecycles-=2;
                                        break;
                                        case 0x8D: /*LDMIA ^*/
                                        case 0x8F: /*LDMIA ^!*/
                                        case 0x9D: /*LDMIB ^*/
                                        case 0x9F: /*LDMIB ^!*/
                                        addr=armregs[RN];
                                        if (opcode&0x1000000) addr+=4;
                                        if (opcode&0x200000) armregs[RN]+=countbits(opcode&0xFFFF);
                                        LDMallS();
                                        tubecycles-=2;
                                        break;

                                        case 0xB0: case 0xB1: case 0xB2: case 0xB3: /*BL*/
                                        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
                                        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
                                        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[14]=armregs[15]-4;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        tubecycles-=4;
                                        break;

                                        case 0xA0: case 0xA1: case 0xA2: case 0xA3: /*B*/
                                        case 0xA4: case 0xA5: case 0xA6: case 0xA7:
                                        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                                        case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                                        templ=(opcode&0xFFFFFF)<<2;
                                        armregs[15]=((armregs[15]+templ+4)&0x3FFFFFC)|(armregs[15]&0xFC000003);
                                        refillpipeline();
                                        tubecycles-=4;
                                        break;

                                        case 0xE0: case 0xE2: case 0xE4: case 0xE6: /*MCR*/
                                        case 0xE8: case 0xEA: case 0xEC: case 0xEE:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xE1: case 0xE3: case 0xE5: case 0xE7: /*MRC*/
                                        case 0xE9: case 0xEB: case 0xED: case 0xEF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xC0: case 0xC1: case 0xC2: case 0xC3: /*Co-pro*/
                                        case 0xC4: case 0xC5: case 0xC6: case 0xC7:
                                        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
                                        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                                        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
                                        case 0xD4: case 0xD5: case 0xD6: case 0xD7:
                                        case 0xD8: case 0xD9: case 0xDA: case 0xDB:
                                        case 0xDC: case 0xDD: case 0xDE: case 0xDF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000008;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        case 0xF0: case 0xF1: case 0xF2: case 0xF3: /*SWI*/
                                        case 0xF4: case 0xF5: case 0xF6: case 0xF7:
                                        case 0xF8: case 0xF9: case 0xFA: case 0xFB:
                                        case 0xFC: case 0xFD: case 0xFE: case 0xFF:
                                        templ=armregs[15]-4;
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x0800000C;
                                        tubecycles-=4;
                                        refillpipeline();
                                        break;

                                        default:
                                        break;
                                }
                        }
                        if (databort|armirq|tube_irq)
                        {
                                if (databort==1)     /*Data abort*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000014;
                                        refillpipeline();
                                        databort=0;
                                }
                                else if (databort==2) /*Address Exception*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(SUPERVISOR);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000003;
                                        armregs[15]|=0x08000018;
                                        refillpipeline();
                                        databort=0;
                                }
                                else if ((armirq&2) && !(armregs[15]&0x4000000)) /*FIQ*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(FIQ);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000001;
                                        armregs[15]|=0x0C000020;
                                        refillpipeline();
//                                        log_debug("FIQ\n");
                                }
                                else if ((armirq&1) && !(armregs[15]&0x8000000)) /*IRQ*/
                                {
                                        templ=armregs[15];
                                        armregs[15]|=3;
                                        updatemode(IRQ);
                                        armregs[14]=templ;
                                        armregs[15]&=0xFC000002;
                                        armregs[15]|=0x0800001C;
                                        refillpipeline();
//                                        log_debug("IRQ\n");
                                }
                        }
//                if (armregs[12]==0x1000000) log_debug("R12=1000000 %08X  %i\n",PC,armins);
                armirq=tube_irq;
                if ((armregs[15]&3)!=mode) updatemode(armregs[15]&3);
                armregs[15]+=4;
//                log_debug("%08X : %08X %08X %08X  %08X\n",PC,armregs[0],armregs[1],armregs[2],opcode);
/*                if (!PC)
                {
                        log_debug("Branch through zero\n");
                        dumpregs();
                        exit(-1);
                }*/
                if (endtimeslice)
                {
                        endtimeslice=0;
                        return;
                }
//                if (output && !(*armregs[15]&0x8000000) && PC<0x2000000) log_debug("%07X : %08X %08X %08X %08X %08X %08X %08X %08X\n%08i: %08X %08X %08X %08X %08X %08X %08X %08X\n",PC,*armregs[0],*armregs[1],*armregs[2],*armregs[3],*armregs[4],*armregs[5],*armregs[6],*armregs[7],inscount,*armregs[8],*armregs[9],*armregs[10],*armregs[11],*armregs[12],*armregs[13],*armregs[14],*armregs[15]);
        }
}
