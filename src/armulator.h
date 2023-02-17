/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2001  David Sharp
ARM7TDMI Co-Processor Emulator
Copyright (C) 2010 Kieran Mockford

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public 
License along with this program; if not, write to the Free 
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/

#ifndef _ARMULATOR_H
#define _ARMULATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ml675001.h"
#include "ARMulator/armopts.h"
#include "ARMulator/armos.h"
#include "ARMulator/ansidecl.h"
#include "ARMulator/armdis.h"

int stop_simulator = 1;

#ifdef __cplusplus
}
#endif

#define IFLAG (state->IFFlags >> 1)
#define FFLAG (state->IFFlags & 1)

#endif