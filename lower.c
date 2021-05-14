// SPDX-License-Identifier: MIT
// Copyright (C) 2021 Luc Van Oostenryck

///
// Lowering IR code
// ----------------
//
// Sparse's IR contains a few relatively high-level instructions,
// for example OP_SWITCH, or instructions that deosn't exist on
// some architectures (for example OP_MOD{US} on arm64).
// This is good for generic optimization but these instructions
// need to be lowered to something that the architecture can handle
// once we care about code generation.

#include "linearize.h"
#include "flow.h"		// for REPEAT_XYZ
#include "flowgraph.h"		// for cfg_postorder()


static pseudo_t add_binop(struct basic_block *bb, struct position pos, int op, struct symbol *type, pseudo_t src1, long long val)
{
	pseudo_t src2 = value_pseudo(val);
	struct instruction *insn = add_binary_insn(bb, type, op, src1, src2);
	return insn->target;
}

static pseudo_t add_test(struct basic_block *bb, int op, struct symbol *type, pseudo_t src1, long long val)
{
	pseudo_t src2 = value_pseudo(val);
	struct instruction *insn = add_binary_insn(bb, &bool_ctype, op, src1, src2);
	insn->itype = type;
	return insn->target;
}

static struct instruction *add_cbr(struct basic_block *bb, pseudo_t cond, struct basic_block *def)
{
	struct instruction *insn = __alloc_instruction(0);

	insn->opcode = OP_CBR;
	insn->bb = bb;
	use_pseudo(insn, cond, &insn->cond);
	add_instruction(&bb->insns, insn);

	insn->bb_false = def;
	add_bb(&def->parents, bb);
	add_bb(&bb->children, def);
	return insn;
}

// This is currently quite crude: it just converts the OP_SWITCH into
// a linear series of test-and-branch.
static int lower_switch(struct instruction *insn, struct basic_block *bb)
{
	struct basic_block *def_bb;
	struct multijmp *def_jmp;
	struct multijmp *jmp;
	typeof(jmp->end) prev = 0;
	int first = 1;

	// get the bb for the default case
	def_jmp = last_ptr_list((struct ptr_list *) insn->multijmp_list);
	def_bb = def_jmp->target;

	FOR_EACH_PTR(insn->multijmp_list, jmp) {
		struct instruction *br;
		pseudo_t src;

		if (jmp->begin > jmp->end) {		// default (no values)
			br = __alloc_instruction(0);
			br->opcode = OP_BR;
			br->pos = insn->pos;
			br->bb_true = def_bb;
			br->bb = bb;
			add_bb(&def_bb->parents, bb);
			add_bb(&bb->children, def_bb);
			add_instruction(&bb->insns, br);
			bb = def_bb;
		} else {
			struct basic_block *nxt = alloc_bb(bb->ep, insn->pos);
			pseudo_t cond = insn->src;
			long long max = jmp->end;
			int op = OP_SET_LE;

			if (jmp->begin == jmp->end) {	// a single value
				op = OP_SET_EQ;
			} else if (first || jmp->begin != prev + 1) {

				long long min = jmp->begin;
				cond = add_binop(bb, insn->pos, OP_SUB, insn->type, cond, min);
				op = OP_SET_BE;
				max -= min;
			}

			src = add_test(bb, op, insn->type, cond, max);
			br = add_cbr(bb, src, nxt);
			br->pos = insn->pos;

			br->bb_true = jmp->target;
			add_bb(&jmp->target->parents, bb);
			add_bb(&bb->children, jmp->target);

			bb = nxt;
		}

		first = 0;
		prev = jmp->end;
	} END_FOR_EACH_PTR(jmp);

	return REPEAT_CFG_CLEANUP;
}

int lower(struct entrypoint *ep, int cse)
{
	struct basic_block *bb;
	int changed = 0;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			if (!insn->bb)
				continue;
			switch (insn->opcode) {
			case OP_SWITCH:
				DELETE_CURRENT_PTR(insn);
				changed |= lower_switch(insn, bb);
				goto next_bb;
			default:
				break;
			}
		} END_FOR_EACH_PTR(insn);
next_bb:
		;
	} END_FOR_EACH_PTR(bb);

	if (changed & REPEAT_CFG_CLEANUP) {
		pack_basic_blocks(ep);
		kill_unreachable_bbs(ep);
		if (simplify_cfg_early(ep))
			kill_unreachable_bbs(ep);
		domtree_build(ep);
	}
	return changed;
}
