AC_INIT([librb], [2.1.1], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES(rb.h)

AC_CHECK_HEADERS([assert.h errno.h stddef.h stdlib.h string.h])

# gcov coverage reporting
m4_include([m4/gcov.m4])
AC_TDD_GCOV
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_CXXFLAGS)
AC_SUBST(COVERAGE_LDFLAGS)

# --enable-threads
AC_ARG_ENABLE([threads],
    AS_HELP_STRING([--enable-threads], [Enable thread awarness]),
    [], [enable_threads="yes"])

ENABLE_THREADS=0
AS_IF([test "x$enable_threads" = "xyes"],
[
    AX_PTHREAD
    LIBS="$PTHREAD_LIBS $LIBS"
    CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
    CC="$PTHREAD_CC"
    AC_CHECK_HEADERS([fcntl.h sys/socket.h sys/time.h])

    ENABLE_THREADS=1
    AC_DEFINE([ENABLE_THREADS], [1], [Enable thread awarness])
],
# else
[
    enable_threads="no"
])
AC_SUBST(ENABLE_THREADS)

# --enable-docs
AC_ARG_ENABLE([docs],
	AS_HELP_STRING([--enable-docs], [Build man pages and html docs]),
    [enable_docs="yes"], [enable_docs="no"])
AM_CONDITIONAL([ENABLE_DOCS], [test "x$enable_docs" = "xyes"])
#AS_IF([test "x$enable_docs" = "xyes"],
#[
#    AC_DEFINE([BUILD_DOCS], [1], [Build man pages and html docs])
#],
## else
#[
#    enable_docs="no"
#])


# --enable-trace
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

# --enable-analyzer
AC_ARG_ENABLE([analyzer],
    AS_HELP_STRING([--enable-analyzer], [Enable static code analyzer]),
    [enable_analyzer="yes"], [enable_analyzer="no"])
AM_CONDITIONAL([ENABLE_ANALYZER], [test "x$enable_analyzer" = "xyes"])

AC_OUTPUT

echo
echo "librb compilation configuration summary"
echo
echo "enable threads.........: $enable_threads"
echo "enable trace messages..: $enable_trace"
echo "build docs.............: $enable_docs"
