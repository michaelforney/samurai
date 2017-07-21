#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include "util.h"

void *
xmalloc(size_t n)
{
	void *p;

	p = malloc(n);
	if (!p)
		err(1, "malloc");

	return p;
}

void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n);
	if (!p)
		err(1, "realloc");

	return p;
}

void *
xcalloc(size_t n, size_t sz)
{
	void *p;

	p = calloc(n, sz);
	if (!p)
		err(1, "calloc");

	return p;
}

char *
xstrdup(const char *s, size_t n)
{
	char *r;

	r = xmalloc(n + 1);
	memcpy(r, s, n);
	r[n] = '\0';

	return r;
}

struct string *
mkstr(size_t n)
{
	struct string *str;

	str = xmalloc(sizeof(*str) + n + 1);
	str->n = n;

	return str;
}

void
canonpath(struct string *path)
{
	char *component[60];
	int n;
	char *s, *d, *end;

	s = d = path->s;
	end = path->s + path->n;
	n = 0;
	if (*s == '/') {
		++s;
		++d;
	}
	while (s < end) {
		switch (s[0]) {
		case  '/':
			++s;
			continue;
		case '.':
			switch (s[1]) {
			case '\0': case '/':
				s += 2;
				continue;
			case '.':
				if (s[2] != '/' && s[2] != '\0')
					break;
				if (n > 0) {
					d = component[--n];
				} else {
					*d++ = s[0];
					*d++ = s[1];
					*d++ = s[2];
				}
				s += 3;
				continue;
			}
		}
		if (n == LEN(component))
			errx(1, "path has too many components: %s", path->s);
		component[n++] = d;
		while (*s != '/' && *s != '\0')
			*d++ = *s++;
		*d++ = *s++;
	}
	if (d == path->s) {
		*d++ = '.';
		*d = '\0';
	} else {
		*--d = '\0';
	}
	path->n = d - path->s;
}
