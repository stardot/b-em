VPATH = . resid-fp
CPP  = g++.exe
CC   = gcc.exe
WINDRES = windres.exe
CFLAGS = -O3 -march=i686 -ffast-math -fomit-frame-pointer -falign-loops -falign-jumps -falign-functions
OBJ = main.o 6502.o mem.o sysvia.o uservia.o video.o 6850acia.o uef.o csw.o serial.o 1770.o ssd.o 8271.o disc.o adf.o cmos.o sound.o soundopenal.o debugger.o tube.o 6502tube.o arm.o z80.o x86.o fdi.o fdi2raw.o resid.o adc.o ddnoise.o win.o 65816.o win-keydefine.o config.o win-catalogue.o tapenoise.o 32016.o i2ccmos.o ide.o savestate.o vidalleg.o vidopengl.o b-em.res
SIDOBJ = convolve.o envelope.o extfilt.o filter.o pot.o sid.o voice.o wave6581__ST.o wave6581_P_T.o wave6581_PS_.o wave6581_PST.o wave8580__ST.o wave8580_P_T.o wave8580_PS_.o wave8580_PST.o wave.o

LIBS =  -mwindows -lagl -lalleg -lz -lalut -lopenal32 -lgdi32 -lwinmm -lstdc++ -lopengl32 -lglu32

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
