#include <time.h>   /* for struct timespec */

/* set in the tv_nsec field of a node's mtime */
enum {
	/* we haven't stat the file yet */
	MTIME_UNKNOWN = -1,
	/* the file does not exist */
	MTIME_MISSING = -2,
};

struct node {
	/* shellpath is the escaped shell path, and is populated as needed by nodeescape */
	struct string *path, *shellpath;
	struct timespec mtime;

	/* does the node need to be rebuilt */
	_Bool dirty;

	/* generating edge and dependent edges.
	 *
	 * only gen and nuse are set in parse.c:parseedge; use is allocated and
	 * populated in build.c:computedirty. */
	struct edge *gen, **use;
	size_t nuse;
};

struct edge {
	struct rule *rule;
	struct pool *pool;
	struct environment *env;

	/* input and output nodes */
	struct node **out, **in;
	size_t nout, nin;

	/* index of first implicit output */
	size_t outimpidx;
	/* index of first implicit and order-only input */
	size_t inimpidx, inorderidx;

	/* how many remaining inputs we are waiting for. -1 if we don't care about it */
	int nblock;

	/* how far we are with processing this edge */
	enum {
		MARK_STAT = 1,  /* queried the mtime of all outputs */
		MARK_WORK = 2,  /* scheduled for build */
	} mark;

	/* used to coordinate ready work in build() */
	struct edge *worknext;
	/* used for alledges linked list */
	struct edge *allnext;
};

void graphinit(void);

/* create a new node or return existing node */
struct node *mknode(struct string *);
/* lookup a node by name; returns NULL if it does not exist */
struct node *nodeget(char *);
/* update the mtime field of a node */
void nodestat(struct node *);
/* escape a node's path, populating shellpath */
void nodeescape(struct node *);

/* create a new edge with the given parent environment */
struct edge *mkedge(struct environment *parent);

/* a single linked list of all edges, valid up until build() */
extern struct edge *alledges;
