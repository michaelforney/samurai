/* binary tree node, such that keys are sorted lexicographically for fast lookup */
struct treenode;

/* free a tree and its' children recursively, free values with a function */
void deltree(struct treenode *, void(void *));

/* search a binary tree for a key, return the key's value or NULL*/
void *treefind(struct treenode *, const char *);
/* insert into a binary tree a key and a value, return and replace the old treenode if the key already exists */
void *treeinsert(struct treenode **, char *, void *);
