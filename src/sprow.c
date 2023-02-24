/*B-em v2.2 by Tom Walker
  SPROW ARMv7TDMI parasite CPU emulation
  Originally from beebem */

#include <stdio.h>
#include <stdint.h>
#include "b-em.h"
#include "armulator.h"
#include "sprow.h"
#include "tube.h"
#include "cpu_debug.h"
//#include "ssinline.h"
#include "map.h"

unsigned char  m_ROMMemory[0x80000];
ARMul_State   *m_State;
int            m_CycleCount;

#define OFFSETBITS 0xffff
#define INSN_SIZE 4

int swi_vector_installed = FALSE;
int tenval = 1;

static int sprow_debug_enabled = 0;

#define PRIMEPIPE     4
#define RESUME        8
#define FLUSHPIPE state->NextInstr |= PRIMEPIPE

#if defined MODET && defined MODE32
#define PCBITS (0xffffffffL)
#else
#define PCBITS (0xfffffffcL)
#endif

#ifdef MODE32
#define PCMASK PCBITS
#define PCWRAP(pc) (pc)
#else
#define PCMASK R15PCBITS
#define PCWRAP(pc) ((pc) & R15PCBITS)
#endif

// NULL pointer terminated list of register names.
static const char *sprow_dbg_reg_names[] = {
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


static const char *sprow_trap_names[] = { NULL };

unsigned ARMul_OSHandleSWI (ARMul_State * state, ARMword number)
{
    if (swi_vector_installed)
    {
        ARMword cpsr = ARMul_GetCPSR (state);
        ARMword i_size = INSN_SIZE;

        ARMul_SetSPSR (state, SVC32MODE, cpsr);

        cpsr &= ~0xbf;
        cpsr |= SVC32MODE | 0x80;
        ARMul_SetCPSR (state, cpsr);

        state->RegBank[SVCBANK][14] = state->Reg[14] = state->Reg[15] - i_size;
        state->NextInstr            = RESUME;
        state->Reg[15]              = state->pc = ARMSWIV;
        FLUSHPIPE;
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}
static void sprow_savestate(ZFILE *zfp)
{
}

static void sprow_loadstate(ZFILE *zfp)
{
}

static inline uint8_t do_sprow_readb(uint32_t addr)
{
  return ARMul_ReadByte (m_State, addr);
}

static inline uint32_t do_sprow_readl(uint32_t addr)
{
  return ARMul_ReadWord (m_State, addr);
}

static uint8_t sprow_readb(uint32_t addr)
{
    uint8_t v = do_sprow_readb(addr);
    if (sprow_debug_enabled)
        debug_memread(&tubesprow_cpu_debug, addr, v, 1);
    return v;
}

static inline void do_sprow_writeb(uint32_t addr, uint8_t val)
{
  ARMul_WriteByte (m_State, addr, val);
}

static void sprow_writeb(uint32_t addr, uint8_t val)
{
    if (sprow_debug_enabled)
        debug_memwrite(&tubesprow_cpu_debug, addr, val, 1);
    do_sprow_writeb(addr, val);
}


void sprow_close()
{
  if (m_State != 0L)
  {
      ARMul_MemoryExit(m_State);
      m_State = 0L;
  }
}

void sprow_reset()
{
  m_State->ROMDataPtr = m_ROMMemory;
  m_State->pc = 0x0000;
  m_State->Reg[15] = 0x0000;

  ARMul_WriteWord(m_State, RMPCON, 0);
  ARMul_WriteWord(m_State, ROMSEL, 1);
  m_CycleCount = 0;

}

bool sprow_init(void *rom)
{
  memcpy(m_ROMMemory, rom, 0x80000);

  ARMul_EmulateInit();
  m_State = ARMul_NewState();
  m_State->ROMDataPtr = m_ROMMemory;

  ARMul_MemoryInit(m_State, 0x4000000);
  m_CycleCount = 0;

  tube_type = TUBESPROW;
  tube_readmem = sprow_readb;
  tube_writemem = sprow_writeb;
  tube_exec  = sprow_exec;
  tube_proc_savestate = sprow_savestate;
  tube_proc_loadstate = sprow_loadstate;
  sprow_reset();

  MAP_newmap();

  return true;
}

static ARMword PeekRegister(ARMul_State *state, ARMword registerNumber)
{
  int value = 0;
  if (MAP_getsecond(registerNumber, &value) == MAP_NO_ERROR)
    return value;
  else
    return 0;
}

static ARMword GetRegister(ARMul_State * state, ARMword registerNumber)
{
    int data = PeekRegister(state, registerNumber);

    switch(registerNumber)
    {
    case IRN:
        {
          MAP_putpair(registerNumber, 0);
          MAP_putpair(CIL, 0);
          break;
        }
    }

    return data;
}

static int sprow_dbg_debug_enable(int newvalue) {
    int oldvalue = sprow_debug_enabled;
    sprow_debug_enabled = newvalue;
    return oldvalue;
};

static uint32_t sprow_dbg_memread(uint32_t addr)
{
  return do_sprow_readb(addr);
}

static void sprow_dbg_memwrite(uint32_t addr, uint32_t val)
{
  do_sprow_writeb(addr, val);
}

static uint32_t sprow_dbg_disassemble(cpu_debug_t *cpu, uint32_t addr, char *buf, size_t bufsize) {

  char dest[256];
  int flags = 0;
  disArm(m_State, addr, dest, flags);

  uint32_t instr = do_sprow_readl(addr);
  int len = snprintf(buf, bufsize, "%08"PRIx32" %08"PRIx32" ", addr, instr);
  buf += len;
  bufsize -= len;
  strncpy(buf, dest, bufsize);

  if ((len = strlen(buf)) < 40)
  {
    memset(buf+len, ' ', 40-len);
    buf[40] = 0;
  }
  return addr + 4;
};

static uint32_t sprow_dbg_reg_get(int which) 
{
  if (which == i_PC) 
  {
    return ARMul_GetPC (m_State);
  }
  else if (which == i_PSR) 
  {
    return ARMul_GetSPSR (m_State, m_State->Mode);
  }
  else 
  {
    return ARMul_GetReg(m_State, m_State->Mode, which);
  }

  {
    log_warn("sprow: unrecognised register %d", which);
    return 0;
  }
}

// Set a register.
static void  sprow_dbg_reg_set(int which, uint32_t value) {
  if (which <= i_LR) 
  {
    return ARMul_SetReg(m_State, m_State->Mode, which, value);
  } 
  else if (which == i_PC) 
  {
    return ARMul_SetPC (m_State, value);
  }
  else if (which == i_PSR) 
  {
    return ARMul_SetSPSR (m_State, m_State->Mode, value);
  }
  else 
  {
    log_warn("sprow: unrecognised register %d", which);
  }
}

static const char* flagname = "N Z C V I F M1 M0 ";

// Print register value in CPU standard form.
static size_t sprow_dbg_reg_print(int which, char *buf, size_t bufsize) {

  char *buff_start = buf;

    if (which == i_PSR) {

        int i;
        int bit;
        char c;
        const char *flagnameptr = flagname;
        int psr = sprow_dbg_reg_get(i_PSR);

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
        return strlen(buff_start);
    } else {
        return snprintf(buf, bufsize, "%08"PRIx32, sprow_dbg_reg_get(which));
    }

  return 0;
};

// Parse a value into a register.
static void sprow_dbg_reg_parse(int which, const char *strval) 
{
 uint32_t val = 0;
 sscanf(strval, "%"SCNx32, &val);
 sprow_dbg_reg_set(which, val);
};

static uint32_t sprow_dbg_get_instr_addr(void) 
{
  return(m_State->Reg[15] & PCMASK);
}

cpu_debug_t tubesprow_cpu_debug = {
   .cpu_name       = "SPROW",
   .debug_enable   = sprow_dbg_debug_enable,
   .memread        = sprow_dbg_memread,
   .memwrite       = sprow_dbg_memwrite,
   .disassemble    = sprow_dbg_disassemble,
   .reg_names      = sprow_dbg_reg_names,
   .reg_get        = sprow_dbg_reg_get,
   .reg_set        = sprow_dbg_reg_set,
   .reg_print      = sprow_dbg_reg_print,
   .reg_parse      = sprow_dbg_reg_parse,
   .get_instr_addr = sprow_dbg_get_instr_addr,
   .trap_names     = sprow_trap_names,
   .print_addr     = debug_print_addr32,
   .parse_addr     = debug_parse_addr
};

void sprow_exec()
{
  while (tubecycles > 0)
  {
    if (sprow_debug_enabled)
      debug_preexec(&tubesprow_cpu_debug, m_State->pc);

    ARMul_DoInstr(m_State);

    m_State->Exception = FALSE;
    m_State->NirqSig = HIGH;
    m_State->NfiqSig = HIGH;

      // TODO: This is not accurate, it assumes instructions take 3 cycles each,
      // on average. More info at:
      // https://developer.arm.com/documentation/ddi0210/c/Instruction-Cycle-Timings

      tubecycles -= 3;
  }
}

/***************************************************************************\
*        Get a Word from Virtual Memory, maybe allocating the page          *
\***************************************************************************/

static ARMword
GetWord (ARMul_State * state, ARMword address, int check)
{
    unsigned char *offset_address;
    unsigned char *base_address;

    // All fetches are word-aligned, caller rearranges bytes as needed
    address &= ~(ARMword)3;

    // Hardware Registers..
    if (address >= 0x78000000 && address < 0xc0000000)
    {
        return GetRegister(state, address);
    }

    if (address >= 0xF0000000 && address <= 0xF0000010)
    {
        return 0xFF;
    }

    // If the ROM has been selected to appear at 0x00000000 then
    // we need to ensure that accesses go to the ROM
    if (state->romSelectRegister & 1 && ((state->remapControlRegister & 8) == 0))
    {
        base_address = state->ROMDataPtr;
    }
    else
    {
        base_address = state->MemDataPtr;
    }

    if (address <= state->MemSize) // RAM
    {
        offset_address = (base_address + address);
    }
    else if (address >= 0xC8000000 && address <= 0xC8080000) // Always ROM
    {
        address = address - 0xC8000000;
        offset_address = (state->ROMDataPtr + address);
    }
    else if (address >= 0xC0000000 && address < 0xD0000000)
    {
        unsigned int wrapaddress = address - 0xC0000000;
        offset_address = state->MemDataPtr + (wrapaddress % (unsigned int)state->MemSize);
    }
    else
    {
        // Where are we??
        return 0;
    }

    ARMword *actual_address = (ARMword*)offset_address;
    return actual_address[0];
}

static void PutRegister(ARMul_State * state, ARMword registerNumber, ARMword data)
{
  MAP_putpair(registerNumber, data);

    switch (registerNumber)
    {
    case FIQEN:
        {
            if (data == 0)
            {
                state->NfiqSig = HIGH;
            }
            else
            {
                state->NfiqSig = LOW;
            }
            break;
        }
    case ILC:
        {
            if ((data & 0xF0000000) > 0)
            {
                state->NirqSig = HIGH; // enabled
            }
            else if ((data & 0xF0000000) == 0)
            {
                state->NirqSig = LOW; // disabled
            }
            break;
    case CILCL:
        {
            if (data == 0)
            {
                PutRegister(state, CIL, 0);
            }
            break;
        }
        }
    }
}

/***************************************************************************\
*        Put a Word into Virtual Memory, maybe allocating the page          *
\***************************************************************************/

static void
PutWord (ARMul_State * state, ARMword address, ARMword data, int check)
{
    /*
    0xB7000004 Block clock control register BCKCTL R/W 16 0x0000
    0xB8000004 Clock stop register CLKSTP R/W 32 0x00000000
    0xB8000008 Clock select register CGBCNT0 R/W 32 0x00000000
    0xB800000C Clock wait register CKWT R/W 32 0x000000FF
    */

    // All stores are word-aligned and unrotated
    address &= ~(ARMword)3;

    if (address >= 0xF0000000 && address <= 0xF0000010)
    {
        return;
    }

    if (address == RMPCON) // Remap control register
    {
        state->remapControlRegister = data;
    }
    else if (address == ROMSEL) //ROM select register
    {
        state->romSelectRegister = data;
    }
    else if (address >= 0x78000000 && address < 0xc0000000)
    {
        PutRegister(state, address, data);
    }
    else
    {
        unsigned char *base_address;
        unsigned char *offset_address;
        ARMword *actual_address;

             // If the ROM has been selected to appear at 0x00000000 then
        // we need to ensure that accesses go to the ROM
        if (state->romSelectRegister & 1 && ((state->remapControlRegister & 8) == 0))
        {
            base_address = state->ROMDataPtr;
        }
        else
        {
            base_address = state->MemDataPtr;
        }

        if (address <= state->MemSize) // RAM
        {
            if (state->romSelectRegister & 1 && ((state->remapControlRegister & 8) == 0)) // we're trying to write to ROM - NOPE!
            {
                return;
            }
            else
            {
                offset_address = (base_address + address);

                if (address == 0x8)
                {
                    SWI_vector_installed = TRUE;
                }

                /* if (address <= 0x1c)
                {
                    int a= 0;
                    // WHAT!?
                } */
            }
        }
        else if (address >= 0xC0000000 && address < 0xC8000000)
        {
            unsigned int wrapaddress = address - 0xC0000000;
            offset_address = state->MemDataPtr + (wrapaddress % (unsigned int)state->MemSize);
        }
        else
        {
            // int a = 0; // where are we??
            return;
        }

        actual_address = (ARMword*)offset_address;
        actual_address[0] = data;
    }
}

/***************************************************************************\
*                      Initialise the memory interface                      *
\***************************************************************************/

unsigned
ARMul_MemoryInit (ARMul_State * state, unsigned long initmemsize)
{
    if (initmemsize)
        state->MemSize = initmemsize;

    unsigned char *memory = (unsigned char *)malloc(initmemsize);

    if (memory == 0)
        return FALSE;

    state->MemDataPtr = memory;

    return TRUE;
}

/***************************************************************************\
*                         Remove the memory interface                       *
\***************************************************************************/

void
ARMul_MemoryExit (ARMul_State * state)
{
    unsigned char* memory = state->MemDataPtr;

    free(memory);
}

/***************************************************************************\
*                   ReLoad Instruction                                     *
\***************************************************************************/

ARMword
ARMul_ReLoadInstr (ARMul_State * state, ARMword address, ARMword isize)
{
#ifdef ABORTS
    if (address >= LOWABORT && address < HIGHABORT)
    {
        ARMul_PREFETCHABORT (address);
        return ARMul_ABORTWORD;
    }
    else
    {
        ARMul_CLEARABORT;
    }
#endif

    if ((isize == 2) && (address & 0x2))
    {
        /* We return the next two halfwords: */
        ARMword lo = GetWord (state, address, FALSE);
        ARMword hi = GetWord (state, address + 4, FALSE);

        if (state->bigendSig == HIGH)
            return (lo << 16) | (hi >> 16);
        else
            return ((hi & 0xFFFF) << 16) | (lo >> 16);
    }

    return GetWord (state, address, TRUE);
}

/***************************************************************************\
*                   Load Instruction, Sequential Cycle                      *
\***************************************************************************/

ARMword ARMul_LoadInstrS (ARMul_State * state, ARMword address, ARMword isize)
{
    state->NumScycles++;

#ifdef HOURGLASS
    if ((state->NumScycles & HOURGLASS_RATE) == 0)
    {
        HOURGLASS;
    }
#endif

    return ARMul_ReLoadInstr (state, address, isize);
}

/***************************************************************************\
*                 Load Instruction, Non Sequential Cycle                    *
\***************************************************************************/

ARMword ARMul_LoadInstrN (ARMul_State * state, ARMword address, ARMword isize)
{
    state->NumNcycles++;

    return ARMul_ReLoadInstr (state, address, isize);
}

/***************************************************************************\
*                      Read Word (but don't tell anyone!)                   *
\***************************************************************************/

ARMword ARMul_ReadWord (ARMul_State * state, ARMword address)
{
#ifdef ABORTS
    if (address >= LOWABORT && address < HIGHABORT)
    {
        ARMul_DATAABORT (address);
        return ARMul_ABORTWORD;
    }
    else
    {
        ARMul_CLEARABORT;
    }
#endif

    return GetWord (state, address, TRUE);
}

/***************************************************************************\
*                        Load Word, Sequential Cycle                        *
\***************************************************************************/

ARMword ARMul_LoadWordS (ARMul_State * state, ARMword address)
{
    state->NumScycles++;

    return ARMul_ReadWord (state, address);
}

/***************************************************************************\
*                      Load Word, Non Sequential Cycle                      *
\***************************************************************************/

ARMword ARMul_LoadWordN (ARMul_State * state, ARMword address)
{
    state->NumNcycles++;

    return ARMul_ReadWord (state, address);
}

/***************************************************************************\
*                     Load Halfword, (Non Sequential Cycle)                 *
\***************************************************************************/

ARMword ARMul_LoadHalfWord (ARMul_State * state, ARMword address)
{
    state->NumNcycles++;

    ARMword temp = ARMul_ReadWord (state, address);
    ARMword offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3;	/* bit offset into the word */

    return (temp >> offset) & 0xffff;
    //ARMword temp = ARMul_ReadWord (state, address);
    //return temp & 0xFFFF;
}

/***************************************************************************\
*                      Read Byte (but don't tell anyone!)                   *
\***************************************************************************/

ARMword ARMul_ReadByte (ARMul_State * state, ARMword address)
{
    if (address >= 0xF0000000 && address <= 0xF0000010)
    {
        if (address == 0xF0000010)
        {
            return tenval;
        }
        else
        {
            unsigned char addr = (address & 0x0f) / 2;
            unsigned char data = tube_parasite_read(addr);
            return data;
        }
    }

    ARMword temp = ARMul_ReadWord (state, address);
    ARMword offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;	/* bit offset into the word */

    return (temp >> offset & 0xffL);
    //return temp & 0xffL;
}

/***************************************************************************\
*                     Load Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

ARMword ARMul_LoadByte (ARMul_State * state, ARMword address)
{
    state->NumNcycles++;

    return ARMul_ReadByte (state, address);
}

/***************************************************************************\
*                     Write Word (but don't tell anyone!)                   *
\***************************************************************************/

void
ARMul_WriteWord (ARMul_State * state, ARMword address, ARMword data)
{
#ifdef ABORTS
    if (address >= LOWABORT && address < HIGHABORT)
    {
        ARMul_DATAABORT (address);
        return;
    }
    else
    {
        ARMul_CLEARABORT;
    }
#endif

    PutWord (state, address, data, TRUE);
}

/***************************************************************************\
*                       Store Word, Sequential Cycle                        *
\***************************************************************************/

void
ARMul_StoreWordS (ARMul_State * state, ARMword address, ARMword data)
{
    state->NumScycles++;

    ARMul_WriteWord (state, address, data);
}

/***************************************************************************\
*                       Store Word, Non Sequential Cycle                        *
\***************************************************************************/

void
ARMul_StoreWordN (ARMul_State * state, ARMword address, ARMword data)
{
    state->NumNcycles++;

    ARMul_WriteWord (state, address, data);
}

/***************************************************************************\
*                    Store HalfWord, (Non Sequential Cycle)                 *
\***************************************************************************/

void
ARMul_StoreHalfWord (ARMul_State * state, ARMword address, ARMword data)
{
    ARMword temp;

    state->NumNcycles++;

#ifdef VALIDATE
    if (address == TUBE)
    {
        if (data == 4)
            state->Emulate = FALSE;
        else
            (void) putc ((char) data, stderr);	/* Write Char */
        return;
    }
#endif

    temp = ARMul_ReadWord (state, address);
    ARMword offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3;	/* bit offset into the word */
    PutWord (state, address,
        (temp & ~(0xffffL << offset)) | ((data & 0xffffL) << offset),
        TRUE);
    //temp = ARMul_ReadWord (state, address);
    //temp = temp & 0xFFFF0000;
    //temp = temp | (data & 0xFFFF);
    //PutWord (state, address, temp, TRUE);
}

/***************************************************************************\
*                     Write Byte (but don't tell anyone!)                   *
\***************************************************************************/

void
ARMul_WriteByte (ARMul_State * state, ARMword address, ARMword data)
{
    if (address >= 0xF0000000 && address <= 0xF0000010)
    {
        if (address == 0xF0000010)
        {
            tenval = data;
            return;
        }
        else
        {
            unsigned char addr = (address & 0x0f) / 2;
            tube_parasite_write(addr, (unsigned char)data);
            return;
        }
    }

    ARMword temp = ARMul_ReadWord (state, address);
    ARMword offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;	/* bit offset into the word */

    PutWord (state, address,
        (temp & ~(0xffL << offset)) | ((data & 0xffL) << offset),
        TRUE);
    //temp = temp & 0xFFFFFF00;
    //temp = temp | (data & 0xFF);
    //PutWord (state, address, temp, TRUE);
}

/***************************************************************************\
*                    Store Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

void
ARMul_StoreByte (ARMul_State * state, ARMword address, ARMword data)
{
    state->NumNcycles++;

#ifdef VALIDATE
    if (address == TUBE)
    {
        if (data == 4)
            state->Emulate = FALSE;
        else
            (void) putc ((char) data, stderr);	/* Write Char */
        return;
    }
#endif

    ARMul_WriteByte (state, address, data);
}

