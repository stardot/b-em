# configure.ac for B-em

AC_INIT([B-em],
	m4_esyscmd_s([utils/b-em-version-str.sh]),
	[thomas@xteddy.org])

AC_CONFIG_AUX_DIR(etc)
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

AC_CANONICAL_HOST

# Set up the compiler in two different ways and say yes we may want to install.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O

# Whether this is a released build or not.
ISRELEASED="no"

AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE(debug,
	      AC_HELP_STRING([--enable-debug], [build debug executable]))

CF_WARNINGS="-Wall -Wno-format-security"
CFLAGS="$CFLAGS $CF_WARNINGS -std=gnu99 -D_GNU_SOURCE"
if test "$enable_debug" = "yes"; then
   CFLAGS="$CFLAGS -O0 -ggdb -D_DEBUG"
   LDFLAGS="$LDFLAGS -L/usr/local/lib"
   AC_MSG_RESULT([yes])
else
   CFLAGS="$CFLAGS -O3"
   LDFLAGS="$LDFLAGS -L/usr/local/lib"
   AC_MSG_RESULT([no])
fi

# Check if the platform is anything other than Windows, as we want to check
# for Allegro if not.

# Do not run test for Allegro with Win32/MinGW version, as binary builds have
# `allegro-config' missing.
# NOTE: For the following Autoconf macro to be supported, you need to extract
#       allegro.m4 from the DOS/Windows Allegro sources (the file is contained
#       in `misc') and copy it to this directory or MSYS's `/share/aclocal'.
AC_MSG_CHECKING(platform...)
if test "$host_os" != "win"; then
   AM_PATH_ALLEGRO(, ,
	AC_MSG_ERROR(building B-em requires Allegro to be installed))
fi

AM_CONDITIONAL(OS_WIN, test "$host_os" = "win")

AC_CHECK_LIB(z,
	     gzopen,
	     ,
	     AC_MSG_ERROR("You need to install the zlib library.")
)
AC_CHECK_LIB(openal,
	     alGetError,
	     ,
	     AC_MSG_ERROR("You need to install the OpenAL library.")
)
AC_CHECK_LIB(alut,
	     alutInit,
	     ,
	     AC_MSG_ERROR("You need to install the ALUT library.")
)

# Check for asprintf -- if it's not defined, use our version.
AC_CHECK_FUNC(asprintf, found_asprintf=yes, found_asprintf=no)
if test "x$found_asprintf" = xyes; then
	AC_DEFINE(HAVE_ASPRINTF)
fi

# Check tsearch for tdestroy and include that for non-GNU systems.
AC_CHECK_FUNC(tdestroy, found_tdestroy=yes, found_tdestroy=no)
if test "x$found_tdestroy" = xyes; then
	AC_DEFINE(HAVE_TDESTROY)
fi

AM_CONDITIONAL(NO_TSEARCH, [test x"$found_tdestroy" = xno ])

AC_CHECK_FUNC(stpcpy, found_stpcpy=yes, found_stpcpy=no)
if test "x$found_stpcpy" = xyes; then
	AC_DEFINE(HAVE_STPCPY)
fi

# Checks for Linux MIDI implementations.
AC_CHECK_HEADERS(jack/jack.h alsa/asoundlib.h)

AC_OUTPUT([Makefile src/Makefile])

echo
echo "Configured version: $VERSION"
echo
