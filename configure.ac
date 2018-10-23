AC_INIT([librb], [1.1.0], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile man/Makefile www/Makefile examples/Makefile])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])

AC_CHECK_HEADERS([assert.h errno.h stddef.h stdlib.h string.h])


##
# solaris needs to link agains libsocket
#


AC_SEARCH_LIBS([socket], [socket])


###
# gcov coverage reporting
#


m4_include([m4/gcov.m4])
AC_TDD_GCOV
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_CXXFLAGS)
AC_SUBST(COVERAGE_LDFLAGS)


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
    AC_CHECK_HEADERS([fcntl.h sys/socket.h sys/time.h])

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
    AC_CHECK_HEADERS(unistd.h sys/select.h)
],
# else
[
    enable_posix_calls="no"
])

###
# --enable-trace
#

AC_ARG_ENABLE([trace],
    AS_HELP_STRING([--enable-trace], [Enable trace logging from librb]),
    [enable_trace="yes"], [enable_trace="no"])
AS_IF([test "x$enable_trace" = "xyes"],
[
    AC_DEFINE([TRACE_LOG], [1], [Enable trace logging from librb])
    AC_CHECK_HEADERS([stdio.h syscall.h unistd.h])
],
# else
[
    enable_trace="no"
])

###
# --enable-analyzer
#

AC_ARG_ENABLE([analyzer],
    AS_HELP_STRING([--enable-analyzer], [Enable static code analyzer]),
    [enable_analyzer="yes"], [enable_analyzer="no"])
AM_CONDITIONAL([ENABLE_ANALYZER], [test "x$enable_analyzer" = "xyes"])


AC_OUTPUT

echo
echo "librb compilation configuration summary"
echo
echo "enable threads.........: $enable_threads"
echo "enable posix calls.....: $enable_posix_calls"
echo "enable trace messages..: $enable_trace"
