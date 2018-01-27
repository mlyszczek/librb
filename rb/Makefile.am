lib_LTLIBRARIES = librb.la
librb_la_SOURCES = rb.c valid.h
librb_la_LDFLAGS = -version-info 3:2:1
librb_la_CFLAGS = -I$(top_srcdir)/include

check_PROGRAMS = tests
tests_SOURCES = rb.c tests.c mtest.h
tests_CFLAGS = -I$(top_srcdir)/include

TESTS = $(check_PROGRAMS)
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

# static code analyzer

if ENABLE_ANALYZER

analyze_plists = rb.plist
MOSTLYCLEANFILES = $(analyze_plists)

$(analyze_plists): %.plist: %.c
	@echo "  CCSA  " $@
	@clang --analyze -I.. $< -o $@

analyze: $(analyze_plists)

endif
