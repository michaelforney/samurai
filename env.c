#include <search.h>
#include <stdlib.h>
#include <string.h>
#include "env.h"
#include "graph.h"
#include "lex.h"
#include "util.h"

struct binding {
	char *var;
	void *val;
};

struct environment {
	struct environment *parent;
	void *bindings;
	void *rules;
};

struct environment *rootenv;
struct rule phonyrule = {.name = "phony"};
struct pool consolepool = {.name = "console", .maxjobs = 1};
static void *pools;

static void addpool(struct pool *);

void
envinit(void)
{
	/* TODO: delete old root environment and pools (in case we rebuilt
	 * build.ninja). for now, we leak memory. */
	rootenv = mkenv(NULL);
	envaddrule(rootenv, &phonyrule);
	pools = NULL;
	addpool(&consolepool);
}

static int
bindingcmp(const void *k1, const void *k2)
{
	const struct binding *b1 = k1, *b2 = k2;

	return strcmp(b1->var, b2->var);
}

static void
addvar(void **tree, char *var, void *val)
{
	struct binding *b, **node;

	b = xmalloc(sizeof(*b));
	b->var = var;
	node = tsearch(b, tree, bindingcmp);
	if (!node)
		err(1, "tsearch");
	if (*node != b) {
		free((*node)->val);
		free(var);
		free(b);
	}
	(*node)->val = val;
}

struct environment *
mkenv(struct environment *parent)
{
	struct environment *env;

	env = xmalloc(sizeof(*env));
	env->parent = parent;
	env->bindings = NULL;
	env->rules = NULL;

	return env;
}

struct string *
envvar(struct environment *env, char *var)
{
	struct binding key = {.var = var}, **node;

	do {
		node = tfind(&key, &env->bindings, bindingcmp);
		if (node)
			return (*node)->val;
		env = env->parent;
	} while (env);

	return NULL;
}

void
envaddvar(struct environment *env, char *var, struct string *val)
{
	addvar(&env->bindings, var, val);
}

static struct string *
merge(struct evalstring *str, size_t n)
{
	struct string *result;
	struct evalstringpart *p;
	char *s;

	result = mkstr(n);
	s = result->s;
	if (str) {
		for (p = str->parts; p; p = p->next) {
			if (!p->str)
				continue;
			memcpy(s, p->str->s, p->str->n);
			s += p->str->n;
		}
	}
	*s = '\0';

	return result;
}

struct string *
enveval(struct environment *env, struct evalstring *str)
{
	size_t n;
	struct evalstringpart *p;

	n = 0;
	if (str) {
		for (p = str->parts; p; p = p->next) {
			if (p->var)
				p->str = envvar(env, p->var);
			if (p->str)
				n += p->str->n;
		}
	}

	return merge(str, n);
}

static int
rulecmp(const void *k1, const void *k2)
{
	const struct rule *r1 = k1, *r2 = k2;

	return strcmp(r1->name, r2->name);
}

void
envaddrule(struct environment *env, struct rule *r)
{
	struct rule **node;

	node = tsearch(r, &env->rules, rulecmp);
	if (!node)
		err(1, "tsearch");
	if (*node != r)
		errx(1, "rule %s already defined", r->name);
}

struct rule *
envrule(struct environment *env, char *name)
{
	struct rule key = {.name = name}, **node;

	do {
		node = tfind(&key, &env->rules, rulecmp);
		if (node)
			return *node;
		env = env->parent;
	} while (env);

	return NULL;
}

static struct string *
pathlist(struct node **nodes, size_t n, char sep)
{
	size_t i, len;
	struct string *path, *result;
	char *s;

	if (n == 0)
		return NULL;
	for (i = 0, len = 0; i < n; ++i) {
		nodeescape(nodes[i]);
		len += nodes[i]->shellpath->n;
	}
	result = mkstr(len + n - 1);
	s = result->s;
	for (i = 0; i < n; ++i) {
		path = nodes[i]->shellpath;
		memcpy(s, path->s, path->n);
		s += path->n;
		*s++ = sep;
	}
	*--s = '\0';

	return result;
}

struct rule *
mkrule(char *name)
{
	struct rule *r;

	r = xmalloc(sizeof(*r));
	r->name = name;
	r->bindings = NULL;

	return r;
}

void
ruleaddvar(struct rule *r, char *var, struct evalstring *val)
{
	addvar(&r->bindings, var, val);
}

struct string *
edgevar(struct edge *e, char *var)
{
	struct binding key = {.var = var}, **node;
	struct string *val;
	struct evalstring *str;
	struct evalstringpart *p;
	size_t n;

	node = tfind(&key, &e->env->bindings, bindingcmp);
	if (node && (*node)->val)
		return (*node)->val;
	if (strcmp(var, "in") == 0) {
		val = pathlist(e->in, e->inimpidx, ' ');
	} else if (strcmp(var, "in_newline") == 0) {
		val = pathlist(e->in, e->inimpidx, '\n');
	} else if (strcmp(var, "out") == 0) {
		val = pathlist(e->out, e->outimpidx, ' ');
	} else {
		val = envvar(e->env->parent, var);
		if (val)
			return val;
		node = tfind(&key, &e->rule->bindings, bindingcmp);
		if (!node)
			return NULL;
		str = (*node)->val;
		n = 0;
		if (str) {
			for (p = str->parts; p; p = p->next) {
				if (p->var)
					p->str = edgevar(e, p->var);
				if (p->str)
					n += p->str->n;
			}
		}
		val = merge(str, n);
	}
	envaddvar(e->env, var, val);

	return val;
}

static int
poolcmp(const void *k1, const void *k2)
{
	const struct pool *p1 = k1, *p2 = k2;

	return strcmp(p1->name, p2->name);
}

static void
addpool(struct pool *p)
{
	struct pool **node;

	node = tsearch(p, &pools, poolcmp);
	if (!node)
		err(1, "tsearch");
	if (*node != p)
		errx(1, "pool redefined: %s", p->name);
}

struct pool *
mkpool(char *name)
{
	struct pool *p;

	p = xmalloc(sizeof(*p));
	p->name = name;
	p->numjobs = 0;
	p->maxjobs = 0;
	p->work = NULL;
	addpool(p);

	return p;
}

struct pool *
poolget(char *name)
{
	struct pool **p;

	p = tfind(&(struct pool){.name = name}, &pools, poolcmp);
	if (!p)
		errx(1, "unknown pool: %s", name);

	return *p;
}
