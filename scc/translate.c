// SPDX-License-Identifier: MIT

#include "linearize.h"
#include "insncode.h"
#include "gencode.h"
#include <assert.h>
#include <stdio.h>

// - for each insn, generate the corresponding state
//   (via label_insn()) and try to link these with their
//   operand.
//
// - linking with an operand can be done if it is defined
//   in the current bb and the instruction is the only user
//   (or at least no user before (load/store with update)).
//
// - during this phase, some instruction may or need to be rewritten.
//   * OP_CBR whose cond is not an OP_SET_XXX
//   * load/store -> specific for size 8, 16, 32 (and 64) and other?
//   * idem for cast with sign extension
//   * bitfield extraction/insertion are ugly

static inline int is_reglike(pseudo_t p)
{
	switch (p->type) {
	case PSEUDO_REG:
	case PSEUDO_ARG:
		return 1;
	default:
		return 0;
	}
}

static struct cg_state *get_src_state(struct basic_block *bb, pseudo_t src)
{
	struct instruction *insn;
	struct cg_state *s;
	int op;

	switch (src->type) {
	case PSEUDO_REG:
		insn = src->def;
		if (insn && insn->bb == bb && one_use(src)) {
			s = insn->cg_state;
			insn->cg_state = NULL;		// FIXME: why ???
			return s;
		}
		s = alloc_state(INSN_REG, src, insn);
		break;

	case PSEUDO_ARG:		// FIXME: arch dep
		s = alloc_state(INSN_ARG, src, NULL);
		break;

	case PSEUDO_UNDEF:		// FIXME
		s = alloc_state(INSN_UNDEF, src, NULL);
		break;
	case PSEUDO_VOID:		// FIXME
		s = alloc_state(INSN_VOID, src, NULL);
		break;

	case PSEUDO_VAL:
		s = alloc_state(INSN_CONST, src, NULL);
		break;

	case PSEUDO_SYM:
		op = src->sym->ctype.modifiers & (MOD_NONLOCAL|MOD_STATIC) ? INSN_GSYM : INSN_LSYM;
		s = alloc_state(op, src, NULL);
		break;

	default:
		assert(0);
	}

	label_state(s);
	return s;
}

static void translate_call(struct instruction *insn)
{
	struct cg_state *s;
	pseudo_t arg;
	int op;

	op = INSN_CALL;
	if (is_reglike(insn->func))
		op = INSN_CALLR;
	s = alloc_state(op, insn->target, insn);

	// calls are special because:
	// * can have an unbounded number of args
	// * because of this the state of the args are not stored in this s->kids
	// => reduce() must process calls accordingly
	// OTOH we won't have rules with args for CALL: so we can process these args independently
	FOR_EACH_PTR(insn->arguments, arg) {
		struct cg_state *s;

		s = get_src_state(insn->bb, arg);
		arg->priv = s;
	} END_FOR_EACH_PTR(arg);

	s->kids[0] = get_src_state(insn->bb, insn->func);
	label_state(s);
}

static void translate_setval(struct instruction *insn)
{
	struct cg_state *s = alloc_state(insn->opcode, insn->target, insn);

	label_state(s);
}

static void translate_unop(struct instruction *insn)
{
	struct cg_state *s = alloc_state(insn->opcode, insn->target, insn);

	s->kids[0] = get_src_state(insn->bb, insn->src);
	label_state(s);
}

static void translate_binop(struct instruction *insn)
{
	struct cg_state *s = alloc_state(insn->opcode, insn->target, insn);

	s->kids[0] = get_src_state(insn->bb, insn->src1);
	s->kids[1] = get_src_state(insn->bb, insn->src2);

	label_state(s);
}

/*
 * OP_SEL() is a ternary op:
*/
static void translate_sel(struct instruction *insn)
{
	struct cg_state *s = alloc_state(insn->opcode, insn->target, insn);

	s->kids[0] = get_src_state(insn->bb, insn->src1);
	s->kids[1] = get_src_state(insn->bb, insn->src2);
	s->kids[2] = get_src_state(insn->bb, insn->src3);
	label_state(s);
}

