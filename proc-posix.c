#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include "proc.h"
#include "util.h"

int
procspawn(union process *p, char *cmd, int fd[], bool console)
{
	extern char **environ;
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};
	pid_t pid;

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init:");
		goto err0;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		warn("posix_spawn_file_actions_addclose:");
		goto err1;
	}
	if (!console) {
		if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0))) {
			warn("posix_spawn_file_actions_addopen:");
			goto err1;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 1))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err1;
		}
		if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 2))) {
			warn("posix_spawn_file_actions_adddup2:");
			goto err1;
		}
		if ((errno = posix_spawn_file_actions_addclose(&actions, fd[1]))) {
			warn("posix_spawn_file_actions_addclose:");
			goto err1;
		}
	}
	argv[2] = cmd;
	if ((errno = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s:", cmd);
		goto err1;
	}
	posix_spawn_file_actions_destroy(&actions);

	p->pid = pid;
	return 0;

err1:
	posix_spawn_file_actions_destroy(&actions);
err0:
	return -1;
}

void
prockill(union process p)
{
	kill(p.pid, SIGKILL);
}

int
procwait(union process p, int *status)
{
	return waitpid(p.pid, status, 0);
}
