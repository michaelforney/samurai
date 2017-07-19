struct evalstring;
struct edge;

struct rule {
	char *name;
	void *bindings;
};

struct environment *mkenv(struct environment *);
void envaddvar(struct environment *, char *, char *);
char *enveval(struct environment *, struct evalstring *);
struct rule *envrule(struct environment *, char *);
void envaddrule(struct environment *, struct rule *);

struct rule *mkrule(char *);
void ruleaddvar(struct rule *, char *, struct evalstring *);

char *edgevar(struct edge *, char *);

extern struct rule *phonyrule;
