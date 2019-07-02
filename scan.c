#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scan.h"
#include "util.h"

#define getc getc_unlocked

static struct buffer buf;

void
scaninit(struct scanner *s, const char *path)
{
	s->path = path;
	s->line = 1;
	s->col = 1;
	s->f = fopen(path, "r");
	if (!s->f)
		fatal("open %s:", path);
	s->chr = getc(s->f);
}

void
scanclose(struct scanner *s)
{
	fclose(s->f);
}

void
scanerror(struct scanner *s, const char *fmt, ...)
{
	extern const char *argv0;
	va_list ap;

	fprintf(stderr, "%s: %s:%d:%d: ", argv0, s->path, s->line, s->col);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);
	exit(1);
}

static int
next(struct scanner *s)
{
	if (s->chr == '\n') {
		++s->line;
		s->col = 1;
	} else {
		++s->col;
	}
	s->chr = getc(s->f);

	return s->chr;
}

static int
issimplevar(int c)
{
	return isalnum(c) || c == '_' || c == '-';
}

static int
isvar(int c)
{
	return issimplevar(c) || c == '.';
}

static void
crlf(struct scanner *s)
{
	if (next(s) != '\n')
		scanerror(s, "expected '\\n' after '\\r'");
}

static bool
singlespace(struct scanner *s) {
	int c;

	switch (s->chr) {
	case '$':
		c = getc(s->f);
		switch (c) {
		case '\r':
			crlf(s);
			/* fallthrough */
		case '\n':
			break;
		default:
			ungetc(c, s->f);
			return false;
		}
		/* fallthrough */
	case ' ':
		next(s);
		return true;
	}
	return false;
}

static bool
space(struct scanner *s)
{
	if (!singlespace(s))
		return false;
	while (singlespace(s))
		;
	return true;
}

static bool
comment(struct scanner *s)
{
	if (s->chr != '#')
		return false;
	do next(s);
	while (s->chr != '\n');
	next(s);
	return true;
}

static void
name(struct scanner *s)
{
	buf.len = 0;
	while (isvar(s->chr)) {
		bufadd(&buf, s->chr);
		next(s);
	}
	if (!buf.len)
		scanerror(s, "expected name");
	bufadd(&buf, '\0');
	space(s);
}

int
scankeyword(struct scanner *s, char **var)
{
	/* must stay in sorted order */
	static const struct {
		const char *name;
		int value;
	} keywords[] = {
		{"build",    BUILD},
		{"default",  DEFAULT},
		{"include",  INCLUDE},
		{"pool",     POOL},
		{"rule",     RULE},
		{"subninja", SUBNINJA},
	};
	int low = 0, high = LEN(keywords) - 1, mid, cmp;

	for (;;) {
		switch (s->chr) {
		case ' ':
			space(s);
			if (s->chr != '#')
				scanerror(s, "unexpected indent");
			/* fallthrough */
		case '#':
			comment(s);
			break;
		case '\r':
			crlf(s);
			/* fallthrough */
		case '\n':
			next(s);
			break;
		case EOF:
			return EOF;
		default:
			name(s);
			while (low <= high) {
				mid = (low + high) / 2;
				cmp = strcmp(buf.data, keywords[mid].name);
				if (cmp == 0)
					return keywords[mid].value;
				if (cmp < 0)
					high = mid - 1;
				else
					low = mid + 1;
			}
			*var = xmemdup(buf.data, buf.len);
			return VARIABLE;
		}
	}
}

char *
scanname(struct scanner *s)
{
	name(s);
	return xmemdup(buf.data, buf.len);
}

static void
addstringpart(struct evalstringpart ***end, bool var)
{
	struct evalstringpart *p;

	p = xmalloc(sizeof(*p));
	p->next = NULL;
	**end = p;
	if (var) {
		bufadd(&buf, '\0');
		p->var = xmemdup(buf.data, buf.len);
	} else {
		p->var = NULL;
		p->str = mkstr(buf.len);
		memcpy(p->str->s, buf.data, buf.len);
		p->str->s[buf.len] = '\0';
	}
	*end = &p->next;
	buf.len = 0;
}

static void
escape(struct scanner *s, struct evalstringpart ***end)
{
	switch (s->chr) {
	case '$':
	case ' ':
	case ':':
		bufadd(&buf, s->chr);
		next(s);
		break;
	case '{':
		if (buf.len > 0)
			addstringpart(end, false);
		while (isvar(next(s)))
			bufadd(&buf, s->chr);
		if (s->chr != '}')
			scanerror(s, "invalid variable name");
		next(s);
		addstringpart(end, true);
		break;
	case '\r':
		crlf(s);
		/* fallthrough */
	case '\n':
		next(s);
		space(s);
		break;
	default:
		if (buf.len > 0)
			addstringpart(end, false);
		while (issimplevar(s->chr)) {
			bufadd(&buf, s->chr);
			next(s);
		}
		if (!buf.len)
			scanerror(s, "invalid $ escape");
		addstringpart(end, true);
	}
}

struct evalstring *
scanstring(struct scanner *s, bool path)
{
	struct evalstring *str;
	struct evalstringpart *parts = NULL, **end = &parts;

	buf.len = 0;
	for (;;) {
		switch (s->chr) {
		case '$':
			next(s);
			escape(s, &end);
			break;
		case ':':
		case '|':
		case ' ':
			if (path)
				goto out;
			/* fallthrough */
		default:
			bufadd(&buf, s->chr);
			next(s);
			break;
		case '\r':
		case '\n':
		case EOF:
			goto out;
		}
	}
out:
	if (buf.len > 0)
		addstringpart(&end, 0);
	if (path)
		space(s);
	if (!parts)
		return NULL;
	str = xmalloc(sizeof(*str));
	str->parts = parts;

	return str;
}

void
scanchar(struct scanner *s, int c)
{
	if (s->chr != c)
		scanerror(s, "expected '%c'", c);
	next(s);
	space(s);
}

int
scanpipe(struct scanner *s, int n)
{
	if (s->chr != '|')
		return 0;
	next(s);
	if (s->chr != '|') {
		if (!(n & 1))
			scanerror(s, "expected '||'");
		space(s);
		return 1;
	}
	if (!(n & 2))
		scanerror(s, "unexpected '||'");
	next(s);
	space(s);
	return 2;
}

bool
scanindent(struct scanner *s)
{
	bool indent;

	for (;;) {
		indent = space(s);
		if (!comment(s))
			return indent;
	}
}

void
scannewline(struct scanner *s)
{
	switch (s->chr) {
	case '\r':
		crlf(s);
		/* fallthrough */
	case '\n':
		next(s);
		break;
	default:
		scanerror(s, "expected newline");
	}
}
