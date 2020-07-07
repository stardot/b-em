"""
Copyright (C) 2014 David Boddie <david@boddie.org.uk>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

__author__ = "David Boddie <david@boddie.org.uk>"
__date__ = "2014-04-22"
__version__ = "0.1"
__license__ = "GNU General Public License (version 3 or later)"

import StringIO
from diskutils import Directory, DiskError, File, Utilities

class Catalogue(Utilities):

    def __init__(self, file):
    
        self.file = file
        self.sector_size = 256
        self.track_size = 10 * self.sector_size
        self.interleaved = False
        
        # The free space map initially contains all the space after the
        # catalogue.
        self.free_space = [(2, 798)]
        self.sectors = 800
        self.tracks = self.sectors / 10
        self.disk_cycle = 0
        self.boot_option = 0
    
    def read_free_space(self):
    
        # Using notes from http://mdfs.net/Docs/Comp/Disk/Format/DFS
        
        self.free_space = []
    
    def read(self):
    
        disk_title, files = self.read_catalogue(0)
        
        if self.interleaved:
            other_title, more_files = self.read_catalogue(self.track_size)
            files += more_files
        
        return disk_title, files
    
    def read_catalogue(self, offset):
    
        disk_title = self._read(offset, 8) + self._read(offset + 0x100, 4)
        self.disk_cycle = self._read_unsigned_byte(self._read(offset + 0x104, 1))
        last_entry = self._read_unsigned_byte(self._read(offset + 0x105, 1))
        extra = self._read_unsigned_byte(self._read(offset + 0x106, 1))
        sectors = self._read_unsigned_byte(self._read(offset + 0x107, 1))
        self.sectors = sectors | ((extra & 0x03) << 8)
        self.boot_option = (extra & 0x30) >> 4
        
        files = []
        p = 8
        
        while p <= last_entry:
        
            name = self._read(offset + p, 7)
            if name[0] == "\x00":
                break
            
            name = name.strip()
            extra = self._read_unsigned_byte(self._read(offset + p + 7))
            prefix = chr(extra & 0x7f)
            locked = (extra & 0x80) != 0
            
            load = self._read_unsigned_half_word(self._read(offset + 0x100 + p, 2))
            exec_ = self._read_unsigned_half_word(self._read(offset + 0x100 + p + 2, 2))
            length = self._read_unsigned_half_word(self._read(offset + 0x100 + p + 4, 2))
            
            extra = self._read_unsigned_byte(self._read(offset + 0x100 + p + 6))
            load = load | ((extra & 0x0c) << 14)
            length = length | ((extra & 0x30) << 12)
            exec_ = exec_ | ((extra & 0xc0) << 10)
            
            if load & 0x30000 == 0x30000:
                load = load | 0xfc0000
            if exec_ & 0x30000 == 0x30000:
                exec_ = exec_ | 0xfc0000
            
            file_start_sector = self._read_unsigned_byte(self._read(offset + 0x100 + p + 7))
            file_start_sector = file_start_sector | ((extra & 0x03) << 8)
            
            if not self.interleaved:
                data = self._read(file_start_sector * self.sector_size, length)
                disk_address = file_start_sector * self.sector_size
            else:
                data = ""
                sector = file_start_sector
                disk_address = self._disk_address(sector)
                
                while len(data) < length:
                
                    addr = offset + self._disk_address(sector)
                    data += self._read(addr, min(self.sector_size, length - len(data)))
                    sector += 1
            
            files.append(File(prefix + "." + name, data, load, exec_, length, locked,
                              disk_address))
            
            p += 8
        
        return disk_title, files
    
    def write(self, disk_title, files):
    
        if len(files) > 31:
            raise DiskError, "Too many entries to write."
        
        disk_name = self._pad(self._safe(disk_title), 12, " ")
        self._write(0, disk_title[:8])
        self._write(0x100, disk_title[8:12])
        
        # Write the number of files and the disk cycle.
        self.disk_cycle += 1
        self._write(0x104, self._write_unsigned_byte(self.disk_cycle))
        self._write(0x105, self._write_unsigned_byte(len(files) * 8))
        
        extra = (self.sectors >> 8) & 0x03
        extra = extra | (self.boot_option << 4)
        self._write(0x106, self._write_unsigned_byte(extra))
        self._write(0x107, self._write_unsigned_byte(self.sectors & 0xff))
        
        p = 8
        for file in files:
        
            prefix, name = file.name.split(".")
            name = self._pad(name, 7, " ")
            self._write(p, name)
            
            extra = ord(prefix)
            if file.locked:
                extra = extra | 128
            
            self._write(p + 7, self._write_unsigned_byte(extra))
            
            load = file.load_address
            exec_ = file.execution_address
            length = file.length
            
            self._write(0x100 + p, self._write_unsigned_half_word(load & 0xffff))
            self._write(0x100 + p + 2, self._write_unsigned_half_word(exec_ & 0xffff))
            self._write(0x100 + p + 4, self._write_unsigned_half_word(length & 0xffff))
            
            disk_address = self._find_space(file)
            file_start_sector = disk_address / self.sector_size
            self._write(disk_address, file.data)
            
            extra = ((file_start_sector >> 8) & 0x03)
            extra = extra | ((load >> 14) & 0x0c)
            extra = extra | ((exec_ >> 12) & 0x30)
            extra = extra | ((length >> 10) & 0xc0)
            
            self._write(0x100 + p + 6, self._write_unsigned_byte(extra))
            self._write(0x100 + p + 7, self._write_unsigned_byte(file_start_sector & 0xff))
            
            p += 8
    
    def _find_space(self, file):
    
        for i in range(len(self.free_space)):
        
            sector, length = self.free_space[i]
            file_length = file.length/self.sector_size
            
            if file.length % self.sector_size != 0:
                file_length += 1
            
            if length >= file_length:
            
                if length > file_length:
                    # Update the free space entry to contain the remaining space.
                    self.free_space[i] = (sector + file_length, length - file_length)
                else:
                    # Remove the free space entry.
                    del self.free_space[i]
                
                return sector * self.sector_size
        
        raise DiskError, "Failed to find space for file: %s" % file.name
    
    def _disk_address(self, sector):
    
        track = sector/10
        addr = 0
        
        # Handle some .dsd files with interleaved tracks.
        if track >= self.tracks:
            track -= self.tracks
            addr += self.track_size
        
        addr += (track * self.track_size * 2) + ((sector % 10) * self.sector_size)
        
        return addr


class Disk:

    DiskSizes = {None: 200 * 1024}
    SectorSizes = {None: 256}
    Catalogues = {None: Catalogue}
    
    def __init__(self, format = None):
    
        self.format = format
    
    def new(self):
    
        self.size = self.DiskSizes[self.format]
        self.data = "\x00" * self.size
        self.file = StringIO.StringIO(self.data)
    
    def open(self, file_object):
    
        self.size = self.DiskSizes[self.format]
        self.file = file_object
    
    def catalogue(self):
    
        sector_size = self.SectorSizes[self.format]
        return self.Catalogues[self.format](self.file)
