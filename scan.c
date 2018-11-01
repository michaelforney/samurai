#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scan.h"
#include "util.h"

struct scanner {
	FILE *f;
	const char *path;
	int chr, line, col;
};

static struct buffer buf;

struct scanner *
mkscanner(const char *path)
{
	struct scanner *s;

	s = xmalloc(sizeof(*s));
	s->path = path;
	s->line = 1;
	s->col = 1;
	s->f = fopen(path, "r");
	if (!s->f)
		err(1, "open %s", path);
	s->chr = getc(s->f);

	return s;
}

void
delscanner(struct scanner *s)
{
	fclose(s->f);
	free(s);
}

void
scanerror(struct scanner *s, const char *fmt, ...)
{
	extern char *argv0;
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

static bool
space(struct scanner *s)
{
	if (s->chr != ' ')
		return false;
	do next(s);
	while (s->chr == ' ');
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
ident(struct scanner *s)
{
	buf.len = 0;
	while (isvar(s->chr)) {
		bufadd(&buf, s->chr);
		next(s);
	}
	if (!buf.len)
		scanerror(s, "expected identifier");
	space(s);
}

static void
crlf(struct scanner *s)
{
	if (next(s) != '\n')
		scanerror(s, "expected '\\n' after '\\r'");
}

int
scankeyword(struct scanner *s, char **name)
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
			ident(s);
			bufadd(&buf, '\0');
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
			*name = xstrdup(buf.data, buf.len - 1);
			return NAME;
		}
	}
}

char *
scanname(struct scanner *s)
{
	ident(s);
	return xstrdup(buf.data, buf.len);
}

static void
addstringpart(struct evalstringpart ***end, bool var)
{
	struct evalstringpart *p;

	p = xmalloc(sizeof(*p));
	p->next = NULL;
	**end = p;
	if (var) {
		p->var = xstrdup(buf.data, buf.len);
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
		for (;;) {
			next(s);
			if (!isvar(s->chr))
				break;
			bufadd(&buf, s->chr);
		}
		if (s->chr != '}')
			scanerror(s, "invalid variable name");
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
		scanerror(s, "expected '%c'");
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
	if (s->chr != '\n') {
		if (s->chr != '\r')
			scanerror(s, "expected newline");
		crlf(s);
	}
	next(s);
}
