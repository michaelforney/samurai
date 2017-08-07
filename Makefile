CFLAGS=-Wall -std=c11 -pedantic
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

.PHONY: clean
clean:
	rm -f samu $(OBJ)
