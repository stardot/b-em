             ██████████▄           █████████  ████▄    ▄████
             ██       ▀██          ██         ██ ▀██▄▄██▀ ██
             ██       ▄██          ██         ██   ▀██▀   ██
             ███████████    █████  █████      ██          ██
             ██       ▀██          ██         ██          ██
             ██       ▄██          ██         ██          ██
             ██████████▀           █████████  ██          ██

                                 Version 0.4a
                         A freeware BBC Micro emulator

Introduction
~~~~~~~~~~~~

B-Em is an attempt to emulate a BBC Micro, made by Acorn Computers in the 80's

Features
~~~~~~~~

- Emulates Models A & B
- All documented video modes supported
- All documented and undocumented 6502 instructions
- 8271 Floppy Disc Controller emulated (single drive, double sided, 80 track)
- Supports four formats for BBC storage on PC - .ssd, .dsd, .inf and
  __catalog__
- Sound emulation
- Snapshots
- Some CRTC tricks, such as overscan and raster splitting emulated.
- Sideways RAM emulation


Differences from last version
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Some 6502 bugs fixed, Exile now working properly.
- Re-added Model A emulation.
- Disc drive noise added.
- Default config file no longer points to a non-existant file.
- Can now log sound - invented new file format for this (.sn - player and
  format description included)
- Arrow keys and delete/copy now work again.
- Updated documentation


Requirements
~~~~~~~~~~~~

B-Em (bbc model B EMulator), requires the following :

A 386-SX16 or better computer (realisticaly a P166)
4mb RAM (?)

Four ROMs are provided with B-em -
os        - UK BBC MOS
usos      - US BBC MOS
basic.rom - BBC BASIC 2
dfs.rom   - Watford DFS 1.30

If you want to use more paged ROMs, put them in the roms directory.


Known bugs
~~~~~~~~~~

