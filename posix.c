#define _POSIX_C_SOURCE 200809L
#include <errno.h>    /* for errno */
#include <stdbool.h>  /* for bool */
#include <stdint.h>
#include <stdio.h>    /* for FILE, getc_unlocked */
#include <sys/stat.h> /* for mkdir */
#include <unistd.h>   /* for chdir, getcwd */
#include "graph.h"    /* for MTIME_MISSING */


bool
os_chdir(const char *dir)
{
	return chdir(dir) == 0;
}

int
os_getc_unlocked(FILE *stream)
{
	return getc_unlocked(stream);
}

bool
os_getcwd(char *dir, size_t len)
{
	return getcwd(dir, len) != NULL;
}

bool
os_mkdir(const char *dir)
{
	return mkdir(dir, 0777) == 0 || errno == EEXIST;
}

int64_t
os_query_mtime(const char *name)
{
	struct stat st;

	if (stat(name, &st) < 0) {
		if (errno != ENOENT)
			fatal("stat %s:", n->path->s);
		return MTIME_MISSING;
	}
#ifdef __APPLE__
	return (int64_t)st.st_mtime * 1000000000 + st.st_mtimensec;
/*
Illumos hides the members of st_mtim when you define _POSIX_C_SOURCE
since it has not been updated to support POSIX.1-2008:
https://www.illumos.org/issues/13327
*/
#elif defined(__sun)
	return (int64_t)st.st_mtim.__tv_sec * 1000000000 + st.st_mtim.__tv_nsec;
#else
	return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}
