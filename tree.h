/* binary tree node, such that keys are sorted lexicographically for fast lookup */
struct treenode {
	char *key;
	void *value;
	struct treenode *child[2];
	int height;
};

/* free a tree and its children recursively, free keys and values with a function */
void deltree(struct treenode *, void(void *), void(void *));
/* search a binary tree for a key, return the key's value or NULL */
struct treenode *treefind(struct treenode *, const char *);
/* insert into a binary tree a key and a value, replace and return the old value if the key already exists */
void *treeinsert(struct treenode **, char *, void *);
