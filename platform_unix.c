#include "platform.h"
#include "build.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
