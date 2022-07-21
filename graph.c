#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "env.h"
#include "graph.h"
#include "htab.h"
#include "util.h"

static struct hashtable *allnodes;
struct edge *alledges;

static void
delnode(void *p)
{
	struct node *n = p;

	if (n->shellpath != n->path)
		free(n->shellpath);
	free(n->use);
	free(n->path);
	free(n);
}

void
graphinit(void)
{
	struct edge *e;

	/* delete old nodes and edges in case we rebuilt the manifest */
	delhtab(allnodes, delnode);
	while (alledges) {
		e = alledges;
		alledges = e->allnext;
		free(e->out);
		free(e->in);
		free(e);
	}
	allnodes = mkhtab(1024);
}

struct node *
mknode(struct string *path)
{
	void **v;
	struct node *n;
	struct hashtablekey k;

	htabkey(&k, path->s, path->n);
	v = htabput(allnodes, &k);
	if (*v) {
		free(path);
		return *v;
	}
	n = xmalloc(sizeof(*n));
	n->path = path;
	n->shellpath = NULL;
	n->gen = NULL;
	n->use = NULL;
	n->nuse = 0;
	n->mtime = MTIME_UNKNOWN;
	n->logmtime = MTIME_MISSING;
	n->hash = 0;
	n->id = -1;
	*v = n;

	return n;
}

struct node *
nodeget(const char *path, size_t len)
{
	struct hashtablekey k;

	if (!len)
		len = strlen(path);
	htabkey(&k, path, len);
	return htabget(allnodes, &k);
}

void
nodestat(struct node *n)
{
	struct stat st;

	if (stat(n->path->s, &st) < 0) {
		if (errno != ENOENT)
			fatal("stat %s:", n->path->s);
		n->mtime = MTIME_MISSING;
	} else {
#ifdef __APPLE__
		n->mtime = (int64_t)st.st_mtime * 1000000000 + st.st_mtimensec;
/*
Illumos hides the members of st_mtim when you define _POSIX_C_SOURCE
since it has not been updated to support POSIX.1-2008:
https://www.illumos.org/issues/13327
*/
#elif defined(__sun)
		n->mtime = (int64_t)st.st_mtim.__tv_sec * 1000000000 + st.st_mtim.__tv_nsec;
#else
		n->mtime = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
#endif
	}
}

struct string *
nodepath(struct node *n, bool escape)
{
	char *s, *d;
	int nquote;

	if (!escape)
		return n->path;
	if (n->shellpath)
		return n->shellpath;
	escape = false;
	nquote = 0;
	for (s = n->path->s; *s; ++s) {
		if (!isalnum((int)(*s)) && !strchr("_+-./", *s))
			escape = true;
		if (*s == '\'')
			++nquote;
	}
	if (escape) {
		n->shellpath = mkstr(n->path->n + 2 + 3 * nquote);
		d = n->shellpath->s;
		*d++ = '\'';
		for (s = n->path->s; *s; ++s) {
			*d++ = *s;
			if (*s == '\'') {
				*d++ = '\\';
				*d++ = '\'';
				*d++ = '\'';
			}
		}
		*d++ = '\'';
	} else {
		n->shellpath = n->path;
	}
	return n->shellpath;
}

void
nodeuse(struct node *n, struct edge *e)
{
	/* allocate in powers of two */
	if (!(n->nuse & (n->nuse - 1)))
		n->use = xreallocarray(n->use, n->nuse ? n->nuse * 2 : 1, sizeof(e));
	n->use[n->nuse++] = e;
}

struct edge *
mkedge(struct environment *parent)
{
	struct edge *e;

	e = xmalloc(sizeof(*e));
	e->env = mkenv(parent);
	e->pool = NULL;
	e->out = NULL;
	e->nout = 0;
	e->in = NULL;
	e->nin = 0;
	e->flags = 0;
	e->allnext = alledges;
	alledges = e;

	return e;
}

void
edgehash(struct edge *e)
{
	static const char sep[] = ";rspfile=";
	struct string *cmd, *rsp, *s;

	if (e->flags & FLAG_HASH)
		return;
	e->flags |= FLAG_HASH;
	cmd = edgevar(e, "command", true);
	if (!cmd)
		fatal("rule '%s' has no command", e->rule->name);
	rsp = edgevar(e, "rspfile_content", true);
	if (rsp && rsp->n > 0) {
		s = mkstr(cmd->n + sizeof(sep) - 1 + rsp->n);
		memcpy(s->s, cmd->s, cmd->n);
		memcpy(s->s + cmd->n, sep, sizeof(sep) - 1);
		memcpy(s->s + cmd->n + sizeof(sep) - 1, rsp->s, rsp->n);
		s->s[s->n] = '\0';
		e->hash = murmurhash64a(s->s, s->n);
		free(s);
	} else {
		e->hash = murmurhash64a(cmd->s, cmd->n);
	}
}

static struct edge *
mkphony(struct node *n)
{
	struct edge *e;

	e = mkedge(rootenv);
	e->rule = &phonyrule;
	e->inimpidx = 0;
	e->inorderidx = 0;
	e->outimpidx = 1;
	e->nout = 1;
	e->out = xmalloc(sizeof(n));
	e->out[0] = n;

	return e;
}

void
edgeadddeps(struct edge *e, struct node **deps, size_t ndeps)
{
	struct node **order, *n;
	size_t norder, i;

	for (i = 0; i < ndeps; ++i) {
		n = deps[i];
		if (!n->gen)
			n->gen = mkphony(n);
		nodeuse(n, e);
	}
	e->in = xreallocarray(e->in, e->nin + ndeps, sizeof(e->in[0]));
	order = e->in + e->inorderidx;
	norder = e->nin - e->inorderidx;
	memmove(order + ndeps, order, norder * sizeof(e->in[0]));
	memcpy(order, deps, ndeps * sizeof(e->in[0]));
	e->inorderidx += ndeps;
	e->nin += ndeps;
}
