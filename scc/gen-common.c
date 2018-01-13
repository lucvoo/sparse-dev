// SPDX-License-Identifier: MIT

#include "gencode.h"
#include "../expression.h" // only needed for emit SETVAL
#include <ctype.h>
#include <stdio.h>

#if 0
#define TRACE 1
#define	trace_reduce(fmt, ...)	printf("#%*sreducing %s with rule %d @ line %d " fmt, tlevel, "", name_opcode(s->op), rule, rinfo->lineno, ##__VA_ARGS__)
#define	trace_emit(tmpl)	if (tmpl && *tmpl != '%') printf("\n#emit: '%s'\n", tmpl)
#else
#define TRACE 0
#define	trace_reduce(fmt, ...)	do {} while (0)
#define	trace_emit(tmpl)	do {} while (0)
#endif

DECLARE_ALLOCATOR(state);
ALLOCATOR(state, "states");
struct cg_state *alloc_state(int op, pseudo_t src, struct instruction *insn)
{
	struct state *s = __alloc_state(0);

	s->op = op;
	s->src = src;
	s->insn = insn;
	s->kids[0] = s->kids[1] = NULL;
	memset(s->rules, 0x00, sizeof(s->rules)); // FIXME: needed ?
	memset(s->costs, 0xff, sizeof(s->costs));

	if (insn && !insn->cg_state)
		insn->cg_state = &s->ext;

	return &s->ext;
}

ALLOCATOR(rstate, "rstates");
struct rstate *alloc_rstate(struct cg_state *cgs, int ruleno)
{
	struct rstate *rs = __alloc_rstate(0);

	rs->cgs	= cgs;
	rs->ruleno = ruleno;
	return rs;
}


static const char *emit_value(long long val, bool neg_ok, bool hex)
{
	static char buff[32];

	if (!hex && (val < 256) && (val > -256) && (val >= 0 || neg_ok))
		snprintf(buff, sizeof(buff), "%lld", val);
	else
		snprintf(buff, sizeof(buff), "0x%llx", val);
	return buff;
}

#define	FUN_LOG2	1
#define	FUN_NEG		2
#define	FUN_16_0	'l'
#define	FUN_16_1	'u'
#define	FUN_16_2	'L'
#define	FUN_16_3	'U'

static const char *emit_pseudo(pseudo_t p, int type, int part)
{
	long long val = p->value;

	switch (type) {
	case 'r':
		if (p->type == PSEUDO_VAL && p->value == 0)
			return "wzr";		// FIXME
	default:
		return show_pseudo(p);
	case 'n':
		if (part == 0)
			part = FUN_NEG;
		/* fallthrough */
	case 'c':
		if (p->type != PSEUDO_VAL)
			return show_pseudo(p);	// FIXME: error
		break;
	}

	switch (part) {
	case FUN_16_0: val = (val >>  0) & 0xffff; break;
	case FUN_16_1: val = (val >> 16) & 0xffff; break;
	case FUN_16_2: val = (val >> 32) & 0xffff; break;
	case FUN_16_3: val = (val >> 48) & 0xffff; break;
	case FUN_NEG:  val = -val;      break;
	case FUN_LOG2: val = log2_exact(val);      break;
	}
	return emit_value(val, 1, 0);
}

#define	ARG_MAX	('0' + NBR_KIDS)

