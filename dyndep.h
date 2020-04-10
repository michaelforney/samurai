struct node;

struct dyndep {
	/* the node building the dyndep file */
	struct node *node;

	/* edges using this dyndep */
	struct edge **use;
	size_t nuse;

	/* nodes this dyndep updated */
	struct node **update;
	size_t nupdate;

	/* is this dyndep file already loaded */
	_Bool done;

	/* used for alldyndeps linked list */
	struct dyndep *allnext;
};

void dyndepinit(void);

/* create a new dyndep or return existing dyndep */
struct dyndep *mkdyndep(struct node *);
/* load the dyndep */
bool dyndepload(struct dyndep *, bool);
/* record the usage of a dyndep by an edge */
void dyndepuse(struct dyndep *, struct edge *);
