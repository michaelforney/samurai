enum token {
	BUILD,
	DEFAULT,
	INCLUDE,
	POOL,
	RULE,
	SUBNINJA,
	VARIABLE,
};

struct scanner {
	FILE *f;
	const char *path;
	int chr, line, col;
};

void scaninit(struct scanner *, const char *);
void scanclose(struct scanner *);

void scanerror(struct scanner *, const char *, ...);
int scankeyword(struct scanner *, char **);
char *scanname(struct scanner *);
struct evalstring *scanstring(struct scanner *, _Bool);
void scanchar(struct scanner *, int);
int scanpipe(struct scanner *, int);
_Bool scanindent(struct scanner *);
void scannewline(struct scanner *);
