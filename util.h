struct buffer {
	char *data;
	size_t len, cap;
};

struct string {
	size_t n;
	char s[];
};

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

void warnx(const char *, ...);
void warn(const char *, ...);
void errx(int, const char *, ...);
void err(int, const char *, ...);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
void *xcalloc(size_t, size_t);
char *xstrdup(const char *, size_t);

/* append a byte to a buffer */
void bufadd(struct buffer *buf, char c);

/* allocates a new string with length n. n + 1 bytes are allocated for
 * s, but not initialized. */
struct string *mkstr(size_t n);

/* canonicalizes the given path by removing duplicate slashes, and
 * folding '/.' and 'foo/..' */
void canonpath(struct string *);
/* creates all the parent directories of the given path */
int makedirs(struct string *);
/* write a new file with the given name and contents */
int writefile(const char *, struct string *);
