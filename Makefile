PREFIX=/usr/local
CFLAGS=-Wall -std=c99 -pedantic -D_POSIX_C_SOURCE=200809L
OBJ=\
	build.o\
	env.o\
	deps.o\
	graph.o\
	htab.o\
	lex.o\
	log.o\
	parse.o\
	samurai.o\
	tool.o\
	util.o

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

samu: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

install: samu samu.1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp samu $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp samu.1 $(DESTDIR)$(PREFIX)/share/man/man1/

clean:
	rm -f samu $(OBJ)

.PHONY: install clean
