             ██████████▄           █████████  ████▄    ▄████
             ██       ▀██          ██         ██ ▀██▄▄██▀ ██
             ██       ▄██          ██         ██   ▀██▀   ██
             ███████████    █████  █████      ██          ██
             ██       ▀██          ██         ██          ██
             ██       ▄██          ██         ██          ██
             ██████████▀           █████████  ██          ██

                                 Version 2.0a
                         A freeware BBC Micro emulator

Introduction
~~~~~~~~~~~~

B-em is an attempt to emulate a BBC Micro, made by Acorn Computers in the 80's. It is presently
available for Win32 and Linux.

B-em is licensed under the GPL, see COPYING for more details.


Features
~~~~~~~~

- Emulates Models A, B, B+, Master 128, Master 512, Master Turbo and Master Compact
- Also emulates ARM evaluation system on Master 128
- Also emulates 6502, 65816 and Z80 tubes.
- Cycle-accurate video emulation
- All documented and undocumented 6502 instructions
- 8271 Floppy Disc Controller emulated (double drive, double sided, 80 track, read/write)
- 1770 Floppy Disc Controller emulated (double drive, double sided, 80 track, read/write)
- Supports following formats - .ssd, .dsd, .adf, .adl, .img, .fdi, .uef and .csw
- Can run many protected disc and tape games.
- Sound emulation, including sample playback
- BeebSID emulation
- Lots of video tricks, such as overscan, raster splitting, rupture, interlace, mid-line
  palette and mode splits, etc.
- Sideways RAM emulation
- Joystick emulation


Differences from last version
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

v2.0a is a bugfix release, these were the changes for v2.0 :

- Most of the emulator re-written
- Cycle-accurate video emulation
- Higher quality mode 7
- Added 80186 (Master 512) and 65816 second processors
- BeebSID emulation (using resid-fp)
- FDI support is back
- Improved sound overall
- Much more stable
- Linux port (preliminary)
- Debugger
- Redefineable keyboard



Default keyboard mapping :
~~~~~~~~~~~~~~~~~~~~~~~~~~

BBC key     - PC key
--------------------
 BREAK          f12
  *:            @'
  +;            :;
  -=            -_
  ^~            +=
  f0            f1 (function keys are based on keycaps rather than positioning)
  3#             3
  6&             6
  7'             7
  8(             8
  9)             9
  0              0
Shift lock -    ALT



GUI :
~~~~~

The options are :

File :
        Hard reset - resets the emulator, clearing all memory.
        Exit       - exit to Windows.

Disc :
        Load disc 0/2          - load a disc image into drives 0 and 2.
        Load disc 1/3          - load a disc image into drives 1 and 3.
        Eject disc 0/2         - removes disc image from drives 0 and 2.
        Eject disc 1/3         - removes disc image from drives 1 and 3.
        New disc 0/2           - creates a new DFS/ADFS disc and loads it into drives 0 and 2.
        New disc 1/3           - creates a new DFS/ADFS disc and loads it into drives 1 and 3.
        Write protect disc 0/2 - toggles write protection on drives 0 and 2.
        Write protect disc 1/3 - toggles write protection on drives 1 and 3.
        Default write protect  - determines whether loaded discs are write protected by default

Tape :
        Load tape   - load a tape image.
        Eject tape  - removes a tape image.
        Rewind tape - rewind the emulated tape.
        Show tape catalogue - shows the catalogue of the current tape image.
        Tape speed  - select between normal and fast tape speed.

