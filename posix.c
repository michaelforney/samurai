#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "graph.h"
#include "os.h"
#include "util.h"

void
changedir(const char *dir)
{
	if (chdir(dir) < 0)
		err(1, "chdir %s", dir);
}

int
makedirs(struct string *path)
{
	int ret;
	struct stat st;
	char *s, *end;
	bool missing;

	ret = 0;
	missing = false;
	end = path->s + path->n;
	for (s = end - 1; s > path->s; --s) {
		if (*s != '/')
			continue;
		*s = '\0';
		if (stat(path->s, &st) == 0)
			break;
		if (errno != ENOENT) {
			warn("stat %s", path->s);
			ret = -1;
			break;
		}
		missing = true;
	}
	if (s > path->s)
		*s = '/';
	if (!missing)
		return ret;
	for (++s; s < end; ++s) {
		if (*s != '\0')
			continue;
		if (ret == 0 && mkdir(path->s, 0777) < 0 && errno != EEXIST) {
			warn("mkdir %s", path->s);
			ret = -1;
		}
		*s = '/';
	}

	return ret;
}

int64_t
querymtime(const char *name)
{
	struct stat st;

	if (stat(name, &st) < 0) {
		if (errno != ENOENT)
			err(1, "stat %s", name);
		return MTIME_MISSING;
	}
#ifdef __APPLE__
	return (int64_t)st.st_mtime * 1000000000 + st.st_mtimensec;
#else
	return (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
}
