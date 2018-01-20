ACLOCAL_AMFLAGS=-I m4

SUBDIRS = include rb man www
EXTRA_DIST = tap-driver.sh

www:
	./gen-download-page.sh
	./man2html.sh
	make www -C www

.PHONY: www
