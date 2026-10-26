#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scan.h"
#include "util.h"

struct evalstring **paths;
size_t npaths;
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
	s->error[0] = '\0';
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
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(s->error, sizeof(s->error), fmt, ap);
	va_end(ap);
}

void
scanfatal(struct scanner *s)
{
	fatal("%s:%d:%d: %s", s->path, s->line, s->col, s->error);
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

/* The helpers below return 1 if they matched, 0 if they did not, and -1
 * on error, with the message stored in s->error. */

static int
newline(struct scanner *s)
{
	switch (s->chr) {
	case '\r':
		if (next(s) != '\n') {
			scanerror(s, "expected '\\n' after '\\r'");
			return -1;
		}
		/* fallthrough */
	case '\n':
		next(s);
		return 1;
	}
	return 0;
}

static int
singlespace(struct scanner *s)
{
	int r;

	switch (s->chr) {
	case '$':
		next(s);
		r = newline(s);
		if (r)
			return r;
		ungetc(s->chr, s->f);
		s->chr = '$';
		return 0;
	case ' ':
		next(s);
		return 1;
	}
	return 0;
}

static int
space(struct scanner *s)
{
	int r;

	r = singlespace(s);
	if (r <= 0)
		return r;
	while ((r = singlespace(s)) > 0)
		;
	return r < 0 ? -1 : 1;
}

static int
comment(struct scanner *s)
{
	int r;

	if (s->chr != '#')
		return 0;
	do next(s);
	while (!(r = newline(s)));
	return r < 0 ? -1 : 1;
}

static bool
name(struct scanner *s)
{
	buf.len = 0;
	while (isvar(s->chr)) {
		bufadd(&buf, s->chr);
		next(s);
	}
	if (!buf.len) {
		scanerror(s, "expected name");
		return false;
	}
	bufadd(&buf, '\0');
	return space(s) >= 0;
}

int
scankeyword(struct scanner *s, char **var)
{
	int r;

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
	int low = 0, high = countof(keywords) - 1, mid, cmp;

	for (;;) {
		switch (s->chr) {
		case ' ':
			if (space(s) < 0)
				return ERROR;
			r = comment(s);
			if (r == 0)
				r = newline(s);
			if (r < 0)
				return ERROR;
			if (r == 0) {
				scanerror(s, "unexpected indent");
				return ERROR;
			}
			break;
		case '#':
			if (comment(s) < 0)
				return ERROR;
			break;
		case '\r':
		case '\n':
			if (newline(s) < 0)
				return ERROR;
			break;
		case EOF:
			return EOF;
		default:
			if (!name(s))
				return ERROR;
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
	if (!name(s))
		return NULL;
	return xmemdup(buf.data, buf.len);
}

static void
addstringpart(struct evalstring ***end, bool var)
{
	struct evalstring *p;

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

static bool
escape(struct scanner *s, struct evalstring ***end)
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
		if (s->chr != '}') {
			scanerror(s, "invalid variable name");
			return false;
		}
		next(s);
		addstringpart(end, true);
		break;
	case '\r':
	case '\n':
		if (newline(s) < 0 || space(s) < 0)
			return false;
		break;
	default:
		if (buf.len > 0)
			addstringpart(end, false);
		while (issimplevar(s->chr)) {
			bufadd(&buf, s->chr);
			next(s);
		}
		if (!buf.len) {
			scanerror(s, "invalid $ escape");
			return false;
		}
		addstringpart(end, true);
	}
	return true;
}

bool
scanstring(struct scanner *s, bool path, struct evalstring **str)
{
	struct evalstring **end = str;

	*str = NULL;
	buf.len = 0;
	for (;;) {
		switch (s->chr) {
		case '$':
			next(s);
			if (!escape(s, &end))
				return false;
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
	if (path && space(s) < 0)
		return false;
	return true;
}

bool
scanpaths(struct scanner *s)
{
	static size_t max;
	struct evalstring *str;

	for (;;) {
		if (!scanstring(s, true, &str))
			return false;
		if (!str)
			return true;
		if (npaths == max) {
			max = max ? max * 2 : 32;
			paths = xreallocarray(paths, max, sizeof(paths[0]));
		}
		paths[npaths++] = str;
	}
}

bool
scanchar(struct scanner *s, int c)
{
	if (s->chr != c) {
		scanerror(s, "expected '%c'", c);
		return false;
	}
	next(s);
	return space(s) >= 0;
}

int
scanpipe(struct scanner *s, int n)
{
	if (s->chr != '|')
		return 0;
	next(s);
	if (s->chr != '|') {
		if (!(n & 1)) {
			scanerror(s, "expected '||'");
			return -1;
		}
		return space(s) < 0 ? -1 : 1;
	}
	if (!(n & 2)) {
		scanerror(s, "unexpected '||'");
		return -1;
	}
	next(s);
	return space(s) < 0 ? -1 : 2;
}

int
scanindent(struct scanner *s)
{
	int indent, r;

	for (;;) {
		indent = space(s);
		if (indent < 0)
			return -1;
		r = comment(s);
		if (r < 0)
			return -1;
		if (!r)
			break;
	}
	if (!indent)
		return 0;
	r = newline(s);
	if (r < 0)
		return -1;
	return !r;
}

bool
scannewline(struct scanner *s)
{
	int r = newline(s);

	if (!r)
		scanerror(s, "expected newline");
	return r > 0;
}