/***************************************************************************\
*                   Swap Word, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapWord (ARMul_State * state, ARMword address, ARMword data)
{
    state->NumNcycles++;

    ARMword temp = ARMul_ReadWord (state, address);

    state->NumNcycles++;

    PutWord (state, address, data, TRUE);

    return temp;
}

/***************************************************************************\
*                   Swap Byte, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapByte (ARMul_State * state, ARMword address, ARMword data)
{
    ARMword temp = ARMul_LoadByte(state, address);
    ARMul_StoreByte (state, address, data);

    return temp;
}

/***************************************************************************\
*                             Count I Cycles                                *
\***************************************************************************/

void
ARMul_Icycles (ARMul_State * state, unsigned number, ARMword address)
{
    state->NumIcycles += number;
    ARMul_CLEARABORT;
}

/***************************************************************************\
*                             Count C Cycles                                *
\***************************************************************************/

void
ARMul_Ccycles (ARMul_State * state, unsigned number, ARMword address)
{
    state->NumCcycles += number;
    ARMul_CLEARABORT;
}


/* Read a byte.  Do not check for alignment or access errors.  */

ARMword
ARMul_SafeReadByte (ARMul_State * state, ARMword address)
{
    ARMword temp = GetWord (state, address, FALSE);
    ARMword offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;

    return (temp >> offset & 0xffL);
}

void
ARMul_SafeWriteByte (ARMul_State * state, ARMword address, ARMword data)
{
    ARMword temp = GetWord (state, address, FALSE);
    ARMword offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;

    PutWord (state, address,
        (temp & ~(0xffL << offset)) | ((data & 0xffL) << offset),
        FALSE);
}

void sprow_interrupt(int type)
{
  if (type == 2)
  {
      m_State->NfiqSig = LOW;
      m_State->Exception = TRUE;
  }
  else 
  {
      PutRegister(m_State, IRN, INT_EX3);
      m_State->NirqSig = LOW;
      m_State->Exception = TRUE;
  }
}


