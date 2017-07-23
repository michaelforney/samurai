struct node;

/* schedule a particular target to be built */
void buildadd(struct node *);
/* execute rules to build the scheduled targets */
void build(int maxjobs, int maxfail);