static void emit_tmpl(struct state *s, const struct rule_info *rinfo)
{
	const char *tmpl = rinfo->action;
	struct state *k;
	pseudo_t tmp_reg = NULL;
	int nt;
	int c;

	trace_emit(tmpl);

	if (!tmpl) {
		int rule, nt;

		if (!rinfo->chain)
			return;

		nt = rinfo->rhs[1];
	        rule = s->rules[nt];
		return emit_tmpl(s, &rules_info[rule]);
	}

	while ((c = *tmpl++)) {
		const char *out;
		int part = 0;
		int type;
		int arg;

		switch (c) {
		case '%':
			break;

		case ';':
			// ignore following spaces
			while (isspace(*tmpl))
				tmpl++;

			// and emit a newline
			putchar('\n');
			putchar('\t');

			continue;

		case '\\':
			switch (*tmpl) {
			case 't':
				tmpl++;
				putchar('\t');
				break;
			case 'r':
				tmpl++;
				putchar('\r');
				break;
			default:
				break;
			}
		default:
			putchar(c);
			continue;
		}

		out = "??";
		// process the '%...'
		type = *tmpl++;
		switch (type) {
		case '%':
			fputs("%", stdout);
			break;

		case 'c':	// constant
		case 'n':	// negative constant
			switch (*tmpl) {
			case ':':
				tmpl++;
				if (memcmp(tmpl, "log2:", 5) == 0) {
					tmpl += 5;
					part = FUN_LOG2;
				} else {
					// error
				}
				break;
			case 'l': case 'u': case 'L': case 'U':
				part = *tmpl++;
				break;
			default:
				part = 0;
			}
			/* fallthrough */
		case 'r':	// register
		case 'l':	// label
			switch ((arg = *tmpl++)) {
			case 't':
				if (s->src) {
					if (!tmp_reg)
						tmp_reg = alloc_pseudo(NULL);
					out = emit_pseudo(tmp_reg, type, 0);
				}
				break;
			case 'd':
				if (s->insn)
					out = show_pseudo(s->insn->target);
				break;
			case '0':
				out = emit_pseudo(s->src, type, part);
				break;

			case '1' ... ARG_MAX: {
				struct state *p = s->kids[arg-'1'];
				arg = *tmpl;
				while (arg >= '1' && arg <= ARG_MAX) {
					p = p->kids[arg-'1'];
					arg = *++tmpl;
				}
				out = emit_pseudo(p->src, type, part);
				break;

			case 'z':	// for ARM64
				out = "wzr";	// FIXME: or "xzr"
				break;
			}

			default:
				/* FIXME */;
			}

			fputs(out, stdout);
			break;

		case 'm':	// constant mask
			switch ((arg = *tmpl++)) {
			case '1' ... ARG_MAX: {
				struct state *p = s->kids[arg-'1'];

				arg = *tmpl;
				while (arg >= '1' && arg <= ARG_MAX) {
					p = p->kids[arg-'1'];
					arg = *++tmpl;
				}

				if (p && p->src && p->src->type == PSEUDO_VAL)
					printf("0x%lx" , (1UL << p->src->value) - 1);
				break;
			}

			default:
				/* FIXME */;
			}
			break;

		case '#':
			printf("%u", s->insn->size);
			break;

		case 'f':	// floating-point constant
			printf("%Lg", s->insn->fvalue);
			break;

		case 'b':	// branch
			printf(".L%d", s->insn->bb_true->nr);
			if (s->insn->bb_false)
				printf(", .L%d", s->insn->bb_false->nr);

			break;

		case 'x':	// expression, only used for SETVAL
				// and even then, only with labels?
			if (!s->insn->val) {
				printf("?? (@%d)", __LINE__);
				break;
			}
			switch (s->insn->val->type) {
			case EXPR_LABEL:
				printf(".L%d", s->insn->val->symbol->bb_target->nr);
				break;
			default:
				printf("?? (@%d)", __LINE__);
			}
			break;

		case 'a':	// action/template from parent rule
			switch ((arg = *tmpl++)) {
				int rule;

			case '0':
				nt = rinfo->rhs[1];
			        rule = s->rules[nt];
				emit_tmpl(s, &rules_info[rule]);
				break;
			case '1' ... ARG_MAX:
				k = s;
				do {
					nt = rinfo->rhs[arg-'1'];
					k = k->kids[arg-'1'];
					rule = k->rules[nt];
					arg = *tmpl++;
				} while (arg >= '1' && arg <= ARG_MAX);
				tmpl--;
				emit_tmpl(k, &rules_info[rule]);
				break;
			}
			break;
		}
	}

	if (tmp_reg)
		s->src = tmp_reg;
}

