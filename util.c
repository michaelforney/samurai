#define _POSIX_C_SOURCE 200809L
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

extern const char *argv0;

static void
vwarn(const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", argv0);
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		putc(' ', stderr);
		perror(NULL);
	} else {
		putc('\n', stderr);
	}
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void
fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
	exit(1);
}

void *
xmalloc(size_t n)
{
	void *p;

	p = malloc(n);
	if (!p)
		fatal("malloc:");

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
		fatal("reallocarray:");

	return p;
}

char *
xmemdup(const char *s, size_t n)
{
	char *p;

	p = xmalloc(n);
	memcpy(p, s, n);

	return p;
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
		fatal("vsnprintf:");
	n = ret + 1;
	*s = xmalloc(n);
	va_start(ap, fmt);
	ret = vsnprintf(*s, n, fmt, ap);
	va_end(ap);
	if (ret < 0 || (size_t)ret >= n)
		fatal("vsnprintf:");

	return ret;
}

void
bufadd(struct buffer *buf, char c)
{
	if (buf->len >= buf->cap) {
		buf->cap = buf->cap ? buf->cap * 2 : 1<<8;
		buf->data = realloc(buf->data, buf->cap);
		if (!buf->data)
			fatal("realloc:");
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

	if (path->n == 0)
		fatal("empty path");
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
			fatal("path has too many components: %s", path->s);
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
makedirs(struct string *path, bool parent)
{
	int ret;
	struct stat st;
	char *s, *end;

	ret = 0;
	end = path->s + path->n;
	for (s = end - parent; s > path->s; --s) {
		if (*s != '/' && *s)
			continue;
		*s = '\0';
		if (stat(path->s, &st) == 0)
			break;
		if (errno != ENOENT) {
			warn("stat %s:", path->s);
			ret = -1;
			break;
		}
	}
	if (s > path->s && s < end)
		*s = '/';
	while (++s <= end - parent) {
		if (*s != '\0')
			continue;
		if (ret == 0 && mkdir(path->s, 0777) < 0 && errno != EEXIST) {
			warn("mkdir %s:", path->s);
			ret = -1;
		}
		if (s < end)
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
		warn("creat %s:", name);
		return -1;
	}
	ret = 0;
	if (s) {
		for (p = s->s, n = s->n; n > 0; p += nw, n -= nw) {
			nw = write(fd, p, n);
			if (nw <= 0) {
				warn("write:");
				ret = -1;
				break;
			}
		}
	}
	close(fd);

	return ret;
}
