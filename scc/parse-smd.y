// SPDX-License-Identifier: MIT

%{
#define	_GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include "codegen.h"

static void yyerror(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int yylex(void);
static int yylineno;
static int is_tmpl = 0;
extern int yychar, yynerrs;
//int yydebug = 1;

static struct ptree *tree(const char *, int, int, int, struct ptree *, struct ptree *, struct ptree *);
static struct ptree *CONST(void) { return tree("CONST", 0,0,0,  NULL,NULL,NULL); }
%}
%union {
	int		val;
	char		*str;
	struct ptree	*tree;
	struct nterm	*nt;
}

%token  <str>		TID
%token  <str>		NID
%token  <str>		STR
%token  <str>		TMPL
%token  <val>		INT
%token			EMIT "=>"
%token			EXEC "=="
%token			IF   "if"
%token			SIZEB		/* '.B' */
%token			SIZEH		/* '.H' */
%token			SIZEL		/* '.L' */
%token			SIZEQ		/* '.Q' */
%token			COUNTB		/* '#B' */
%token			COUNTH		/* '#H' */
%token			COUNTL		/* '#L' */
%token			COUNTQ		/* '#Q' */
%token			COUNTS		/* '#S' */
%token			COUNTD		/* '#D' */
%token	<val>		COUNT		/* '#<number>' */
%type	<nt>		lhs
%type   <tree>		tree
%type   <val>		cost
%type   <val>		count
%type   <val>		size
%type   <str>		cond

%%
start	: entries
	;

entries	:
	| entries entry
	; 
entry	: rule	'\n'
	| error	'\n'
	| 	'\n'	/* empty line */
	;

rule	: lhs ':' tree cost	
				{ mkrule(yylineno, $1, $3, $4, 0, NULL, NULL); }
	| lhs ':' tree cost "if" cond
				{ mkrule(yylineno, $1, $3,  0, 0, NULL, $6); }
	| lhs ':' tree cost "==" TMPL
				{ mkrule(yylineno, $1, $3, $4, 0, $6, NULL); }
	| lhs ':' tree cost "=>" TMPL
				{ mkrule(yylineno, $1, $3, $4, 1, $6, NULL); }
	;


lhs	: NID			{ $$ = get_nterm($1); }
	| 			{ $$ = get_nterm(""); }
	;

tree	: NID
				{ $$ = tree($1, 0,  0, 0,  NULL, NULL, NULL); }
	| TID size
				{ $$ = tree($1, 0,  0, $2, NULL, NULL, NULL); }
	| TID size '(' tree ')'
				{ $$ = tree($1, 1,  0, $2,   $4, NULL, NULL); }
	| TID size '(' tree ',' tree ')'
				{ $$ = tree($1, 2,  0, $2,   $4,   $6, NULL); }
	| TID size '(' tree ',' count ')'
				{ $$ = tree($1, 2, $6, $2,   $4, CONST(), NULL); }
	| TID size '(' tree ',' tree ',' tree ')'
				{ $$ = tree($1, 3,  0, $2,   $4,   $6,   $8); }
	;

size	: 			{ $$ = 0; }
	| SIZEB			{ $$ = 1; }
	| SIZEH			{ $$ = 2; }
	| SIZEL			{ $$ = 3; }
	| SIZEQ			{ $$ = 4; }
	;

count	: 			{ $$ = 0; }
	| COUNTB		{ $$ = 8; }
	| COUNTH		{ $$ = 16; }
	| COUNTL		{ $$ = 32; }
	| COUNTQ		{ $$ = 64; }
	| COUNTS		{ $$ = 32; }
	| COUNTD		{ $$ = 64; }
	| COUNT			{ $$ = $1; }
	;

cost	: 			{ $$ = 0; }
	| '[' INT ']'		{ $$ = $2; }
	;

cond	: { is_tmpl = 1; } TMPL { $$ = $2; }
	;
%%
#include <stdarg.h>
#include <ctype.h>

static int nbr_errors;

static void yyerror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "error @ line %d: ", yylineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	nbr_errors++;
}

