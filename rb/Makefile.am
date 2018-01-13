lib_LTLIBRARIES = librb.la
librb_la_SOURCES = rb.c
librb_la_LDFLAGS = -version-info 2:1:0
librb_la_CFLAGS = -I$(top_srcdir)/include

check_PROGRAMS = tests
tests_SOURCES = rb.c tests.c mtest.h
tests_CFLAGS = -I$(top_srcdir)/include
tests_LDFLAGS = $(PTHREAD_CFLAGS)

TESTS = $(check_PROGRAMS)
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh
