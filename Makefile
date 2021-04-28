.POSIX:
.PHONY: all install clean

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
DATADIR=$(PREFIX)/share
DOCDIR=$(DATADIR)/doc/samurai
MANDIR=$(DATADIR)/man
ALL_CFLAGS=$(CFLAGS) -std=c99 -Wall -Wextra -Wpedantic -Wno-unused-parameter
OBJ=\
	build.o\
	deps.o\
	env.o\
	graph.o\
	htab.o\
	log.o\
	parse.o\
	samu.o\
	scan.o\
	tool.o\
	tree.o\
	util.o
HDR=\
	arg.h\
	build.h\
	deps.h\
	env.h\
	graph.h\
	htab.h\
	log.h\
	parse.h\
	scan.h\
	tool.h\
	tree.h\
	util.h

all: samu

.c.o:
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

samu: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(OBJ): $(HDR)

install: samu samu.1
	mkdir -p $(DESTDIR)$(BINDIR)
	cp samu $(DESTDIR)$(BINDIR)/
	mkdir -p $(DESTDIR)$(DOCDIR)
	cp LICENSE $(DESTDIR)$(DOCDIR)
	cp README.md $(DESTDIR)$(DOCDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp samu.1 $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f samu $(OBJ)
