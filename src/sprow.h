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

#include "ARMulator/armdefs.h"
#include "cpu_debug.h"
#include "savestate.h"

bool sprow_init(void *rom);
void sprow_reset(void);
void sprow_exec(void);
void sprow_close(void);

void sprow_interrupt(int type);

extern cpu_debug_t tubesprow_cpu_debug;

