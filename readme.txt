             ██████████▄           █████████  ████▄    ▄████
             ██       ▀██          ██         ██ ▀██▄▄██▀ ██
             ██       ▄██          ██         ██   ▀██▀   ██
             ███████████    █████  █████      ██          ██
             ██       ▀██          ██         ██          ██
             ██       ▄██          ██         ██          ██
             ██████████▀           █████████  ██          ██

                                 Version 0.7
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


Differences from last version
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Master 128 emulation (buggy)
- Better video timing, Uridium works
- VIA interrupt bug fixed, many games better
- Interrupt bug fixed, Empire Strikes Back now working
- Disc write (B+ and Master 128 only)
- Sound mixing now 16 bit
- Low pass filter for sound
- Improved disc drive noise
- More reliable tape emulation


Requirements
~~~~~~~~~~~~

B-Em (bbc model B EMulator), requires the following :

A Pentium or better computer (try a P233 at least)
16mb RAM (?)

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


Known bugs
~~~~~~~~~~

Master 128 has various bugs
TFS only works on PAL models A and B AFAIK
ADFS corrupts on COMPACT command
Formatting not supported
UEF has problems on some games


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
8271 FDC           - Double disc, double sided, 40/80 tracks, read only. With
                     authentic noise. Only in model B mode.
1770 FDC           - Double disc, double sided, 40/80 tracks, read/write. With
                     authentic noise. Only in model B+ and Master 128 mode.
tape filing system - Supports .inf and __CATALOG__ format.
Sound              - All channels emulated, with sample support and some
                     undocumented behaviour (Crazee Rider). With optional low
                     pass filter.
ADC                - Real joystick emulation, supporting both joysticks.


Hardware NOT emulated
~~~~~~~~~~~~~~~~~~~~~

serial port
AMX mouse
Tube
Econet
Printer


Running :
~~~~~~~~~

When you type b-em, you get the standard start-up text -

BBC Computer 32K
Watford DFS 1.30
BASIC
>_

or

Acorn OS 128k
Acorn 1770 DFS
BASIC
>_

or some other variation.


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
        Exit   - exit to Windows.

Model :
        PAL A          - emulate a PAL model A.
        NTSC A         - emulate an NTSC model A.
        PAL B          - emulate a PAL model B.
        NTSC B         - emulate an NTSC model B.
        PAL B+         - emulate a PAL model B+ with 64k RAM.
        PAL B+96K      - emulate a PAL model B+ with 96k RAM.
        PAL B+128K     - emulate a PAL model B+ with 128k RAM.
        PAL Master 128 - emulate a PAL Master 128 (buggy).

Disc :
        Load drive 0/2 - load a disc into drives 0 and 2.
        Load drive 1/3 - load a disc into drives 1 and 3.
        Disc sounds - emulate authentic 5.25" disc drive noise.

Tape :
        Change tape - load a new UEF file.
        Rewind tape - rewind the emulated tape.
        Tape enable - enables the UEF support, but disables the INF support.

Video :
        Blur filter - enable a blurring filter. Can make games with hi-res
                      dithering look better.
        Monochrome  - disables colour.

Sound options :
        Sound enable    - enable/disable sound.
        Low pass filter - applies a low pass filter to the sound.
        Waveform        - alters the waveform type. Original BBC uses square.
        Start SN log    - start logging sound to an SN file.
        Stop SN log     - stop logging sound.

Misc options :
        Calibrate joystick 1 - Calibrates the first joystick.
        Calibrate joystick 2 - Calibrates the second joystick.
        Save screenshot      - saves screenshot in BMP,PCX, or TGA format.


Tape emulation :
~~~~~~~~~~~~~~~~

Currently, *TAPE can select one of two filing systems, depending on the tape
enable option in the GUI.

The first is the old .INF/__CATALOG__ FS. This always loads from the INF
directory, and is the format used by all the files at The BBC Lives!. This
patches into the OS to take over tape handling.

The second is the new UEF emulation. This is more accurate, as it emulates the
tape hardware. However, it is much slower, read only, and has some bugs.


FAQ :
~~~~~

Q : Why doesn't B-em look right on my monitor?
A : If the screen is just off centre, then use the positioning controls. If
    not, then your monitor just won't handle B-em's 400x300 video mode, and
    there's not much you can do.

Q : Why have I got no sound?
A : B-em should work with all DirectSound compatible cards. If you get no
    sound, check your drivers. Also, some older cards (like the pre-Live
    SoundBlasters), can't have sound playing in two programs simulatiously,
    so close whatever other program is playing sound.

Q : What happened to snapshots?
A : They were removed for technical reasons. They will probably return in the
    next release (if there is one).

Q : How do I contact you?
A : E-mail me at b-em@bbcmicro.com


Tested games :
~~~~~~~~~~~~~~

Slightly less impressive than before, but the previous lists were over the top.

Working perfectly (as far as I know) :
A & F       Chuckie Egg
A & F       Cylon Attack

Aardvark    Firetrack
Aardvark    Frak
Aardvark    Zalaga

Acornsoft   Arcadians
Acornsoft   Elite
Acornsoft   Magic Mushrooms
Acornsoft   Planetoids
Acornsoft   Revs
Acornsoft   Rocket Raid

Audiogenic  Sphere of Destiny

Domark      Empire Strikes Back

Godax       Skirmish

Hewson      Uridium

Imagine     Pedro

Mandarin    Cute To Kill

Micropower  Bumble
Micropower  Castle Quest
Micropower  Cybertron
Micropower  Dr. Who (B+ and Master version)
Micropower  Ghouls

Superior    Crazee Rider (go for enhanced mode! - not on B+ though)
Superior    Citadel
Superior    Citadel 2
Superior    Codename Droid
Superior    Exile
Superior    Galaforce 2
Superior    Overdrive
Superior    Pipeline
Superior    Repton
Superior    Repton 2
Superior    Repton 3
Superior    Road Runner

Tynesoft    Rig Attack

Ultimate    Alien 8
Ultimate    Cookie
Ultimate    Jetpac
Ultimate    Sabre Wulf

US Gold     Impossible Mission
US Gold     Spy Hunter
US Gold     Tapper

Working imperfectly :

Audiogenic  Psycastria  (screen low down, but who cares? Go play Uridium)

Superior    Stryker's Run             (bad gfx in places?)
Superior    Vertigo (master 128 mode) (video bugs?)


Thanks to :
~~~~~~~~~~~

David Gilbert for writing Beebem and distributing the sources with it

James Fidell for writing Xbeeb and distributing the sources with it

Tom Seddon for updating Model B recently, indirectly motivating me to do
another update this year, and for identifying the Empire Strikes Back bug.

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


Appendix A : The source code
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you want to use the source code for anything, that's fine. But, if you can,
you are encouraged to contribute to it by adding new features and/or fixing it
To recompile the code, you will need DJGPP 2, GCC and Allegro 4.


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

Beebem - beebem.bbcmicro.com - eventually
The most famous BBC emulator. Runs pretty much everything, but is really slow.

BeebIt - homepages.paradise.net.nz/mjfoot/bbc.htm
The main BBC emulator for the RiscPC. I can't really comment on this (my ARM6
is way too slow), but according to the author, it's really really good.

pcBBC - can't remember the site address
One of the few non-free BBC emulators, this one has good compatibility, but
bad sound and costs money.

BeebInC - www.beebinc.net
Seemingly dead, this was one of my favourite emulators. Surprisingly fast and
compatible, but let down by poor sound (uses sine waves, not square waves) and
low refresh rate (25 fps instead of 50 fps).
