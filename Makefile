# love make, make love
#
# This Works is placed under the terms of the Copyright Less License,
# see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.

BINS=json2sh

CFLAGS=-Wall -O3 -DVERSION='"$(shell dpkg-parsechangelog --show-field Version)"'

.PHONY:	love all
love all:	$(BINS)

.PHONY:	install
install:	$(BINS)
	install -DCt $(DESTDIR)/usr/bin/ $(BINS)

.PHONY:	clean
clean:
	rm -f $(BINS)

.PHONY:	deb
deb:
	ok="`git status --porcelain`" && [ -z "$$ok" ]
	gbp buildpackage --git-tag --git-retag
	mv debian/*.debhelper.log debian/*.substvars ..
	git clean -f -d

.PHONY:	dch
dch:
	ok="`git status --porcelain`" && [ -z "$$ok" ]
	gbp dch --commit --spawn-editor=always --distribution=unstable

# show the manual page
.PHONY: man
man:
	nroff -man debian/json2sh.1 | less

# In first terminal: make loop
# In a 2nd terminal: vim debian/json2sh.1
# Each time you save the edit, the first terminal refreshes after a second.
# End this by quickly pressing q and RETURN or just close the first terminal.
# needs https://github.com/hilbix/run-until-change
.PHONY: loop
loop:
	bash -c 'trap "stty sane" 0; while date && ! read -t0.2; do run-until-change debian/json2sh.1 -- make man; done'

