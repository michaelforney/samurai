#include <stdlib.h>
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "graph.h"
#include "os.h"
#include "util.h"

#include <time.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

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

int
osclock_gettime_monotonic(struct ostimespec* time)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) {
		return -1;
	}
	time->tv_sec = t.tv_sec;
	time->tv_nsec = t.tv_nsec;
	return 0;
}

////////////// Jobs

struct osjob_ctx {
    struct pollfd* pfds;
    size_t pfds_len;
};


struct osjob_ctx*
osjob_ctx_create()
{
    struct osjob_ctx* result = xmalloc(sizeof(struct osjob_ctx));
    memset(result, 0, sizeof(*result));
    return result;
}

void
osjob_ctx_close(struct osjob_ctx* ctx)
{
    if (ctx->pfds)
        free(ctx->pfds);
    free(ctx);
}

int osjob_close(struct osjob_ctx* ctx, struct osjob* ojob)
{

    close(ojob->fd);
	kill(ojob->pid, SIGTERM);
	memset(ojob, 0, sizeof(*ojob));
	return 0;
}

int
osjob_done(struct osjob_ctx* ctx, struct osjob* ojob, struct string* cmd)
{
	int status;
	if (waitpid(ojob->pid, &status, 0) < 0) {
		warn("waitpid %d:", ojob->pid);
		goto err;
	} else if (WIFSIGNALED(status)) {
		warn("job terminated due to signal %d: %s", WTERMSIG(status), cmd->s);
		goto err;
	} else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
		warn("job failed with status %d: %s", WEXITSTATUS(status), cmd->s);
		goto err;
	}
	return osjob_close(ctx, ojob);
err:
	osjob_close(ctx, ojob);
	return -1;
}

int
osjob_wait(struct osjob_ctx *ctx, struct osjob* ojobs, size_t jobslen, int timeout)
{
	if (ctx->pfds_len < jobslen) {
		ctx->pfds = xreallocarray(ctx->pfds, jobslen, sizeof(ctx->pfds[0]));
		ctx->pfds_len = jobslen;
	}
	nfds_t count = 0;
	for (size_t i = 0; i < jobslen; ++i) {
                if (ojobs[i].valid) {
                        ctx->pfds[i].events = POLLIN;
                        ctx->pfds[i].fd = ojobs[i].fd;
                        ctx->pfds[i].revents = 0;
			count++;
		}
	}
	if (poll(ctx->pfds, count, timeout) < 0) {
		fatal("poll:");
	}
        struct osjob* curr = ojobs;
	for(nfds_t i = 0; i < count; ++i) {
                while(!curr->valid)
                        curr++;
                curr->has_data = ctx->pfds[i].revents;
		curr++;
	}
	return 0;
}

ssize_t
osjob_work(struct osjob_ctx *ctx, struct osjob *ojob, void *buf, size_t buflen)
{
	assert(ojob->has_data);
	return read(ojob->fd, buf, buflen);
}

int
osjob_create(struct osjob_ctx *ctx, struct osjob *created, struct string *cmd, bool console)
{
	extern char **environ;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", cmd->s, NULL};

	if (pipe(fd) < 0) {
		warn("pipe:");
		return -1;
	}

	created->has_data = false;
	created->fd = fd[0];

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init:");
		goto err2;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		warn("posix_spawn_file_actions_addclose:");
		goto err3;
	}
	if (!console) {
		if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0))) {
			warn("posix_spawn_file_actions_addopen:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 1))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 2))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_addclose(&actions, fd[1]))) {
			warn("posix_spawn_file_actions_addclose:");
			goto err3;
		}
	}
	if ((errno = posix_spawn(&created->pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s:", cmd->s);
		goto err3;
	}
	posix_spawn_file_actions_destroy(&actions);
	close(fd[1]);

	return 0;

err3:
	posix_spawn_file_actions_destroy(&actions);
err2:
	close(fd[0]);
	close(fd[1]);
	return -1;
}

