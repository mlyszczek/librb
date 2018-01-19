ACLOCAL_AMFLAGS=-I m4

SUBDIRS = include rb man
EXTRA_DIST = tap-driver.sh

www:
	./man2html.sh
	make -C www

.PHONY: www
