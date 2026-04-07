#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>
#include "graph.h"
#include "os.h"
#include "util.h"

void
osgetcwd(char *buf, size_t len)
{
	if (!getcwd(buf, len))
		fatal("getcwd:");
}

void
oschdir(const char *dir)
{
	if (chdir(dir) < 0)
		fatal("chdir %s:", dir);
}

int
osmkdirs(struct string *path, bool parent)
{
	int ret;
	struct stat st;
	char *s, *end;

	ret = 0;
	end = path->s + path->n;
	for (s = end - parent; s > path->s; --s) {
		if (*s != '/' && *s)
			continue;
		*s = '\0';
		if (stat(path->s, &st) == 0)
			break;
		if (errno != ENOENT) {
			warn("stat %s:", path->s);
			ret = -1;
			break;
		}
	}
	if (s > path->s && s < end)
		*s = '/';
	while (++s <= end - parent) {
		if (*s != '\0')
			continue;
		if (ret == 0 && mkdir(path->s, 0777) < 0 && errno != EEXIST) {
			warn("mkdir %s:", path->s);
			ret = -1;
		}
		if (s < end)
			*s = '/';
	}

	return ret;
}

int64_t
osmtime(const char *name)
{
	struct stat st;

	if (stat(name, &st) < 0) {
		if (errno != ENOENT)
			fatal("stat %s:", name);
		return MTIME_MISSING;
	} else {
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
}

long
osnproc(void)
{
#ifdef _SC_NPROCESSORS_ONLN
	return sysconf(_SC_NPROCESSORS_ONLN);
#else
	return 1;
#endif
}

pid_t
osspawn(char *const argv[], int outfd)
{
	extern char **environ;
	pid_t pid;
	posix_spawn_file_actions_t actions;

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init:");
		goto err0;
	}
	if (outfd != -1) {
		if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err1;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, outfd, 1))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err1;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, outfd, 2))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err1;
		}
	}
	if ((errno = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s:", argv[0]);
		goto err1;
	}
	posix_spawn_file_actions_destroy(&actions);
	return pid;

err1:
	posix_spawn_file_actions_destroy(&actions);
err0:
	return -1;
}
