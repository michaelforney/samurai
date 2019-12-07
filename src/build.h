struct node;

struct buildoptions {
	size_t maxjobs, maxfail;
	_Bool verbose, explain, keepdepfile, keeprsp;
};

extern struct buildoptions buildopts;

/* schedule a particular target to be built */
void buildadd(struct node *);
/* execute rules to build the scheduled targets */
void build(void);
