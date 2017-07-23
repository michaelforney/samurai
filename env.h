struct edge;
struct evalstring;
struct string;

struct rule {
	char *name;
	void *bindings;
};

struct pool {
	char *name;
	int numjobs, maxjobs;

	/* a queue of ready edges blocked by the pool's capacity */
	struct edge *work;
};

void envinit(void);

struct environment *mkenv(struct environment *);
struct string *envvar(struct environment *, char *);
void envaddvar(struct environment *, char *, struct string *);
struct string *enveval(struct environment *, struct evalstring *);
struct rule *envrule(struct environment *, char *);
void envaddrule(struct environment *, struct rule *);

struct rule *mkrule(char *);
void ruleaddvar(struct rule *, char *, struct evalstring *);

/* create a new pool with the given name */
struct pool *mkpool(char *);
/* lookup a pool by name. fails if it does not exist */
struct pool *poolget(char *);

struct string *edgevar(struct edge *, char *);

extern struct environment *rootenv;
extern struct rule phonyrule;
extern struct pool consolepool;
