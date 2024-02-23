struct node;

struct buildoptions {
	size_t maxjobs, maxfail;
	_Bool verbose, explain, keepdepfile, keeprsp, dryrun;
	const char *statusfmt;
	double maxload;
	int jobserver[2];
};

extern struct buildoptions buildopts;

/* reset state, so a new build can be executed */
void buildreset(void);
/* schedule a particular target to be built */
void buildadd(struct node *);
/* execute rules to build the scheduled targets */
void build(void);
