struct string;

/* changes the working directory to the given path */
void changedir(const char *);
/* creates all the parent directories of the given path */
int makedirs(struct string *);
/* queries the mtime of a file in nanoseconds since the UNIX epoch */
int64_t querymtime(const char *);
