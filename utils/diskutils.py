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

import struct, time

# Find the number of centiseconds between 1900 and 1970.
between_epochs = ((365 * 70) + 17) * 24 * 360000L

class DiskError(Exception):
    pass


class Utilities:

    # Little endian reading
    
    def _read_signed_word(self, s):
    
        return struct.unpack("<i", s)[0]
    
    def _read_unsigned_word(self, s):
    
        return struct.unpack("<I", s)[0]
    
    def _read_signed_byte(self, s):
    
        return struct.unpack("<b", s)[0]
    
    def _read_unsigned_byte(self, s):
    
        return struct.unpack("<B", s)[0]
    
    def _read_unsigned_half_word(self, s):
    
        return struct.unpack("<H", s)[0]
    
    def _read_signed_half_word(self, s):
    
        return struct.unpack("<h", s)[0]
    
    def _read(self, offset, length = 1):
    
        self.file.seek(offset, 0)
        return self.file.read(length)
    
    def _write_unsigned_word(self, v):
    
        return struct.pack("<I", v)
    
    def _write_unsigned_half_word(self, v):
    
        return struct.pack("<H", v)
    
    def _write_unsigned_byte(self, v):
    
        return struct.pack("<B", v)
    
    def _write(self, offset, data):
    
        self.file.seek(offset, 0)
        self.file.write(data)
    
    def _str2num(self, s):
    
        i = 0
        n = 0
        while i < len(s):
        
            n = n | (ord(s[i]) << (i*8))
            i = i + 1
        
        return n
    
    def _num2str(self, size, n):
    
        i = 0
        s = ""
        while i < size:
        
            s += chr(n & 0xff)
            n = n >> 8
            i += 1
        
        return s
    
    def _binary(self, size, n):
    
        new = ""
        while (n != 0) & (size > 0):
        
            if (n & 1)==1:
                new = "1" + new
            else:
                new = "0" + new
            
            n = n >> 1
            size = size - 1
        
        if size > 0:
            new = ("0"*size) + new
        
        return new
    
    def _safe(self, s, with_space = 0):
    
        new = ""
        if with_space == 1:
            lower = 31
        else:
            lower = 32
        
        for c in s:
        
            if ord(c) >= 128:
                i = ord(c) ^ 128
                c = chr(i)
            
            if ord(c) <= lower:
                break
            
            new = new + c
        
        return new
    
    def _pad(self, s, length, ch):
    
        s = s[:length]
        if len(s) < length:
            s += (length - len(s)) * ch
        
        return s


class Directory:

    """directory = Directory(name, address)
    
    The directory created contains name and files attributes containing the
    directory name and the objects it contains.
    """
    
    def __init__(self, name, files):
    
        self.name = name
        self.files = files
    
    def __repr__(self):
    
        return '<%s instance, "%s", at %x>' % (self.__class__, self.name, id(self))


class File:

    """file = File(name, data, load_address, execution_address, length)
    """
    
    def __init__(self, name, data, load_address, execution_address, length,
                       locked = False, disk_address = 0):
    
        self.name = name
        self.data = data
        self.load_address = load_address
        self.execution_address = execution_address
        self.length = length
        self.locked = locked
        self.disk_address = disk_address
    
    def __repr__(self):
    
        return '<%s instance, "%s", at %x>' % (self.__class__, self.name, id(self))
    
    def has_filetype(self):
    
        """Returns True if the file's meta-data contains filetype information."""
        return self.load_address & 0xfff00000 == 0xfff00000
    
    def filetype(self):
    
        """Returns the meta-data containing the filetype information.
        
        Note that a filetype can be obtained for all files, though it may not
        necessarily be valid. Use has_filetype() to determine whether the file
        is likely to have a valid filetype."""
        
        return "%03x" % ((self.load_address >> 8) & 0xfff)
    
    def time_stamp(self):
    
        """Returns the time stamp for the file as a tuple of values containing
        the local time, or an empty tuple if the file does not have a time stamp."""
        
        # RISC OS time is given as a five byte block containing the
        # number of centiseconds since 1900 (presumably 1st January 1900).
        
        # Convert the time to the time elapsed since the Epoch (assuming
        # 1970 for this value).
        date_num = struct.unpack("<Q",
            struct.pack("<IBxxx", self.execution_address, self.load_address & 0xff))[0]
        
        centiseconds = date_num - between_epochs
        
        # Convert this to a value in seconds and return a time tuple.
        try:
            return time.localtime(centiseconds / 100.0)
        except ValueError:
            return ()

