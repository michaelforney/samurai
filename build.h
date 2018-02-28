struct node;

struct buildoptions {
	int maxjobs, maxfail;
	_Bool verbose;
};

extern struct buildoptions buildopts;

/* schedule a particular target to be built */
void buildadd(struct node *);
/* execute rules to build the scheduled targets */
void build(void);
