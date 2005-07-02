             ██████████▄           █████████  ████▄    ▄████
             ██       ▀██          ██         ██ ▀██▄▄██▀ ██
             ██       ▄██          ██         ██   ▀██▀   ██
             ███████████    █████  █████      ██          ██
             ██       ▀██          ██         ██          ██
             ██       ▄██          ██         ██          ██
             ██████████▀           █████████  ██          ██

                                 Version 0.8
                         A freeware BBC Micro emulator

Introduction
~~~~~~~~~~~~

B-Em is an attempt to emulate a BBC Micro, made by Acorn Computers in the 80's

Features
~~~~~~~~

- Emulates Models A, B, B+, and Master 128
- All documented video modes supported
- All documented and some undocumented 6502 instructions
- 8271 Floppy Disc Controller emulated (double drive, double sided, 80 track, read only)
- 1770 Floppy Disc Controller emulated (double drive, double sided, 80 track, read/write)
- Supports six formats for BBC storage on PC - .ssd, .dsd, .adf, .inf, .uef
  and __catalog__
- Sound emulation, including sample playback
- Some CRTC tricks, such as overscan, raster splitting and rupture.
- Sideways RAM emulation
- Joystick emulation
- Save states


Differences from last version
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Timing improvements, some stuff that encrypts via timers (eg Frogman) now
  works
- VIA improvements - fixed Planetoids/Super Defender/whatever, Volcano and
  Pharoah's Curse
- Improvements to sound accuracy, Ghouls and Killer Gorilla (and probably
  others) have better sound
- Cassette emulation now much more reliable
- Save states are implemented again
- Config file now in English
- Added high pass sound filter and SID waveform


Requirements
~~~~~~~~~~~~

B-Em (bbc model B EMulator), requires the following :

A Pentium or better computer (try a P233 at least)
8mb RAM (?)

Eight ROM images are provided with B-em -
os           - UK BBC MOS
usos         - US BBC MOS
bpos         - BBC B+ MOS
mos3.20      - Master 128 MOS 3.20
a\basic.rom  - BBC BASIC 2
b\basic.rom
bp\basic.rom
b\dfs.rom    - Watford DFS 1.30
bp\dfs.rom   - Acorn 1770 DFS
bp\nadfs.rom - Acorn ADFS

If you want to use more paged ROMs, put them in the roms directory, in either
directory B or BP.


Notes
~~~~~

You can use the .INF filing system (disable tape) _only_ in model A&B modes
(and only in PAL).

The UEF filing system is read-only. All others should be able to be written
to.

400x300 mode doesn't work on some machines. If you end up with a bad display,
hit CTRL-ALT-END to kill it.

Savestates do not save states of FDCs or serial stuff, so don't save during
loading.


Known bugs
~~~~~~~~~~

ADFS corrupts on COMPACT command
Formatting not supported
Still some timing bugs on protected games (eg Hypersports)


Keyboard mapping :
~~~~~~~~~~~~~~~~~~

BBC key     - PC key
--------------------
 BREAK          f12
  *:            @'
  +;            :;
  -=            -_
  ^~            +=
  f0            f1 (function keys are based on positioning rather than keycaps)
  3#             3
  6&             6
  7'             7
  8(             8
  9)             9
  0              0
Shift lock -    ALT

Note that when you do a hard reset (CTRL-Break), unlike a real BBC, this
performs a cold boot instead, to wipe traces of programs such as W.A.R which
hang the BBC on reset.


GUI :
~~~~~

Hit F11 or click the right mouse button to go to the GUI.
The options are :

File :
        Return - return to emulator.
        Load State - Load a saved state.
        Save State - Save current machine state.
        Exit   - exit to DOS/Windows/whatever.

