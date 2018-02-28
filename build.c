#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "util.h"

struct job {
	pid_t pid;
	struct string *cmd;
	int fd;
	struct edge *edge;
	struct buffer buf;
	bool failed;
};

struct buildoptions buildopts = {.maxfail = 1};
static struct edge *work;
static size_t nstarted, ntotal;
static bool consoleused;

/* returns whether n1 is newer than n2, or false if n1 is NULL */
static bool
isnewer(struct node *n1, struct node *n2)
{
	if (!n1)
		return false;
	if (n1->mtime.tv_sec > n2->mtime.tv_sec)
		return true;
	if (n1->mtime.tv_sec < n2->mtime.tv_sec)
		return false;
	return n1->mtime.tv_nsec > n2->mtime.tv_nsec;
}

/* returns whether this output node is dirty in relation to the newest input */
static bool
isdirty(struct node *n, struct node *newest, bool generator, bool restat)
{
	struct edge *e;

	e = n->gen;
	if (e->rule == &phonyrule) {
		if (e->nin > 0 || n->mtime.tv_nsec != MTIME_MISSING)
			return false;
		if (buildopts.explain)
			warnx("explain %s: phony and no inputs", n->path->s);
		return true;
	}
	if (n->mtime.tv_nsec == MTIME_MISSING) {
		if (buildopts.explain)
			warnx("explain %s: missing", n->path->s);
		return true;
	}
	if (isnewer(newest, n) && !restat) {
		if (buildopts.explain) {
			warnx("explain %s: older than input '%s': %ju vs %ju",
			      n->path->s, newest->path->s, (uintmax_t)n->logmtime, (uintmax_t)newest->mtime.tv_sec);
		}
		return true;
	}
	if (newest && n->logmtime < newest->mtime.tv_sec) {
		if (buildopts.explain) {
			warnx("explain %s: recorded mtime is older than input '%s': %ju vs %ju",
			      n->path->s, newest->path->s, (uintmax_t)n->logmtime, (uintmax_t)newest->mtime.tv_sec);
		}
		return true;
	}
	if (generator)
		return false;
	edgehash(e);
	if (e->hash == n->hash)
		return false;
	if (buildopts.explain)
		warnx("explain %s: command line changed", n->path->s);
	return true;
}

/* add an edge to the work queue */
static void
queue(struct edge *e)
{
	struct edge **front = &work;

	if (e->pool && e->rule != &phonyrule) {
		if (e->pool->numjobs == e->pool->maxjobs)
			front = &e->pool->work;
		else
			++e->pool->numjobs;
	}
	e->worknext = *front;
	*front = e;
}

void
buildadd(struct node *n)
{
	struct edge *e;
	struct node *newest;
	size_t i;
	bool generator, restat;

	e = n->gen;
	if (!e) {
		if (n->mtime.tv_nsec == MTIME_UNKNOWN)
			nodestat(n);
		if (n->mtime.tv_nsec == MTIME_MISSING)
			errx(1, "file is missing and not created by any action: '%s'", n->path->s);
		n->dirty = false;
		return;
	}
	if (e->flags & FLAG_CYCLE)
		errx(1, "dependency cycle involving '%s'", n->path->s);
	if (e->flags & FLAG_WORK)
		return;
	e->flags |= FLAG_CYCLE | FLAG_WORK;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->dirty = false;
		if (n->mtime.tv_nsec == MTIME_UNKNOWN)
			nodestat(n);
	}
	depsload(e);
	e->nblock = 0;
	newest = NULL;
	for (i = 0; i < e->nin; ++i) {
		n = e->in[i];
		buildadd(n);
		if (i < e->inorderidx) {
			if (n->dirty)
				e->flags |= FLAG_DIRTY_IN;
			if (n->mtime.tv_nsec != MTIME_MISSING && !isnewer(newest, n))
				newest = n;
		}
		if (n->dirty || (n->gen && n->gen->nblock > 0)) {
			++e->nblock;
			nodeuse(n, e);
		}
	}
	/* all outputs are dirty if any are older than the newest input */
	generator = edgevar(e, "generator");
	restat = edgevar(e, "restat");
	for (i = 0; i < e->nout && !(e->flags & FLAG_DIRTY_OUT); ++i) {
		n = e->out[i];
		if (isdirty(n, newest, generator, restat)) {
			n->dirty = true;
			e->flags |= FLAG_DIRTY_OUT;
		}
	}
	if (e->flags & FLAG_DIRTY) {
		for (i = 0; i < e->nout; ++i) {
			n = e->out[i];
			if (buildopts.explain && !n->dirty) {
				if (e->flags & FLAG_DIRTY_IN)
					warnx("explain %s: input is dirty", n->path->s);
				else if (e->flags & FLAG_DIRTY_OUT)
					warnx("explain %s: output of generating action is dirty", n->path->s);
			}
			n->dirty = true;
		}
	}
	if (!(e->flags & FLAG_DIRTY_OUT))
		e->nprune = e->nblock;
	if (e->flags & FLAG_DIRTY) {
		if (e->nblock == 0)
			queue(e);
		if (e->rule != &phonyrule)
			++ntotal;
	}
	e->flags &= ~FLAG_CYCLE;
}

