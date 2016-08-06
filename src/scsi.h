// Ported to b-em 04/08/2016
/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2006  Jon Welch

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
/* SASI Support for Beebem */
/* Written by Jon Welch */

#ifndef SCSI_HEADER
#define SCSI_HEADER

extern char scsi_enabled;

void scsi_init(void);
void scsi_close(void);
void scsi_reset(void);

uint8_t scsi_read(uint16_t addr);
void scsi_write(uint16_t addr, uint8_t value);

#endif
