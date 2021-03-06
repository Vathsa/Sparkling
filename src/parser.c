/*
 * parser.c
 * Sparkling, a lightweight C-style scripting language
 *
 * Created by Árpád Goretity on 02/05/2013
 * Licensed under the 2-clause BSD License
 *
 * Simple recursive descent parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "parser.h"
#include "private.h"


static SpnAST *parse_program(SpnParser *p);
static SpnAST *parse_program_nonempty(SpnParser *p);
static SpnAST *parse_stmt(SpnParser *p, int is_global);
static SpnAST *parse_stmt_list(SpnParser *p);
static SpnAST *parse_function(SpnParser *p, int is_stmt);
static SpnAST *parse_expr(SpnParser *p);

static SpnAST *parse_assignment(SpnParser *p);
static SpnAST *parse_concat(SpnParser *p);
static SpnAST *parse_condexpr(SpnParser *p);

static SpnAST *parse_logical_or(SpnParser *p);
static SpnAST *parse_logical_and(SpnParser *p);
static SpnAST *parse_bitwise_or(SpnParser *p);
static SpnAST *parse_bitwise_xor(SpnParser *p);
static SpnAST *parse_bitwise_and(SpnParser *p);

static SpnAST *parse_comparison(SpnParser *p);
static SpnAST *parse_shift(SpnParser *p);
static SpnAST *parse_additive(SpnParser *p);
static SpnAST *parse_multiplicative(SpnParser *p);

static SpnAST *parse_prefix(SpnParser *p);
static SpnAST *parse_postfix(SpnParser *p);
static SpnAST *parse_term(SpnParser *p);

static SpnAST *parse_decl_args(SpnParser *p);
static SpnAST *parse_call_args(SpnParser *p);

static SpnAST *parse_if(SpnParser *p);
static SpnAST *parse_while(SpnParser *p);
static SpnAST *parse_do(SpnParser *p);
static SpnAST *parse_for(SpnParser *p);
static SpnAST *parse_foreach(SpnParser *p);
static SpnAST *parse_break(SpnParser *p);
static SpnAST *parse_continue(SpnParser *p);
static SpnAST *parse_return(SpnParser *p);
static SpnAST *parse_vardecl(SpnParser *p);
static SpnAST *parse_expr_stmt(SpnParser *p);
static SpnAST *parse_empty(SpnParser *p);
static SpnAST *parse_block(SpnParser *p);


static SpnAST *parse_binexpr_rightassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
);

static SpnAST *parse_binexpr_leftassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
);


SpnParser *spn_parser_new()
{
	SpnParser *p = malloc(sizeof(*p));
	if (p == NULL) {
		abort();
	}

	p->pos = NULL;
	p->eof = 0;
	p->error = 0;
	p->lineno = 1;
	p->errmsg = NULL;

	return p;
}

void spn_parser_free(SpnParser *p)
{
	free(p->errmsg);
	free(p);
}

#define ERRMSG_FORMAT "Sparkling: syntax error near line %lu: "
void spn_parser_error(SpnParser *p, const char *fmt, ...)
{
	int stublen, n;
	va_list args;

	/* indicate error by setting error flag */
	p->error = 1;

	/* because C89 *still* doesn't have snprintf */
	stublen = fprintf(stderr, ERRMSG_FORMAT, p->lineno);
	if (stublen < 0) {
		abort();
	}

	va_start(args, fmt);
	n = vfprintf(stderr, fmt, args);
	va_end(args);
	if (n < 0) {
		abort();
	}

	fputc('\n', stderr);

	p->errmsg = realloc(p->errmsg, stublen + n + 1);
	if (p->errmsg == NULL) {
		abort();
	}

	sprintf(p->errmsg, ERRMSG_FORMAT, p->lineno);

	va_start(args, fmt);
	vsprintf(p->errmsg + stublen, fmt, args);
	va_end(args);
}
#undef ERRMSG_FORMAT

/* re-initialize the parser object (so that it can be reused for parsing
 * multiple translation units), then kick off the actual recursive descent
 * parser to process the source text
 */
SpnAST *spn_parser_parse(SpnParser *p, const char *src)
{
	p->pos = src;
	p->eof = 0;
	p->error = 0;
	p->lineno = 1;

	return parse_program(p);
}

