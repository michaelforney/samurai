.POSIX:
.PHONY: all install install-bin install-man clean

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/share/man
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

install: install-bin install-man
install-bin: samu
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $< $(DESTDIR)$(BINDIR)/
install-man: samu.1
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp $< $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f samu $(OBJ)
