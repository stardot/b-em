# Teletext Fonts
This directory contains a small number of teletext font files for B-Em
to load at run time.  This gives a choice of which font to use so a
user may choose authenticity or something that looks better.

## Where the Fonts Came From
### saa5050.fnt
This originates from BeebEm where it is stored, in a different format,
as the file teletext.fnt.  This was determined to be an accurate
capture of the pixels produced by the SAA5050 in the Stardot forum
after the 'W' was corrected.

This uses graphics generated directly into 16x20 format as the SAA5050
does not store the sixel graphics but generates them on the fly and
they are therefore unconstrained by the 5x9 character grid.  Like the
real SAA5050, the separated graphics has the blank space on the left
and bottom edges.

## basicsdl.fnt
This comes from Richard Russell's BBC BASIC for SDL.  As far as I know
this is a tided up version of the Mode 7 font used by Acorn in their
ARM-based machines with with changes to improve the appearance.

This uses graphics generated directly into 16x20 format as the SAA5050
does not store the sixel graphics but generates them on the fly and
they are therefore unconstrained by the 5x9 character grid.  Unlike the
real SAA5050, the separated graphics has the blank space balanced
between top and bottom and left and right.

## brandy.fnt
This comes from Matrix Brandy BASIC and is also derived from font used
by Acorn on the ARM-based machines, but with a different set of changes.

This uses graphics generated directly into 16x20 format as the SAA5050
does not store the sixel graphics but generates them on the fly and
they are therefore unconstrained by the 5x9 character grid.  Unlike the
real SAA5050, the separated graphics has the blank space balanced
between top and bottom and left and right.

## original.fnt
This is the font that was generated on-the-fly by B-Em.  It does not do
the diagonal rounding exactly as the SAA5050 does and therefore has
narrow horizontals.  It also generates the sixel graphics from
definitions stored as 6x10 pixels and the result is therefore uneven.
This is contrary to how the real SAA5050 generates the sixels via
some logic.

## File Format
Each file starts with a "magic number" to confirm it is a B-Em font
file and which version of the format.  Currently this is the 8 bytes
BEMTTX01

Next are the width and height of the font in that order, each stored
as a single byte.  The font would usually be expected to be 16x20.
B-Em can cope with different widths, though it may look rather odd.

Next is the name, stored as a one-byte length followed by the
characters of the name itself.

The pixel data follows.  Pixels are stored in three banks of 96
characters with no separator.  The first character stored in each
bank is ASCII space so these are the characters with codes 0x20 to
0x7f (32 to 127) inclusive.

The first bank are for the textual characters that are displayed when
graphics is not in effect.  The second bank contains the set of
characters used for contiguous graphics and the third bank the set used
for separated graphics.  The 32 character block containing the capital
letter is duplicated in each bank to implement "blast through", though
you could put any set of glyphs there and there would be used,
contrary to the teletext specification.

Pixels are grey scale and can have a value between 0 and 15 and two
pixels are multiplexed into each byte, one in the upper nibble (for even
fields of the interlaced display) and one in the lower nibble (for the
odd fields).  The order of bytes is starting at the left of line zero
or the character, working right until the width (usually 16) is reached
then on to the next line.  There are ten stored lines for a 16x20 font
as the even lines have their values in the upper nibbles as mentioned
earlier.  After that the next byte is for the next character.

Nothing after the pixel data is read so this could be used for
attaching any notes.
