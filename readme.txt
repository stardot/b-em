             ██████████▄           █████████  ████▄    ▄████
             ██       ▀██          ██         ██ ▀██▄▄██▀ ██
             ██       ▄██          ██         ██   ▀██▀   ██
             ███████████    █████  █████      ██          ██
             ██       ▀██          ██         ██          ██
             ██       ▄██          ██         ██          ██
             ██████████▀           █████████  ██          ██

                                 Version 0.3a
                         A freeware BBC Micro emulator

Introduction
~~~~~~~~~~~~

B-Em is an attempt to emulate a BBC Micro, made by Acorn Computers in the 80's

Features
~~~~~~~~

- Emulates both Models A and B
- All documented video modes supported
- All documented and undocumented 6502 instructions
- 8271 Floppy Disc Controller emulated (single drive, double sided, 80 track)
- Supports five formats for BBC storage on PC - .ssd, .dsd, .inf, .uef and
  __catalog__
- Sound emulation
- Snapshots
- Optional 6502 debugging to file


Differences from last version
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- `clear screen' command removed, as it was using the same key as change disc.
- Stuff held in seperate directories :
  roms  - OS ROM and paged ROMs
  inf   - .INF files (with the extension-less files as well)
  uef   - .UEF files
  discs - .SSD, .IMG, and .DSD files
- You shouldn't get `chunk ID' errors anymore with the UEF code
- American BBC support


Requirements
~~~~~~~~~~~~

B-Em (bbc model B EMulator), requires the following :

A 386 or better computer (realisticaly a P166)
4mb RAM (?)

Four ROMs are provided with B-em -
os        - UK BBC MOS
usos      - US BBC MOS
basic.rom - BBC BASIC 2
dfs.rom   - Watford DFS 1.30

If you want to use more paged ROMs, put them in the roms directory.


Hardware emulated
~~~~~~~~~~~~~~~~~

The 6502 processor - All instructions emulated. Timing is a bit suspect though
The 6845 CRTC      - Registers R0,R4,R5,R12,R13,R14 and R15 affect the display
The Video ULA      - All modes emulated, but 8 is buggy
The System VIA     - Keyboard and sound emulated
The User VIA       - Printer emulated
8271 FDC           - Single disc, double sided, 40/80 tracks, read only
tape filing system - Supports .inf, .uef and __CATALOG__ format
Sound              - Sound hardware is emulated, but sound output isn't very
                     good or well tested
Serial ULA         - Only the cassette motor LED
6850 ACIA          - Reads tape sample. Not usable yet
ADC                - No joystick emulation yet though
Printer            - Prints to file printer.txt. Very buggy
Joystick           - With numlock off, the arrow keys control the stick and
                     insert is the button.


Hardware NOT emulated
~~~~~~~~~~~~~~~~~~~~~

serial port
AMX mouse
Tube
Econet


Running :
~~~~~~~~~

Run setup to configure the video mode, sound, disc image, and UEF support
first. When you type b-em, you get the standard start-up text -

BBC Computer 32K
Watford DFS 1.30
BASIC
>_

or

Acorn OS
Watford DFS 1.30
BASIC
>_

Isn't it nice to relive the 80's again?

The 6502 emulation is about 80% bug free, but has a few problems with some
BASIC programs, and out of the 75 games I have tested so far, 64 (85%) are
playable.

Command line options :
~~~~~~~~~~~~~~~~~~~~~~

