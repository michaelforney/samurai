struct string;
struct edge;

struct rule {
	char *name;
	void *bindings;
};

struct environment {
	struct environment *parent;
	void *bindings;
	void *rules;
};

struct environment *mkenv(struct environment *);
void envaddvar(struct environment *, char *, char *);
char *enveval(struct environment *, struct string *);
struct rule *envrule(struct environment *, char *);
void envaddrule(struct environment *, struct rule *);

void ruleaddvar(struct rule *, char *, struct string *);

char *edgevar(struct edge *, char *);
