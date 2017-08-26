/* Copied from myrddin's util/htab.c, by Ori Bernstein.
 * See LICENSE file for copyright details. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "htab.h"
#include "util.h"

#define SEED 0xdecafbaddecafbadULL

/* Creates a new empty hash table, using 'sz' as the initial size, 'hash' as the
 * hash function, and 'eq' to verify that there are no hash collisions. */
struct hashtable *
mkht(size_t sz, hashfn hash, eqfn eq)
{
	struct hashtable *ht;

	ht = xmalloc(sizeof(*ht));
	ht->nelt = 0;
	ht->sz = sz;
	ht->hash = hash;
	ht->eq = eq;
	ht->keys = xcalloc(sz, sizeof(ht->keys[0]));
	ht->vals = xcalloc(sz, sizeof(ht->vals[0]));
	ht->hashes = xcalloc(sz, sizeof(ht->hashes[0]));

	return ht;
}

/* Frees a hash table. Passing this function NULL is a no-op. */
void
htfree(struct hashtable *ht)
{
	if (!ht)
		return;
	free(ht->keys);
	free(ht->vals);
	free(ht->hashes);
	free(ht);
}

/* Offsets the hash so that '0' can be used as a 'no valid value' */
static unsigned long
hash(struct hashtable *ht, void *k)
{
	unsigned long h;
	h = ht->hash(k);
	if (h == 0)
		return 1;
	else
		return h;
}

/* Resizes the hash table by copying all the old keys into the right slots in a
 * new table. */
static void
grow(struct hashtable *ht, int sz)
{
	void **oldk;
	void **oldv;
	unsigned long *oldh;
	int oldsz;
	int i;

	oldk = ht->keys;
	oldv = ht->vals;
	oldh = ht->hashes;
	oldsz = ht->sz;

	ht->nelt = 0;
	ht->sz = sz;
	ht->keys = xcalloc(sz, sizeof(ht->keys[0]));
	ht->vals = xcalloc(sz, sizeof(ht->vals[0]));
	ht->hashes = xcalloc(sz, sizeof(ht->hashes[0]));

	for (i = 0; i < oldsz; i++) {
		if (oldh[i])
			*htput(ht, oldk[i]) = oldv[i];
	}

	free(oldh);
	free(oldk);
	free(oldv);
}

/* Inserts or retrieves 'k' from the hash table, possibly killing any previous
 * key that compare as equal. */
void **
htput(struct hashtable *ht, void *k)
{
	int i;
	unsigned long h;
	int di;

	if (ht->sz < ht->nelt * 2)
		grow(ht, ht->sz * 2);

	di = 0;
	h = hash(ht, k);
	i = h & (ht->sz - 1);
	while (ht->hashes[i]) {
		if (ht->hashes[i] == h && ht->eq(ht->keys[i], k))
			return &ht->vals[i];
		di++;
		i = (h + di) & (ht->sz - 1);
	}
	ht->nelt++;
	ht->hashes[i] = h;
	ht->keys[i] = k;

	return &ht->vals[i];
}

/* Finds the index that we would insert the key into */
static ssize_t
htidx(struct hashtable *ht, void *k)
{
	ssize_t i;
	unsigned long h;
	int di;

	di = 0;
	h = hash(ht, k);
	i = h & (ht->sz - 1);
	for (;;) {
		if (!ht->hashes[i])
			return -1;
		if (ht->hashes[i] == h && ht->eq(ht->keys[i], k))
			return i;
		di++;
		i = (h + di) & (ht->sz - 1);
	}
}

/* Looks up a key, returning NULL if the value is not present. Note, if NULL is
 * a valid value, you need to check with hthas() to see if it's not there */
void *
htget(struct hashtable *ht, void *k)
{
	ssize_t i;

	i = htidx(ht, k);
	if (i < 0)
		return NULL;
	else
		return ht->vals[i];
}

/* Tests for 'k's presence in 'ht' */
int
hthas(struct hashtable *ht, void *k)
{
	return htidx(ht, k) >= 0;
}

uint64_t
murmurhash64a(void *ptr, size_t len)
{
	uint64_t m = 0xc6a4a7935bd1e995ULL;
	uint64_t h, k, n;
	uint8_t *p, *end;
	int r = 47;

	h = SEED ^ (len * m);
	n = len & ~0x7ull;
	end = ptr;
	end += n;
	for (p = ptr; p != end; p += 8) {
		memcpy(&k, p, sizeof(k));

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	switch (len & 0x7) {
	case 7: h ^= (uint64_t)p[6] << 48;  /* fallthrough */
	case 6: h ^= (uint64_t)p[5] << 40;  /* fallthrough */
	case 5: h ^= (uint64_t)p[4] << 32;  /* fallthrough */
	case 4: h ^= (uint64_t)p[3] << 24;  /* fallthrough */
	case 3: h ^= (uint64_t)p[2] << 16;  /* fallthrough */
	case 2: h ^= (uint64_t)p[1] <<  8;  /* fallthrough */
	case 1: h ^= (uint64_t)p[0];
		h *= m;
	}

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

unsigned long
strhash(void *s)
{
	if (!s)
		return SEED;
	return murmurhash64a(s, strlen(s));
}

int
streq(void *a, void *b)
{
	if (a == b)
		return 1;
	if (a == NULL || b == NULL)
		return 0;
	return !strcmp(a, b);
}
