#define _GNU_SOURCE /* for pipe2, in posix-next */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <search.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "env.h"
#include "build.h"
#include "graph.h"
#include "util.h"

struct job {
	pid_t pid;
	char *cmd;
	int fd;
	struct edge *edge;
	struct buffer buf;
};

static struct {
	void *ready;
} work;
extern char **environ;

static int
ptrcmp(const void *p1, const void *p2)
{
	if (p1 < p2)
		return -1;
	return p1 > p2;
}

static bool
nodenewer(struct node *n1, struct node *n2)
{
	if (n1->mtime.tv_sec > n2->mtime.tv_sec)
		return true;
	if (n1->mtime.tv_sec < n2->mtime.tv_sec)
		return false;
	return n1->mtime.tv_nsec > n2->mtime.tv_nsec;
}

/* calculate e->ready and n->dirty for n in e->out */
static void
computedirty(struct edge *e)
{
	struct node *n, *newest = NULL;
	size_t i;
	bool dirty;

	dirty = false;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime.tv_nsec == MTIME_UNKNOWN)
			nodestat(n);
	}
	for (i = 0; i < e->nin; ++i) {
		n = e->in[i];

		/* record edge dependency on node */
		if (n->nuse && !n->use) {
			n->use = xmalloc(n->nuse * sizeof(*n->use));
			n->nuse = 0;
		}
		n->use[n->nuse++] = e;

		if (n->mtime.tv_nsec == MTIME_UNKNOWN) {
			nodestat(n);
			if (n->gen)
				computedirty(n->gen);
			else
				n->dirty = n->mtime.tv_nsec == MTIME_MISSING;
		}
		if (!dirty) {
			if (n->dirty)
				dirty = true;
			else if (!newest || nodenewer(n, newest) > 0)
				newest = n;
		}
	}
	if (!dirty) {
		/* all outputs are dirty if any are older than the newest input */
		for (i = 0; i < e->nout; ++i) {
			n = e->out[i];
			if (n->mtime.tv_nsec == MTIME_MISSING || nodenewer(newest, n)) {
				dirty = true;
				break;
			}
		}
	}
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->dirty = dirty;
	}
	if (dirty)
		e->want = true;
}

static void
addsubtarget(struct node *n)
{
	size_t i;

	// XXX: cycle detection
	if (!n->dirty)
		return;
	if (!n->gen)
		errx(1, "file is missing and not created by any action: '%s'", n->path);
	for (i = 0; i < n->gen->nin; ++i) {
		if (n->gen->in[i]->dirty)
			goto notready;
	}
	if (!tsearch(n->gen, &work.ready, ptrcmp))
		err(1, "tsearch");
notready:
	for (i = 0; i < n->gen->nin; ++i)
		addsubtarget(n->gen->in[i]);
}

void
addtarget(struct node *n)
{
	if (n->gen) {
		computedirty(n->gen);
	} else {
		if (n->mtime.tv_nsec == MTIME_UNKNOWN)
			nodestat(n);
		n->dirty = n->mtime.tv_nsec == MTIME_MISSING;
	}
	addsubtarget(n);
}

static int
jobstart(struct job *j, struct edge *e)
{
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};

	if (pipe2(fd, O_CLOEXEC) < 0)
		err(1, "pipe");
	j->edge = e;
	j->cmd = edgevar(e, "command");
	j->fd = fd[0];
	argv[2] = j->cmd;

	puts(j->cmd);

	if ((errno = posix_spawn_file_actions_init(&actions)))
		err(1, "posix_spawn_file_actions_init");
	if ((errno = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0)))
		err(1, "posix_spawn_file_actions_addopen");
	if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 1)))
		err(1, "posix_spawn_file_actions_adddup2");
	if ((errno = posix_spawn_file_actions_adddup2(&actions, fd[1], 2)))
		err(1, "posix_spawn_file_actions_adddup2");
	if ((errno = posix_spawn(&j->pid, argv[0], &actions, NULL, argv, environ)))
		err(1, "posix_spawn %s", j->cmd);
	posix_spawn_file_actions_destroy(&actions);
	close(fd[1]);

	return j->fd;
}

static bool
jobread(struct job *j)
{
	ssize_t n;

	if (j->buf.cap - j->buf.len < BUFSIZ) {
		j->buf.cap += BUFSIZ;
		j->buf.data = realloc(j->buf.data, j->buf.cap);
		if (!j->buf.data)
			err(1, "realloc"); // XXX: handle this
	}
	n = read(j->fd, j->buf.data + j->buf.len, j->buf.cap - j->buf.len);
	if (n < 0)
		err(1, "read"); // XXX: handle this
	j->buf.len += n;
	return n == 0;
}

static void
jobclose(struct job *j)
{
	int status;

	fwrite(j->buf.data, 1, j->buf.len, stdout);
	if (waitpid(j->pid, &status, 0) < 0)
		err(1, "waitpid %d", j->pid); // XXX: handle this
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		errx(1, "job failed: %s", j->cmd); // XXX: handle this
	close(j->fd);
	free(j->cmd);
	j->buf.len = 0;
}

static void
nodedone(struct node *n)
{
	struct edge *e;
	size_t i, j;

	n->dirty = false;
	/* if we did not already populate n->use, we do not care about the dependent edges. */
	if (!n->use)
		return;
	for (i = 0; i < n->nuse; ++i) {
		e = n->use[i];
		if (!e->want)
			continue;
		for (j = 0; j < e->nin; ++j) {
			if (e->in[j]->dirty)
				goto notready;
		}
		if (!tsearch(e, &work.ready, ptrcmp))
			err(1, "tsearch");
	notready:;
	}
}

static void
edgedone(struct edge *e)
{
	size_t i;

	for (i = 0; i < e->nout; ++i)
		nodedone(e->out[i]);
}

void
build(int maxjobs)
{
	struct job jobs[maxjobs];
	struct pollfd fds[maxjobs];
	int avail[maxjobs], i, next = 0, numjobs = 0;
	struct edge *e;

	for (i = 0; i < maxjobs; ++i) {
		jobs[i].buf.data = NULL;
		jobs[i].buf.len = 0;
		jobs[i].buf.cap = 0;
		fds[i].fd = -1;
		fds[i].events = POLLIN;
		avail[i] = i + 1;
	}
	avail[maxjobs - 1] = -1;

	if (!work.ready)
		puts("nothing to do");

	while (work.ready || numjobs > 0) {
		/* start ready edges */
		while (work.ready && numjobs < maxjobs) {
			e = *(struct edge **)work.ready;
			tdelete(e, &work.ready, ptrcmp);
			fds[next].fd = jobstart(&jobs[next], e);
			next = avail[next];
			++numjobs;
		}

		/* wait for job to finish */
		do {
			if (poll(fds, maxjobs, -1) < 0)
				err(1, "poll");
			for (i = 0; i < maxjobs; ++i) {
				if (!fds[i].revents)
					continue;
				if (jobread(&jobs[i])) {
					--numjobs;
					jobclose(&jobs[i]);
					avail[i] = next;
					fds[i].fd = -1;
					next = i;
					edgedone(jobs[i].edge);
				}
			}
		} while (numjobs == maxjobs);
	}
}