static SpnAST *parse_program(SpnParser *p)
{
	SpnAST *tree = NULL;

	if (spn_lex(p))	{	/* there are tokens */
		tree = parse_program_nonempty(p);
	} else {
		return p->error ? NULL : spn_ast_new(SPN_NODE_PROGRAM, p->lineno);
	}

	if (p->eof) {		/* if EOF after parsing, then all went fine */
		return tree;
	}

	/* if not, then there's garbage after the source */
	spn_ast_free(tree);

	if (!p->error) {	/* if there's no error message yet */
		spn_parser_error(p, "garbage after input");
	}

	return NULL;
}

static SpnAST *parse_program_nonempty(SpnParser *p)
{
	SpnAST *ast;

	/* parse global statements */
	SpnAST *sub = parse_stmt(p, 1);
	if (sub == NULL) {
		return NULL;
	}

	while (!p->eof) {
		SpnAST *tmp;
		SpnAST *right = parse_stmt(p, 1);
		if (right == NULL) {
			spn_ast_free(sub);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_COMPOUND, p->lineno);
		tmp->left = sub;	/* this node	*/
		tmp->right = right;	/* next node	*/
		sub = tmp;		/* update head	*/
	}

	/* here the same hack is performed that is used in parse_block()
	 * (refer there for an explanation)
	 */

	if (sub->node == SPN_NODE_COMPOUND) {
		sub->node = SPN_NODE_PROGRAM;
		return sub;
	}

	ast = spn_ast_new(SPN_NODE_PROGRAM, p->lineno);
	ast->left = sub;
	return ast;
}

/* statement lists appear in block statements, so loop until `}' is found */
static SpnAST *parse_stmt_list(SpnParser *p)
{
	/* parse local statement */
	SpnAST *ast = parse_stmt(p, 0);
	if (ast == NULL) {
		return NULL;
	}

	while (p->curtok.tok != SPN_TOK_RBRACE) {
		SpnAST *tmp;
		SpnAST *right = parse_stmt(p, 0);
		if (right == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_COMPOUND, p->lineno);
		tmp->left = ast;	/* this node	*/
		tmp->right = right;	/* next node	*/
		ast = tmp;		/* update head	*/
	}

	return ast;
}

static SpnAST *parse_stmt(SpnParser *p, int is_global)
{
	switch (p->curtok.tok) {
		case SPN_TOK_IF:	return parse_if(p);
		case SPN_TOK_WHILE:	return parse_while(p);
		case SPN_TOK_DO:	return parse_do(p);
		case SPN_TOK_FOR:	return parse_for(p);
		case SPN_TOK_FOREACH:	return parse_foreach(p);
		case SPN_TOK_BREAK:	return parse_break(p);
		case SPN_TOK_CONTINUE:	return parse_continue(p);
		case SPN_TOK_RETURN:	return parse_return(p);
		case SPN_TOK_SEMICOLON:	return parse_empty(p);
		case SPN_TOK_LBRACE:	return parse_block(p);
		case SPN_TOK_VAR:	return parse_vardecl(p);
		case SPN_TOK_FUNCTION:
			if (is_global) {
				/* assume function statement at file scope */
				return parse_function(p, 1);
			} else {
				/* and a function expression at local scope */
				return parse_expr_stmt(p);
			}
		default:
			return parse_expr_stmt(p);
	}
}

