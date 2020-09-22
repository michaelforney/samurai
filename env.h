#pragma once

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

/* create a new environment with an optional parent */
struct environment *mkenv(struct environment *);
/* search environment and its parents for a variable, returning the value or NULL if not found */
struct string *envvar(struct environment *, char *);
/* add to environment a variable and its value, replacing the old value if there is one */
void envaddvar(struct environment *, char *, struct string *);
/* evaluate an unevaluated string within an environment, returning the result */
struct string *enveval(struct environment *, struct evalstring *);
/* search an environment and its parents for a rule, returning the rule or NULL if not found */
struct rule *envrule(struct environment *, char *);
/* add a rule to an environment, or fail if the rule already exists */
void envaddrule(struct environment *, struct rule *);

/* create a new rule with the given name */
struct rule *mkrule(char *);
/* add to rule a variable and its value */
void ruleaddvar(struct rule *, char *, struct evalstring *);

/* create a new pool with the given name */
struct pool *mkpool(char *);
/* lookup a pool by name, or fail if it does not exist */
struct pool *poolget(char *);

/* evaluate and return an edge's variable, optionally shell-escaped */
struct string *edgevar(struct edge *, char *, _Bool);

extern struct environment *rootenv;
extern struct rule phonyrule;
extern struct pool consolepool;
