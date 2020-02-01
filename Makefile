.POSIX:

PREFIX=/usr/local
MANDIR=$(PREFIX)/share/man
ALL_CFLAGS=$(CFLAGS) -Wall -Wextra -std=c99 -pedantic -Ireproc/include
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
	util.o\
	reproc/src/clock.posix.o\
	reproc/src/error.posix.o\
	reproc/src/handle.posix.o\
	reproc/src/init.posix.o\
	reproc/src/pipe.posix.o\
	reproc/src/process.posix.o\
	reproc/src/redirect.posix.o\
	reproc/src/reproc.o\
	reproc/src/run.o\
	reproc/src/sink.o
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
	util.h\
	reproc/include/reproc/export.h\
	reproc/include/reproc/reproc.h\
	reproc/include/reproc/run.h\
	reproc/include/reproc/sink.h\
	reproc/src/clock.h\
	reproc/src/error.h\
	reproc/src/handle.h\
	reproc/src/init.h\
	reproc/src/macro.h\
	reproc/src/pipe.h\
	reproc/src/process.h\
	reproc/src/redirect.h

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
