# =============================================================================
#                       ░█▀▄░█░█░▀█▀░█░░░█▀▄░▀█▀░█▀█░█▀▀
#                       ░█▀▄░█░█░░█░░█░░░█░█░░█░░█░█░█░█
#                       ░▀▀░░▀▀▀░▀▀▀░▀▀▀░▀▀░░▀▀▀░▀░▀░▀▀▀
# =============================================================================
include Makefile.am.coverage
ACLOCAL_AMFLAGS=-I m4

# =============================================================================
#                                ╻  ╻┏┓ ┏━┓┏┓
#                                ┃  ┃┣┻┓┣┳┛┣┻┓
#                                ┗━╸╹┗━┛╹┗╸┗━┛
# =============================================================================
# building librb library
nodist_include_HEADERS = rb.h

lib_LTLIBRARIES = librb.la
librb_la_SOURCES = rb.c
librb_la_LDFLAGS = -version-info 3:0:1
#       .------------------------' | |
#       |        .-----------------' |
#       |        |     .-------------'
#    current:revision:age
#       |        |     `- increment if interfaces have been added set to
#       |        |        zero if interfaces have been removed  or changed
#       |        `- increment if source code has changed
#       |           set to zero if current is incremented
#       `- increment if interfaces have been added, removed or changed
librb_la_CFLAGS = -I$(top_srcdir)

# =============================================================================
#                               ╺┳╸┏━╸┏━┓╺┳╸┏━┓
#                                ┃ ┣╸ ┗━┓ ┃ ┗━┓
#                                ╹ ┗━╸┗━┛ ╹ ┗━┛
# =============================================================================
# building test suite
check_PROGRAMS = tests
tests_SOURCES = rb.c tests.c mtest.h
tests_CFLAGS = -I$(top_srcdir) $(COVERAGE_CFLAGS)
tests_LDFLAGS = $(COVERAGE_LDFLAGS)

TESTS = $(check_PROGRAMS)
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh

# =============================================================================
#                           ┏━╸╻ ╻┏━┓┏┳┓┏━┓╻  ┏━╸┏━┓
#                           ┣╸ ┏╋┛┣━┫┃┃┃┣━┛┃  ┣╸ ┗━┓
#                           ┗━╸╹ ╹╹ ╹╹ ╹╹  ┗━╸┗━╸┗━┛
# =============================================================================
EXTRA_PROGRAMS = event_loop \
	posix_network_parser \
	prod_cons_threads \
	simple_read_write

common_src = rb.c examples/common.c
event_loop_SOURCES = $(common_src) examples/event-loop.c
posix_network_parser_SOURCES = $(common_src) examples/posix-network-parser.c
prod_cons_threads_SOURCES = $(common_src) examples/prod-cons-threads.c
simple_read_write_SOURCES = $(common_src) examples/simple-read-write.c

examples: $(EXTRA_PROGRAMS)
.PHONY: examples

# =============================================================================
#                       ░█▀█░█▀█░█▀█░█░░░█░█░▀▀█░█▀▀░█▀▄
#                       ░█▀█░█░█░█▀█░█░░░░█░░▄▀░░█▀▀░█▀▄
#                       ░▀░▀░▀░▀░▀░▀░▀▀▀░░▀░░▀▀▀░▀▀▀░▀░▀
# =============================================================================
if ENABLE_ANALYZER

analyze_plists = rb.plist
MOSTLYCLEANFILES = $(analyze_plists)

$(analyze_plists): %.plist: %.c
	@echo "  CCSA  " $@
	@clang --analyze -I$(top_srcdir) -DHAVE_CONFIG_H $< -o $@

analyze: $(analyze_plists)
.PHONY: analyze

endif

# =============================================================================
#         ░█▄█░█▀█░█▀█░█░█░█▀█░█░░░█▀▀░░░█▀█░█▀█░█▀▄░░░█░█░▀█▀░█▄█░█░░
#         ░█░█░█▀█░█░█░█░█░█▀█░█░░░▀▀█░░░█▀█░█░█░█░█░░░█▀█░░█░░█░█░█░░
#         ░▀░▀░▀░▀░▀░▀░▀▀▀░▀░▀░▀▀▀░▀▀▀░░░▀░▀░▀░▀░▀▀░░░░▀░▀░░▀░░▀░▀░▀▀▀
# =============================================================================
man3_MANS = \
	man/rb_cleanup.3 \
	man/rb_clear.3 \
	man/rb_count.3 \
	man/rb_destroy.3 \
	man/rb_discard.3 \
	man/rb_init.3 \
	man/rb_new.3 \
	man/rb_peek_size.3 \
	man/rb_read.3 \
	man/rb_read_claim.3 \
	man/rb_read_commit.3 \
	man/rb_recv.3 \
	man/rb_send.3 \
	man/rb_set_hard_max_count.3 \
	man/rb_space.3 \
	man/rb_stop.3 \
	man/rb_write.3 \
	man/rb_write_claim.3 \
	man/rb_write_commit.3
man7_MANS=man/rb_overview.7

if ENABLE_DOCS
SPHINXOPTS    ?= -j auto
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     = $(top_srcdir)/www
BUILDDIR      = sphinx

help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
.PHONY: help

www/downloads.rst: www/gen-download-page.sh
	./gen-download-page.sh > $@

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
html: Makefile www/downloads.rst
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
.PHONY: html

doc: Makefile
	$(SPHINXBUILD) -M man "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
	@echo fix broken tables
	sed -i 's/^center;$$/box center;/' $(BUILDDIR)/man/*
.PHONY: doc

# sphinx-build clean does not remove _build dir
clean-local:
	rm -rf $(BUILDDIR)

$(man3_MANS) $(man7_MANS) &:
	@$(SPHINXBUILD) -M man "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
	@echo fix broken tables
	sed -i 's/^center;$$/box center;/' $(BUILDDIR)/man/*
	mkdir -p man
	cp $(BUILDDIR)/man/* man
endif

EXTRA_DIST = $(man3_MANS) $(man7_MANS) readme.rst tap-driver.sh \
	examples/common.h

# clean targets
clean_targets=

if HAVE_GCOV
clean_targets += clean-gcov
endif

clean-local: $(clean_targets)

.PHONY: analyze tests librb.la
