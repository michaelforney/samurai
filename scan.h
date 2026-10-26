enum token {
	BUILD,
	DEFAULT,
	INCLUDE,
	POOL,
	RULE,
	SUBNINJA,
	VARIABLE,
	ERROR,
};

struct scanner {
	FILE *f;
	const char *path;
	int chr, line, col;
	char error[512];
};

extern struct evalstring **paths;
extern size_t npaths;

void scaninit(struct scanner *, const char *);
void scanclose(struct scanner *);

void scanerror(struct scanner *, const char *, ...);
void scanfatal(struct scanner *);
int scankeyword(struct scanner *, char **);
char *scanname(struct scanner *);
_Bool scanstring(struct scanner *, _Bool, struct evalstring **);
_Bool scanpaths(struct scanner *);
_Bool scanchar(struct scanner *, int);
int scanpipe(struct scanner *, int);
int scanindent(struct scanner *);
_Bool scannewline(struct scanner *);
