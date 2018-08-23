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

