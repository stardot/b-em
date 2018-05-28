# Makefile.am for B-em

bin_PROGRAMS = b-em jstest
noinst_SCRIPTS = ../b-em
CLEANFILES = $(noinst_SCRIPTS)

../b-em: b-em
	cp b-em ..

amrefresh:

# The following flags are used by PiTubeDirect Co Pros:
#                BEM - fixes for compilation in B-Em envorinment
#   INCLUDE_DEBUGGER - include the cpu_debug implementation
# USE_MEMORY_POINTER - do not assume Co Pro memory starts at address 0

b_em_CFLAGS = $(allegro_CFLAGS) -DBEM -DINCLUDE_DEBUGGER -DUSE_MEMORY_POINTER

# workaround for Win32 Allegro, which has `allegro-config' missing
if OS_WIN
b_em_LDADD = -lallegro_audio -lallegro_acodec -lallegro_primitives -lallegro_dialog -lallegro_image -lallegro_font -lallegro -lz -lm
else
b_em_LDADD = -lallegro_audio -lallegro_acodec -lallegro_primitives -lallegro_dialog -lallegro_image -lallegro_font -lallegro_main -lallegro -lz -lm -lpthread
endif

b_em_SOURCES = \
	6502.c \
	6502debug.c \
	6502tube.c \
	65816.c \
	NS32016/32016.c \
	NS32016/32016_debug.c \
	NS32016/Decode.c \
	NS32016/NSDis.c \
	NS32016/Profile.c \
	NS32016/Trap.c \
	NS32016/mem32016.c \
	z80.c \
	z80dis.c \
	acia.c \
	adc.c \
	arm.c \
	darm/darm.c \
	darm/darm-tbl.c \
	darm/armv7.c \
	darm/armv7-tbl.c \
	darm/thumb.c \
	darm/thumb-tbl.c \
	darm/thumb2.c \
	darm/thumb2-decoder.c \
	darm/thumb2-tbl.c \
	cmos.c \
	compact_joystick.c \
	compactcmos.c \
	compat_wrappers.c \
	config.c \
	csw.c \
	ddnoise.c \
	debugger.c \
	disc.c fdi.c \
	fdi2raw.c \
	gui-allegro.c\
	i8271.c \
	ide.c \
	joystick.c \
	keyboard.c \
	keydef-allegro.c \
	linux.c \
	logging.c \
	main.c \
	mem.c \
	model.c \
	mouse.c \
	midi-linux.c \
	music2000.c \
	music4000.c \
	music5000.c \
	pal.c\
	resid.cc \
	savestate.c \
	scsi.c \
	sdf.c \
	serial.c \
	sn76489.c \
	sound.c \
	sysacia.c \
	sysvia.c \
	tape.c \
	tapecat-allegro.c \
	tapenoise.c \
	tube.c \
	uef.c \
	uservia.c \
	vdfs.c \
	via.c \
	vidalleg.c \
	video.c \
	wd1770.c \
	x86.c \
	x86dasm.c \
	resid-fp/convolve-sse.cc \
	resid-fp/convolve.cc \
	resid-fp/envelope.cc \
	resid-fp/extfilt.cc \
	resid-fp/filter.cc \
	resid-fp/pot.cc \
	resid-fp/sid.cc \
	resid-fp/voice.cc \
	resid-fp/wave.cc \
	resid-fp/wave6581_PST.cc \
	resid-fp/wave6581_PS_.cc \
	resid-fp/wave6581_P_T.cc \
	resid-fp/wave6581__ST.cc \
	resid-fp/wave8580_PST.cc \
	resid-fp/wave8580_PS_.cc \
	resid-fp/wave8580_P_T.cc \
	resid-fp/wave8580__ST.cc

if NO_TSEARCH
b_em_SOURCES += tsearch.c
endif

jstest_SOURCES = jstest.c

jstest_LDADD = -lallegro -lallegro_main
