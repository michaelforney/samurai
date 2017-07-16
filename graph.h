/* set in the tv_nsec field of a node's mtime */
enum {
	/* we haven't stat the file yet */
	MTIME_UNKNOWN = -1,
	/* the file does not exist */
	MTIME_MISSING = -2,
};

struct node {
	char *path;
	struct timespec mtime;

	/* does the node need to be rebuilt */
	bool dirty;

	/* generating edge and dependent edges.
	 *
	 * only gen and nuse are set in parse.c:parseedge; use is allocated and
	 * populated in build.c:computedirty. */
	struct edge *gen, **use;
	size_t nuse;
};

struct edge {
	struct rule *rule;
	struct environment *env;

	/* whether or not we need to build this edge */
	bool want;

	/* input and output nodes */
	struct node **out, **in;
	size_t nout, nin;

	/* index of first implicit output */
	size_t outimpidx;
	/* index of first implicit and order-only input */
	size_t inimpidx, inorderidx;

	struct edge *next;
};

void graphinit(void);

/* update the mtime field of a node */
void nodestat(struct node *);
/* lookup a node by name, optionally creating it if it does not exist */
struct node *nodeget(char *, bool);

struct edge *mkedge(void);

extern struct edge *alledges;
