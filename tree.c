/* Based on musl's src/search/tsearch_avl.c, by Szabolcs Nagy.
 * See LICENSE file for copyright details. */
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "util.h"

struct treenode {
	char *name;
	void *value;
	struct treenode *left, *right;
	int height;
};


static int
delta(struct treenode *n)
{
	return (n->left ? n->left->height : 0) - (n->right ? n->right->height : 0);
}

static void
updateheight(struct treenode *n)
{
	n->height = 0;
	if (n->left && n->left->height > n->height)
		n->height = n->left->height;
	if (n->right && n->right->height > n->height)
		n->height = n->right->height;
	++n->height;
}

static void
rotl(struct treenode **n)
{
	struct treenode *r = (*n)->right;

	(*n)->right = r->left;
	r->left = *n;
	updateheight(*n);
	updateheight(r);
	*n = r;
}

static void
rotr(struct treenode **n)
{
	struct treenode *l = (*n)->left;

	(*n)->left = l->right;
	l->right = *n;
	updateheight(*n);
	updateheight(l);
	*n = l;
}

static void
balance(struct treenode **n)
{
	int d = delta(*n);

	if (d < -1) {
		if (delta((*n)->right) > 0)
			rotr(&(*n)->right);
		rotl(n);
	} else if (d > 1) {
		if (delta((*n)->left) < 0)
			rotl(&(*n)->left);
		rotr(n);
	} else {
		updateheight(*n);
	}
}

void *
treefind(struct treenode *n, const char *k)
{
	int c;

	if (!n)
		return NULL;
	c = strcmp(k, n->name);
	if (c < 0)
		return treefind(n->left, k);
	if (c > 0)
		return treefind(n->right, k);

	return n->value;
}

void *
treeinsert(struct treenode **n, char *k, void *v)
{
	void *old;
	int c;

	if (!*n) {
		*n = xmalloc(sizeof(**n));
		(*n)->name = k;
		(*n)->value = v;
		(*n)->left = (*n)->right = NULL;
		(*n)->height = 1;
		return NULL;
	}
	c = strcmp(k, (*n)->name);
	if (c == 0) {
		old = (*n)->value;
		(*n)->value = v;
		return old;
	}
	old = treeinsert(c < 0 ? &(*n)->left : &(*n)->right, k, v);
	balance(n);

	return old;
}
