#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "token.h"
#include "util.h"

struct job {
	struct string *cmd;
	struct edge *edge;
	struct buffer buf;
	size_t next;
	pid_t pid;
	int fd;
	bool failed;
};

struct buildoptions buildopts = {.maxfail = 1};
static struct edge *work;
static size_t nstarted, nfinished, ntotal;
static bool consoleused;
static struct timespec starttime;

/* for signal handling */
static volatile sig_atomic_t killall;
static volatile sig_atomic_t stopsig;

void
buildreset(void)
{
	struct edge *e;

	for (e = alledges; e; e = e->allnext)
		e->flags &= ~FLAG_WORK;
}

/* returns whether n1 is newer than n2, or false if n1 is NULL */
static bool
isnewer(struct node *n1, struct node *n2)
{
	return n1 && n1->mtime > n2->mtime;
}

/* returns whether this output node is dirty in relation to the newest input */
static bool
isdirty(struct node *n, struct node *newest, bool generator, bool restat)
{
	struct edge *e;

	e = n->gen;
	if (e->rule == &phonyrule) {
		if (e->nin > 0 || n->mtime != MTIME_MISSING)
			return false;
		if (buildopts.explain)
			warn("explain %s: phony and no inputs", n->path->s);
		return true;
	}
	if (n->mtime == MTIME_MISSING) {
		if (buildopts.explain)
			warn("explain %s: missing", n->path->s);
		return true;
	}
	if (isnewer(newest, n) && (!restat || n->logmtime == MTIME_MISSING)) {
		if (buildopts.explain) {
			warn("explain %s: older than input '%s': %" PRId64 " vs %" PRId64,
			     n->path->s, newest->path->s, n->mtime, newest->mtime);
		}
		return true;
	}
	if (n->logmtime == MTIME_MISSING) {
		if (!generator) {
			if (buildopts.explain)
				warn("explain %s: no record in .ninja_log", n->path->s);
			return true;
		}
	} else if (newest && n->logmtime < newest->mtime) {
		if (buildopts.explain) {
			warn("explain %s: recorded mtime is older than input '%s': %" PRId64 " vs %" PRId64,
			     n->path->s, newest->path->s, n->logmtime, newest->mtime);
		}
		return true;
	}
	if (generator)
		return false;
	edgehash(e);
	if (e->hash == n->hash)
		return false;
	if (buildopts.explain)
		warn("explain %s: command line changed", n->path->s);
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

static bool have_work(void)
{
	return work && !stopsig;
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
		if (n->mtime == MTIME_UNKNOWN)
			nodestat(n);
		if (n->mtime == MTIME_MISSING)
			fatal("file is missing and not created by any action: '%s'", n->path->s);
		n->dirty = false;
		return;
	}
	if (e->flags & FLAG_CYCLE)
		fatal("dependency cycle involving '%s'", n->path->s);
	if (e->flags & FLAG_WORK)
		return;
	e->flags |= FLAG_CYCLE | FLAG_WORK;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		n->dirty = false;
		if (n->mtime == MTIME_UNKNOWN)
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
			if (n->mtime != MTIME_MISSING && !isnewer(newest, n))
				newest = n;
		}
		if (n->dirty || (n->gen && n->gen->nblock > 0))
			++e->nblock;
	}
	/* all outputs are dirty if any are older than the newest input */
	generator = edgevar(e, "generator", true);
	restat = edgevar(e, "restat", true);
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
					warn("explain %s: input is dirty", n->path->s);
				else if (e->flags & FLAG_DIRTY_OUT)
					warn("explain %s: output of generating action is dirty", n->path->s);
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

static size_t
formatstatus(char *buf, size_t len)
{
	const char *fmt;
	size_t ret = 0;
	int n;
	struct timespec endtime;

	for (fmt = buildopts.statusfmt; *fmt; ++fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			if (len > 1) {
				*buf++ = *fmt;
				--len;
			}
			++ret;
			continue;
		}
		n = 0;
		switch (*fmt) {
		case 's':
			n = snprintf(buf, len, "%zu", nstarted);
			break;
		case 'f':
			n = snprintf(buf, len, "%zu", nfinished);
			break;
		case 't':
			n = snprintf(buf, len, "%zu", ntotal);
			break;
		case 'r':
			n = snprintf(buf, len, "%zu", nstarted - nfinished);
			break;
		case 'u':
			n = snprintf(buf, len, "%zu", ntotal - nstarted);
			break;
		case 'p':
			n = snprintf(buf, len, "%3zu%%", 100 * nfinished / ntotal);
			break;
		case 'o':
			if (clock_gettime(CLOCK_MONOTONIC, &endtime) != 0) {
				warn("clock_gettime:");
				break;
			}
			n = snprintf(buf, len, "%.1f", nfinished / ((endtime.tv_sec - starttime.tv_sec) + 0.000000001 * (endtime.tv_nsec - starttime.tv_nsec)));
			break;
		case 'e':
			if (clock_gettime(CLOCK_MONOTONIC, &endtime) != 0) {
				warn("clock_gettime:");
				break;
			}
			n = snprintf(buf, len, "%.3f", (endtime.tv_sec - starttime.tv_sec) + 0.000000001 * (endtime.tv_nsec - starttime.tv_nsec));
			break;
		default:
			fatal("unknown placeholder '%%%c' in $NINJA_STATUS", *fmt);
			continue;  /* unreachable, but avoids warning */
		}
		if (n < 0)
			fatal("snprintf:");
		ret += n;
		if ((size_t)n > len)
			n = len;
		buf += n;
		len -= n;
	}
	if (len > 0)
		*buf = '\0';
	return ret;
}

