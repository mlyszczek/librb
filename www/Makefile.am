EXTRA_DIST = custom.css footer.in header.in index.in index.md

www:
	kursg -o out
	chmod a+rX out -R

clean-local:
	$(RM) -r out
	$(RM) -r manuals
	$(RM) downloads.html
