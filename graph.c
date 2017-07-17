#define _POSIX_C_SOURCE 200809L
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
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

static struct node *
mknode(char *path)
{
	struct node *n;

	n = xmalloc(sizeof(*n));
	n->path = path;
	n->gen = NULL;
	n->use = NULL;
	n->nuse = 0;
	n->mtime.tv_nsec = MTIME_UNKNOWN;

	return n;
}

void
nodestat(struct node *n)
{
	struct stat st;

	if (stat(n->path, &st) < 0) {
		if (errno != ENOENT)
			err(1, "stat %s", n->path);
		n->mtime.tv_nsec = MTIME_MISSING;
	} else {
		n->mtime = st.st_mtim;
	}
}

struct node *
nodeget(char *path, bool create)
{
	void **n;

	if (!create)
		return htget(allnodes, path);
	n = htput(allnodes, path);
	if (!*n)
		*n = mknode(path);

	return *n;
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
