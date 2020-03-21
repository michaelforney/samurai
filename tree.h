struct treenode;

void deltree(struct treenode *, void(void *), void(void *));
void *treefind(struct treenode *, const char *);
void *treeinsert(struct treenode **, char *, void *);