Settings :
        Model :
                BBC A w/OS 0.1        - emulate a model A with OS 0.1 - 1981 style.
                BBC B w/OS 0.1        - emulate a model B with OS 0.1.
                BBC A                 - emulate a model A.
                BBC B w/8271 FDC      - emulate a model B with 8271 FDC.
                BBC B w/8271+SWRAM    - emulate a model B with 8271 FDC and plenty of sideways RAM.
                BBC B w/1770 FDC      - emulate a model B with 1770 FDC and plenty of sideways RAM.
                BBC B US              - emulate an American model B with 8271 FDC.
                BBC B German          - emulate an German model B with 8271 FDC.
                BBC B+64K             - emulate a model B+ with 64k RAM.
                BBC B+128K            - emulate a model B+ with 128k RAM.
                BBC Master 128        - emulate a Master 128.
                BBC Master 512        - emulate a Master 512 (Master 128 with 80186 copro).
                BBC Master Turbo      - emulate a Master Turbo (Master 128 with 65C102 copro)
                BBC Master Compact    - emulate a Master Compact
                ARM Evaluation System - emulate a Master 128 with an ARM copro.
        Second processor :
                None            - disable copro emulation
                6502            - emulate Acorn 6502 copro
                65816           - emulate 16mhz 65816 ReCoPro (when ROM images are available)
                Z80             - emulate 4mhz Acorn Z80 copro
                6502 Tube Speed - select speed of Acorn 6502 copro, from 4mhz to 64mhz
        Video :
                Display Type :
                        Software line Doubling - stretch the BBC screen by doubling every line in software.
                                                 Allows high resolution mode 7.
                        Hardware line Doubling - stretch the BBC screen by doubling every line in hardware.
                        Scanlines     - stretch the BBC screen by blanking every other line
                        Interlaced    - emulate an interlaced display (useful for a handful of demos). Allows
                                        high resolution mode 7.
                Display borders - either display all video borders or crop them slightly or totally.
                Fullscreen - enters fullscreen mode. Use ALT-ENTER to return to windowed mode.
        Sound :
                Internal sound chip   - enable output of the normal BBC sound chip
                BeebSID               - enable output of the SID emulation
                Disc drive noise      - enable output of the disc drive sounds
                Tape noise            - enable output of the cassette emulation.
                Internal sound filter - enable bandpass filtering of sound. Reproduces the poor quality of
                                        the internal speaker.
                Internal waveform     - choose between several waveforms for the normal BBC sound chip. Square
                                        wave is the original.
                reSID configuration :
                        Model         - choose between many different models of SID. Many tunes sound quite
                                        different depending on the model chosen.
                        Sample method - choose between interpolation and resampling. Resampling is in theory
                                        higher quality, but I can't tell the difference.
                Disc drive type       - choose between sound from 5.25" drive or 3.5" drive.
                Disc drive volume     - set the relative volume of the disc drive noise.
        Keyboard :
                Redefine keys        - redefine keyboard setup.
                Map CAPS/CTRL to A/S - remaps those 2 keys. Useful for games where CAPS/CTRL are left/right.

Misc :
        Save screenshot      - saves screenshot in BMP, PCX, or TGA format.
        Debugger             - enters debugger. Type '?' to get list of commands.


Command line options :
~~~~~~~~~~~~~~~~~~~~~~

b-em [discimage.ssd/dsd/adf/adl/fdi] [-u name.uef] [-mx] [-tx] [-i] [-c] [-fx]

-u name.uef - load UEF image name.uef

-mx - model to emulate, where x is
0 - BBC A with OS 0.1
1 - BBC B with OS 0.1
2 - BBC A
3 - BBC B with 8271 FDC
4 - BBC B with 8271 FDC + loads of sideways RAM
5 - BBC B with 1770 FDC + loads of sideways RAM
6 - BBC B US
7 - BBC B German
8 - B+ 64K
9 - B+ 128K
10 - Master 128
11 - Master 512
12 - Master Turbo
13 - Master Compact
14 - ARM Evaluation System

-tx - enable tube, where x is
0 - 6502
1 - ARM (Master only)
2 - Z80
3 - 80186 (Master only)
4 - 65816 (if ROMs available)

-i - enable interlace mode (only useful on a couple of demos)
-c - enables scanlines
-fx - set frameskip to x (1-9, 1=no skip)
-fasttape - speeds up tape access


Master 512
~~~~~~~~~~

Master 512 includes mouse emulation. This is used by trapping the mouse in the emulator window - click in the
window to capture it, and press CTRL + END to release.

All disc images used by the Master 512 should have the .img extension - the type is determined by size. Both
640k and 800k discs are supported, as well as DOS standard 360k and 720k.


65816 coprocessor
~~~~~~~~~~~~~~~~~

The ROMs for this coprocessor are not included with the emulator. You will need to acquire the file
ReCo6502ROM_816 and place it in roms\tube.

