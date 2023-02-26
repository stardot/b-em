/*  armmem.c -- ARMulator simple memory interace:  ARM6 Instruction Emulator.
    Copyright (C) 2010 Kieran Mockford.
*/

#include <stdint.h>

#include "armopts.h"
#include "armos.h"
#include "armdefs.h"
#include "ansidecl.h"

#include "../tube.h"

#ifdef VALIDATE			/* for running the validate suite */
#define TUBE 48 * 1024 * 1024	/* write a char on the screen */
#define ABORTS 1
#endif

/* #define ABORTS */

#ifdef ABORTS			/* the memory system will abort */
/* For the old test suite Abort between 32 Kbytes and 32 Mbytes
   For the new test suite Abort between 8 Mbytes and 26 Mbytes */
/* #define LOWABORT 32 * 1024
#define HIGHABORT 32 * 1024 * 1024 */
#define LOWABORT 8 * 1024 * 1024
#define HIGHABORT 26 * 1024 * 1024

#endif

#define OFFSETBITS 0xffff

int SWI_vector_installed = FALSE;

#if 0
/***************************************************************************\
*        Get a Word from Virtual Memory, maybe allocating the page          *
\***************************************************************************/

static ARMword
GetWord (ARMul_State * state, ARMword address, int check)
{
    ARMword *offset_address;

    if (address <= state->MemSize) // RAM
    {
        offset_address = (ARMword*)(state->MemDataPtr + address);
    }
    else if (address >= 0xC8000000 && address <= 0xC8080000) // ROM
    {
        address = address - 0xC8000000;
        offset_address = (ARMword*)(state->ROMDataPtr + address);
    }
    else
    {
        // Where are we??
        return 0;
    }

    return offset_address[0];
}

/***************************************************************************\
*        Put a Word into Virtual Memory, maybe allocating the page          *
\***************************************************************************/

static void
PutWord (ARMul_State * state, ARMword address, ARMword data, int check)
{
    if (address <= state->MemSize) // RAM
    {
      ARMword *offset_address = (ARMword*)(state->MemDataPtr + address);

      if (address == 0x8)
        SWI_vector_installed = TRUE;

      offset_address[0] = data;
    }
    else
    {
        //int a = 0; // where are we??
        return;
    }
}

/***************************************************************************\
*                      Initialise the memory interface                      *
\***************************************************************************/

unsigned
ARMul_MemoryInit (ARMul_State * state, unsigned long initmemsize)
{
  unsigned char *memory;

  if (initmemsize)
    state->MemSize = initmemsize;

  memory = (unsigned char *) malloc (initmemsize);

  if (memory == NULL)
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
  unsigned char * memory;

  memory = state->MemDataPtr;

  free(memory);

  return;
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
  ARMword temp, offset;

  state->NumNcycles++;

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3; /* bit offset into the word */

  return (temp >> offset) & 0xffff;
}

/***************************************************************************\
*                      Read Byte (but don't tell anyone!)                   *
\***************************************************************************/

ARMword ARMul_ReadByte (ARMul_State * state, ARMword address)
{
  if ((address & ~0x1f) == 0xF000000)
  {
//    return ReadTubeFromParasiteSide((address & 0x1c) >> 2);
    return tube_parasite_read((address & 0x1c) >> 2);
  }
    else
    {
      ARMword temp, offset;

      temp = ARMul_ReadWord (state, address);
      offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3; /* bit offset into the word */

      return (temp >> offset & 0xffL);
    }
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
  ARMword temp, offset;

  state->NumNcycles++;

#ifdef VALIDATE
  if (address == TUBE)
    {
      if (data == 4)
  state->Emulate = FALSE;
      else
  (void) putc ((char) data, stderr);  /* Write Char */
      return;
    }
#endif

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3; /* bit offset into the word */

  PutWord (state, address,
     (temp & ~(0xffffL << offset)) | ((data & 0xffffL) << offset),
     TRUE);
}

/***************************************************************************\
*                     Write Byte (but don't tell anyone!)                   *
\***************************************************************************/

void
ARMul_WriteByte (ARMul_State * state, ARMword address, ARMword data)
{

  if ((address & ~0x1f) == 0xF000000)
  {
//		WriteLog("Write byte %02x (%c) to tube %08x, reg %d\n", value,
//			((value & 127) > 31) && ((value & 127) != 127) ? value & 127 : '.',
//			address, (address & 0x1c) >> 2);
    tube_parasite_write((address & 0x1c) >> 2, data & 0xFF);
    //WriteTubeFromParasiteSide((address & 0x1c) >> 2, data & 0xFF);
  }
    else
    {
      ARMword temp, offset;

      temp = ARMul_ReadWord (state, address);
      offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3; /* bit offset into the word */

      PutWord (state, address,
         (temp & ~(0xffL << offset)) | ((data & 0xffL) << offset),
         TRUE);
    }
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
  (void) putc ((char) data, stderr);  /* Write Char */
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
  ARMword temp;

  state->NumNcycles++;

  temp = ARMul_ReadWord (state, address);

  state->NumNcycles++;

  PutWord (state, address, data, TRUE);

  return temp;
}

/***************************************************************************\
*                   Swap Byte, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapByte (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp;

  temp = ARMul_LoadByte (state, address);
  ARMul_StoreByte (state, address, data);

  return temp;
}

/***************************************************************************\
*                             Count I Cycles                                *
\***************************************************************************/

void
ARMul_Icycles (ARMul_State * state, unsigned number, ARMword address ATTRIBUTE_UNUSED)
{
  state->NumIcycles += number;
  ARMul_CLEARABORT;
}

/***************************************************************************\
*                             Count C Cycles                                *
\***************************************************************************/

void
ARMul_Ccycles (ARMul_State * state, unsigned number, ARMword address ATTRIBUTE_UNUSED)
{
  state->NumCcycles += number;
  ARMul_CLEARABORT;
}


/* Read a byte.  Do not check for alignment or access errors.  */

ARMword
ARMul_SafeReadByte (ARMul_State * state, ARMword address)
{
  ARMword temp, offset;

  temp = GetWord (state, address, FALSE);
  offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;

  return (temp >> offset & 0xffL);
}

void
ARMul_SafeWriteByte (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp, offset;

  temp = GetWord (state, address, FALSE);
  offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;

  PutWord (state, address,
     (temp & ~(0xffL << offset)) | ((data & 0xffL) << offset),
     FALSE);
}
#endif

