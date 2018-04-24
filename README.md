# B-Em

Introduction
============

B-em is an emulator for various models of BBC Microcomputer as made
by Acorn Computers in the 1980s along with a selection of 2nd
processors.  It is supported for Win32 and Linux/UNIX but may also work
on other systems supported by the Allegro library.

B-em is licensed under the GPL, see COPYING for more details.

DEVELOPMENT
===========

Development is active!  If you want to know how to work with us,
then please make sure [you have a look at the SUBMITTING\_PATCHES.md
file](SUBMITTING\_PATCHES.md)

TODO File
=========

A [TODO.md](TODO.md) file exists, with ideas for future functionality
for future versions of B-em.

Contact
=======

B-em is maintained by a group of volunteers -- please [enquire over
on the stardot forums](http://www.stardot.org.uk/forums) for more details.

# Compiling
## Linux

You will need the following libraries:

* Allegro 5.2 or later
* Zlib

Linux distros which include Allegro 5.2 at the time of writing include:

* Arch
* Debian Stable (stretch)
* Ubuntu 17.10 (Artful Aardvark)

Allegro 5.2 packages for Ubuntu 16.04 LTS can be had from
[Launchpad](https://launchpad.net/~allegro/+archive/ubuntu/5.2)

### Released version

Open a terminal window, navigate to the B-em directory then enter:

```
./configure && make
```

### From Git Sources

```
./autogen.sh && ./configure && make
```

### Notes

* B-Em looks for its config file at $XDG_CONFIGID_RI/b-em/b-em.cfg
  or, if $XDG_CONFIG_DIR is not defined, at ~/.config/b-em.cfg
* If a config file cannot be found in those locations it will pick
  up a default config from where the package is installed or from
  the build directory, if being run from there.  It will always be
  saved back to $XDG_CONFIGID_RI/b-em/b-em.cfg or ~/.config/b-em.cfg
* This same config dir is used for CMOS RAM images and hard disc images.
* On Linux, the debugger expects to use the terminal window from
  swhich you started b-em for input and output.  On Windows it opens
  a console window for this purpose.
* With the port to Allegro 5 all the video output modes are available
  on both Win32 and Linux.

## Windows

You will need version 5.2.2.0 of Allegro from:

https://github.com/liballeg/allegro5/releases/tag/5.2.2.0

Later versions have a bug which prevents the ticks in the menus from
working correctly.  There is a choice of compilers but these
instructions only cover MingW and assume you have this correctly
installed.

Unpack/clone B-Em into a folder and unpack the Allegro5 into a parallel
folder, i.e. so the allegro folder and the b-em folder have the same
parent and then from within the b-em folder run makebem.bat

### Notes

* On Windows B-Em looks for resource files in the same directory as
  the executable with the exception of b-em.cfg, the CMOS RAM image
  file and hard discs which are stored in a b-em.exe folder within
  your Windows roaming profile.

Features
========

- Emulates Models A, B, B+, Master 128, Master 512, Master Turbo and
  Master Compact
- Also emulates ARM evaluation system on Master 128
- Emulates 6502, 65816 and Z80 and 32016 tubes.
- Cycle-accurate video emulation
- All documented and undocumented 6502 instructions
- 8271 Floppy Disc Controller emulated (double drive, double sided,
  80 track, read/write)
- 1770-based Floppy Disc Controllers from various manufacters emulated
  including Acorn, Opus, Solidisk, Watford Electronics
  (double drive, double sided, 80 track, read/write)
- Supports following disc formats - .ssd, .dsd, .adf, .adl, .img, .fdi
  and variants thereof for the non-Acorn DFSes.
- Supports the following tape formats: .uef and .csw
- Can run many protected disc and tape games.
- SCSI and IDE hard disc emulation
- Sound emulation, including sample playback
- BeebSID emulation
- Hybrid Music System emulating including Music 500o (synth),
  Music 4000 (keyboard, emulated via MIDI) and Music 2000 (MIDI).
- Lots of video tricks, such as overscan, raster splitting, rupture,
  interlace, mid-line palette and mode splits, etc.
- Video NuLA extended pallete ULA emulation.
- Sideways RAM emulation
- Joystick emulation
- AMX Mouse emulation

New Features
============

# New With V2.2

- MOS 3.50 emulation
- Fixed CRTC bug when programmed with stupid values (MOS 3.50 startup)
- ADFS disc corruption bug fixed (Carlo Concari)
- Fixed ACIA bug - Pro Boxing Simulator tape version now works
- Fixed bug which created endless blank hard disc images
- Printer port DAC emulation
- AMX mouse emulation
- Master 512 mouse now works properly
- Master Compact joystick emulation
- IDE emulation available in non-Master models
- UI fixes (some from Carlo Concari)
- Improvements to VIA emulation
- PAL video filter
- Bugfixes in ARM and 65816 coprocessors
- Debugger fixes
- Tidying up of code
- Windows version can now build on MSVC as well as GCC

# New Since Version 2.2

##Features

* Implement RTC for Master
* Working 32016 co-processor (shared with PiTubeDirect)
* Emulate a SCSI hard disk.
* VDFS - selective access to host filesystem as a standard Acorn filing system.
* Music 5000 emulation (from Beech, via Hoglet)
* Music 4000 emulation via MIDI
* Load ROMs into specific slots
* Debugging on all current tube processors
* Debugger "step over subroutine"
* Debugger: optional refresh screen in single-step/when breakpoint hit.
* Debugger: tracing instructions to a file
* Video NuLa
* Add ROMs to an existing model from anywhere on your PC via menu and
  file picker.
* Save state to a snapshot file for a machine running with Tube
  processor (except 32016)
* Emulate the external and internal 6502 tube processors with the
  correct ROMs and speeds
* Get the visualisation of memory access when debugging working
  cross-platform
* Cross-platform keyboard mapping
* Cross-platform tape catalogue
* Replace diverged Windows/Linux GUI with single GUI

## Bug Fixes

* potential crashes when loading tapes
* CSW files now work on 64-bit Linux
* Unix: Fix fullscreen handling
* Add missing SBC zero page indirect X on tube 6502
* Fix SBC overflow (V) in binary mode on main and tube 6502
* Fix aparent error with carry flag in undocumented instructions.
* i8271: fix emulation always reporting drive as ready
* i8271: ensure spindown happens on disk fault and on closing a disk image.
* 65816: Fix failure to remember 65816 is enabled
* mouse: Fix not working Y direction in 80186 co-pro Gem.
* 6502: fix BCD errors on both main and tube 6502 (but not 65C02)
* 65c02: Added missing BIT zp,X (0x34) instruction
* 65c02 core/tube: Correted NOP lengths
* 65c02 core/tube: Fixed ZP wrapping issue with inditect addressing
* 6502tube: implement Rockwell instructions RMB/SMB and BBR/BBS
* debugging: fix disassembly of 6502 opcode 24, BIT zp
* Fix 256 byte transfer over Tube hangs
* video: fix loadstate/savestate inconsistency

Default keyboard mapping
========================

|BBC key  | PC key |
|---------|--------|
| BREAK   |      f12|
|  *:     |      @'|
|  +;     |      :;|
|  -=     |      -_|
|  ^~     |      +=|
|  f0     |      f0 (function keys are based on keycaps, not positioning)|
|  3#     |       3|
|  6&     |       6|
|  7'     |       7|
|  8(     |       8|
|  9)     |       9|
|  0      |       0|
Shift lock - |    ALT|

The PC key Page Up acts as a speedup key and Page Down as pause.

GUI
===

The options are:

## File

| Option | Meaning |
| ------ | ------- |
| Hard reset | resets the emulator, clearing all memory. |
| Load state | load a previously saved savestate. |
| Save state | save current emulation status. |
| Save Screenshot | save the current screen to a file |
| Exit       | exit to OS. |

## Edit

| Option | Meaning |
| ------ | ------- |
| Paste via Keyboard | send the contents of the clipbaord as keystoked|
| Printer to Clipboard | capture printer output and copy to clipboard|

## Disc

| Option | Meaning |
| ------ | ------- |
| Autoboot disc 0/2    | load a disc image into drives 0 and 2, and boot it.|
| Load disc 0/2        | load a disc image into drives 0 and 2.|
| Load disc 1/3        | load a disc image into drives 1 and 3.|
| Eject disc 0/2       | removes disc image from drives 0 and 2.|
| Eject disc 1/3       | removes disc image from drives 1 and 3.|
| New disc 0/2  | creates a new DFS/ADFS disc and loads it into drives 0 and 2.|
| New disc 1/3  | creates a new DFS/ADFS disc and loads it into drives 1 and 3.|
| Write protect disc 0/2| toggles write protection on drives 0 and 2.|
| Write protect disc 1/3| toggles write protection on drives 1 and 3.|
| Default write protect | determines whether loaded discs are write protected by default|
| IDE Hard disc | Enables emulation of an IDE hard disc |
| SCSI Hard disc | Enables emulation of a SCSI hard disc |
| Enable VDFS | Enable a subset of host OS files to be visible as an Acorn filing system|
| Choose VDFS Root | Chose the directory on the host that is visible via VDFS|

## Tape

| Option | Meaning |
| ------ | ------- |
| Load tape | load a tape image.|
| Eject tape | removes a tape image.|
| Rewind tape | rewind the emulated tape.|
| Show tape catalogue | shows the catalogue of the current tape image.|
| Tape speed | select between normal and fast tape speed.|

## ROMS

This menu shows the contents of the Sideways ROM/RAM banks and enables
these to be loaded with a different ROM, enabled as RAM or cleared.

## Model

This lists the models in alphabetical order and enabled the model being
emulated to be changed.  Be aware this causes a reset.

## Tube

This lists the available second processors and enabled one to be chosen.
The choice in this menu is overridden if the model picked in the model
menu is always supplied with a 2nd processor, for example the Master
512.  For models like this the bundled 2nd processor is always used.

## Settings

### Video

#### Display Type

| Option | Meaning |
| ------ | ------- |
| Line Doubling | stretch the BBC screen by doubling every line.|
| Scanlines     | stretch the BBC screen by blanking every other line|
| Interlaced    | emulate an interlaced display (useful for a handful of demos).  Allows high resolution mode 7. |
| PAL	      | use PAL filter!
| PAL interlaced | use PAL filter, with interlacing. Slow! Allows high resolution mode 7.|

#### Borders

| Option | Meaning |
| ------ | ------- |
| None | Remove all the black space around the image area.
| Medium | borders intended to be attractive rather than accurate |
| Full | The full borders as seen on an old TV |


| Option | Meaning |
| ------ | ------- |
| Fullscreen | enters fullscreen mode. Use ALT-ENTER to return to windowed mode.|

### Sound

| Option | Meaning |
| ------ | ------- |
| Internal sound chip | enable output of the normal BBC sound chip. |
| BeebSID | enable output of the SID emulation.|
| Music 5000 | enable output from an emulated Music 5000 synth|
| Printer Port DAC | enable output of 8-bit DAC connected to the printer port. |
| Disc drive noise | enable output of the disc drive sounds. |
| Tape noise | enable output of the cassette emulation. |
| Internal sound filter | enable bandpass filtering of sound. Reproduces the poor quality of the internal speaker. |
| Internal waveform | choose between several waveforms for the normal BBC sound chip.  Square wave is the original. |

### reSID configuration

| Option | Meaning |
| ------ | ------- |
| Model | choose between many different models of SID. Many tunes sound quite different depending on the model chosen. |
| Simple method | Choose between interpolation and resampling.  Resampling is in theory higher quality, but I can't tell the difference. |
| Disc drive type | choose between sound from 5.25" drive or 3.5" drive. |
| Disc drive volume | set the relative volume of the disc drive noise.|

## Keyboard

| Option | Meaning |
| ------ |-------- |
| Redefine keys | redefine keyboard setup.  Map CAPS/CTRL to A/S - remaps those 2 keys. Useful for games where CAPS/CTRL are left/right. |

## Mouse

| Option | Meaning |
| ------ |-------- |
| AMX mouse | enables AMX mouse emulation. |

## Speed

Choose an soeed relative to a real model of that type.

## Debug

| Option | Meaning |
| ------ | ------- |
| Debugger | Enters debugger for debugging the main 6502. Type '?' to get list of commands. |
| Debug Tube | Enters debugger for debugging the current 2nd processor. |
| Break | break into debugger.|


Command Line Options
====================

```
b-em [discimage|tapeimage|snapshot] [-u name.uef] [-mx] [-tx] [-i] [-c] [-fx]
```

`discimage` name.ssd/dsd/adf/adl/img etc.
`tapeimage` name.uef/csw
`snapshote` name.snp (previously saved snapshot)
`-u` name.uef - load UEF image name.uef

`-mx` - model to emulate, where x is

```
0 - BBC A with OS 0.1
1 - BBC B with OS 0.1
2 - BBC A
3 - BBC B with 8271 FDC
4 - BBC B with 8271 FDC + loads of sideways RAM
5 - BBC B with 1770 FDC + loads of sideways RAM
6 - BBC B US with 8271 FDC
7 - BBC B German with 8271 FDC
8 - B+ 64K
9 - B+ 128K
10 - Master 128 w/ MOS 3.20
11 - Master 512
12 - Master Turbo
13 - Master Compact
14 - ARM Evaluation System
15 - Master 128 w/ MOS 3.50
16 - BBC B with no FDC + loads of sideways RAM
17 - BBC B w/Solidisk 1770 FDC
18 - BBC B w/Opus 1770 FDC
19 - BBC B w/Watford 1770 FDC
20 - BBC B w/65C02, Acorn 1770
21 - BBC B with 65C02, no FDC

```

`-tx` - enable tube, where x is:

```
0 - 6502 (internal)
1 - ARM (Master only)
2 - Z80
3 - 80186 (Master only)
4 - 65816 (if ROMs available)
5 - 32016
6 - 6502 (external)
```

`-i` - enable interlace mode (only useful on a couple of demos)

`-c` - enables scanlines

`-fx` - set frameskip to x (1-9, 1=no skip)

`-fasttape` - speeds up tape access


IDE Hard Discs
==============

To initialise a hard disc, use the included HDINIT program. Press 'I' to investigate the drive - this will
set up the default parameters for a 50 mb drive. If you want a different size press 'Z' and enter the desired
size - it does not matter that this does not match the size given in the emulated hardware.

Then press 'F' to format, and follow the prompts.


Master 512
==========

Master 512 includes mouse emulation. This is used by trapping the mouse in the emulator window - click in the
window to capture it, and press CTRL + END to release.

All disc images used by the Master 512 should have the .img extension - the type is determined by size. Both
640k and 800k discs are supported, as well as DOS standard 360k and 720k.

You can use the IDE hard disc emulation with this, run HDISK.CMD in DOS-Plus, then HDINSTAL.BAT. Go make a cup
of tea while doing this, it takes _forever_! HDISK.CMD will create a file 'DRIVE_C' in ADFS so you will need
to format the hard disc using ADFS first.


65816 coprocessor
=================

The ROMs for this coprocessor are not included with the emulator. You will need to acquire the file
ReCo6502ROM_816 and place it in roms\tube.

The 65816 runs at 16mhz, regardless of what the firmware is set to.


Hardware emulated
=================

| Processor | Meaning |
| --------- | ------- |
| The 6502 processor | All instructions should be emulated. Attempts to be cycle perfect. 65C02 is emulated for Master 128 mode. |
| The 65C12 tube    | As a parasite processor.|
| The 65816 tube    | As a parasite processor. Emulator from Snem. |
| The Z80 tube      | As a parasite processor. Probably a few bugs. Emulator from ZX82. |
| The ARM processor | As a parasite processor for Master 128 only. Emulator from Arculator. |
| The 80186 tube    | As a parasite processor for Master 512 only. Emulator from PCem. |
| The 6845 CRTC     | Cycle-exact emulation. Runs everything I've tried, with all effects working. |
| The Video ULA     | Cycle-exact emulation, though I think palette changes are a cycle off? Might be a delay in the real chip. |
| The System VIA    | Keyboard and sound emulated. Also CMOS on Master-based models.|
| The User VIA      | Emulated.|
| 8271 FDC          | Double disc, double sided, 40/80 tracks, read/write. With authentic noise. Supports read-only access of protected FDI images.|
| 1770 FDC          | Double disc, double sided, 40/80 tracks, read/write. With authentic noise. Supports read-only access of protected FDI images.|
| IDE hard disc	    | Emulates 2 discs. Emulation from Arculator.|
| SCSI hard disc	| Emulates 4 discs. Emulation from BeebEm.|
| Sound             | All channels emulated, with sample support and some undocumented behaviour (Crazee Rider).  With optional bandpass filter.|
| BeebSID |  Emulated using resid-fp, so should be pretty accurate. Is only emulated when accessed, to reduce CPU load.|
| ADC               | Real joystick emulation, supporting both joysticks.|
| 6850 ACIA         | Emulated for cassettes. Read only.|
| Serial ULA        | Emulated.|
| Music 5000        | Hybrid synth emulated.  Emulation from Beech|
| Music 4000        | Hybrid music keyboard.  Emulated via MIDI,  Emulation original to B-Em|
| Music 2000        | Hybrid MIDI I/F.  Connects to host MIDI,  Emulation original to B-Em|


Hardware NOT emulated
=====================

* Serial Port
* Econet

Thanks
======

* Sarah Walker for B-Em up to V2.2
* David Gilbert for writing Beebem and distributing the sources with it
* James Fidell for writing Xbeeb and distributing the sources with it
* Tom Seddon for updating Model B, indirectly motivating me to do v0.6, and for
  identifying the Empire Strikes Back bug.
* Ken Lowe for assistance with the Level 9 adventures.
* Rebecca Gellman for her help with a few things (hardware-related).
* Thomas Harte for some UEF code - I wrote my own in the end - and for the
  OS X port.
* Dave Moore for making and hosting the B-em site
* Rich Talbot-Watkins and Peter Edwards (also Dave Moore) for testing
* Robert Schmidt for The BBC Lives!
* DJ Delorie for DJGPP
* Shawn Hargreaves for Allegro

Acorn for making the BBC in the first place:

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
