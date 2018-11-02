#include "platform.h"
#include "build.h"
#include "graph.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct implplatformprocess {
        int fd;
        pid_t pid;
};

static struct
implplatformprocess processtoimpl(struct platformprocess p)
{
	struct implplatformprocess impl;
	memcpy(&impl, &p, sizeof(impl));
	return impl;
}

static struct
platformprocess impltoprocess(struct implplatformprocess impl)
{
	struct platformprocess p;
	memcpy(&p, &impl, sizeof(impl));
	return p;
}

bool
createprocess(struct string *command, struct platformprocess *p, bool captureoutput)
{
	extern char **environ;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", command->s, NULL};
	struct implplatformprocess impl;

	if (pipe(fd) < 0) {
		warn("pipe");
		goto err0;
	}
	impl.fd = fd[0];

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init");
		goto err2;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		warn("posix_spawn_file_actions_addclose");
		goto err3;
	}
	if (captureoutput) {
		if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0))) {
			warn("posix_spawn_file_actions_addopen");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 1))) {
			warn("posix_spawn_file_actions_adddup2");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 2))) {
			warn("posix_spawn_file_actions_adddup2");
			goto err3;
		}
		if ((errno = posix_spawn_file_actions_addclose(&actions, fd[1]))) {
			warn("posix_spawn_file_actions_addclose");
			goto err3;
		}
	}
	if ((errno = posix_spawn(&impl.pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s", command->s);
		goto err3;
	}
	posix_spawn_file_actions_destroy(&actions);
	close(fd[1]);

	*p = impltoprocess(impl);

	return true;

err3:
	posix_spawn_file_actions_destroy(&actions);
err2:
	close(fd[0]);
	close(fd[1]);
err0:
	return false;
}

bool
readprocessoutput(struct platformprocess p, char *buf, size_t buflen, size_t *n)
{
	struct implplatformprocess impl = processtoimpl(p);
	ssize_t r = read(impl.fd, buf, buflen);
	if (r < 0)
		return false;

	*n = (size_t)r;
	return true;
}

bool
waitexit(struct job *j)
{
	int status;
	bool failed = false;
	struct implplatformprocess impl = processtoimpl(j->process);

	if (waitpid(impl.pid, &status, 0) < 0) {
		warn("waitpid %d", impl.pid);
		failed = true;
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			warnx("job failed: %s", j->cmd->s);
			failed = true;
		}
	} else if (WIFSIGNALED(status)) {
		warnx("job terminated due to signal %d: %s", WTERMSIG(status), j->cmd->s);
		failed = true;
	} else {
		/* cannot happen according to POSIX */
		warnx("job status unknown: %s", j->cmd->s);
		failed = true;
	}
	close(impl.fd);

	return failed;
}

void
killprocess(struct platformprocess p)
{
	struct implplatformprocess impl = processtoimpl(p);
	kill(impl.pid, SIGTERM);
	close(impl.fd);
}

static struct pollfd *pollfds = NULL;

void
initplatform(size_t maxjobs)
{
	pollfds = malloc(maxjobs * sizeof(pollfds[0]));
	for (size_t i = 0; i < maxjobs; i++) {
		pollfds[i].events = POLLIN;
	}
}

void
shutdownplatform()
{
	free(pollfds);
}

size_t
waitforjobs(const struct job *jobs, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		struct implplatformprocess impl = processtoimpl(jobs[i].process);
		pollfds[i].fd = impl.fd;
	}

	if (poll(pollfds, n, -1) < 0)
		err(1, "poll");

	for (size_t i = 0; i < n; i++) {
		if (pollfds[i].revents)
			return i;
	}

	errx(1, "poll returned when it shouldn't have!");
	return 0;
}

void
changedir(const char *dir)
{
	if (chdir(dir) < 0)
		err(1, "chdir %s", dir);
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

bool
createdir(const char *path)
{
	return mkdir(path, 0777) == 0 || errno == EEXIST;
}

/*int
direxists(const char *path)
{
	struct stat st;
	if (stat(path->s, &st) == 0)
		return 1;
	if (errno == ENOENT)
		return 0;

	warn("stat %s", path);
	return -1;
}*/

bool
renamereplace(const char *oldpath, const char *newpath)
{
	return rename(oldpath, newpath) == 0;
}
