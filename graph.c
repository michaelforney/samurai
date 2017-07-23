#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <err.h>
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

void
graphinit(void)
{
	struct node *n;
	struct edge *e;
	size_t i;

	/* delete old nodes and edges in case we rebuilt the manifest */
	if (allnodes) {
		for (i = 0; i < allnodes->sz; ++i) {
			if (!allnodes->hashes[i])
				continue;
			n = allnodes->vals[i];
			if (n->shellpath != n->path)
				free(n->shellpath);
			free(n->path);
			free(n);
		}
		htfree(allnodes);
	}
	while (alledges) {
		e = alledges;
		alledges = e->allnext;
		free(e->out);
		free(e->in);
		free(e);
	}
	allnodes = mkht(1024, strhash, streq);
}

struct node *
mknode(struct string *path)
{
	void **v;
	struct node *n;

	v = htput(allnodes, path->s);
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
	n->mtime.tv_nsec = MTIME_UNKNOWN;
	*v = n;

	return n;
}

struct node *
nodeget(char *path)
{
	return htget(allnodes, path);
}

void
nodestat(struct node *n)
{
	struct stat st;

	if (stat(n->path->s, &st) < 0) {
		if (errno != ENOENT)
			err(1, "stat %s", n->path->s);
		n->mtime.tv_nsec = MTIME_MISSING;
	} else {
		n->mtime = st.st_mtim;
	}
}

void
nodeescape(struct node *n)
{
	char *s, *d;
	bool escape;
	int nquote;

	if (n->shellpath)
		return;
	escape = false;
	nquote = 0;
	for (s = n->path->s; *s; ++s) {
		if (!isalnum(*s) && !strchr("_+-./", *s))
			escape = true;
		if (*s == '\'')
			++nquote;
	}
	if (escape) {
		n->shellpath = mkstr(n->path->n + 2 + 2 * nquote);
		d = n->shellpath->s;
		*d++ = '\'';
		for (s = n->path->s; *s; ++s) {
			*d++ = *s;
			if (*s == '\'') {
				*d++ = '\\';
				*d++ = '\'';
			}
		}
		*d++ = '\'';
	} else {
		n->shellpath = n->path;
	}
}

struct edge *
mkedge(struct environment *parent)
{
	struct edge *e;

	e = xmalloc(sizeof(*e));
	e->env = mkenv(parent);
	e->pool = NULL;
	e->nout = 0;
	e->nin = 0;
	e->nblock = -1;
	e->mark = 0;
	e->allnext = alledges;
	alledges = e;

	return e;
}