In line mode, resetting can cause odd things, such as no prompt, the startup
text being written slowly, and crashing out.
Still a few 6502 emulation bugs (mostly noticeably Phantom Combat, Frak! and
Zalaga)
Sideways RAM doesn't quite work properly.
Only first character of print job gets sent to file.
A bit slow.
TFS doesn't work in NTSC mode - I need the OSFSC/OSFILE entry points on the US
OS (no they don't use vectors)
The cancel button in the file selector does the same thing as the OK button.


Hardware emulated
~~~~~~~~~~~~~~~~~

The 6502 processor - All instructions emulated. Timing is a bit suspect though
The 6845 CRTC      - Many effects, such as overscan, are emulated.
The Video ULA      - All modes emulated.
The System VIA     - Keyboard and sound emulated.
The User VIA       - Printer emulated.
8271 FDC           - Single disc, double sided, 40/80 tracks, read only. With
                     authentic noise.
tape filing system - Supports .inf and __CATALOG__ format.
Sound              - Tone generators are fine, but noise is a bit crap.
ADC                - No real joystick emulation yet though
Printer            - Prints to file printer.txt. Very buggy
Joystick           - With numlock off, the arrow keys control the stick and
                     insert is the button.


Hardware NOT emulated
~~~~~~~~~~~~~~~~~~~~~

serial port
AMX mouse
Tube
Econet
Low pass filter (muffles sound)

Running :
~~~~~~~~~

When you type b-em, you get the standard start-up text -

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
BASIC programs, and out of the 85 games I have tested so far, 79 (93%) are
playable.


Command line options :
~~~~~~~~~~~~~~~~~~~~~~

Most of these are obselete, due to the GUI, but they can be used as
`overrides'.

-frameskip: Follow this with the number of frames you want to skip, plus 1.
            The default value is 1. The following values are legal :
            2 - skip every other frame
            3 - skip every third frame
            4 - skip every fourth frame, etc
            Frameskip doesn't do anything when scanlines are enabled.
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
-scanline : Enables scanline drawing. Use this if you are trying to run a
            program that uses split mode/palette displays. It doesn't really
            help because of timing problems.
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
  f0            f1 (function keys are based on positioning rather than keycaps)
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


FAQ :
~~~~~

Q : Why doesn't B-em look right on my monitor?
A : If the screen is just off centre, then use the positioning controls. If
    not, then try Scitech Display Doctor, and see if that helps. If not, your
    monitor just won't handle B-em's 400x300 video mode, and there's not much
    you can do about it.

Q : Why have I got no sound?
A : B-em should work with 100% SoundBlaster compatibles, ESS Audiodrive,
    Ensoniq Soundscape, and Windoze $ound System. It will not work with cards
    not in that list (such as Creative's newer SoundBlasters, which aren't
    really compatible, or sadly the GUS), it will not work with non-100%
    compatibles with crappy drivers (such as the ones found in laptops) and it
    will not work if you have a bad BLASTER enviroment label.

Q : Why has B-em crashed?
A : There are three known circumstances where B-em will crash:
    1. When an application tries to play a sample or complicated sound effect
       (such as the start of Spy Hunter). This is not a bug in B-em, this is
       a bug in Allegro, which B-em uses for graphics and sound.
    2. When you run a model B app in model A mode.
    3. When you try to play Fortress.
    If it crashes for any other reason, email me.

Q : What is playsn?
A : playsn will play back any .sn file B-em generates.

Q : How do I contact you?
A : E-mail me at tommowalker@hotmail.com


Filing System (FS) :
~~~~~~~~~~~~~~~~~~~~

The FS is very primitive at the moment. It patches the OS to essentially
create a tape emulation. LOAD, *RUN, *LOAD and *CAT are currently supported.
*ENABLE, *OPT and *TAPE are ignored, everything else will probably bomb back
to DOS. If you are trying to load a file that doesn't exist, the emulator will
also bomb back to DOS. Entering the filename as "" will currently bomb out,
although I may have implemented that by the next version.

You can use both .INF format and __CATALOG__ format (at the same time).


Sound :
~~~~~~~

The sound is much better than 0.3. The noise is a bit hissy though, and
periodic noise sounds too 'strong'.


Menu :
~~~~~~

Press F11 to bring up the menu.

The menu options are:

Return - return to emulator.

Exit - Exit to DOS/Windoze/OS2/Linux/Desqview/whatever

Change disc image - change disc

Sound - enable/disable sound

Line drawing mode - switches drawing mode. In line drawing mode, frameskip is
                    not used, and it is slower, but it attempts to reproduce
                    split palette/mode effects.

TV standard - choose between PAL/NTSC beeb.

Model A - when ticked, emulates a model A.

Monochrome - switches between mono/colour TV/monitor

Disc drive noise - enables 5.25" disc drive noise, sampled from my drive with
                   a 99p microphone.

Frameskip - sets the number of frames skipped - the higher the number, the
            faster the emulation, but the animation gets jerkier.

Load snapshot - loads a previously saved snapshot
Save snapshot - saves a snapshot

Save screenshot - saves a screenshot

Start SN log - starts logging sound
Stop SN log  - stops logging sound


Tested games :
~~~~~~~~~~~~~~


Working perfectly (as far as I know) :
A&B Computing  Ripton

A+F Software   Chuckie Egg
A+F Software   Chuckie Egg II (not actual Chuckie Egg II, that game was never released on BBC AFAIK)
A+F Software   Cylon Attack
A+F Software   Painter

Acornsoft      Arcadians
Acornsoft      Carousel (better than arcade version IMO)
Acornsoft      Hopper
Acornsoft      Magic Mushrooms
Acornsoft      Monsters
Acornsoft      Planetoids
Acornsoft      Simcity
Acornsoft      Snapper
Acornsoft      Star Swarm

Alligata       Blagger
Alligate       Dambusters

Atarisoft      Donkey Kong Jr
Atarisoft      Sinistar

Bug Byte       Twin Kingdom Valley

Comsoft        SAS Commander

Elite          Commando (crap version)
Elite          Paperboy

Icon Software  Chrysalis
Icon Software  Contraption

IJK            Caterpiller

Impact Soft.   Zenon

Lothlorien     Battlezone 2000

Martech        Gisburne's Castle

Pace           Sorcery

Program Power  Bumble Bee
Program Power  Castle Quest
Program Power  Cybertron
Program Power  Danger UXB
Program Power  Dr. Who and the Mines of Terror
Program Power  Frenzy
Program Power  Ghouls
Program Power  Imogen
Program Power  Killer Gorilla
Program Power  Mr. EE

Soft. Invasion 3D Grand Prix

Soft. Projects Jet Set Willy (what I've spending hours revamping mode 1 for)
Soft. Projects Manic Miner   (unplayable, just like on a real beeb)
Soft. Projects Pyramid       (is it just me, or were all the sprites ripped
                              from Citadel?)

Superior Soft  Citadel
Superior Soft  Exile
Superior Soft  Overdrive
Superior Soft  Quest
Superior Soft  Repton
Superior Soft  Repton 2 (Speech crashes when sound is on)
Superior Soft  Repton 3 (Speech crashes when sound is on)
Superior Soft  Speech! (Crashes with sound on though, so a bit useless)
Superior Soft  Stryker's Run
Superior Soft  Stryker's Run Part 2 (Codename Droid)
Superior Soft  Tempest
Superior Soft  Thrust
Superior Soft  Wallaby
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

?????????????  Boffin
?????????????  Star Wars

Working imperfectly :
Aardvark Soft  FireTrack (Horrible scrolling)

Acornsoft      Elite (Split mode slightly off, but otherwise playable)
Acornsoft      Rocket Raid (Split mode problems)

Alligata       Olympic Decathlon (Palette split is a bit jumpy)

Bubble Bus     Starquake (Palette split a bit wrong)

Dr Soft        Phantom (Graphic errors)

Elite          Airwolf (Helicopter graphics go wrong sometimes)

Superior Soft  Repton Infinity (Frequent slowdowns)

US Gold        Bounty Bob Strikes Back (wrong colours)

Ultimate PTG   Sabre Wulf (Wrong colours)


Not working :
Aardvark Soft  Frak! (Crashes)
Aardvark Soft  Zalaga (Crashes)

Acornsoft      Revs (Relies on exact timing)

BBC Soft       Dr. Who The 1st Adventure (You can't control the game)

Virgin Games   Trench (Hangs)


Things for version 0.5 :
~~~~~~~~~~~~~~~~~~~~~~~~

Complete rewrite of 6502 emulation (in progress)
1770 FDC emulation (I just got docs)
Working printer emulation
Better sideways RAM emulation


Wanted :
~~~~~~~~

Replica 2 (good tape-to-disc menu prog).
Tube docs.
Working ACIA emulation.
Any ideas on how to get SN76489 samples working in Allegro's sound mixer.
Pinouts for the BBC tape port (and to know if it's safe to poke wires in
there - I can't be bothered to make a real cable).


Todo list :
~~~~~~~~~~~

Rewrite 6502 emulation (in progress)
Make the noise better
Implement joystick reading
Fix the ACIA emulation (can someone help with this?) - to be honest this isn't
likely to happen soon, as I don't really care about tapes.


Thanks to :
~~~~~~~~~~~

David Gilbert for writing Beebem and distributing the sources with it

James Fidell for writing Xbeeb and distributing the sources with it

Robert Schmidt for The BBC Lives!

DJ Delorie for DJGPP

Shawn Hargreaves for Allegro

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
To recompile the code, you will need DJGPP 2, GCC and Allegro 4. Note that
some of the source code is either David Gilbert's or James Fidell's, so you
should acknowledge that if you use it.


Appendix B : Transfering BBC files to the PC
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are several ways to do this, five of which are listed here :

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
Or, of course, you could just copy the games off the Internet, but that's
illegal (maybe).


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


Other BBC emulators :

Beebem - www.rickynet.net/beebem
The main BBC emulator at the moment. Runs pretty much everything (including
Revs), but is really slow.

pcBBC - can't remember the site address
One of the few non-free BBC emulators, this one has good compatibility, but
bad sound and costs money.

BeebInC - get it from The BBC Lives!, the site is down
Seemingly dead, this is one of my favourite emulators. Probably the most
compatible of the lot, but let down by poor sound (uses sine waves, not square
waves) and low refresh rate (25 fps instead of 50 fps).

Model B - get it from The BBC Lives!, the site is down
Also dead, this is the fastest BBC emulator I've seen. It's also one of the
first for DOS, although it suffers from low compatibility, bad sound, and
runs too fast on most machines.
