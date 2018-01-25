AC_INIT([librb], [2.1.1], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile rb/Makefile include/Makefile man/Makefile www/Makefile])
AC_CONFIG_SRCDIR([configure.ac])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
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


AC_OUTPUT
