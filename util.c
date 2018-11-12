#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util.h"

extern char *argv0;

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
xcalloc(size_t n, size_t sz)
{
	void *p;

	p = calloc(n, sz);
	if (!p)
		err(1, "calloc");

	return p;
}

static void *
reallocarray(void *p, size_t n, size_t m)
{
	if (m && n > SIZE_MAX / m) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(p, n * m);
}

void *
xreallocarray(void *p, size_t n, size_t m)
{
	p = reallocarray(p, n, m);
	if (!p)
		err(1, "reallocarray");

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

void
bufadd(struct buffer *buf, char c)
{
	if (buf->len >= buf->cap) {
		buf->cap = buf->cap ? buf->cap * 2 : 1<<8;
		buf->data = realloc(buf->data, buf->cap);
		if (!buf->data)
			err(1, "realloc");
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

int
makedirs(struct string *path)
{
	int ret;
	struct stat st;
	char *s, *end;
	bool missing;

	ret = 0;
	missing = false;
	end = path->s + path->n;
	for (s = end - 1; s > path->s; --s) {
		if (*s != '/')
			continue;
		*s = '\0';
		if (stat(path->s, &st) == 0)
			break;
		if (errno != ENOENT) {
			warn("stat %s", path->s);
			ret = -1;
			break;
		}
		missing = true;
	}
	if (s > path->s)
		*s = '/';
	if (!missing)
		return ret;
	for (++s; s < end; ++s) {
		if (*s != '\0')
			continue;
		if (ret == 0 && mkdir(path->s, 0777) < 0 && errno != EEXIST) {
			warn("mkdir %s", path->s);
			ret = -1;
		}
		*s = '/';
	}

	return ret;
}

int
writefile(const char *name, struct string *s)
{
	int fd, ret;
	const char *p;
	size_t n;
	ssize_t nw;

	fd = creat(name, 0666);
	if (fd < 0) {
		warn("creat %s", name);
		return -1;
	}
	ret = 0;
	if (s) {
		for (p = s->s, n = s->n; n > 0; p += nw, n -= nw) {
			nw = write(fd, p, n);
			if (nw <= 0) {
				warn("write");
				ret = -1;
				break;
			}
		}
	}
	close(fd);

	return ret;
}
