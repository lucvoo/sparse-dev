// SPDX-License-Identifier: MIT

#ifndef	GENCODE_H
#define	GENCODE_H

#include "insncode.h"
#include "../ptrlist.h"

struct pseudo;
struct instruction;

#define	NBR_KIDS	3

struct cg_state {
	enum insncode		op;
	struct instruction	*insn;
	pseudo_t		src;
	struct cg_state		*kids[NBR_KIDS];
};

struct rstate {
	struct cg_state		*cgs;
	unsigned short		ruleno;
};
DECLARE_PTR_LIST(rstate_list, struct rstate);

// gen-common.c
struct state;
DECLARE_ALLOCATOR(state);
DECLARE_ALLOCATOR(rstate);

struct cg_state *alloc_state(int op, struct pseudo *src, struct instruction *insn);
struct rstate *alloc_rstate(struct cg_state *s, int ruleno);
void emit_rstate(struct rstate *rs);

// gen-$ARCH.c
void label_state(struct cg_state *s);
void reduce_state(struct rstate_list **list, struct cg_state *s, int nt);

// translate.c
void codegen_bb(struct basic_block *bb);

// common conditions
static inline bool range(long long x, long long min, long long max)
{
	return x >= min && x <= max;
}

static inline bool masked_range(long long x, unsigned nbits, long long min, long long max)
{
	if (x & ((1 << nbits) - 1))
		return 0;
	x >>= nbits;
	return x >= min && x <= max;
}

static inline bool power_of_two(unsigned long long x)
{
	return x != 0 && (x & (x - 1)) == 0;
}

#endif