static int
jobstart(struct job *j, struct edge *e)
{
	extern char **environ;
	size_t i;
	struct node *n;
	struct string *rspfile, *content, *description;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};

	++nstarted;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime.tv_nsec == MTIME_MISSING) {
			if (makedirs(n->path) < 0)
				goto err0;
		}
	}
	rspfile = edgevar(e, "rspfile");  // XXX: should use unescaped $out and $in
	if (rspfile) {
		content = edgevar(e, "rspfile_content");
		if (writefile(rspfile->s, content) < 0)
			goto err0;
	}

	if (pipe(fd) < 0) {
		warn("pipe");
		goto err1;
	}
	j->edge = e;
	j->cmd = edgevar(e, "command");
	if (!j->cmd) {
		warnx("rule '%s' has no command", e->rule->name);
		goto err2;
	}
	j->fd = fd[0];
	argv[2] = j->cmd->s;

	if (!consoleused) {
		description = buildopts.verbose ? NULL : edgevar(e, "description");
		if (!description || description->n == 0)
			description = j->cmd;
		printf("[%zu/%zu] %s\n", nstarted, ntotal, description->s);
	}

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init");
		goto err2;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		warn("posix_spawn_file_actions_addclose");
		goto err3;
	}
	if (e->pool != &consolepool) {
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
	if ((errno = posix_spawn(&j->pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s", j->cmd->s);
		goto err3;
	}
	posix_spawn_file_actions_destroy(&actions);
	close(fd[1]);
	j->failed = false;
	if (e->pool == &consolepool)
		consoleused = true;

	return j->fd;

err3:
	posix_spawn_file_actions_destroy(&actions);
err2:
	close(fd[0]);
	close(fd[1]);
err1:
	if (rspfile)
		unlink(rspfile->s);
err0:
	return -1;
}

static void
nodedone(struct node *n, bool prune)
{
	struct edge *e;
	size_t i, j;

	/* if we did not already populate n->use, we do not care about the dependent edges. */
	if (!n->use)
		return;
	for (i = 0; i < n->nuse; ++i) {
		e = n->use[i];
		if (!(e->flags & (prune ? FLAG_DIRTY_OUT : FLAG_DIRTY)) && --e->nprune == 0) {
			/* either edge was clean (possible with order-only
			 * inputs), or all its blocking inputs were pruned, so
			 * its outputs can be pruned as well */
			for (j = 0; j < e->nout; ++j)
				nodedone(e->out[j], true);
			if (e->flags & FLAG_DIRTY && e->rule != &phonyrule)
				--ntotal;
		} else if (--e->nblock == 0) {
			queue(e);
		}
	}
}

static bool
shouldprune(struct edge *e, struct node *n, const struct timespec *old)
{
	struct node *in, *newest;
	size_t i;

	if (old->tv_nsec != n->mtime.tv_nsec)
		return false;
	if (old->tv_nsec >= 0 && old->tv_sec != n->mtime.tv_sec)
		return false;
	newest = NULL;
	for (i = 0; i < e->inorderidx; ++i) {
		in = e->in[i];
		nodestat(in);
		if (in->mtime.tv_nsec != MTIME_MISSING && !isnewer(newest, in))
			newest = in;
	}
	if (newest)
		n->logmtime = newest->mtime.tv_sec;

	return true;
}

static void
edgedone(struct edge *e)
{
	struct edge *new;
	struct pool *p;
	struct node *n;
	size_t i;
	struct string *rspfile;
	bool restat;
	struct timespec old;

	if (e->pool) {
		p = e->pool;

		if (p == &consolepool)
			consoleused = false;
		/* move edge from pool queue to main work queue */
		if (p->work) {
			new = p->work;
			p->work = p->work->worknext;
			new->worknext = work;
			work = new;
		} else {
			--p->numjobs;
		}
	}
	restat = edgevar(e, "restat");
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		old = n->mtime;
		nodestat(n);
		if (n->mtime.tv_nsec != MTIME_MISSING)
			n->logmtime = n->mtime.tv_sec;
		nodedone(n, restat && shouldprune(e, n, &old));
	}
	rspfile = edgevar(e, "rspfile");
	if (rspfile)
		unlink(rspfile->s);
	edgehash(e);
	depsrecord(e);
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->hash = e->hash;
		logrecord(n);
	}
}

