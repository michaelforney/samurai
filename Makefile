.POSIX:

PREFIX=/usr/local
MANDIR=$(PREFIX)/share/man
ALL_CFLAGS=$(CFLAGS) -Wall -Wextra -std=c99 -pedantic
OBJ=\
	build.o\
	env.o\
	deps.o\
	graph.o\
	htab.o\
	log.o\
	parse.o\
	platform.o\
	samurai.o\
	scan.o\
	tool.o\
	tree.o\
	util.o

.c.o:
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

samu: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

install: samu samu.1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp samu $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp samu.1 $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f samu $(OBJ)

.PHONY: install clean