The 65816 runs at 16mhz, regardless of what the firmware is set to.


Hardware emulated
~~~~~~~~~~~~~~~~~

The 6502 processor - All instructions should be emulated. Attempts to be cycle perfect. 65C02 is emulated for
                     Master 128 mode.

The 65C12 tube     - As a parasite processor.

The 65816 tube     - As a parasite processor. Emulator from Snem.

The Z80 tube       - As a parasite processor. Probably a few bugs. Emulator from ZX82.

The ARM processor  - As a parasite processor for Master 128 only. Emulator from Arculator.

The 80186 tube     - As a parasite processor for Master 512 only. Emulator from PCem.

The 6845 CRTC      - Cycle-exact emulation. Runs everything I've tried, with all effects working.

The Video ULA      - Cycle-exact emulation, though I think palette changes are a cycle off? Might be a delay in
                     the real chip.

The System VIA     - Keyboard and sound emulated. Also CMOS on Master-based models.

The User VIA       - Emulated.

8271 FDC           - Double disc, double sided, 40/80 tracks, read/write. With authentic noise. Supports read-only
                     access of protected FDI images.

1770 FDC           - Double disc, double sided, 40/80 tracks, read/write. With authentic noise. Supports read-only
                     access of protected FDI images.

Sound              - All channels emulated, with sample support and some undocumented behaviour (Crazee Rider).
                     With optional bandpass filter.

BeebSID            - Emulated using resid-fp, so should be pretty accurate. Is only emulated when accessed, to reduce
                     CPU load.

ADC                - Real joystick emulation, supporting both joysticks.

6850 ACIA          - Emulated for cassettes. Read only.

Serial ULA         - Emulated.


Hardware NOT emulated
~~~~~~~~~~~~~~~~~~~~~

serial port
AMX mouse (Master 512 mouse is kind-of emulated though)
Econet
Printer


Thanks to :
~~~~~~~~~~~

David Gilbert for writing Beebem and distributing the sources with it

James Fidell for writing Xbeeb and distributing the sources with it

Tom Seddon for updating Model B, indirectly motivating me to do v0.6, and for
identifying the Empire Strikes Back bug.

Ken Lowe for assistance with the Level 9 adventures.

Richard Gellman for help with a few things.

Thomas Harte for some UEF code - I wrote my own in the end - and for the OS X port.

Dave Moore for making and hosting the B-em site

Rich Talbot-Watkins and Peter Edwards (also Dave Moore) for testing

Robert Schmidt for The BBC Lives!

DJ Delorie for DJGPP

Shawn Hargreaves for Allegro

Acorn for making the BBC in the first place

David Allen,Bob Austin,Ram Banerjee,Paul Bond,Allen Boothroyd,Cambridge,
Cleartone,John Coll,John Cox,Andy Cripps,Chris Curry,6502 designers,
Jeremy Dion,Tim Dobson,Joe Dunn,Paul Farrell,Ferranti,Steve Furber,Jon Gibbons,
Andrew Gordon,Lawrence Hardwick,Dylan Harris,Hermann Hauser,Hitachi,
Andy Hopper,ICL,Martin Jackson,Brian Jones,Chris Jordan,David King,
David Kitson,Paul Kriwaczek,Computer Laboratory,Peter Miller,Arthur Norman,
Glyn Phillips,Mike Prees,John Radcliffe,Wilberforce Road,Peter Robinson,
Richard Russell,Kim Spence-Jones,Graham Tebby,Jon Thackray,Chris Turner,
Adrian Warner,Roger Wilson and Alan Wright for contributing to the development
of the BBC Computer (among others too numerous to mention)



Tom Walker

b-em@bbcmicro.com

<plug>
Also check out Elkulator (elkulator.acornelectron.co.uk), my Electron emulator,
Arculator (b-em.bbcmicro.com/arculator), my Archimedes emulator, and RPCemu
(www.riscos.info/RPCEmu), my RiscPC/A7000 emulator (that I'm not really involved
with anymore).
</plug>

<plug>
Also check out www.tommowalker.co.uk, for more emulators than is really necessary.
</plug>