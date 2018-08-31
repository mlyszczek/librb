AC_INIT([librb], [1.0.0], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile man/Makefile www/Makefile])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])

###
# --enable-threads
#

AC_ARG_ENABLE([threads],
    AS_HELP_STRING([--enable-threads], [Enable thread awarness]),
    [], [enable_threads="yes"])

AS_IF([test "x$enable_threads" = "xyes"],
[
    AX_PTHREAD
    LIBS="$PTHREAD_LIBS $LIBS"
    CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
    CC="$PTHREAD_CC"

    AC_DEFINE([ENABLE_THREADS], [1], [Enable thread awarness])
],
# else
[
    enable_threads="no"
])

###
# --enable-posix-calls
#

AC_ARG_ENABLE([posix-calls],
    AS_HELP_STRING([--enable-posix-calls], [Enable posix helper functions]),
    [], [enable_posix_calls="yes"])

AS_IF([test "x$enable_posix_calls" = "xyes"],
[
    AC_DEFINE([ENABLE_POSIX_CALLS], [1], [Enable posix helper functions])
    AC_CHECK_HEADERS(unistd.h)
    AC_CHECK_FUNCS([read],,
        AC_MSG_ERROR(not found, needed by --enable-posix-calls))
],
# else
[
    enable_posix_calls="no"
])


###
# --enable-analyzer
#

AC_ARG_ENABLE([analyzer],
    AS_HELP_STRING([--enable-analyzer], [Enable static code analyzer]),
    [enable_analyzer="yes"], [enable_analyzer="no"])
AM_CONDITIONAL([ENABLE_ANALYZER], [test "x$enable_analyzer" = "xyes"])

AC_OUTPUT
