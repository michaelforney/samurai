#define _POSIX_C_SOURCE 200809L
#include <err.h>
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "env.h"
#include "graph.h"
#include "lex.h"
#include "util.h"

struct binding {
	char *var, *val;
};

struct rulebinding {
	char *var;
	struct string *val;
};

struct environment {
	struct environment *parent;
	void *bindings;
	void *rules;
};

struct rule *phonyrule;

static int
bindingcmp(const void *k1, const void *k2)
{
	const struct binding *b1 = k1, *b2 = k2;

	return strcmp(b1->var, b2->var);
}

static int
rulebindingcmp(const void *k1, const void *k2)
{
	const struct rulebinding *b1 = k1, *b2 = k2;

	return strcmp(b1->var, b2->var);
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

static char *
lookupvar(struct environment *env, char *var)
{
	struct binding key = {var}, **node;

	node = tfind(&key, &env->bindings, bindingcmp);
	if (!node) {
		if (!env->parent)
			return NULL;
		return lookupvar(env->parent, var);
	}

	return (*node)->val;
}

void
envaddvar(struct environment *env, char *var, char *val)
{
	struct binding *b, **node;

	b = xmalloc(sizeof(*b));
	b->var = var;
	node = tsearch(b, &env->bindings, bindingcmp);
	if (!node)
		err(1, "tsearch");
	if (*node != b) {
		free((*node)->val);
		free(var);
		free(b);
	}
	(*node)->val = val;
}

char *
enveval(struct environment *env, struct string *str)
{
	size_t n;
	struct stringpart *p;
	char *result, *s;

	n = 0;
	for (p = str ? str->parts : NULL; p; p = p->next) {
		if (p->var) {
			p->str = lookupvar(env, p->var);
			p->len = p->str ? strlen(p->str) : 0;
		}
		n += p->len;
	}
	result = xmalloc(n + 1);
	s = result;
	for (p = str ? str->parts : NULL; p; p = p->next) {
		memcpy(s, p->str, p->len);
		s += p->len;
	}
	*s = '\0';

	return result;
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
	struct rule key = {name}, **node;

	node = tfind(&key, &env->rules, rulecmp);
	if (!node) {
		if (!env->parent)
			errx(1, "undefined rule %s", name);
		return envrule(env->parent, name);
	}

	return *node;
}

char *
pathlist(struct node **nodes, size_t n)
{
	size_t i, len;
	char *result, *s;

	for (i = 0, len = 0; i < n; ++i)
		len += strlen(nodes[i]->path); // XXX: store length
	result = xmalloc(len + n);
	s = result;
	for (i = 0; i < n; ++i) {
		s = stpcpy(s, nodes[i]->path);
		*s++ = ' ';
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
ruleaddvar(struct rule *r, char *var, struct string *val)
{
	struct rulebinding *b, **node;

	b = xmalloc(sizeof(*b));
	b->var = var;
	b->val = 0;
	node = tsearch(b, &r->bindings, rulebindingcmp);
	if (!node)
		err(1, "tsearch");
	if (*node != b) {
		free((*node)->val);
		free(var);
		free(b);
	}
	(*node)->val = val;
}

char *
edgevar(struct edge *e, char *var)
{
	struct rulebinding key = {var}, **node;
	struct string *str;
	struct stringpart *p;
	char *result, *s;
	size_t n;

	if (strcmp(var, "in") == 0) {
		return pathlist(e->in, e->inimpidx);
	} else if (strcmp(var, "out") == 0) {
		return pathlist(e->out, e->outimpidx);
	}

	result = lookupvar(e->env, var);
	if (result)
		return result;

	node = tfind(&key, &e->rule->bindings, rulebindingcmp);
	if (!node)
		return NULL;
	str = (*node)->val;

	// XXX: reduce duplication with enveval
	n = 0;
	for (p = str ? str->parts : NULL; p; p = p->next) {
		if (p->var) {
			p->str = edgevar(e, p->var);
			p->len = p->str ? strlen(p->str) : 0;
		}
		n += p->len;
	}
	result = xmalloc(n + 1);
	s = result;
	for (p = str ? str->parts : NULL; p; p = p->next) {
		memcpy(s, p->str, p->len);
		s += p->len;
	}
	*s = '\0';

	return result;
}
