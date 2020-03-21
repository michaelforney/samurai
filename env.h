struct evalstring;
struct string;

struct rule {
	char *name;
	struct treenode *bindings;
};

struct pool {
	char *name;
	int numjobs, maxjobs;

	/* a queue of ready edges blocked by the pool's capacity */
	struct edge *work;
};

void envinit(void);

/* create a new environment, with an optional parent */
struct environment *mkenv(struct environment *);
/* search environment and its parents for a variable, return the value or NULL if not found */
struct string *envvar(struct environment *, char *);
/* add to environment a variable and its value, replacing the old value if there is one */
void envaddvar(struct environment *, char *, struct string *);
/* using an environment and its parents variables, evaluate an unevaluated string, returning the result */
struct string *enveval(struct environment *, struct evalstring *);
/* search an environment and its parents for a rule, return the rule or NULL if not found */
struct rule *envrule(struct environment *, char *);
/* add a rule to an environment, fails if the rule already exists */
void envaddrule(struct environment *, struct rule *);

/* create rule with given name, bindings not allocated */
struct rule *mkrule(char *);
/* add to rule a variable and its value */
void ruleaddvar(struct rule *, char *, struct evalstring *);

/* create a new pool with the given name */
struct pool *mkpool(char *);
/* lookup a pool by name. fails if it does not exist */
struct pool *poolget(char *);

/* return an edge's variable evaluated, optionally shell-escaped */
struct string *edgevar(struct edge *, char *, _Bool);

extern struct environment *rootenv;
extern struct rule phonyrule;
extern struct pool consolepool;
