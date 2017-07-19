#define _POSIX_C_SOURCE 200809L
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "graph.h"
#include "htab.h"
#include "util.h"

static struct hashtable *allnodes;
struct edge *alledges;

void
graphinit(void)
{
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

struct edge *
mkedge(void)
{
	struct edge *e;

	e = xmalloc(sizeof(*e));
	e->nout = 0;
	e->nin = 0;
	e->want = false;
	e->seen = 0;
	e->allnext = alledges;
	alledges = e;

	return e;
}
