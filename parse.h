#pragma once

struct environment;

struct parseoptions {
	_Bool dupbuildwarn;
};

void parseinit(void);
void parse(const char *, struct environment *);

extern struct parseoptions parseopts;

/* supported ninja version */
extern const char *ninjaversion;

/* default targets */
extern struct node **deftarg;
extern size_t ndeftarg;
