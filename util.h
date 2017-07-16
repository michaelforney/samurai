struct buffer {
	char *data;
	size_t len, cap;
};

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
void *xcalloc(size_t, size_t);
char *xstrndup(const char *, size_t);