static SpnAST *parse_function(SpnParser *p, int is_stmt)
{
	enum spn_ast_node node;
	SpnString *name;
	SpnAST *ast, *body;

	/* skip `function' keyword */
	if (!spn_accept(p, SPN_TOK_FUNCTION)) {
		spn_parser_error(p, "internal error, expected `function'");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	if (is_stmt) {
		/* named global function statement */

		if (p->curtok.tok != SPN_TOK_IDENT) {
			spn_parser_error(p, "expected function name in function statement");
			spn_value_release(&p->curtok.val);
			return NULL;
		}

		node = SPN_NODE_FUNCSTMT;
		name = p->curtok.val.v.ptrv;

		/* skip identifier */
		spn_lex(p);
	} else {
		/* unnamed function expression, lambda */
		node = SPN_NODE_FUNCEXPR;
		name = NULL;
	}

	if (!spn_accept(p, SPN_TOK_LPAREN)) {
		spn_parser_error(p, "expected `(' in function header");
		spn_value_release(&p->curtok.val);

		if (name != NULL) {
			spn_object_release(name);
		}

		return NULL;
	}

	ast = spn_ast_new(node, p->lineno);
	ast->name = name;

	if (!spn_accept(p, SPN_TOK_RPAREN)) {
		SpnAST *arglist = parse_decl_args(p);
		if (arglist == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		ast->left = arglist;

		if (!spn_accept(p, SPN_TOK_RPAREN)) { /* error */
			spn_parser_error(p, "expected `)' after function argument list");
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast); /* frees `arglist' and `name' too */
			return NULL;
		}
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	ast->right = body;
	return ast;
}

static SpnAST *parse_block(SpnParser *p)
{
	SpnAST *list, *ast;

	if (!spn_accept(p, SPN_TOK_LBRACE)) {
		spn_parser_error(p, "expected `{' in block statement");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_RBRACE)) {	/* empty block */
		return spn_ast_new(SPN_NODE_EMPTY, p->lineno);
	}

	list = parse_stmt_list(p);

	if (list == NULL) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_RBRACE)) {
		spn_parser_error(p, "expected `}' at end of block statement");
		spn_value_release(&p->curtok.val);
		spn_ast_free(list);
		return NULL;
	}

	/* hack: we look at the subtree that parse_stmt_list() returned.
	 * if it is a compound, we change its node from the generic
	 * SPN_NODE_COMPOUND to the correct SPN_NODE_BLOCK. If it isn't (i. e.
	 * it's a single statement), then we create a wrapper SPN_NODE_BLOCK
	 * node, add the original subtree as its (left) child, and then we
	 * return the new (block) node.
	 * 
	 * The reason why SPN_NODE_BLOCK isn't used immediately
	 * in parse_stmt_list() is that it may return multiple levels of nested
	 * compounds, but only the top-level node should be marked as a block.
	 */

	if (list->node == SPN_NODE_COMPOUND) {
		list->node = SPN_NODE_BLOCK;
		return list;
	}

	ast = spn_ast_new(SPN_NODE_BLOCK, p->lineno);
	ast->left = list;
	return ast;
}

static SpnAST *parse_expr(SpnParser *p)
{
	return parse_assignment(p);
}

static SpnAST *parse_assignment(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_ASSIGN,
		SPN_TOK_PLUSEQ,
		SPN_TOK_MINUSEQ,
		SPN_TOK_MULEQ,
		SPN_TOK_DIVEQ,
		SPN_TOK_MODEQ,
		SPN_TOK_ANDEQ,
		SPN_TOK_OREQ,
		SPN_TOK_XOREQ,
		SPN_TOK_SHLEQ,
		SPN_TOK_SHREQ,
		SPN_TOK_DOTDOTEQ,
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_ASSIGN,
		SPN_NODE_ASSIGN_ADD,
		SPN_NODE_ASSIGN_SUB,
		SPN_NODE_ASSIGN_MUL,
		SPN_NODE_ASSIGN_DIV,
		SPN_NODE_ASSIGN_MOD,
		SPN_NODE_ASSIGN_AND,
		SPN_NODE_ASSIGN_OR,
		SPN_NODE_ASSIGN_XOR,
		SPN_NODE_ASSIGN_SHL,
		SPN_NODE_ASSIGN_SHR,
		SPN_NODE_ASSIGN_CONCAT,
	};

	return parse_binexpr_rightassoc(p, toks, nodes, COUNT(toks), parse_concat);
}

static SpnAST *parse_concat(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_DOTDOT };
	static const enum spn_ast_node nodes[] = { SPN_NODE_CONCAT };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_condexpr);
}