static void
jobdone(struct job *j)
{
	int status;

	if (waitpid(j->pid, &status, 0) < 0) {
		warn("waitpid %d", j->pid);
		j->failed = true;
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			warnx("job failed: %s", j->cmd->s);
			j->failed = true;
		}
	} else if (WIFSIGNALED(status)) {
		warnx("job terminated due to signal %d: %s", WTERMSIG(status), j->cmd->s);
		j->failed = true;
	} else {
		/* cannot happen according to POSIX */
		warnx("job status unknown: %s", j->cmd->s);
		j->failed = true;
	}
	close(j->fd);
	if (j->buf.len && (!consoleused || j->failed))
		fwrite(j->buf.data, 1, j->buf.len, stdout);
	j->buf.len = 0;
	if (!j->failed)
		edgedone(j->edge);
}

/* returns whether a job still has work to do. if not, sets j->failed */
static bool
jobwork(struct job *j)
{
	char *newdata;
	size_t newcap;
	ssize_t n;

	if (j->buf.cap - j->buf.len < BUFSIZ / 2) {
		newcap = j->buf.cap + BUFSIZ;
		newdata = realloc(j->buf.data, newcap);
		if (!newdata) {
			warn("realloc");
			goto kill;
		}
		j->buf.cap = newcap;
		j->buf.data = newdata;
	}
	n = read(j->fd, j->buf.data + j->buf.len, j->buf.cap - j->buf.len);
	if (n > 0) {
		j->buf.len += n;
		return true;
	}
	if (n == 0)
		goto done;
	warn("read");

kill:
	kill(j->pid, SIGTERM);
	j->failed = true;
done:
	jobdone(j);

	return false;
}

void
build(void)
{
	struct job jobs[buildopts.maxjobs];
	struct pollfd fds[buildopts.maxjobs];
	size_t i;
	int avail[buildopts.maxjobs], j, next = 0, numjobs = 0, numfail = 0;
	struct edge *e;

	for (j = 0; j < buildopts.maxjobs; ++j) {
		jobs[j].buf.data = NULL;
		jobs[j].buf.len = 0;
		jobs[j].buf.cap = 0;
		fds[j].fd = -1;
		fds[j].events = POLLIN;
		avail[j] = j + 1;
	}
	avail[buildopts.maxjobs - 1] = -1;

	if (!work)
		warnx("nothing to do");

	nstarted = 0;
	for (;;) {
		/* start ready edges */
		while (work && numjobs != buildopts.maxjobs && numfail != buildopts.maxfail) {
			e = work;
			work = work->worknext;
			if (e->rule == &phonyrule) {
				for (i = 0; i < e->nout; ++i)
					nodedone(e->out[i], false);
				continue;
			}
			fds[next].fd = jobstart(&jobs[next], e);
			if (fds[next].fd < 0) {
				warnx("job failed to start");
				++numfail;
			} else {
				next = avail[next];
				++numjobs;
			}
		}
		if (numjobs == 0)
			break;

		/* wait for job to finish */
		do {
			if (poll(fds, buildopts.maxjobs, -1) < 0)
				err(1, "poll");
			for (j = 0; j < buildopts.maxjobs; ++j) {
				if (!fds[j].revents || jobwork(&jobs[j]))
					continue;
				--numjobs;
				avail[j] = next;
				fds[j].fd = -1;
				next = j;
				if (jobs[j].failed)
					++numfail;
			}
		} while (numjobs == buildopts.maxjobs);
	}
	for (j = 0; j < buildopts.maxjobs; ++j)
		free(jobs[j].buf.data);
	if (numfail > 0) {
		if (numfail < buildopts.maxfail)
			errx(1, "cannot make progress due to previous errors");
		else if (numfail > 1)
			errx(1, "subcommands failed");
		else
			errx(1, "subcommand failed");
	}
	ntotal = 0;  /* reset in case we just rebuilt the manifest */
}
