struct edge;
struct evalstring;
struct string;

struct rule {
	char *name;
	void *bindings;
};

void envinit(void);

struct environment *mkenv(struct environment *);
void envaddvar(struct environment *, char *, struct string *);
struct string *enveval(struct environment *, struct evalstring *);
struct rule *envrule(struct environment *, char *);
void envaddrule(struct environment *, struct rule *);

struct rule *mkrule(char *);
void ruleaddvar(struct rule *, char *, struct evalstring *);

struct string *edgevar(struct edge *, char *);

extern struct environment *rootenv;
extern struct rule phonyrule;