static void
printstatus(struct edge *e, struct string *cmd)
{
	struct string *description;
	char status[256];

	description = buildopts.verbose ? NULL : edgevar(e, "description", true);
	if (!description || description->n == 0)
		description = cmd;
	formatstatus(status, sizeof(status));
	fputs(status, stdout);
	puts(description->s);
}

static int
jobstart(struct job *j, struct edge *e)
{
	extern char **environ;
	size_t i;
	struct node *n;
	struct string *rspfile, *content;
	int fd[2];
	posix_spawn_file_actions_t actions;
	char *argv[] = {"/bin/sh", "-c", NULL, NULL};

	++nstarted;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime == MTIME_MISSING) {
			if (makedirs(n->path, true) < 0)
				goto err0;
		}
	}
	rspfile = edgevar(e, "rspfile", false);
	if (rspfile) {
		content = edgevar(e, "rspfile_content", true);
		if (writefile(rspfile->s, content) < 0)
			goto err0;
	}

	if (pipe(fd) < 0) {
		warn("pipe:");
		goto err1;
	}
	j->edge = e;
	j->cmd = edgevar(e, "command", true);
	j->fd = fd[0];
	argv[2] = j->cmd->s;

	if (!consoleused)
		printstatus(e, j->cmd);

	if ((errno = posix_spawn_file_actions_init(&actions))) {
		warn("posix_spawn_file_actions_init:");
		goto err2;
	}
	if ((errno = posix_spawn_file_actions_addclose(&actions, fd[0]))) {
		warn("posix_spawn_file_actions_addclose:");
		goto err3;
	}
	if (e->pool != &consolepool) {
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
	if ((errno = posix_spawn(&j->pid, argv[0], &actions, NULL, argv, environ))) {
		warn("posix_spawn %s:", j->cmd->s);
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
	if (rspfile && !buildopts.keeprsp)
		remove(rspfile->s);
err0:
	return -1;
}

static void
nodedone(struct node *n, bool prune)
{
	struct edge *e;
	size_t i, j;

	for (i = 0; i < n->nuse; ++i) {
		e = n->use[i];
		/* skip edges not used in this build */
		if (!(e->flags & FLAG_WORK))
			continue;
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
shouldprune(struct edge *e, struct node *n, int64_t old)
{
	struct node *in, *newest;
	size_t i;

	if (old != n->mtime)
		return false;
	newest = NULL;
	for (i = 0; i < e->inorderidx; ++i) {
		in = e->in[i];
		nodestat(in);
		if (in->mtime != MTIME_MISSING && !isnewer(newest, in))
			newest = in;
	}
	if (newest)
		n->logmtime = newest->mtime;

	return true;
}

static void
edgedone(struct edge *e)
{
	struct node *n;
	size_t i;
	struct string *rspfile;
	bool restat;
	int64_t old;

	restat = edgevar(e, "restat", true);
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		old = n->mtime;
		nodestat(n);
		n->logmtime = n->mtime == MTIME_MISSING ? 0 : n->mtime;
		nodedone(n, restat && shouldprune(e, n, old));
	}
	rspfile = edgevar(e, "rspfile", false);
	if (rspfile && !buildopts.keeprsp)
		remove(rspfile->s);
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
	struct edge *e, *new;
	struct pool *p;

	++nfinished;
	if (waitpid(j->pid, &status, 0) < 0) {
		warn("waitpid %d:", j->pid);
		j->failed = true;
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			warn("job failed with status %d: %s", WEXITSTATUS(status), j->cmd->s);
			j->failed = true;
		}
	} else if (WIFSIGNALED(status)) {
		if (stopsig != WTERMSIG(status))
			warn("job terminated due to signal %d: %s", WTERMSIG(status), j->cmd->s);
		j->failed = true;
	} else {
		/* cannot happen according to POSIX */
		warn("job status unknown: %s", j->cmd->s);
		j->failed = true;
	}
	close(j->fd);
	if (j->buf.len && (!consoleused || j->failed))
		fwrite(j->buf.data, 1, j->buf.len, stdout);
	j->buf.len = 0;
	e = j->edge;
	j->edge = NULL;

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
	if (!j->failed)
		edgedone(e);
}

static void
jobkill(struct job *j)
{
	kill(j->pid, SIGTERM);
	j->failed = true;
	jobdone(j);
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
			warn("realloc:");
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
	if (n < 0) {
		warn("read:");
		goto kill;
	}

	jobdone(j);
	return false;

kill:
	jobkill(j);
	return false;
}

/* queries the system load average */
static double
queryload(void)
{
#ifdef HAVE_GETLOADAVG
	double load;

	if (getloadavg(&load, 1) == -1) {
		warn("getloadavg:");
		load = 100.0;
	}

	return load;
#else
	return 0;
#endif
}

static void sighandler(int sig)
{
	/*
	 * Both SIGINT and SIGTERM will not start any more processes,
	 * but only SIGTERM needs to be forwarded to samurai's child
	 * processes.  That's because samurai is a process group leader
	 * and SIGINT has already been sent to all children, and they
	 * will stop on their own.
	 */
	if (sig == SIGTERM)
		killall = true;
	stopsig = sig;
}

void
build(void)
{
	struct job *jobs = NULL;
	struct pollfd *fds = NULL;
	size_t i, next = 0, jobslen = 0, maxjobs = buildopts.maxjobs, numjobs = 0, numfail = 0;
	struct edge *e;
	int jobserver_rfd;
	struct sigaction sa;

	if (ntotal == 0) {
		warn("nothing to do");
		return;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	clock_gettime(CLOCK_MONOTONIC, &starttime);
	formatstatus(NULL, 0);
	jobserver_rfd = tokeninit();

	/* if running under job server start as many jobs as allowed */
	if (jobserver_rfd != -1)
		buildopts.maxjobs = INT_MAX;

	nstarted = 0;
	for (;;) {
		/* limit number of of jobs based on load */
		if (buildopts.maxload)
			maxjobs = queryload() > buildopts.maxload ? 1 : buildopts.maxjobs;
		/* start ready edges */
		while (have_work() && numjobs < maxjobs && numfail < buildopts.maxfail) {
			e = work;
			if (e->rule != &phonyrule && buildopts.dryrun) {
				++nstarted;
				printstatus(e, edgevar(e, "command", true));
				++nfinished;
			}
			if (e->rule == &phonyrule || buildopts.dryrun) {
				work = work->worknext;
				for (i = 0; i < e->nout; ++i)
					nodedone(e->out[i], false);
				continue;
			}
			if (!tokenget(e))
				break;
			work = work->worknext;
			if (next == jobslen) {
				jobslen = jobslen ? jobslen * 2 : 8;
				if (jobslen > buildopts.maxjobs)
					jobslen = buildopts.maxjobs;
				jobs = xreallocarray(jobs, jobslen, sizeof(jobs[0]));
				fds = xreallocarray(fds, jobslen + 1, sizeof(fds[0]));
				for (i = next; i < jobslen; ++i) {
					jobs[i].buf.data = NULL;
					jobs[i].buf.len = 0;
					jobs[i].buf.cap = 0;
					jobs[i].next = i + 1;
					fds[i].fd = -1;
					fds[i].events = POLLIN;
				}
				fds[jobslen].fd = jobserver_rfd;
				fds[jobslen].events = POLLIN;
			}
			fds[next].fd = jobstart(&jobs[next], e);
			if (fds[next].fd < 0) {
				warn("job failed to start");
				++numfail;
			} else {
				next = jobs[next].next;
				++numjobs;
			}
		}
		if (!have_work() && !numjobs)
			break;
		/*
		 * the last slot is for the jobserver and is only used to
		 * kick samurai out of poll()
		 */
		if (poll(fds, jobslen + 1, 5000) < 0 && errno != EINTR)
			fatal("poll:");
		for (i = 0; i < jobslen; ++i) {
			if (fds[i].fd == -1)
				continue;
			else if (killall)
				jobkill(&jobs[i]);
			else if (!fds[i].revents || jobwork(&jobs[i]))
				continue;

			tokenput();
			--numjobs;
			jobs[i].next = next;
			fds[i].fd = -1;
			next = i;
			if (jobs[i].failed)
				++numfail;
		}
	}
	for (i = 0; i < jobslen; ++i)
		free(jobs[i].buf.data);
	free(jobs);
	free(fds);
	if (numfail > 0) {
		if (numfail < buildopts.maxfail)
			fatal("cannot make progress due to previous errors");
		else if (stopsig == SIGINT)
			fatal("interrupted by user");
		else if (stopsig)
			fatal("interrupted by signal %d", stopsig);
		else if (numfail > 1)
			fatal("subcommands failed");
		else
			fatal("subcommand failed");
	}
	ntotal = 0;  /* reset in case we just rebuilt the manifest */
}
