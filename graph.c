#define _POSIX_C_SOURCE 200809L
#include <search.h>
#include <stddef.h>
#include <stdbool.h>
#include <err.h>
#include <errno.h>
#include <sys/stat.h>
#include "graph.h"
#include "util.h"

struct edge *alledges;

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
	ENTRY *e;

	e = hsearch((ENTRY){.key = path}, create ? ENTER : FIND);
	if (!e) {
		if (create)
			err(1, "hsearch");
		return NULL;
	}
	if (create && !e->data)
		e->data = mknode(path);

	return e->data;
}

struct edge *
mkedge(void)
{
	struct edge *e;

	e = xmalloc(sizeof(*e));
	e->nout = 0;
	e->nin = 0;
	e->want = false;
	e->next = alledges;
	alledges = e;

	return e;
}
