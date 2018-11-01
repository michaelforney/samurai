enum token {
	BUILD,
	DEFAULT,
	INCLUDE,
	POOL,
	RULE,
	SUBNINJA,
	NAME,
};

struct scanner *mkscanner(const char *);
void delscanner(struct scanner *);

void scanerror(struct scanner *, const char *, ...);
int scankeyword(struct scanner *, char **);
char *scanname(struct scanner *);
struct evalstring *scanstring(struct scanner *, _Bool);
void scanchar(struct scanner *, int);
int scanpipe(struct scanner *, int);
_Bool scanindent(struct scanner *);
void scannewline(struct scanner *);