static void translate_memop(struct instruction *insn)
{
	struct cg_state *s;
	int size = insn->size;
	int align = insn->type->ctype.alignment;
	int op = OP_BADOP;
	int unit;

	// FIXME: we may want to split the bigger ones in a few of smaller ones

	// FIXME: the case exists when accessing the fileds of struct (s.a)
	if (align == 0)
		align = size / 8;

	unit = align;
	while (size & (unit - 1))
		unit /= 2;

	switch (insn->opcode) {
	case OP_LOAD:
		if (is_bool_type(insn->type))
			insn->size = size = 8;
		op = INSN_LOADMEM;
		switch (unit) {
		case 1:
			switch (size) {
			case 8:  op = INSN_LOAD; break;
			case 16: op = INSN_LOAD2; break;
			}
			break;
		case 2:
			switch (size) {
			case 16: op = INSN_LOAD; break;
			case 32: op = INSN_LOAD2; break;
			}
			break;
		case 4:
			switch (size) {
			case 32: op = INSN_LOAD; break;
			case 64: op = INSN_LOAD2; break;
			}
			break;
		case 8:
			switch (size) {
			case 64: op = INSN_LOAD; break;
			}
			break;
		}
		break;
	case OP_STORE:
		op = INSN_STOREMEM;
		if (insn->target->type == PSEUDO_ARG)
			unit = size / 8;
		switch (unit) {
		case 1:
			switch (size) {
			case 8:  op = INSN_STORE; break;
			case 16: op = INSN_STORE2; break;
			}
			break;
		case 2:
			switch (size) {
			case 16: op = INSN_STORE; break;
			case 32: op = INSN_STORE2; break;
			}
			break;
		case 4:
			switch (size) {
			case 32: op = INSN_STORE; break;
			case 64: op = INSN_STORE2; break;
			}
			break;
		case 8:
			switch (size) {
			case 64: op = INSN_STORE; break;
			}
			break;
		}
		break;
	}

	s = alloc_state(op, insn->target, insn);
	if (insn->offset) {
		struct cg_state* addr = alloc_state(INSN_ADD, insn->src, insn);

		addr->kids[0] = get_src_state(insn->bb, insn->src);
		addr->kids[1] = get_src_state(insn->bb, value_pseudo(insn->offset));
		label_state(addr);
		s->kids[0] = addr;
	} else {
		s->kids[0] = get_src_state(insn->bb, insn->src);
	}
	if (insn->opcode == OP_STORE)
		s->kids[1] = get_src_state(insn->bb, insn->target);
	label_state(s);
}

static void translate_cast(struct instruction *insn)
{
	struct cg_state *s;
	struct symbol *otype = insn->orig_type;
	unsigned int new_size = insn->size;
	unsigned int old_size = otype->bit_size;
	unsigned int siz = 0;
	enum insncode op;

	switch (insn->opcode) {
	case OP_UTPTR:
	case OP_PTRTU:
	case OP_PTRCAST:
		// no-op casts
		op = INSN_COPY;
		break;
	case OP_TRUNC:
		op = INSN_TRUNC;
		siz = new_size;			// ???
		break;
	case OP_ZEXT:
	case OP_SEXT:
		op = insn->opcode;
		siz = old_size;
		break;
	default:
		return;			// can't occur
	}

	s = alloc_state(op, insn->target, insn);
	s->kids[0] = get_src_state(insn->bb, insn->src1);
	if (siz)
		s->kids[1] = get_src_state(insn->bb, value_pseudo(siz));
	label_state(s);
}

static void translate_br(struct instruction *insn)
{
	struct cg_state *s = alloc_state(insn->opcode, NULL, insn);

	if (insn->opcode == OP_CBR)
		s->kids[0] = get_src_state(insn->bb, insn->cond);

	label_state(s);
}

static void translate_computed_goto(struct instruction *insn)
{
	struct cg_state *s;

	s = alloc_state(insn->opcode, NULL, insn);
	s->kids[0] = get_src_state(insn->bb, insn->src);
	label_state(s);
}

