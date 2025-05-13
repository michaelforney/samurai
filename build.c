#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "os.h"
#include "util.h"

struct job {
	struct string *cmd;
	struct edge *edge;
	struct buffer buf;
        size_t next;
	bool failed;
};

struct buildoptions buildopts = {.maxfail = 1};
static struct edge *work;
static size_t nstarted, nfinished, ntotal;
static bool consoleused;
static struct ostimespec starttime;

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
	struct ostimespec endtime;

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
			if (osclock_gettime_monotonic(&endtime) != 0) {
				warn("clock_gettime:");
				break;
			}
			n = snprintf(buf, len, "%.1f", nfinished / ((endtime.tv_sec - starttime.tv_sec) + 0.000000001 * (endtime.tv_nsec - starttime.tv_nsec)));
			break;
		case 'e':
			if (osclock_gettime_monotonic(&endtime) != 0) {
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
jobstart(struct osjob* oj, struct job *j, struct edge *e)
{
	size_t i;
	struct node *n;
	struct string *rspfile, *content;

	++nstarted;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime == MTIME_MISSING) {
			if (osmkdirs(n->path, true) < 0)
				goto err0;
		}
	}
	rspfile = edgevar(e, "rspfile", false);
	if (rspfile) {
		content = edgevar(e, "rspfile_content", true);
		if (writefile(rspfile->s, content) < 0)
			goto err0;
	}
	j->edge = e;
	j->cmd = edgevar(e, "command", true);

	if (!consoleused)
		printstatus(e, j->cmd);

        if (osjob_create(oj, j->cmd, e->pool == &consolepool) < 0) {
		goto err1;
	}

	j->failed = false;
	if (e->pool == &consolepool)
		consoleused = true;
        oj->valid = true;
        return 0;
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
jobdone(struct job *j, struct osjob* oj)
{
	struct edge *e, *new;
	struct pool *p;

	++nfinished;
        if (osjob_done(oj, j->cmd) < 0) {
		j->failed = true;
	}
	if (j->buf.len && (!consoleused || j->failed))
		fwrite(j->buf.data, 1, j->buf.len, stdout);
	j->buf.len = 0;
	e = j->edge;
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

/* returns whether a job still has work to do. if not, sets j->failed */
static bool
jobwork(struct job *j, struct osjob* ojob)
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
        ssize_t result = osjob_work(ojob, j->buf.data + j->buf.len, j->buf.cap - j->buf.len);
	if (result > 0) {
		j->buf.len += result;
		return true;
	} else if (result == 0) {
		goto done;
	} else {
		warn("read:");
kill:
                osjob_close(ojob);
		j->failed = true;
	}
done:
        jobdone(j, ojob);

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

void
build(void)
{
	struct job *jobs = NULL;
    struct osjob* osjobs = NULL;
	struct osjob_ctx osctx = {0};
	size_t i, next = 0, jobslen = 0, maxjobs = buildopts.maxjobs, numjobs = 0, numfail = 0;
	struct edge *e;

	if (ntotal == 0) {
		warn("nothing to do");
		return;
	}

	osclock_gettime_monotonic(&starttime);
	formatstatus(NULL, 0);

	nstarted = 0;
	for (;;) {
		/* limit number of of jobs based on load */
		if (buildopts.maxload)
			maxjobs = queryload() > buildopts.maxload ? 1 : buildopts.maxjobs;
		/* start ready edges */
		while (work && numjobs < maxjobs && numfail < buildopts.maxfail) {
			e = work;
			work = work->worknext;
			if (e->rule != &phonyrule && buildopts.dryrun) {
				++nstarted;
				printstatus(e, edgevar(e, "command", true));
				++nfinished;
			}
			if (e->rule == &phonyrule || buildopts.dryrun) {
				for (i = 0; i < e->nout; ++i)
					nodedone(e->out[i], false);
				continue;
			}
			if (next == jobslen) {
				jobslen = jobslen ? jobslen * 2 : 8;
				if (jobslen > buildopts.maxjobs)
					jobslen = buildopts.maxjobs;
				jobs = xreallocarray(jobs, jobslen, sizeof(jobs[0]));
				osjobs = xreallocarray(osjobs, jobslen, sizeof(osjobs[0]));
                                for (i = next; i < jobslen; ++i) {
                                        jobs[i] = (struct job){0};
                                        jobs[i].next = i + 1;
                                        osjobs[i] = (struct osjob){0};
				}
                        }
                        if (jobstart(&osjobs[next], &jobs[next], e) < 0) {
				warn("job failed to start");
				++numfail;
			} else {
				next = jobs[next].next;
				++numjobs;
			}
		}
		if (numjobs == 0)
			break;
		if (osjob_wait(&osctx, osjobs, jobslen, 5000) < 0)
			fatal("osjob_wait:");
		for (i = 0; i < jobslen; ++i) {
                        if (!osjobs[i].valid || !osjobs[i].has_data || jobwork(&jobs[i], &osjobs[i]))
				continue;
			--numjobs;
			jobs[i].next = next;
                        osjobs[i].valid = false;
			next = i;
			if (jobs[i].failed)
				++numfail;
		}
	}
	for (i = 0; i < jobslen; ++i) {
		free(jobs[i].buf.data);
	}
	free(jobs);
	if (numfail > 0) {
		if (numfail < buildopts.maxfail)
			fatal("cannot make progress due to previous errors");
		else if (numfail > 1)
			fatal("subcommands failed");
		else
			fatal("subcommand failed");
	}
	ntotal = 0;  /* reset in case we just rebuilt the manifest */
}
