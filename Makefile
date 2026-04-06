.POSIX:
.PHONY: all install clean

OS=posix
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/share/man
ALL_CFLAGS=$(CFLAGS) -std=c99 -Wall -Wextra -Wshadow -Wmissing-prototypes -Wpedantic -Wno-unused-parameter
LDLIBS=
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
	os-$(OS).o
HDR=\
	arg.h\
	build.h\
	deps.h\
	env.h\
	graph.h\
	htab.h\
	log.h\
	os.h\
	parse.h\
	scan.h\
	tool.h\
	tree.h\
	util.h

all: samu

.c.o:
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

samu: $(OBJ)
	case "`uname -s`" in Linux) rt=-lrt ;; *) rt= ;; esac; \
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS) $$rt

$(OBJ): $(HDR)

install: samu samu.1
	mkdir -p $(DESTDIR)$(BINDIR)
	cp samu $(DESTDIR)$(BINDIR)/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp samu.1 $(DESTDIR)$(MANDIR)/man1/

clean:
	rm -f samu $(OBJ)