static void translate_ret(struct instruction *insn)
{
	struct cg_state *s;

	if (insn->src && insn->src != VOID) {
		s = alloc_state(INSN_RET, NULL, insn);
		s->kids[0] = get_src_state(insn->bb, insn->src);
	} else {
		s = alloc_state(INSN_RETVOID, NULL, insn);
	}

	label_state(s);
}

static void translate_deathnote(struct instruction *insn)
{
	pseudo_t src = insn->target;	// FIXME, weird

	switch (src->type) {
	case PSEUDO_SYM:
		if (src->sym->ctype.modifiers & (MOD_NONLOCAL|MOD_STATIC))
			break;		// ignore non-local symbols
	case PSEUDO_REG:
	case PSEUDO_ARG:
	case PSEUDO_PHI:
		insn->cg_state = alloc_state(insn->opcode, src, insn);
		break;
	case PSEUDO_VAL:
	case PSEUDO_VOID:
	case PSEUDO_UNDEF:
		break;			// ignore anything else
	}
}

static void translate_default(struct instruction *insn)
{
	struct cg_state *s;

	s = alloc_state(insn->opcode, insn->target, insn);
	label_state(s);
}


static void translate_insn(struct instruction *insn)
{
	enum opcode op = insn->opcode;

	switch (op) {

	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		translate_binop(insn);
		break;
	case OP_NOT:
	case OP_NEG:
	case OP_COPY:
		translate_unop(insn);
		break;
	case OP_LOAD:
	case OP_STORE:
		translate_memop(insn);
		break;

	case OP_PTRTU: case OP_UTPTR:
	case OP_PTRCAST:
	case OP_TRUNC:
	case OP_ZEXT:
	case OP_SEXT:
		translate_cast(insn);
		break;

	case OP_BR:
	case OP_CBR:
		translate_br(insn);
		break;

	case OP_RET:
		translate_ret(insn);
		break;

	case OP_CALL:
		translate_call(insn);
		break;

	case OP_SEL:
		translate_sel(insn);
		break;

	case OP_COMPUTEDGOTO:
		translate_computed_goto(insn);
		break;

	case OP_SETVAL:
	case OP_SETFVAL:
		translate_setval(insn);
		break;

	case OP_DEATHNOTE:
		translate_deathnote(insn);
		break;

	case OP_ENTRY:
	case OP_CONTEXT:
	case OP_RANGE:
		// Ignore for now;
		break;

	case OP_SYMADDR:
	case OP_BADOP:
		// Should never happen ?

	case OP_PHI:
	case OP_PHISOURCE:
		// Should never happen if unSSA is used

	case OP_SLICE:
	case OP_ASM:
		translate_default(insn);
		break;		// FIXME

	case OP_NOP:
	case OP_INLINED_CALL:
	case OP_UNREACH:
		// must ignore
		break;

	default:
	case OP_SWITCH:
		die("unhandled instruction: %s", show_instruction(insn));
	}
}


static void translate_bb(struct basic_block *bb)
{
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		translate_insn(insn);
	} END_FOR_EACH_PTR(insn);
}

static void reduce_args(struct rstate_list **list, struct instruction *insn)
{
	pseudo_t arg;

	FOR_EACH_PTR(insn->arguments, arg) {
		reduce_state(list, arg->priv, 0);
	} END_FOR_EACH_PTR(arg);
}

static void reduce_bb(struct rstate_list **list, struct basic_block *bb)
{
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->opcode == OP_CALL)
			reduce_args(list, insn);
		if (insn->cg_state)
			reduce_state(list, insn->cg_state, 0);
	} END_FOR_EACH_PTR(insn);
}

void codegen_bb(struct basic_block *bb)
{
	struct rstate_list *list = NULL;
	struct rstate *rstate;
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			DELETE_CURRENT_PTR(insn);
	} END_FOR_EACH_PTR(insn);

	translate_bb(bb);
	reduce_bb(&list, bb);

	printf(".L%d:\n", bb->nr);
	FOR_EACH_PTR(list, rstate) {
		emit_rstate(rstate);
	} END_FOR_EACH_PTR(rstate);
}
