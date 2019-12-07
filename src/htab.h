#include <stdint.h>  /* for uint64_t */

struct hashtablekey {
	uint64_t hash;
	const char *str;
	size_t len;
};

void htabkey(struct hashtablekey *, const char *, size_t);

struct hashtable *mkhtab(size_t);
void delhtab(struct hashtable *, void(void *));
void **htabput(struct hashtable *, struct hashtablekey *);
void *htabget(struct hashtable *, struct hashtablekey *);

uint64_t murmurhash64a(const void *, size_t);
