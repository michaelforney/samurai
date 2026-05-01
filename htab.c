#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "htab.h"

struct hashtable {
	size_t len, cap;
	struct hashtablekey *keys;
	void **vals;
};

void
htabkey(struct hashtablekey *k, const char *s, size_t n)
{
	k->str = s;
	k->len = n;
	k->hash = rapidhashv1(s, n);
}

struct hashtable *
mkhtab(size_t cap)
{
	struct hashtable *h;
	size_t i;

	assert(!(cap & (cap - 1)));
	h = xmalloc(sizeof(*h));
	h->len = 0;
	h->cap = cap;
	h->keys = xreallocarray(NULL, cap, sizeof(h->keys[0]));
	h->vals = xreallocarray(NULL, cap, sizeof(h->vals[0]));
	for (i = 0; i < cap; ++i)
		h->keys[i].str = NULL;

	return h;
}

void
delhtab(struct hashtable *h, void del(void *))
{
	size_t i;

	if (!h)
		return;
	if (del) {
		for (i = 0; i < h->cap; ++i) {
			if (h->keys[i].str)
				del(h->vals[i]);
		}
	}
	free(h->keys);
	free(h->vals);
	free(h);
}

static bool
keyequal(struct hashtablekey *k1, struct hashtablekey *k2)
{
	if (k1->hash != k2->hash || k1->len != k2->len)
		return false;
	return memcmp(k1->str, k2->str, k1->len) == 0;
}

static size_t
keyindex(struct hashtable *h, struct hashtablekey *k)
{
	size_t i;

	i = k->hash & (h->cap - 1);
	while (h->keys[i].str && !keyequal(&h->keys[i], k))
		i = (i + 1) & (h->cap - 1);
	return i;
}

void **
htabput(struct hashtable *h, struct hashtablekey *k)
{
	struct hashtablekey *oldkeys;
	void **oldvals;
	size_t i, j, oldcap;

	if (h->cap / 2 < h->len) {
		oldkeys = h->keys;
		oldvals = h->vals;
		oldcap = h->cap;
		h->cap *= 2;
		h->keys = xreallocarray(NULL, h->cap, sizeof(h->keys[0]));
		h->vals = xreallocarray(NULL, h->cap, sizeof(h->vals[0]));
		for (i = 0; i < h->cap; ++i)
			h->keys[i].str = NULL;
		for (i = 0; i < oldcap; ++i) {
			if (oldkeys[i].str) {
				j = keyindex(h, &oldkeys[i]);
				h->keys[j] = oldkeys[i];
				h->vals[j] = oldvals[i];
			}
		}
		free(oldkeys);
		free(oldvals);
	}
	i = keyindex(h, k);
	if (!h->keys[i].str) {
		h->keys[i] = *k;
		h->vals[i] = NULL;
		++h->len;
	}

	return &h->vals[i];
}

void *
htabget(struct hashtable *h, struct hashtablekey *k)
{
	size_t i;

	i = keyindex(h, k);
	return h->keys[i].str ? h->vals[i] : NULL;
}

static inline uint_least32_t
getle32(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = b[0] & 0xfful;
	v |= (b[1] & 0xfful) << 8;
	v |= (b[2] & 0xfful) << 16;
	v |= (b[3] & 0xfful) << 24;
	return v;
}

static inline uint_least64_t
getle64(const void *p)
{
	const unsigned char *b = p;
	uint_least64_t v;

	v = b[0] & 0xffull;
	v |= (b[1] & 0xffull) << 8;
	v |= (b[2] & 0xffull) << 16;
	v |= (b[3] & 0xffull) << 24;
	v |= (b[4] & 0xffull) << 32;
	v |= (b[5] & 0xffull) << 40;
	v |= (b[6] & 0xffull) << 48;
	v |= (b[7] & 0xffull) << 56;
	return v;
}

#if __STDC_VERSION__ >= 202311L && BITINT_MAXWIDTH >= 128
#define uint128 unsigned _BitInt(128)
#elif __SIZEOF_INT128__
#define uint128 __uint128_t
#endif

static inline void
mum(uint64_t *a, uint64_t *b)
{
#ifdef uint128
	uint128 r;

	r = *a;
	r *= *b;
	*a = r;
	*b = r >> 64;
#else
	uint64_t al, ah, bl, bh, rl, rh;
	uint64_t ll, lh, hl, hh, m;

	al = (uint32_t)*a;
	bl = (uint32_t)*b;
	ah = *a >> 32;
	bh = *b >> 32;
	ll = al * bl;
	lh = al * bh;
	hl = ah * bl;
	hh = ah * bh;

	m = lh + hl;
	rl = ll + (m << 32);
	rh = hh + (m >> 32) + ((uint64_t)(m < lh) << 32) + (rl < ll);
	*a = rl;
	*b = rh;
#endif
}

static inline uint64_t
mix(uint64_t a, uint64_t b)
{
	mum(&a, &b);
	return a ^ b;
}

uint64_t
rapidhashv1(const void *ptr, size_t len)
{
	static const uint64_t secret[] = {
		0x2d358dccaa6c78a5ull,
		0x8bb84b93962eacc9ull,
		0x4b33a62ed433d4a3ull,
	};
	uint64_t seed[3];
	const unsigned char *pos, *end;
	int i;

	pos = ptr;
	end = pos + len;
	seed[0] = 0xbdd89aa982704029ull;
	seed[0] ^= mix(seed[0] ^ secret[0], secret[1]) ^ len;
	if (len == 0) {
		seed[1] = seed[2] = 0;
	} else if (len < 4) {
		seed[1] = (uint64_t)pos[0] << 56 | (uint64_t)pos[len > 1] << 32 | end[-1];
		seed[2] = 0;
	} else if (len <= 16) {
		seed[1] = (uint64_t)getle32(pos) << 32 | getle32(end - 4);
		if (len >= 8)
			pos += 4, end -= 4;
		seed[2] = (uint64_t)getle32(pos) << 32 | getle32(end - 4);
	} else {
		seed[1] = seed[2] = seed[0];
		if (len > 48) {
			do {
				for (i = 0; i < 3; ++i, pos += 16)
					seed[i] = mix(getle64(pos) ^ secret[i], getle64(pos + 8) ^ seed[i]);
			} while (end - pos >= 48);
			seed[0] ^= seed[1] ^ seed[2];
		}
		if (end - pos > 16) {
			seed[0] ^= secret[1];
			do seed[0] = mix(getle64(pos) ^ secret[2], getle64(pos + 8) ^ seed[0]);
			while (pos += 16, end - pos > 16);
		}
		seed[1] = getle64(end - 16);
		seed[2] = getle64(end -  8);
	}
	seed[1] ^= secret[1];
	seed[2] ^= seed[0];
	mum(&seed[1], &seed[2]);
	return mix(seed[1] ^ secret[0] ^ len, seed[2] ^ secret[1]);
}
