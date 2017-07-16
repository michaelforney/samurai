struct string;
struct edge;

struct rule {
	char *name;
	void *bindings;
};

struct environment *mkenv(struct environment *);
void envaddvar(struct environment *, char *, char *);
char *enveval(struct environment *, struct string *);
struct rule *envrule(struct environment *, char *);
void envaddrule(struct environment *, struct rule *);

struct rule *mkrule(char *);
void ruleaddvar(struct rule *, char *, struct string *);

char *edgevar(struct edge *, char *);