static SpnAST *parse_condexpr(SpnParser *p)
{
	SpnAST *ast, *br_true, *br_false, *branches, *tmp;

	ast = parse_logical_or(p);
	if (ast == NULL) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_QMARK)) {
		return ast;
	}

	br_true = parse_expr(p);
	if (br_true == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_COLON)) {
		/* error, expected ':' */
		spn_parser_error(p, "expected `:' in conditional expression");
		spn_value_release(&p->curtok.val);
		spn_ast_free(ast);
		spn_ast_free(br_true);
		return NULL;
	}

	br_false = parse_condexpr(p);
	if (br_false == NULL) {
		spn_ast_free(ast);
		spn_ast_free(br_true);
		return NULL;
	}

	branches = spn_ast_new(SPN_NODE_BRANCHES, p->lineno);
	branches->left  = br_true;
	branches->right = br_false;

	tmp = spn_ast_new(SPN_NODE_CONDEXPR, p->lineno);
	tmp->left = ast; /* condition */
	tmp->right = branches; /* true and false values */

	return tmp;
}

/* Functions to parse binary mathematical expressions
 * in ascending precedence order.
 */
static SpnAST *parse_logical_or(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_LOGOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_LOGOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_logical_and);
}

static SpnAST *parse_logical_and(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_LOGAND };
	static const enum spn_ast_node nodes[] = { SPN_NODE_LOGAND };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_comparison);
}

static SpnAST *parse_comparison(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_EQUAL,
		SPN_TOK_NOTEQ,
		SPN_TOK_LESS,
		SPN_TOK_GREATER,
		SPN_TOK_LEQ,
		SPN_TOK_GEQ
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_EQUAL,
		SPN_NODE_NOTEQ,
		SPN_NODE_LESS,
		SPN_NODE_GREATER,
		SPN_NODE_LEQ,
		SPN_NODE_GEQ
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_or);
}

static SpnAST *parse_bitwise_or(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_BITOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_xor);
}

static SpnAST *parse_bitwise_xor(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_XOR };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITXOR };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_bitwise_and);
}

static SpnAST *parse_bitwise_and(SpnParser *p)
{
	static const enum spn_lex_token toks[] = { SPN_TOK_BITAND };
	static const enum spn_ast_node nodes[] = { SPN_NODE_BITAND };
	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_shift);
}

static SpnAST *parse_shift(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_SHL,
		SPN_TOK_SHR
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_SHL,
		SPN_NODE_SHR
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_additive);
}

static SpnAST *parse_additive(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_PLUS,
		SPN_TOK_MINUS
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_ADD,
		SPN_NODE_SUB
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_multiplicative);
}

static SpnAST *parse_multiplicative(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_MUL,
		SPN_TOK_DIV,
		SPN_TOK_MOD
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_MUL,
		SPN_NODE_DIV,
		SPN_NODE_MOD
	};

	return parse_binexpr_leftassoc(p, toks, nodes, COUNT(toks), parse_prefix);
}

static SpnAST *parse_prefix(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_INCR,
		SPN_TOK_DECR,
		SPN_TOK_PLUS,
		SPN_TOK_MINUS,
		SPN_TOK_LOGNOT,
		SPN_TOK_BITNOT,
		SPN_TOK_SIZEOF,
		SPN_TOK_TYPEOF,
		SPN_TOK_HASH
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_PREINCRMT,
		SPN_NODE_PREDECRMT,
		SPN_NODE_UNPLUS,
		SPN_NODE_UNMINUS,
		SPN_NODE_LOGNOT,
		SPN_NODE_BITNOT,
		SPN_NODE_SIZEOF,
		SPN_NODE_TYPEOF,
		SPN_NODE_NTHARG
	};

	SpnAST *operand, *ast;
	int idx = spn_accept_multi(p, toks, COUNT(toks));
	if (idx < 0) {
		return parse_postfix(p);
	}

	/* right recursion for right-associative operators */
	operand = parse_prefix(p);
	if (operand == NULL) {	/* error */
		return NULL;
	}

	ast = spn_ast_new(nodes[idx], p->lineno);
	ast->left = operand;

	return ast;
}

