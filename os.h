
#ifdef _WIN32
# ifdef PATH_MAX
#  undef PATH_MAX
# endif
# include <windows.h>
# define PATH_MAX MAX_PATH
#endif

/*
 * changes the working directory to the given path
 * in:
 *  - samu.c
 */
bool os_chdir(const char *dir);

/*
 * non thread safe version of getc()
 * in:
 *  - scan.c
 */
int os_getc_unlocked(FILE *stream);

/*
 * get the current working directory
 * in:
 *  - tool.c
 */
bool os_getcwd(char *dir, size_t len);

/*
 * creates the given directory path
 * in:
 *  - util.c
 */
bool os_mkdir(const char *dir);

/*
 * queries the mtime of a file in nanoseconds since the UNIX epoch
 * in:
 *  - graph.c
 */
int64_t os_query_mtime(const char *name);
