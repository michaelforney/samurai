.POSIX:

PREFIX=/usr/local
MANDIR=$(PREFIX)/share/man
ALL_CFLAGS=$(CFLAGS) -std=c99 -Wall -Wextra -Wpedantic
OBJ=\
	build.o\
	deps.o\
	dyndep.o\
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

.c.o:
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

samu: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

$(OBJ): $(HDR)

install: samu samu.1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp samu $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp samu.1 $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f samu $(OBJ)

.PHONY: install clean
