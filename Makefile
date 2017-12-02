# love make, make love
#
# This Works is placed under the terms of the Copyright Less License,
# see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.

BINS=json2sh

CFLAGS=-Wall -O3

.PHONY:	love all
love all:	$(BINS)

.PHONY:	install
install:	$(BINS)
	install -C $(BINS) /usr/local/bin

.PHONY:	clean
clean:
	rm -f $(BINS)

