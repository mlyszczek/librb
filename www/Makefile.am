EXTRA_DIST = custom.css footer.in header.in index.in index.md

www:
	kursg -o out
	chmod a+rX out -R

clean-local:
	rm -fr out
	rm -fr manuals
	rm -f downloads.html
