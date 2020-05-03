#pragma once

struct edge;

void depsinit(const char *);
void depsclose(void);
void depsload(struct edge *);
void depsrecord(struct edge *);