Most of these are obselete, due to the setup program, but they can be used
as `overrides'.

-modela   : Emulates a BBC Model A (not working).
-modelb   : Emulates a BBC Model B (default).
-us       : Emulates an American BBC model B.
-frameskip: Follow this with the number of frames you want to skip, plus 1.
            The default value is 1. The following values are legal :
            2 - skip every other frame
            3 - skip every third frame
            4 - skip every fourth frame, etc
-logsound : Write sound output to sound.pcm (8-bit, mono, 22khz, signed)
-dips     : Follow this with an 8-bit number (decimal, octal or hex),
            containing a binary combination of the 8 DIL switches underneath a
            BBC keyboard. Most of the switches are ignored, but you can set
            the initial screen mode by inverting the bits and putting the
            result after -dips. For the lazy people out there, here are the
            combinations :
              -dips 0 - Mode 7
              -dips 1 - Mode 6
              -dips 2 - Mode 5
              -dips 3 - Mode 4
              -dips 4 - Mode 3
              -dips 5 - Mode 2
              -dips 6 - Mode 1
              -dips 7 - Mode 0
-disc     : Follow this with the disc image you would like loaded into disc
            drive 0 of the BBC. You can use long filenames (if in Windows),
            and the number of sides on the disc is determined by the first
            letter of the file extension (if it is D then the disc has 2 sides,
            ie .DSD, .DAD etc, otherwise it has 1 side).
-uef      : Follow this with the name of the UEF file to use. This will
            disable the .inf and __catalog__ handling, even if UEF support is
            disabled in setup.
-sound    : Enables _awful_ sound emulation through a Sound Blaster compatible.
            It sounds VERY bad and I can only say that it works on an Aureal
            Vortex (and aparently AWE 32s as well).
-scanline : Enables scanline drawing. Use this if you are trying to run a
            program that uses split mode/palette displays. It doesn't really
            help because of timing problems.
-firetrack: Coaxes Firetrack into life by messing around with the VIAs. Breaks
            lots of other games.
-fast     : Disables slowdown, so that B-em goes as fast as your PC will run
            it.


Keyboard mapping :
~~~~~~~~~~~~~~~~~~

BBC key     - PC key
--------------------
 BREAK          f12
  *:            @'
  +;            :;
  -=            -_
  ^~            +=
  f0            f10
  3#             3
  6&             6
  7'             7
  8(             8
  9)             9
  0              0
Shift lock -    ALT

Note that when you do a hard reset (CTRL-Break), unlike a real BBC, this
preforms a cold boot instead, to wipe traces of programs such as W.A.R, which
hang the BBC on reset.


Filing System (FS) :
~~~~~~~~~~~~~~~~~~~~

The FS is very primitive at the moment. It patches the OS to essentially
create a tape emulation. LOAD, *RUN, *LOAD and *CAT are currently supported.
*ENABLE, *OPT and *TAPE are ignored, everything else will probably bomb back
to DOS. If you are trying to load a file that doesn't exist, the emulator will
also bomb back to DOS. Entering the filename as "" will currently bomb out,
although I may have implemented that by the next version.

You can use both .INF format and __CATALOG__ format (at the same time). Using
.UEF disables the other two.


UEF support :
~~~~~~~~~~~~~

UEF support is very alpha at the moment. It's currently just a hack into the
TFS to load UEF files. One of the main limitations is that it can't load
gzipped UEF files - which happen to be all the UEFs on The Stairway To Hell,
and all the ones that MakeUEF generates. However, if you have Windows 9x or
NT, and Winzip, then there is a way around this.

Go to rename the UEF, and put a .gz on the end, after the .uef. Then open the
file in Winzip, and extract it. Then you will have a nice decompressed UEF
file that will work in B-Em.

It also has trouble running files off UEF, which can be fixed by someone
fixing the ACIA emulation.


Sound :
~~~~~~~

The sound really is awful at the moment. The tone generators are just too
harsh, although the noise generator isn't bad. Last time I tested the code on
a machine with a sound card other than an Aureal Vortex (an SB 2) it didn't
work properly.

There seem to be a few bugs in the tones, because some tones sound either too
high, or too low. This makes music sound out of tune. The best example I have
found is Repton 2, which is pretty flawless (except for the harshness).
FireTrack can give you an example of how bad the sound can be.


Menu :
~~~~~~

Press F11 to bring up the menu.

The text menu dumps the 6502 & CRTC regs, and these are the options :

Q - Quit emulator
A - Start 6502 log, in `log.log' (these files get very big very quickly)
D - Dumps BBC RAM to `ram.dmp'
S - Saves snapshot to `snap0000.snp'
L - Loads snapshot from `snap0000.snp'
C - Change disc image


Tested games :
~~~~~~~~~~~~~~


Working perfectly (as far as I know) :
A+F Software   Chuckie Egg
A+F Software   Chuckie Egg II
A+F Software   Cylon Attack
A+F Software   Painter

Acornsoft      Arcadians
Acornsoft      Magic Mushrooms
Acornsoft      Planetoids
Acornsoft      Simcity
Acornsoft      Snapper

Alligata       Blagger

Atari          Donkey Kong Jr

Bug Byte       Twin Kingdom Valley

Comsoft        SAS Commander

Elite          Commando
Elite          Paperboy

Icon Software  Chrysalis
Icon Software  Contraption

IJK            Caterpiller

Impact Soft.   Zenon

Lothlorien     Battlezone 2000

Martech        Gisburne's Castle

Program Power  Bumble Bee
Program Power  Castle Quest
Program Power  Cybertron
Program Power  Danger UXB
Program Power  Frenzy
Program Power  Ghouls
Program Power  Killer Gorilla
Program Power  Mr. EE

Soft. Invasion 3D Grand Prix

Soft. Projects Jet Set Willy
Soft. Projects Manic Miner

Superior Soft  Overdrive
Superior Soft  Repton
Superior Soft  Repton 2
Superior Soft  Repton 3
Superior Soft  Speech! (Use with -logsound for good results)
Superior Soft  Stryker's Run
Superior Soft  Stryker's Run Part 2
Superior Soft  Tempest
Superior Soft  Thrust
Superior Soft  Winged Warlords

Ultimate PTG   Alien 8
Ultimate PTG   Atic Atac
Ultimate PTG   Jet-Pac
Ultimate PTG   Knight Lore
Ultimate PTG   Nightshade

US Gold        Dambusters
US Gold        Impossible Mission
US Gold        Spy Hunter
US Gold        Tapper

Virgin         Bug Bomb
Virgin         Microbe

?????????????  Star Wars

Working imperfectly :
Aardvark Soft  FireTrack (Scrolling bugs and needs hack)
Aardvark Soft  Frak! (Timer problems)
Aardvark Soft  Zalaga (The enemies go all over the place when starting a new
                       game and it crashes sometimes)

Acornsoft      Elite (Split mode problems)
Acornsoft      Hopper (Counter counts wrong)
Acornsoft      Rocket Raid (Split mode problems)

Alligata       Dambusters (No sound)

Compu Concepts Android Attack (Only works off disc image, hangs on TFS)

Elite          Airwolf (Helicopter graphics go wrong sometimes)

Software Proj. Pyramid (Messed up score)

Superior Soft  Wallaby (Too fast)

US Gold        Bounty Bob Strikes Back (wrong colours and infinite loop on
               finishing level)

Ultimate PTG   Sabre Wulf (Wrong colours)

????????????   Nutcraka (Flashing colours)


Not working :
Acornsoft      Revs (Video timing a bit off, which means that the split mode
                     display doesn't work)

BBC Soft       Dr. Who The 1st Adventure (You can't control the game)
BBC Soft       White Knight (mk 11) (Hangs)

Bubble Bus     Starquake (Colours keep flashing and game ends immediately)

Dr Soft        Phantom (Hangs, but then it never worked on a real BBC anyway)

Superior Soft  Exile (Exits from main program)
Superior Soft  Repton Infinity (Hangs)

Virgin Games   Trench (Hangs)


Things for version 0.4 :
~~~~~~~~~~~~~~~~~~~~~~~~

1770 FDC emulation (I just got docs)
65C02 emulation
Working printer emulation
Electron emulation (I have ROMs now)


Todo list :
~~~~~~~~~~~

Fix some of the remaining 6502 bugs
Make the sound better
Implement joystick reading
Rewrite the GUI
Speed it up
Add Master emulation (needs 65C02 docs, paging, clock and 1770 docs, and
Master OS ROMs) and/or Electron emulation (should be done soon) and/or Atom
emulation (needs Atom docs and ROMs)
Re-implement Tube emulation
Fix the ACIA emulation (can someone help with this?)


Known bugs/problems :
~~~~~~~~~~~~~~~~~~~~~

Still a few 6502 emulation bugs.
Very poor sound.
Only first character of print job gets sent to file.
A bit slow.
UEF support unfinished.
TFS doesn't work in US mode - I need the OSFSC/OSFILE entry points on the US
OS.

Thanks to :
~~~~~~~~~~~

David Gilbert for writing Beebem and distributing the sources with it

James Fidell for writing Xbeeb and distributing the sources with it

Robert Schmidt for The BBC Lives!

DJ Delorie for DJGPP

Acorn for making the BBC in the first place

Thomas Harte for some UEF code - I wrote my own in the end - and for inventing
UEF files.

Dave Moore for making the B-em web page, and for being kind enough to put it
on The Stairway To Hell.

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

tommowalker@hotmail.com


Appendix A : The source code
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you want to use the source code for anything, that's fine. But, if you can,
you are encouraged to contribute to it by adding new features and/or fixing it
To recompile the code, you will need DJGPP 2, GCC and my graphics library
(included with the source distribution). Note that some of the source code is
either David Gilbert's or James Fidell's, so you should acknowledge that if
you use it.


Appendix B : Transfering BBC files to the PC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are several ways to do this, three of which are listed here :

Serial cable
~~~~~~~~~~~~

Get a BBC serial cable, a PC serial cable, connect them up in a special way,
and copy files. Before you do this, you must have an Xmodem transfer program
for both the BBC and PC. I recommend Xfer, you can get it from The BBC Lives!
website. It also include instructions on how to connect the two cables.


Connecting a BBC drive to a PC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I shall be attempting this soon. Apparently, many PC disc controllers can't
read single density discs (ie DFS ones), but if your's can, you can use FDC or
Anadisk to read them.


The Archimedes
~~~~~~~~~~~~~~

If you don't want to mess about with soldering iron, you can use the
Archimedes (or RISC PC), and a BBC disc drive and adapter, to copy the files
off BBC disc onto PC disc. However, you may have to end up doing this at your
nearest school, and you might have to actually *buy* a disc drive and adapter.
Or, of course, you could just copy the games off the Internet, but that's
illegal.


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

A decent BBC/Electron site, with plenty of games and emulators.

The BBC Lives! - www.nvg.ntnu.no/bbc

Another good BBC site, with even more games and emulators.