Model :
        PAL A          - emulate a PAL model A.
        PAL B          - emulate a PAL model B.
        PAL B + SWRAM  - emulate a PAL model B with sideways RAM.
        NTSC B         - emulate an NTSC model B with sideways RAM.
        PAL B+         - emulate a PAL model B+ with 64k RAM.
        PAL B+96K      - emulate a PAL model B+ with 96k RAM.
        PAL B+128K     - emulate a PAL model B+ with 128k RAM.
        PAL Master 128 - emulate a PAL Master 128.

Disc :
        Load drive 0/2 - load a disc into drives 0 and 2.
        Load drive 1/3 - load a disc into drives 1 and 3.
        Disc sounds - emulate authentic 5.25" disc drive noise.

Tape :
        Change tape - load a new UEF file.
        Rewind tape - rewind the emulated tape.
        Tape enable - enables the UEF support, but disables the INF support.

Video :
        Video mode  - select a video mode. 400x300 is by far the fastest, and
                      800x600 with 2xSaI the slowest. 400x300 does not work
                      correctly on some machine though.
        Blur filter - enable a blurring filter. Can make games with hi-res
                      dithering look better.
        Monochrome  - disables colour.

Sound options :
        Sound enable     - enable/disable sound.
        Low pass filter  - applies a low pass filter to the sound.
        High pass filter - applies a high pass filter to the sound.
        Waveform         - alters the waveform type. Original BBC uses square.
        Start VGM log    - start logging sound to an VGM file.
        Stop VGM log     - stop logging sound.

Misc options :
        Calibrate joystick 1 - Calibrates the first joystick.
        Calibrate joystick 2 - Calibrates the second joystick.
        Save screenshot      - saves screenshot in BMP,PCX, or TGA format.


FAQ :
~~~~~

Q : How do I run a game?
A : If you have a disc image (.ssd, .dsd, .img etc) load it through the disc
    menu, then hold SHIFT and tap F12.
    If you have a tape image (.uef) load it through the tape menu, ensure
    that 'tape enable' is ticked, then type the following commands :
    *TAPE
    PAGE=&E00
    CHAIN""

Q : Why have I got no sound?
A : B-em should work with 100% SoundBlaster compatibles, ESS Audiodrive,
    Ensoniq Soundscape, and Windoze $ound System. It will not work with cards
    not in that list (such as Creative's newer SoundBlasters, which aren't
    really compatible, or sadly the GUS), it will not work with non-100%
    compatibles with crappy drivers (such as the ones found in laptops) and it
    will not work if you have a bad BLASTER enviroment label.
    Minor addition to this, if you use a poor quality card (such as a
    SoundBlaster) and you have some sound running in the background in Windows,
    B-em will deliver no sound. The answer is to upgrade to a good card. B-em
    will disable sound in this case, so you have to enable it again afterwards.

Q : How do I contact you?
A : E-mail me at b-em@bbcmicro.com


Hardware emulated
~~~~~~~~~~~~~~~~~

The 6502 processor - Most instructions should be emulated. Attempts to be cycle
                     perfect. 65C02 is emulated for Master 128 mode, but is
                     probably missing some opcodes.
The 6845 CRTC      - Accurate line-by-line engine. Firetrack, Revs, and
                     Uridium all work.
The Video ULA      - All modes emulated.
The System VIA     - Keyboard and sound emulated.
The User VIA       - Emulated.
8271 FDC           - Double disc, double sided, 40/80 tracks, read/write. With
                     authentic noise. Only in model B mode.
1770 FDC           - Double disc, double sided, 40/80 tracks, read/write. With
                     authentic noise. Only in model B+ and Master 128 mode.
tape filing system - Supports .inf and __CATALOG__ format.
Sound              - All channels emulated, with sample support and some
                     undocumented behaviour (Crazee Rider). With optional low
                     pass filter.
ADC                - Real joystick emulation, supporting both joysticks.
6850 ACIA          - Emulated for cassettes. Read only.
Serial ULA         - Emulated.


Hardware NOT emulated
~~~~~~~~~~~~~~~~~~~~~

serial port
AMX mouse
Tube
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

