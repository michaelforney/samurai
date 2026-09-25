struct node;
struct edge;

struct buildoptions {
	size_t maxjobs, maxfail;
	_Bool verbose, explain, keepdepfile, keeprsp, dryrun;
	const char *statusfmt;
	double maxload;
};

extern struct buildoptions buildopts;

/* reset state, so a new build can be executed */
void buildreset(void);
/* schedule a particular target to be built */
void buildadd(struct node *);
/* refresh an edge after dyndep information changes */
void buildupdate(struct edge *);
/* execute rules to build the scheduled targets */
void build(void);
