#include <stdint.h>  /* for uint64_t */

struct hashtable *mkht(size_t);
void htfree(struct hashtable *, void (*)(void *));
void **htput(struct hashtable *, const char *);
void *htget(struct hashtable *, const char *);
void **htkeys(struct hashtable *, size_t *);

uint64_t murmurhash64a(const void *, size_t);