Thomas Harte for some UEF code - I wrote my own in the end - and for inventing
UEF files.

Dave Moore for making and hosting the B-em site

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
and Arculator (b-em.bbcmicro.com/arculator), my A3xx/A4xx/A3000/A540 emulator
</plug>

Appendix A : The source code
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you want to use the source code for anything, that's fine. But, if you can,
you are encouraged to contribute to it by adding new features and/or fixing it
To recompile the code, you will need DJGPP 2, GCC, Allegro 4, Zlib and 2xSaI.


Appendix B : Transfering BBC files to the PC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are several ways to do this, seven of which are listed here :


Serial cable
~~~~~~~~~~~~

Get a BBC serial cable, a PC serial cable, connect them up in a special way,
and copy files. Before you do this, you must have an Xmodem transfer program
for both the BBC and PC. I recommend Xfer, you can get it from The BBC Lives!
website. It also include instructions on how to connect the two cables.


Parallel cable
~~~~~~~~~~~~~~

Connect a BBC and a PeeCee through their parallel ports (you could use the
user port on the BBC if you wanted), and write a program to transfer data
between the two (probably not hard). The cable will probably be easier to make
than the serial cable (as you won't have to search for a DIN-5 plug), and the
transfer rates faster.


Connecting a BBC drive to a PC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I shall be attempting this soon. Apparently, many PC disc controllers can't
read single density discs (ie DFS ones), but if yours can, you can use FDC or
Anadisk to read them.


Cassette
~~~~~~~~

Hook up the BBC cassette port to a PC, load the data off disc, save it to the
cassette, and use a converting program.


Hex editor
~~~~~~~~~~

Dump the requested file in hex on the BBC screen, and use a hex editor on the
PeeCee to enter the data in. Probably the most suicidal method, but if you
really want that data...


The Archimedes
~~~~~~~~~~~~~~

If you don't want to mess about with soldering iron, you can use the
Archimedes (or RISC PC), and a BBC disc drive and adapter, to copy the files
off BBC disc onto PC disc. However, you may have to end up doing this at your
nearest school, and you might have to actually *buy* a disc drive and adapter.
Also, as I have discovered, some disc drives seem to be incompatible with at
least the RiscPC.


The Internet
~~~~~~~~~~~~

Go find.
You could page down and try one of the sites listed in the next section.


Appendix C : Misc stuff
~~~~~~~~~~~~~~~~~~~~~~~

Publications used :

Creative Assembler : How To Write Arcade Games - Johnathan Griffiths - 6502
instruction set and BASIC error message format

The DNFS instruction booklet - unknown author - DIP switch format

The BBC User Guide - John Coll - Teletext control codes and tape format

The BBC Advanced User Guide - Bray, Dickens and Holmes - VIA info


Web sites :

The Stairway To Hell - www.stairwaytohell.com
A decent BBC/Electron site, with plenty of games and emulators. Was the
original home of B-em.

The BBC Lives! - bbc.nvg.org
Another good BBC site, with even more games and emulators.


Other BBC emulators :

Model B - modelb.bbcmicro.com
Massively improved over the old versions, this is now one of the best BBC
emulators and runs pretty much everything.

Beebem - www.mikebuk.dsl.pipex.com/beebem
The most famous BBC emulator. Runs pretty much everything, but is really
quite slow.

BeebIt - homepages.paradise.net.nz/mjfoot/bbc.htm
The main BBC emulator for the RiscPC. I can't really comment on this (my
RPC600 is far too slow), but according to the author, it's really really good.

pcBBC - can't remember the site address
One of the few non-free BBC emulators, this one has good compatibility, but
bad sound, an awful interface and costs money.

BeebInC - beebinc.bbcmicro.com
Seemingly dead, this was one of my favourite emulators. Surprisingly fast and
compatible, but let down by poor sound (uses sine waves, not square waves) and
low refresh rate (25 fps instead of 50 fps).
