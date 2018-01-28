ACLOCAL_AMFLAGS=-I m4

SUBDIRS = include rb man www
EXTRA_DIST = gen-download-page.sh man2html.sh readme.md

www:
	./gen-download-page.sh
	./man2html.sh
	make www -C www

analyze:
	make analyze -C rb

.PHONY: www
