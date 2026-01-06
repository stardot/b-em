# Makefile.am for B-em

bin_PROGRAMS = b-em m7makechars hdfmt sdf2imd bsnapdump disptrace fsmap hdresize
noinst_PROGRAMS = jstest gtest
noinst_SCRIPTS = ../b-em$(EXEEXT)
CLEANFILES = $(noinst_SCRIPTS)

if !OS_WIN
# requires fnmatch
noinst_PROGRAMS += wildtest
endif

.rc.o:
	$(WINDRES) -o $@ $<

../b-em$(EXEEXT): b-em$(EXEEXT)
	cp b-em$(EXEEXT) ..

amrefresh:

if OS_WIN
b_em_LDFLAGS = $(AM_LDFLAGS) -mwindows
endif

if OS_MACOS
LIBS_ALLEGRO += -lallegro_main
LIBS_ALLEGRO_FULL += -lallegro_main
endif

# The following flags are used by PiTubeDirect Co Pros:
#                BEM - fixes for compilation in B-Em envorinment
#   INCLUDE_DEBUGGER - include the cpu_debug implementation
# USE_MEMORY_POINTER - do not assume Co Pro memory starts at address 0

b_em_CPPFLAGS = $(AM_CPPFLAGS) -DBEM -DINCLUDE_DEBUGGER -DUSE_MEMORY_POINTER -DMODET -DMODE32 -DBEEBEM

b_em_LDADD = $(LIBS_ZLIB) $(LIBS_ALLEGRO_FULL) $(LIBS_MIDI)

b_em_SOURCES = \
    6502.c \
    6502debug.c \
    6502tube.c \
    65816.c \
    6809tube.c \
    acia.c \
    adc.c \
    arm.c \
    cmos.c \
    compact_joystick.c \
    compactcmos.c \
    compat_wrappers.c \
    config.c \
    copro-pdp11.c \
    csw.c \
    ddnoise.c \
    debugger.c \
    debugger_symbols.cpp \
    disc.c \
    fdi.c \
    fdi2raw.c \
    fullscreen.c \
    gui-allegro.c \
    hfe.c \
    i8271.c \
    ide.c \
    imd.c \
    joystick.c \
    keyboard.c \
    keydef-allegro.c \
    led.c \
    logging.c \
    main.c \
    map.c \
    mc68000tube.c \
    mem.c \
    mmb.c \
    mmccard.c \
    model.c \
    mouse.c \
    music2000.c \
    music4000.c \
    music5000.c \
    pal.c \
    paula.c \
    resid.cc \
    savestate.c \
    scsi.c \
    sdf-acc.c \
    sdf-geo.c \
    serial.c \
    sn76489.c \
    sound.c \
    sprow.c \
    sysacia.c \
    sysvia.c \
    tape.c \
    tapecat-allegro.c \
    tapenoise.c \
    textsave.c \
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
    z80.c \
    z80dis.c \
    ARMulator/armdis.cpp \
    ARMulator/armemu.c \
    ARMulator/arminit.c \
    ARMulator/armmem.c \
    ARMulator/armsupp.c \
    ARMulator/thumbemu.c \
    darm/armv7-tbl.c \
    darm/armv7.c \
    darm/darm-tbl.c \
    darm/darm.c \
    darm/thumb-tbl.c \
    darm/thumb.c \
    darm/thumb2-decoder.c \
    darm/thumb2-tbl.c \
    darm/thumb2.c \
    mc6809nc/mc6809_debug.c \
    mc6809nc/mc6809_dis.c \
    mc6809nc/mc6809nc.c \
    musahi/m68kcpu.c \
    musahi/m68kdasm.c \
    musahi/m68kops.c \
    NS32016/32016.c \
    NS32016/32016_debug.c \
    NS32016/Decode.c \
    NS32016/mem32016.c \
    NS32016/NSDis.c \
    NS32016/Profile.c \
    NS32016/Trap.c \
    pdp11/pdp11.c \
    pdp11/pdp11_debug.c \
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

if OS_WIN
b_em_SOURCES += win.c midi-windows.c
b_em_SOURCES += b-em.rc
else
b_em_SOURCES += linux.c midi-linux.c
endif

m7makechars_SOURCES = m7makechars.c

hdfmt_SOURCES = hdfmt.c

jstest_SOURCES = jstest.c

jstest_LDADD = $(LIBS_ALLEGRO)

gtest_SOURCES = sdf-gtest.c sdf-geo.c

sdf2imd_SOURCES = sdf2imd.c sdf-geo.c

bsnapdump_SOURCES = bsnapdump.c

bsnapdump_LDADD = $(LIBS_ZLIB)

disptrace_SOURCES = disptrace.c

fsmap_SOURCES = fsmap.c

hdresize_SOURCES = hdresize.c

wildtest_SOURCES = wildtest.c
