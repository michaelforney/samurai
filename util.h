struct buffer {
	char *data;
	size_t len, cap;
};

struct string {
	size_t n;
	char s[];
};

/* an unevaluated string */
struct evalstring {
	struct evalstringpart *parts;
	/* used temporarily only in parse.c:parseedge to keep track of
	 * input/output lists before we allocate the arrays. */
	struct evalstring *next;
	/* used to detect cycles when evaluating rule variables */
	_Bool visited;
};

struct evalstringpart {
	char *var;
	struct string *str;
	struct evalstringpart *next;
};

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

void warn(const char *, ...);
void fatal(const char *, ...);

void *xmalloc(size_t);
void *xreallocarray(void *, size_t, size_t);
char *xmemdup(const char *, size_t);
int xasprintf(char **, const char *, ...);

/* append a byte to a buffer */
void bufadd(struct buffer *buf, char c);

/* allocates a new string with length n. n + 1 bytes are allocated for
 * s, but not initialized. */
struct string *mkstr(size_t n);

/* delete an unevaluated string */
void delevalstr(struct evalstring *);

/* canonicalizes the given path by removing duplicate slashes, and
 * folding '/.' and 'foo/..' */
void canonpath(struct string *);
/* make a directory (or parent directory of a file) recursively */
int makedirs(struct string *, _Bool);
/* write a new file with the given name and contents */
int writefile(const char *, struct string *);
