#ifndef OPTIMIZE_H
#define OPTIMIZE_H

struct entrypoint;

/* optimize.c */
void optimize(struct entrypoint *ep);
int lower(struct entrypoint *ep, int cse);

#endif
