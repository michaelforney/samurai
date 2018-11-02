struct string;

void osgetcwd(char *, size_t);
/* changes the working directory to the given path */
void oschdir(const char *);
/* creates all the parent directories of the given path */
int osmkdirs(struct string *, _Bool);
/* queries the mtime of a file in nanoseconds since the UNIX epoch */
int64_t osmtime(const char *);
