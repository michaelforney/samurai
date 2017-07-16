typedef unsigned long (*hashfn)(void *);
typedef int (*eqfn)(void *, void *);

struct hashtable {
	size_t nelt;
	size_t sz;
	hashfn hash;
	eqfn eq;
	void **keys;
	void **vals;
	unsigned long *hashes;
};

struct hashtable *mkht(size_t, hashfn, eqfn);
void htfree(struct hashtable *);
void **htput(struct hashtable *, void *);
void *htget(struct hashtable *, void *);
int hthas(struct hashtable *, void *);
void **htkeys(struct hashtable *, size_t *);

unsigned long strhash(void *);
int streq(void *, void *);
