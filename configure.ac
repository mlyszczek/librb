AC_INIT([rb], [2.0.1], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile rb/Makefile include/Makefile man/Makefile])
AC_CONFIG_SRCDIR([configure.ac])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_CONFIG_HEADERS([config.h])

AC_CHECK_FUNCS([memcpy malloc free strcpy strtok])

###
# --enable-threads
#

AC_ARG_ENABLE([threads],
    AS_HELP_STRING([--enable-threads], [Enable thread awarness]),
    [], [enable_threads="yes"])

AS_IF([test "x$enable_threads" = "xyes"],
[
    AX_PTHREAD
    AC_DEFINE([ENABLE_THREADS], [1], [Enable thread awarness])
    AC_CHECK_FUNCS([pthread_mutex_lock pthread_mutex_unlock pthread_cond_wait \
        pthread_cond_signal pthread_mutex_init pthread_mutex_destroy \
        pthread_cond_init pthread_cond_destroy],,
            AC_MSG_ERROR(not found, needed by --enable-threads))
],
# else
[
    enable_threads="no"
])


AC_OUTPUT
