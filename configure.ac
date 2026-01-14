# configure.ac for B-em

AC_PREREQ([2.64])
AC_INIT([B-em],m4_esyscmd_s([utils/b-em-version-str.sh]),[thomas@xteddy.org])

AC_CONFIG_AUX_DIR([etc])
AC_CONFIG_SRCDIR([src/6502.c])
AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign subdir-objects])
AM_SILENT_RULES([yes])

AC_CANONICAL_HOST

# OS check
AC_MSG_CHECKING([host operating system])
AS_CASE([$host_os],
        [mingw*|windows*], [
            os_win=1
            AC_MSG_RESULT([Windows])
        ],
        [darwin*], [
            os_macos=1
            AC_MSG_RESULT([MacOS])
        ],
        [
            os_other=1
            AC_MSG_RESULT([Other])
        ]
)

AM_CONDITIONAL([OS_WIN], [test "$os_win" = "1"])
AM_CONDITIONAL([OS_MACOS], [test "$os_macos" = "1"])
AM_CONDITIONAL([OS_OTHER], [test "$os_other" = "1"])

# avoid default flags
CFLAGS="$CFLAGS"
CXXFLAGS="$CXXFLAGS"

# Set up the compiler in two different ways and say yes we may want to install.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O

AC_CHECK_TOOL([WINDRES], [windres], [:])

# Whether this is a released build or not.
ISRELEASED="no"

CC="$CC -std=gnu11"
CXX="$CXX -std=gnu++11"
CFLAGS_WARNINGS="-Wall"
AM_CFLAGS="$AM_CFLAGS $CFLAGS_WARNINGS"
AM_CXXFLAGS="$AM_CXXFLAGS $CFLAGS_WARNINGS"

AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug], [build debug executable])])
AS_IF([test "$enable_debug" = "yes"],[
    AM_CFLAGS="$AM_CFLAGS -Og -ggdb"
    AM_CXXFLAGS="$AM_CXXFLAGS -Og -ggdb"
    AM_CPPFLAGS="$AM_CPPFLAGS -D_DEBUG"
    AC_MSG_RESULT([yes])
],[
    AM_CFLAGS="$AM_CFLAGS -O3"
    AM_CXXFLAGS="$AM_CXXFLAGS -O3"
    AC_MSG_RESULT([no])
])
AM_CONDITIONAL([WANT_DEBUG], [test "$enable_debug" = "yes"])

AC_USE_SYSTEM_EXTENSIONS

# Checks for libraries.
AC_SEARCH_LIBS([sin], [m])
AC_SEARCH_LIBS([pthread_create], [pthread])
AC_SEARCH_LIBS([al_install_system], [allegro], [],
               [AC_MSG_FAILURE([The Allegro version 5 library is needed but not installed], 1)])
AC_SEARCH_LIBS([al_init_acodec_addon], [allegro_acodec], [], [AC_MSG_FAILURE([allegro_acodec not found], 1)])
AC_SEARCH_LIBS([al_install_audio], [allegro_audio], [], [AC_MSG_FAILURE([allegro_audio not found], 1)])
AC_SEARCH_LIBS([al_init_native_dialog_addon], [allegro_dialog], [], [AC_MSG_FAILURE([allegro_dialog not found], 1)])
AC_SEARCH_LIBS([al_init_font_addon], [allegro_font], [], [AC_MSG_FAILURE([allegro_font not found], 1)])
AC_SEARCH_LIBS([al_init_image_addon], [allegro_image], [], [AC_MSG_FAILURE([allegro_image not found], 1)])
AC_SEARCH_LIBS([al_init_primitives_addon], [allegro_primitives], [], [AC_MSG_FAILURE([allegro_primitives not found], 1)])
AC_SEARCH_LIBS([gzopen], [zlib z], [], [AC_MSG_FAILURE([zlib not found], 1)])
AC_SEARCH_LIBS([jack_port_register], [jack jack2])
AC_SEARCH_LIBS([snd_rawmidi_open], [asound])
# Can't use AC_SEARCH_LIBS for win32 api due to __stdcall
AS_IF([test "$os_win" = "1"],[LIBS="-lwinmm $LIBS"])

# Checks for header files.
AC_HEADER_STDBOOL
AC_HEADER_DIRENT
AC_CHECK_HEADERS([inttypes.h stdint.h unistd.h])
AC_CHECK_HEADER([allegro5/allegro.h], [],
                [AC_MSG_FAILURE([The Allegro version 5 library is needed but not installed], 1)], [])
AC_CHECK_HEADERS([jack/jack.h alsa/asoundlib.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([atexit floor ftruncate memmove memset mkdir pow rmdir sqrt stpcpy strcasecmp strchr strcspn strdup \
                strerror strncasecmp strpbrk strrchr strspn strstr strtol strtoul])

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

echo
echo "Configured version: $VERSION"
echo
