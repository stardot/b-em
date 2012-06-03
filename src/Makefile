VPATH = . resid-fp
CPP  = g++.exe
CC   = gcc.exe
WINDRES = windres.exe
CFLAGS = -O3
OBJ = 6502.o 6502tube.o 32016.o 65816.o acia.o adc.o adf.o arm.o cmos.o compact_joystick.o compactcmos.o config.o csw.o ddnoise.o debugger.o disc.o fdi2raw.o fdi.o i8271.o ide.o keyboard.o main.o mem.o model.o mouse.o pal.o savestate.o serial.o sn76489.o sound.o soundopenal.o ssd.o sysvia.o tape.o tapenoise.o tube.o uef.o uservia.o via.o vidalleg.o video.o wd1770.o win.o win-catalogue.o win-keydefine.o x86.o z80.o resid.o b-em.res
SIDOBJ = convolve.o envelope.o extfilt.o filter.o pot.o sid.o voice.o wave6581__ST.o wave6581_P_T.o wave6581_PS_.o wave6581_PST.o wave8580__ST.o wave8580_P_T.o wave8580_PS_.o wave8580_PST.o wave.o

LIBS =  -mwindows -lalleg -lz -lalut -lopenal32 -lgdi32 -lwinmm -lstdc++

b-em.exe: $(OBJ) $(SIDOBJ)
	$(CC) $(OBJ) $(SIDOBJ) -o "b-em.exe" $(LIBS)

all : b-em.exe

clean :
	del *.o
	del *.exe
	del *.res

%.o : %.c
	$(CC) $(CFLAGS) -c $<

%.o : %.cc
	$(CPP) $(CFLAGS) -c $<

b-em.res: b-em.rc
	$(WINDRES) -i b-em.rc --input-format=rc -o b-em.res -O coff 