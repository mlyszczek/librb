# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?= -j auto
SPHINXBUILD   ?= sphinx-build
SOURCEDIR     = .
BUILDDIR      = _build

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile

downloads.rst: gen-download-page.sh
	./gen-download-page.sh > $@

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
html : Makefile downloads.rst
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

man: Makefile
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
	@echo fix broken tables
	sed -i 's/^center;$$/box center;/' $(BUILDDIR)/man/*

# sphinx-build clean does not remove _build dir
clean-local:
	rm -rf _build

man_MANS = \
_build/man/rb_cleanup.3 \
_build/man/rb_clear.3 \
_build/man/rb_count.3 \
_build/man/rb_destroy.3 \
_build/man/rb_discard.3 \
_build/man/rb_init.3 \
_build/man/rb_new.3 \
_build/man/rb_overview.7 \
_build/man/rb_peek_size.3 \
_build/man/rb_read.3 \
_build/man/rb_read_claim.3 \
_build/man/rb_read_commit.3 \
_build/man/rb_recv.3 \
_build/man/rb_send.3 \
_build/man/rb_set_hard_max_count.3 \
_build/man/rb_space.3 \
_build/man/rb_stop.3 \
_build/man/rb_write.3 \
_build/man/rb_write_claim.3 \
_build/man/rb_write_commit.3

EXTRA_DIST = $(man_MANS) static/custom.css gen-download-page.sh

$(man_MANS) &:
	@$(SPHINXBUILD) -M man "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
	@echo fix broken tables
	sed -i 's/^center;$$/box center;/' $(BUILDDIR)/man/*