void emit_rstate(struct rstate *rs)
{
	unsigned ruleno = rs->ruleno;
	const struct rule_info *rinfo = &rules_info[ruleno];
	struct state *s = (void*)rs->cgs;

	if (!rinfo->action)
		return;

	//printf("# rule %ld\n", rinfo - &rules_info[0]);
	putchar('\t');
	emit_tmpl(s, rinfo);
	putchar('\n');
}


static void dump_state(FILE *f, const struct cg_state *cgs)
{
	const struct state *s = (const void*)cgs;
	struct instruction *insn = s->insn;
	struct position pos = {};
	int nr;
	int i;

	if (insn)
		pos = insn->pos;

	fprintf(f, "cg_state = %p, insn = %p:%s @ %d:%d\n", s, insn, name_opcode(s->op), pos.line, pos.pos);
	for (i = 0; i < NBR_KIDS; i++) {
		if (!s->kids[i])
			continue;
		fprintf(f, "\tkid %2d: %p\n", i, s->kids[i]);
	}
	nr = 0;
	for (i = 0; i < NBR_nterms; i++) {
		if (s->costs[i] == 65535 && s->rules[i] == 0)
			continue;
		fprintf(f, "\tnt  %2d: cost = %2u, rule = %3u\n", i, s->costs[i], s->rules[i]);
		nr++;
	}
	if (nr == 0)
		fprintf(f, "\t!! WARNING: no rules !!\n");
}

static void trace_tree_state(const struct cg_state *s, const char *end)
{
	printf("%s", name_opcode(s->op));
	if (s->insn)
		printf(".%d", s->insn->size);
	if (s->kids[0]) {
		printf("(");
		trace_tree_state(s->kids[0], "");
		if (s->kids[1]) {
			printf(", ");
			trace_tree_state(s->kids[1], "");
		}
		printf(")");
	} else if (s->op == INSN_CONST) {
		printf(" #%lld", s->src->value);
	} else if (s->op == INSN_REG) {
		printf(" %%r%d", s->src->nr);
	}
	printf("%s", end);
}

void trace_state(const char *msg, const struct cg_state *s)
{
	static int level;

	if (!TRACE)
		return;

	printf("%*s> %s:\n", level++, "", msg);
	trace_tree_state(s, "\n");
	dump_state(stdout, s);

	--level;
}


void reduce_state(struct rstate_list **list, struct cg_state *cgs, int nt)
{
	static int tlevel;
	struct state *s = (void*) cgs;
	int rule = s->rules[nt];
	const struct rule_info *rinfo = &rules_info[rule];
	int i;

	tlevel++;
	trace_reduce("(pre)\n");
	if (!rule) {
		struct position pos = {};
		dump_state(stderr, cgs);
		if (s->insn)
			pos = s->insn->pos;
		sparse_error(pos, "%s(): can't reduce %s (nt = %d)\n", __func__, name_opcode(s->op), rule);
		trace_reduce(" ! ERROR !\n");
		trace_state("error norule", cgs);
		tlevel--;
		return;
	}

	trace_reduce("(ok)\n");
	for (i = 0; i < NBR_KIDS; i++) {
		if (!rinfo->rhs[i])
			break;
		trace_reduce("(kid %d)\n", i);
		reduce_state(list, cgs->kids[i], rinfo->rhs[i]);
	}
	trace_reduce("(kids)\n");
	if (rinfo->chain)
		reduce_state(list, cgs, rinfo->rhs[1]);
	trace_reduce("(chain)\n");

	trace_state("reduce", cgs);
	if (rinfo->action && rinfo->emit) {
		struct rstate *rstate = alloc_rstate(cgs, rule);
		add_ptr_list(list, rstate);
	}
	trace_reduce("(emit)\n\n");
	tlevel--;
}