static int yylex(void)
{
	static const char *buffp = "";
	static char *buffer;
	static size_t bufcap;
	static int bufn;
	int c;

	while (1) {
		const char *end;
		const char *buf;
		unsigned len;
		int n;

		if (is_tmpl) {
			c = *buffp;
			while (isblank(*buffp))
				buffp++;

			end = buffer + bufn;
			if (end[-1] == '\n')
				end--;
			yylval.str = strndup(buffp, end - buffp);
			buffp = end;

			is_tmpl = 0;

			return TMPL;
		}

		switch ((c = *buffp++)) {
		case '\0':	// try to get a new input line
			bufn = getline(&buffer, &bufcap, stdin);
			if (bufn < 0)
				return EOF;
			buffp = buffer;

			// strip comments
			end = strstr(buffp, "//");
			if (end) {
				bufn = end - buffer;
				while (bufn > 0 && isspace(buffer[bufn - 1]))
					bufn--;

				buffer[bufn++] = '\n';
				buffer[bufn] = '\0';
			}

			yylineno++;
			continue;

		// those one must be ignored
		case ' ':
		case '\f':
		case '\t':
			continue;

		// report those as-is
		case '\r':
		case '\n':

		case '(':
		case ')':
		case ',':
		case '[':
		case ']':
		case '<':
		case '>':
		case ';':
		case ':':
			return c;

		case '.':
			if (isalnum(buffp[1]))
				return c;
			switch (*buffp++) {
			case 'B': return SIZEB;
			case 'H': return SIZEH;
			case 'L': return SIZEL;
			case 'Q': return SIZEQ;
			default:  buffp--;
			}

			return c;

		case '#':
			c = *buffp++;
			switch (c) {
			case 'B': return COUNTB;
			case 'H': return COUNTH;
			case 'L': return COUNTL;
			case 'Q': return COUNTQ;
			case 'S': return COUNTS;
			case 'D': return COUNTD;
			case '1' ... '9':
				n = 0;
				do {
					n = 10 * n + (c - '0');
					c = *buffp++;
				} while (isdigit(c));
				if (n >= (1 << 11))
					yyerror("count is too large: %u\n", n);
				yylval.val = n;
				buffp--;
				return COUNT;
			default:  buffp--;
			}

			return c;

		// this one is a bit special
		case '=':
			switch (*buffp) {
			case '>': c = EMIT; break;
			case '=': c = EXEC; break;
			default:
				return c;
			}

			buffp++;
			is_tmpl = 1;
			return c;

		case '0' ... '9':
			n = 0;
			do {
				n = 10 * n + (c - '0');
				c = *buffp++;
			} while (isdigit(c));
			yylval.val = n;
			buffp--;
			return INT;

		case 'A' ... 'Z':
		case 'a' ... 'z':
			buf = buffp - 1;
			while (isalnum(*buffp) || *buffp == '_')
				buffp++;
			len = buffp - buf;
			if (buf[0] == 'i' && buf[1] == 'f' && len == 2)
				return IF;
			yylval.str = strndup(buf, len);
			if (lookup_term(yylval.str) != -1)
				return TID;

			return NID;

		// start of string/asm template
		// FIXME: doesn't allow char escaping
		case '"':
			buf = buffp;

			while ((c = *buffp++)) {
				if (c == '"')
					goto case_eos;
			}
			yyerror("missing \" in asm\n");

		case_eos:
			yylval.str = strndup(buf, buffp - buf - 1);
			return STR;

		default:
			if (isprint(c))
				yyerror("invalid char '%c'\n", c);
			else
				yyerror("invalid char '\\x%02x'\n", c);
		}
	}
}

static struct ptree *tree(const char *name, int n, int count, int size, struct ptree *l, struct ptree *r, struct ptree *x)
{
	int op = lookup_term(name);

	if (op == -1) {
		if (n != 0)
			yyerror("invalid terminal: %s\n", name);
	} else {
		if (!check_term(op, n))
			yyerror("arity error for %s/%d\n", name, n);
	}

	return mktree(name, count, size, l, r, x);
}

#include <errno.h>
int read_md(const char *path)
{
	if (!freopen(path, "r", stdin)) {
		fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
		exit(1);
	}

	yyparse();

	return nbr_errors;
}
