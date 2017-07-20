#define _GNU_SOURCE /* for pipe2, in posix-next */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	struct string *cmd;
	int fd;
	struct edge *edge;
	struct buffer buf;
};

static struct edge *work;
extern char **environ;

static bool
nodenewer(struct node *n1, struct node *n2)
{
	if (!n1)
		return false;
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

	if (e->seen > 0)
		return;
	++e->seen;
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
		if (!dirty && i < e->inorderidx) {
			if (n->dirty)
				dirty = true;
			/* a node may be missing but not dirty if it a phony target */
			else if (n->mtime.tv_nsec != MTIME_MISSING && !nodenewer(newest, n))
				newest = n;
		}
	}
	/* all outputs are dirty if any are older than the newest input */
	for (i = 0; i < e->nout && !dirty; ++i) {
		n = e->out[i];
		if (e->rule == phonyrule && e->nin > 0)
			continue;
		if (n->mtime.tv_nsec == MTIME_MISSING || (e->rule != phonyrule && nodenewer(newest, n)))
			dirty = true;
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
	if (n->gen->seen > 1)
		return;
	++n->gen->seen;
	for (i = 0; i < n->gen->nin; ++i) {
		if (n->gen->in[i]->dirty)
			goto notready;
	}
	n->gen->worknext = work;
	work = n->gen;
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

static void
writefile(const char *name, struct string *s)
{
	int fd;
	const char *p;
	size_t n;
	ssize_t nw;

	fd = creat(name, 0666);
	if (fd < 0)
		err(1, "creat %s", name);
	if (s) {
		for (p = s->s, n = s->n; n > 0; p += nw, n -= nw) {
			nw = write(fd, p, n);
			if (nw <= 0)
				err(1, "write");
		}
	}
	close(fd);
}

static int
jobstart(struct job *j, struct edge *e)
{
	struct string *rspfile, *content;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};

	rspfile = edgevar(e, "rspfile");  // XXX: should use unescaped $out and $in
	if (rspfile) {
		content = edgevar(e, "rspfile_content");
		writefile(rspfile->s, content);
	}

	if (pipe2(fd, O_CLOEXEC) < 0)
		err(1, "pipe");
	j->edge = e;
	j->cmd = edgevar(e, "command");
	if (!j->cmd)
		errx(1, "rule '%s' has no command", e->rule->name);
	j->fd = fd[0];
	argv[2] = j->cmd->s;

	puts(j->cmd->s);

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
		errx(1, "job failed: %s", j->cmd->s); // XXX: handle this
	close(j->fd);
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
		e->worknext = work;
		work = e;
	notready:;
	}
}

static void
edgedone(struct edge *e)
{
	size_t i;
	struct string *rspfile;

	for (i = 0; i < e->nout; ++i)
		nodedone(e->out[i]);
	rspfile = edgevar(e, "rspfile");
	if (rspfile)
		unlink(rspfile->s);
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

	if (!work)
		puts("nothing to do");

	while (work || numjobs > 0) {
		/* start ready edges */
		while (work && numjobs < maxjobs) {
			e = work;
			work = work->worknext;
			if (e->rule == phonyrule) {
				edgedone(e);
				continue;
			}
			fds[next].fd = jobstart(&jobs[next], e);
			next = avail[next];
			++numjobs;
		}
		if (numjobs == 0)
			break;

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
