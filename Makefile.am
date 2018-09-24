include Makefile.am.coverage
ACLOCAL_AMFLAGS=-I m4

# building librb library

include_HEADERS = rb.h

lib_LTLIBRARIES = librb.la
librb_la_SOURCES = rb.c valid.h
librb_la_LDFLAGS = -version-info 1:0:0
librb_la_CFLAGS = -I$(top_srcdir)

# building test suite

check_PROGRAMS = tests
tests_SOURCES = rb.c tests.c mtest.h
tests_CFLAGS = -I$(top_srcdir) $(COVERAGE_CFLAGS)
tests_LDFLAGS = $(COVERAGE_LDFLAGS)

TESTS = $(check_PROGRAMS)
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

# static code analyzer

if ENABLE_ANALYZER

analyze_plists = rb.plist
MOSTLYCLEANFILES = $(analyze_plists)

$(analyze_plists): %.plist: %.c
	@echo "  CCSA  " $@
	@clang --analyze -I$(top_srcdir) -DHAVE_CONFIG_H $< -o $@

analyze: $(analyze_plists)

endif

# building website and manuals

SUBDIRS = man www
EXTRA_DIST = gen-download-page.sh man2html.sh readme.md tap-driver.sh

www:
	./gen-download-page.sh
	./man2html.sh
	make www -C www

.PHONY: www analyze tests librb.la
