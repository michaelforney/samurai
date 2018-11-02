#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "os.h"
#include "util.h"

char *argv0;

static void
vwarn(int errnum, const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", argv0);
	vfprintf(stderr, fmt, ap);
	if (errnum)
		fprintf(stderr, ": %s", strerror(errnum));
	fputc('\n', stderr);
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(errno, fmt, ap);
	va_end(ap);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(0, fmt, ap);
	va_end(ap);
}

void
err(int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(errno, fmt, ap);
	va_end(ap);
	exit(status);
}

void
errx(int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(0, fmt, ap);
	va_end(ap);
	exit(status);
}

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

int
xasprintf(char **s, const char *fmt, ...)
{
	va_list ap;
	int ret;
	size_t n;

	va_start(ap, fmt);
	ret = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (ret < 0)
		err(1, "vsnprintf");
	n = ret + 1;
	*s = xmalloc(n);
	va_start(ap, fmt);
	ret = vsnprintf(*s, n, fmt, ap);
	va_end(ap);
	if (ret < 0 || (size_t)ret >= n)
		err(1, "vsnprintf");

	return ret;
}

void
bufadd(struct buffer *buf, char c)
{
	if (buf->len >= buf->cap) {
		buf->cap = buf->cap ? buf->cap * 2 : 1<<8;
		buf->data = xrealloc(buf->data, buf->cap);
	}
	buf->data[buf->len++] = c;
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
delevalstr(struct evalstring *str)
{
	struct evalstringpart *p, *next;

	if (!str)
		return;
	for (p = str->parts; p; p = next) {
		next = p->next;
		if (p->var)
			free(p->var);
		else
			free(p->str);
		free(p);
	}
	free(str);
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

bool
writefile(const char *name, struct string *s)
{
	FILE *file = fopen(name, "w");
	if (file == NULL) {
		warn("fopen %s", name);
		return false;
	}

	bool ok = true;
	if (s != NULL) {
		size_t nw = fwrite(s->s, 1, s->n, file);
		if (nw != s->n || ferror(file))
			ok = false;
	}
	fclose(file);

	return ok;
}
