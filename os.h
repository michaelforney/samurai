struct string;

/* changes the working directory to the given path */
void changedir(const char *);
/* make a directory (or parent directory of a file) recursively */
int makedirs(struct string *, _Bool);
/* queries the mtime of a file in nanoseconds since the UNIX epoch */
int64_t querymtime(const char *);