static SpnAST *parse_postfix(SpnParser *p)
{
	static const enum spn_lex_token toks[] = {
		SPN_TOK_INCR,
		SPN_TOK_DECR,
		SPN_TOK_LBRACKET,
		SPN_TOK_LPAREN,
		SPN_TOK_DOT,
		SPN_TOK_ARROW
	};

	static const enum spn_ast_node nodes[] = {
		SPN_NODE_POSTINCRMT,
		SPN_NODE_POSTDECRMT,
		SPN_NODE_ARRSUB,
		SPN_NODE_FUNCCALL,
		SPN_NODE_MEMBEROF,
		SPN_NODE_MEMBEROF
	};

	SpnAST *tmp, *expr, *ast;
	int idx;

	ast = parse_term(p);
	if (ast == NULL) {	/* error */
		return NULL;
	}

	/* iteration instead of left recursion - we want to terminate */
	while ((idx = spn_accept_multi(p, toks, COUNT(toks))) >= 0) {
		SpnAST *ident;
		tmp = spn_ast_new(nodes[idx], p->lineno);

		switch (nodes[idx]) {
		case SPN_NODE_POSTINCRMT:
		case SPN_NODE_POSTDECRMT:
			tmp->left = ast;
			break;
		case SPN_NODE_ARRSUB:
			expr = parse_expr(p);
			if (expr == NULL) { /* error  */
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			tmp->left = ast;
			tmp->right = expr;

			if (!spn_accept(p, SPN_TOK_RBRACKET)) {
				/* error: expected closing bracket */
				spn_parser_error(p, "expected `]' after expression in array subscript");
				spn_value_release(&p->curtok.val);
				/* this frees ast and expr as well */
				spn_ast_free(tmp);
				return NULL;
			}

			break;
		case SPN_NODE_MEMBEROF:
			if (p->curtok.tok != SPN_TOK_IDENT) { /* error: expected identifier as member */
				spn_parser_error(p, "expected identifier after . or -> operator");
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			ident = parse_term(p);

			if (ident == NULL) { /* error */
				spn_ast_free(ast);
				spn_ast_free(tmp);
				return NULL;
			}

			tmp->left = ast;
			tmp->right = ident;
			
			break;
		case SPN_NODE_FUNCCALL:
			tmp->left = ast;

			if (p->curtok.tok != SPN_TOK_RPAREN) {
				SpnAST *arglist = parse_call_args(p);
			
				if (arglist == NULL) {
					spn_ast_free(tmp); /* this frees `ast' too */
					return NULL;
				}

				tmp->right = arglist;
			}

			if (!spn_accept(p, SPN_TOK_RPAREN)) {
				/* error: expected closing parenthesis */
				spn_parser_error(p, "expected `)' after expression in function call");
				spn_value_release(&p->curtok.val);
				/* this frees ast and arglist as well */
				spn_ast_free(tmp);
				return NULL;
			}

			break;
		default:
			SHANT_BE_REACHED();
		}

		ast = tmp;
	}

	return ast;
}

static SpnAST *parse_term(SpnParser *p)
{
	SpnAST *ast;

	switch (p->curtok.tok) {
	case SPN_TOK_LPAREN:
		spn_lex(p);

		ast = parse_expr(p);
		if (ast == NULL) {
			return NULL;
		}

		if (!spn_accept(p, SPN_TOK_RPAREN)) {
			spn_parser_error(p, "expected `)' after parenthesized expression");
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_FUNCTION:
		/* only allow function expressions in an expression */
		return parse_function(p, 0);
	case SPN_TOK_IDENT:
		ast = spn_ast_new(SPN_NODE_IDENT, p->lineno);
		ast->name = p->curtok.val.v.ptrv;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_TRUE:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value.t = SPN_TYPE_BOOL;
		ast->value.f = 0;
		ast->value.v.boolv = 1;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_FALSE:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value.t = SPN_TYPE_BOOL;
		ast->value.f = 0;
		ast->value.v.boolv = 0;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_NIL:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value.t = SPN_TYPE_NIL;
		ast->value.f = 0;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_NAN:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value.t = SPN_TYPE_NUMBER;
		ast->value.f = SPN_TFLG_FLOAT;
		ast->value.v.fltv = 0.0 / 0.0; /* silent NaN */

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	case SPN_TOK_INT:
	case SPN_TOK_FLOAT:
	case SPN_TOK_STR:
		ast = spn_ast_new(SPN_NODE_LITERAL, p->lineno);
		ast->value = p->curtok.val;

		spn_lex(p);
		if (p->error) {
			spn_ast_free(ast);
			return NULL;
		}

		return ast;
	default:
		spn_parser_error(p, "unexpected token %d", p->curtok.tok);
		spn_value_release(&p->curtok.val);
		return NULL;
	}
}

/* this also makes a linked list */
static SpnAST *parse_decl_args(SpnParser *p)
{
	SpnAST *ast = NULL, *res;
	SpnString *name = p->curtok.val.v.ptrv;

	if (!spn_accept(p, SPN_TOK_IDENT)) {
		spn_parser_error(p, "expected identifier in function argument list");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_DECLARGS, p->lineno);
	ast->name = name;

	res = ast; /* preserve head */

	while (spn_accept(p, SPN_TOK_COMMA)) {
		SpnAST *tmp;
		SpnString *name = p->curtok.val.v.ptrv;

		if (!spn_accept(p, SPN_TOK_IDENT)) {
			spn_parser_error(p, "expected identifier in function argument list");
			spn_value_release(&p->curtok.val);
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(SPN_NODE_DECLARGS, p->lineno);
		tmp->name = name;	/* this is the actual name */
		ast->left = tmp;	/* this builds the link list */
		ast = tmp;		/* update head */
	}

	return res;
}

static SpnAST *parse_call_args(SpnParser *p)
{
	SpnAST *expr, *ast;

	expr = parse_expr(p);
	if (expr == NULL) {
		return NULL; /* fail */
	}

	ast = spn_ast_new(SPN_NODE_CALLARGS, p->lineno);
	ast->right = expr;

	while (spn_accept(p, SPN_TOK_COMMA)) {
		SpnAST *right = parse_expr(p);

		if (right == NULL) { /* fail */
			spn_ast_free(ast);
			return NULL;
		} else {
			SpnAST *tmp = spn_ast_new(SPN_NODE_CALLARGS, p->lineno);
			tmp->left = ast;	/* this node */
			tmp->right = right;	/* next node */
			ast = tmp;		/* update head */
		}
	}

	return ast;
}

static SpnAST *parse_binexpr_rightassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
)
{
	int idx;
	SpnAST *ast, *right, *tmp;

	ast = subexpr(p);
	if (ast == NULL) {
		return NULL;
	}

	idx = spn_accept_multi(p, toks, n);
	if (idx < 0) {
		return ast;
	}

	/* apply right recursion */
	right = parse_binexpr_rightassoc(p, toks, nodes, n, subexpr);

	if (right == NULL) {
		spn_ast_free(ast);
		return NULL;
	}

	tmp = spn_ast_new(nodes[idx], p->lineno);
	tmp->left = ast;
	tmp->right = right;

	return tmp;
}

static SpnAST *parse_binexpr_leftassoc(
	SpnParser *p,
	const enum spn_lex_token toks[],
	const enum spn_ast_node nodes[],
	size_t n,
	SpnAST *(*subexpr)(SpnParser *)
)
{
	int idx;
	SpnAST *ast = subexpr(p);
	if (ast == NULL) {
		return NULL;
	}

	/* iteration instead of left recursion (which wouldn't terminate) */
	while ((idx = spn_accept_multi(p, toks, n)) >= 0) {
		SpnAST *tmp, *right = subexpr(p);

		if (right == NULL) {
			spn_ast_free(ast);
			return NULL;
		}

		tmp = spn_ast_new(nodes[idx], p->lineno);
		tmp->left = ast;
		tmp->right = right;
		ast = tmp;
	}

	return ast;
}

/**************
 * Statements *
 **************/
static SpnAST *parse_if(SpnParser *p)
{
	SpnAST *cond, *br_then, *br_else, *br, *ast;
	/* skip `if' */
	if (!spn_lex(p)) {
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	br_then = parse_block(p);
	if (br_then == NULL) {
		spn_ast_free(cond);
		return NULL;
	}

	/* "else" is optional (hence we set its node to NULL beforehand).
	 * It may be followed by either a block or another if statement.
	 * The reason: we want to stay safe by enforcing blocks, but requiring
	 * the programmer to wrap each and every "else if" into a separate
	 * block is just insane and intolerably ugly -- "else if" is a
	 * common and safe enough construct to be allowed as an exception.
	 */
	br_else = NULL;
	if (spn_accept(p, SPN_TOK_ELSE)) {
		if (p->curtok.tok == SPN_TOK_LBRACE) {
			br_else = parse_block(p);
		} else if (p->curtok.tok == SPN_TOK_IF) {
			br_else = parse_if(p);
		} else {
			spn_parser_error(p, "expected block or 'if' after 'else'");
			spn_ast_free(cond);
			spn_ast_free(br_then);
			return NULL;
		}

		if (br_else == NULL) {
			spn_ast_free(cond);
			spn_ast_free(br_then);
			return NULL;
		}
	}

	br = spn_ast_new(SPN_NODE_BRANCHES, p->lineno);
	br->left = br_then;
	br->right = br_else;

	ast = spn_ast_new(SPN_NODE_IF, p->lineno);
	ast->left = cond;
	ast->right = br;

	return ast;
}

static SpnAST *parse_while(SpnParser *p)
{
	SpnAST *cond, *body, *ast;

	/* skip `while' */
	if (!spn_lex(p)) {
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(cond);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_WHILE, p->lineno);
	ast->left = cond;
	ast->right = body;

	return ast;
}

static SpnAST *parse_do(SpnParser *p)
{
	SpnAST *cond, *body, *ast;

	/* skip `do' */
	if (!spn_lex(p)) {
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		return NULL;
	}

	/* expect "while expr;" */
	if (!spn_accept(p, SPN_TOK_WHILE)) {
		spn_parser_error(p, "expected `while' after body of do-while statement");
		spn_value_release(&p->curtok.val);
		spn_ast_free(body);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_ast_free(body);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after condition of do-while statement");
		spn_value_release(&p->curtok.val);
		spn_ast_free(body);
		spn_ast_free(cond);
		return NULL;
	}

	ast = spn_ast_new(SPN_NODE_DO, p->lineno);
	ast->left = cond;
	ast->right = body;

	return ast;
}

static SpnAST *parse_for(SpnParser *p)
{
	SpnAST *init, *cond, *incr, *body, *h1, *h2, *h3, *ast;

	/* skip `for' */
	if (!spn_lex(p)) {
		return NULL;
	}

	init = parse_expr(p);
	if (init == NULL) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after initialization of for loop");
		spn_value_release(&p->curtok.val);
		spn_ast_free(init);
		return NULL;
	}

	cond = parse_expr(p);
	if (cond == NULL) {
		spn_ast_free(init);
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after condition of for loop");
		spn_value_release(&p->curtok.val);
		spn_ast_free(init);
		spn_ast_free(cond);
		return NULL;
	}

	incr = parse_expr(p);
	if (incr == NULL) {
		spn_ast_free(init);
		spn_ast_free(cond);
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(init);
		spn_ast_free(cond);
		spn_ast_free(incr);
		return NULL;
	}

	/* linked list for the loop header */
	h1 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h2 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h3 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);

	h1->left = init;
	h1->right = h2;
	h2->left = cond;
	h2->right = h3;
	h3->left = incr;

	ast = spn_ast_new(SPN_NODE_FOR, p->lineno);
	ast->left = h1;
	ast->right = body;

	return ast;
}

static SpnAST *parse_foreach(SpnParser *p)
{
	SpnAST *key, *val, *arr, *body, *h1, *h2, *h3, *ast;
	SpnString *name;

	/* skip `foreach' */
	if (!spn_lex(p)) {
		return NULL;
	}

	name = p->curtok.val.v.ptrv;
	if (!spn_accept(p, SPN_TOK_IDENT)) {
		spn_parser_error(p, "key in foreach loop must be a variable");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	key = spn_ast_new(SPN_NODE_IDENT, p->lineno);
	key->name = name;

	if (!spn_accept(p, SPN_TOK_AS)) {
		spn_parser_error(p, "expected `as' after key in foreach loop");
		spn_value_release(&p->curtok.val);
		spn_ast_free(key);
		return NULL;
	}

	name = p->curtok.val.v.ptrv;
	if (!spn_accept(p, SPN_TOK_IDENT)) {
		spn_parser_error(p, "value in foreach loop must be a variable");
		spn_value_release(&p->curtok.val);
		spn_ast_free(key);
		return NULL;
	}

	val = spn_ast_new(SPN_NODE_IDENT, p->lineno);
	val->name = name;

	if (!spn_accept(p, SPN_TOK_IN)) {
		spn_parser_error(p, "expected `in' after value in foreach loop");
		spn_value_release(&p->curtok.val);
		spn_ast_free(key);
		spn_ast_free(val);
		return NULL;
	}

	/* parse the array */
	arr = parse_expr(p);
	if (arr == NULL) {
		spn_ast_free(key);
		spn_ast_free(val);
		return NULL;
	}

	body = parse_block(p);
	if (body == NULL) {
		spn_ast_free(key);
		spn_ast_free(val);
		spn_ast_free(arr);
		return NULL;
	}

	/* linked list for the loop header */
	h1 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h2 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);
	h3 = spn_ast_new(SPN_NODE_FORHEADER, p->lineno);

	h1->left = key;
	h1->right = h2;
	h2->left = val;
	h2->right = h3;
	h3->left = arr;

	ast = spn_ast_new(SPN_NODE_FOREACH, p->lineno);
	ast->left = h1;
	ast->right = body;

	return ast;
}

static SpnAST *parse_break(SpnParser *p)
{
	/* skip `break' */
	if (!spn_lex(p)) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after `break'");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	return spn_ast_new(SPN_NODE_BREAK, p->lineno);
}

static SpnAST *parse_continue(SpnParser *p)
{
	/* skip `continue' */
	if (!spn_lex(p)) {
		return NULL;
	}

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected `;' after `continue'");
		spn_value_release(&p->curtok.val);
		return NULL;
	}

	return spn_ast_new(SPN_NODE_CONTINUE, p->lineno);
}

static SpnAST *parse_return(SpnParser *p)
{
	SpnAST *expr;

	/* skip `return' */
	if (!spn_lex(p)) {
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		return spn_ast_new(SPN_NODE_RETURN, p->lineno); /* return without value */
	}

	expr = parse_expr(p);
	if (expr == NULL) {
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		SpnAST *ast = spn_ast_new(SPN_NODE_RETURN, p->lineno);
		ast->left = expr;
		return ast;
	}

	spn_parser_error(p, "expected `;' after expression in return statement");
	spn_ast_free(expr);

	return NULL;
}

/* this builds a link list of comma-separated variable declarations */
static SpnAST *parse_vardecl(SpnParser *p)
{
	/* `ast' is the head of the list */
	SpnAST *ast = NULL, *tail = NULL;

	/* skip "var" keyword */
	spn_lex(p);

	do {
		SpnString *name = p->curtok.val.v.ptrv;
		SpnAST *expr = NULL, *tmp;

		if (!spn_accept(p, SPN_TOK_IDENT)) {
			spn_parser_error(p, "expected identifier in declaration");
			spn_value_release(&p->curtok.val);
			return NULL;
		}

		if (spn_accept(p, SPN_TOK_ASSIGN)) {
			expr = parse_expr(p);

			if (expr == NULL) {
				spn_object_release(name);
				spn_ast_free(ast);
				return NULL;
			}
		}

		tmp = spn_ast_new(SPN_NODE_VARDECL, p->lineno);
		tmp->name = name;
		tmp->left = expr;

		if (ast == NULL) {
			ast = tmp;
		} else {
			assert(tail != NULL); /* if I've got the logic right */
			tail->right = tmp;
		}

		tail = tmp;
	} while (spn_accept(p, SPN_TOK_COMMA));

	if (!spn_accept(p, SPN_TOK_SEMICOLON)) {
		spn_parser_error(p, "expected`;' after variable initialization");
		spn_value_release(&p->curtok.val);
		spn_ast_free(ast);
		return NULL;
	}

	return ast;
}

static SpnAST *parse_expr_stmt(SpnParser *p)
{
	SpnAST *ast = parse_expr(p);
	if (ast == NULL) {
		return NULL;
	}

	if (spn_accept(p, SPN_TOK_SEMICOLON)) {
		return ast;
	}

	spn_parser_error(p, "expected `;' after expression");
	spn_value_release(&p->curtok.val);
	spn_ast_free(ast);

	return NULL;
}

static SpnAST *parse_empty(SpnParser *p)
{
	/* skip semicolon */
	spn_lex(p);
	if (p->error) {
		return NULL;
	}

	return spn_ast_new(SPN_NODE_EMPTY, p->lineno);
}

