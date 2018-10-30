#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "util.h"
#include "platform.h"

struct buildoptions buildopts = {.maxfail = 1};
static struct edge *work;
static size_t nstarted, ntotal;
static bool consoleused;

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
			warnx("explain %s: phony and no inputs", n->path->s);
		return true;
	}
	if (n->mtime == MTIME_MISSING) {
		if (buildopts.explain)
			warnx("explain %s: missing", n->path->s);
		return true;
	}
	if (isnewer(newest, n) && !restat) {
		if (buildopts.explain) {
			warnx("explain %s: older than input '%s': %" PRId64 " vs %" PRId64,
			      n->path->s, newest->path->s, n->mtime, newest->mtime);
		}
		return true;
	}
	if (n->logmtime == MTIME_MISSING) {
		if (!generator) {
			if (buildopts.explain)
				warnx("explain %s: no record in .ninja_log", n->path->s);
			return true;
		}
	} else if (newest && n->logmtime < newest->mtime / 1000000000) {
		if (buildopts.explain) {
			warnx("explain %s: recorded mtime is older than input '%s': %" PRId64 " vs %" PRId64,
			      n->path->s, newest->path->s, n->logmtime, newest->mtime / 1000000000);
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
		if (n->mtime == MTIME_UNKNOWN)
			nodestat(n);
		if (n->mtime == MTIME_MISSING)
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

static bool
jobstart(struct job *j, struct edge *e)
{
	size_t i;
	struct node *n;
	struct string *rspfile, *content, *description;

	++nstarted;
	for (i = 0; i < e->nout; ++i) {
		n = e->out[i];
		if (n->mtime == MTIME_MISSING) {
			if (makedirs(n->path) < 0)
				goto err0;
		}
	}
	rspfile = edgevar(e, "rspfile");  // XXX: should use unescaped $out and $in
	if (rspfile) {
		content = edgevar(e, "rspfile_content");
		if (!writefile(rspfile->s, content))
			goto err0;
	}

	j->edge = e;
	j->cmd = edgevar(e, "command");

	if (!j->cmd) {
		warnx("rule '%s' has no command", e->rule->name);
		goto err1;
	}

	if (!createprocess(j->cmd, &j->process, e->pool != &consolepool)) {
		goto err1;
	}

	if (!consoleused) {
		description = buildopts.verbose ? NULL : edgevar(e, "description");
		if (!description || description->n == 0)
			description = j->cmd;
		printf("[%zu/%zu] %s\n", nstarted, ntotal, description->s);
	}

	j->failed = false;
	if (e->pool == &consolepool)
		consoleused = true;

	return true;

err1:
	if (rspfile)
		remove(rspfile->s);
err0:
	return false;
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
		n->logmtime = newest->mtime / 1000000000;

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
	int64_t old;

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
		n->logmtime = n->mtime == MTIME_MISSING ? 0 : n->mtime / 1000000000;
		nodedone(n, restat && shouldprune(e, n, old));
	}
	rspfile = edgevar(e, "rspfile");
	if (rspfile)
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
	j->failed = waitexit(j);
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
	size_t n;

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

	if (!readprocessoutput(j->process, j->buf.data + j->buf.len, j->buf.cap - j->buf.len, &n))
		goto kill;

	j->buf.len += n;
	jobdone(j);

	return n > 0;

kill:
	killprocess(j->process);
	j->failed = true;

	return false;
}

static void
swapjobs(struct job* j1, struct job* j2)
{
	struct job t = *j1;
	*j1 = *j2;
	*j2 = t;
}

void
build(void)
{
	struct job *jobs;
	size_t i, numjobs = 0, numfail = 0;
	struct edge *e;

	if (!work)
		warnx("nothing to do");

	jobs = malloc(buildopts.maxjobs * sizeof(jobs[0]));
	for (i = 0; i < buildopts.maxjobs; ++i) {
		jobs[i].buf.data = NULL;
		jobs[i].buf.len = 0;
		jobs[i].buf.cap = 0;
	}
	initplatform(buildopts.maxjobs);

	nstarted = 0;
	for (;;) {
		/* start ready edges */
		while (work && numjobs < buildopts.maxjobs && numfail < buildopts.maxfail) {
			e = work;
			work = work->worknext;
			if (e->rule == &phonyrule) {
				for (i = 0; i < e->nout; ++i)
					nodedone(e->out[i], false);
				continue;
			}

			if (!jobstart(&jobs[numjobs], e)) {
				warnx("job failed to start");
				++numfail;
			} else {
				++numjobs;
			}
		}
		if (numjobs == 0)
			break;
		i = waitforjobs(jobs, numjobs);
		if (!jobwork(&jobs[i])) {
			if (jobs[i].failed)
				++numfail;
			--numjobs;
			swapjobs(jobs + i, jobs + numjobs);
		}
	}
	for (i = 0; i < buildopts.maxjobs; ++i)
		free(jobs[i].buf.data);
	free(jobs);
	shutdownplatform();
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
