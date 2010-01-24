/*
 * Copyright 2008-2009 Katholieke Universiteit Leuven
 * Copyright 2010      INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, K.U.Leuven, Departement
 * Computerwetenschappen, Celestijnenlaan 200A, B-3001 Leuven, Belgium
 * and INRIA Saclay - Ile-de-France, Parc Club Orsay Universite,
 * ZAC des vignes, 4 rue Jacques Monod, 91893 Orsay, France 
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <isl_set.h>
#include <isl_seq.h>
#include "isl_stream.h"
#include "isl_map_private.h"

struct variable {
	char    	    	*name;
	int	     		 pos;
	struct variable		*next;
};

struct vars {
	struct isl_ctx	*ctx;
	int		 n;
	struct variable	*v;
};

static struct vars *vars_new(struct isl_ctx *ctx)
{
	struct vars *v;
	v = isl_alloc_type(ctx, struct vars);
	if (!v)
		return NULL;
	v->ctx = ctx;
	v->n = 0;
	v->v = NULL;
	return v;
}

static void variable_free(struct variable *var)
{
	while (var) {
		struct variable *next = var->next;
		free(var->name);
		free(var);
		var = next;
	}
}

static void vars_free(struct vars *v)
{
	if (!v)
		return;
	variable_free(v->v);
	free(v);
}

static struct variable *variable_new(struct vars *v, const char *name, int len,
				int pos)
{
	struct variable *var;
	var = isl_alloc_type(v->ctx, struct variable);
	if (!var)
		goto error;
	var->name = strdup(name);
	var->name[len] = '\0';
	var->pos = pos;
	var->next = v->v;
	return var;
error:
	variable_free(v->v);
	return NULL;
}

static int vars_pos(struct vars *v, const char *s, int len)
{
	int pos;
	struct variable *q;

	if (len == -1)
		len = strlen(s);
	for (q = v->v; q; q = q->next) {
		if (strncmp(q->name, s, len) == 0 && q->name[len] == '\0')
			break;
	}
	if (q)
		pos = q->pos;
	else {
		pos = v->n;
		v->v = variable_new(v, s, len, v->n);
		if (!v->v)
			return -1;
		v->n++;
	}
	return pos;
}

static struct vars *read_var_list(struct isl_stream *s, struct vars *v)
{
	struct isl_token *tok;

	while ((tok = isl_stream_next_token(s)) != NULL) {
		int p;
		int n = v->n;

		if (tok->type != ISL_TOKEN_IDENT)
			break;

		p = vars_pos(v, tok->u.s, -1);
		if (p < 0)
			goto error;
		if (p < n) {
			isl_stream_error(s, tok, "expecting unique identifier");
			goto error;
		}
		isl_token_free(tok);
		tok = isl_stream_next_token(s);
		if (!tok || tok->type != ',')
			break;

		isl_token_free(tok);
	}
	if (tok)
		isl_stream_push_token(s, tok);

	return v;
error:
	isl_token_free(tok);
	vars_free(v);
	return NULL;
}

static struct vars *read_tuple(struct isl_stream *s, struct vars *v)
{
	struct isl_token *tok;

	tok = isl_stream_next_token(s);
	if (!tok || tok->type != '[') {
		isl_stream_error(s, tok, "expecting '['");
		goto error;
	}
	isl_token_free(tok);
	v = read_var_list(s, v);
	tok = isl_stream_next_token(s);
	if (!tok || tok->type != ']') {
		isl_stream_error(s, tok, "expecting ']'");
		goto error;
	}
	isl_token_free(tok);

	return v;
error:
	if (tok)
		isl_token_free(tok);
	vars_free(v);
	return NULL;
}

static struct isl_basic_map *add_constraints(struct isl_stream *s,
	struct vars **v, struct isl_basic_map *bmap);

static struct isl_basic_map *add_exists(struct isl_stream *s,
	struct vars **v, struct isl_basic_map *bmap)
{
	struct isl_token *tok;
	int n = (*v)->n;
	int extra;
	int seen_paren = 0;
	int i;
	unsigned total;

	tok = isl_stream_next_token(s);
	if (!tok)
		goto error;
	if (tok->type == '(') {
		seen_paren = 1;
		isl_token_free(tok);
	} else
		isl_stream_push_token(s, tok);
	*v = read_var_list(s, *v);
	if (!*v)
		goto error;
	extra = (*v)->n - n;
	bmap = isl_basic_map_cow(bmap);
	bmap = isl_basic_map_extend_dim(bmap, isl_dim_copy(bmap->dim),
			extra, 0, 0);
	total = isl_basic_map_total_dim(bmap);
	for (i = 0; i < extra; ++i) {
		int k;
		if ((k = isl_basic_map_alloc_div(bmap)) < 0)
			goto error;
		isl_seq_clr(bmap->div[k], 1+1+total);
	}
	if (!bmap)
		return NULL;
	if (isl_stream_eat(s, ':'))
		goto error;
	bmap = add_constraints(s, v, bmap);
	if (seen_paren && isl_stream_eat(s, ')'))
		goto error;
	return bmap;
error:
	isl_basic_map_free(bmap);
	return NULL;
}

static struct isl_basic_map *add_constraint(struct isl_stream *s,
	struct vars **v, struct isl_basic_map *bmap)
{
	unsigned total = isl_basic_map_total_dim(bmap);
	int k;
	int sign = 1;
	int equality = 0;
	int op = 0;
	struct isl_token *tok = NULL;

	tok = isl_stream_next_token(s);
	if (!tok)
		goto error;
	if (tok->type == ISL_TOKEN_EXISTS) {
		isl_token_free(tok);
		return add_exists(s, v, bmap);
	}
	isl_stream_push_token(s, tok);

	bmap = isl_basic_map_cow(bmap);
	bmap = isl_basic_map_extend_constraints(bmap, 0, 1);
	k = isl_basic_map_alloc_inequality(bmap);
	if (k < 0)
		goto error;
	isl_seq_clr(bmap->ineq[k], 1+total);

	for (;;) {
		tok = isl_stream_next_token(s);
		if (!tok) {
			isl_stream_error(s, NULL, "unexpected EOF");
			goto error;
		}
		if (tok->type == ISL_TOKEN_IDENT) {
			int n = (*v)->n;
			int pos = vars_pos(*v, tok->u.s, -1);
			if (pos < 0)
				goto error;
			if (pos >= n) {
				isl_stream_error(s, tok, "unknown identifier");
				goto error;
			}
			if (sign > 0)
				isl_int_add_ui(bmap->ineq[k][1+pos],
						bmap->ineq[k][1+pos], 1);
			else
				isl_int_sub_ui(bmap->ineq[k][1+pos],
						bmap->ineq[k][1+pos], 1);
		} else if (tok->type == ISL_TOKEN_VALUE) {
			struct isl_token *tok2;
			int n = (*v)->n;
			int pos = -1;
			tok2 = isl_stream_next_token(s);
			if (tok2 && tok2->type == ISL_TOKEN_IDENT) {
				pos = vars_pos(*v, tok2->u.s, -1);
				if (pos < 0)
					goto error;
				if (pos >= n) {
					isl_stream_error(s, tok2,
						"unknown identifier");
					isl_token_free(tok2);
					goto error;
				}
				isl_token_free(tok2);
			} else if (tok2)
				isl_stream_push_token(s, tok2);
			if (sign < 0)
				isl_int_neg(tok->u.v, tok->u.v);
			isl_int_add(bmap->ineq[k][1+pos],
					bmap->ineq[k][1+pos], tok->u.v);
		} else if (tok->type == '+') {
			/* nothing */
		} else if (tok->type == ISL_TOKEN_LE) {
			op = 1;
			isl_seq_neg(bmap->ineq[k], bmap->ineq[k], 1+total);
		} else if (tok->type == ISL_TOKEN_GE) {
			op = 1;
			sign = -1;
		} else if (tok->type == '=') {
			if (op) {
				isl_stream_error(s, tok, "too many operators");
				goto error;
			}
			op = 1;
			equality = 1;
			sign = -1;
		} else {
			isl_stream_push_token(s, tok);
			break;
		}
		isl_token_free(tok);
	}
	tok = NULL;
	if (!op) {
		isl_stream_error(s, tok, "missing operator");
		goto error;
	}
	if (equality)
		isl_basic_map_inequality_to_equality(bmap, k);
	return bmap;
error:
	if (tok)
		isl_token_free(tok);
	isl_basic_map_free(bmap);
	return NULL;
}

static struct isl_basic_map *add_constraints(struct isl_stream *s,
	struct vars **v, struct isl_basic_map *bmap)
{
	struct isl_token *tok;

	for (;;) {
		bmap = add_constraint(s, v, bmap);
		if (!bmap)
			return NULL;
		tok = isl_stream_next_token(s);
		if (!tok) {
			isl_stream_error(s, NULL, "unexpected EOF");
			goto error;
		}
		if (tok->type != ISL_TOKEN_AND)
			break;
		isl_token_free(tok);
	}
	isl_stream_push_token(s, tok);

	return bmap;
error:
	if (tok)
		isl_token_free(tok);
	isl_basic_map_free(bmap);
	return NULL;
}

static __isl_give isl_basic_map *basic_map_read_polylib_constraint(
	struct isl_stream *s, __isl_take isl_basic_map *bmap)
{
	int j;
	struct isl_token *tok;
	int type;
	int k;
	isl_int *c;
	unsigned nparam;
	unsigned dim;

	if (!bmap)
		return NULL;

	nparam = isl_basic_map_dim(bmap, isl_dim_param);
	dim = isl_basic_map_dim(bmap, isl_dim_out);

	tok = isl_stream_next_token(s);
	if (!tok || tok->type != ISL_TOKEN_VALUE) {
		isl_stream_error(s, tok, "expecting coefficient");
		if (tok)
			isl_stream_push_token(s, tok);
		goto error;
	}
	if (!tok->on_new_line) {
		isl_stream_error(s, tok, "coefficient should appear on new line");
		isl_stream_push_token(s, tok);
		goto error;
	}

	type = isl_int_get_si(tok->u.v);
	isl_token_free(tok);

	isl_assert(s->ctx, type == 0 || type == 1, goto error);
	if (type == 0) {
		k = isl_basic_map_alloc_equality(bmap);
		c = bmap->eq[k];
	} else {
		k = isl_basic_map_alloc_inequality(bmap);
		c = bmap->ineq[k];
	}
	if (k < 0)
		goto error;

	for (j = 0; j < dim; ++j) {
		tok = isl_stream_next_token(s);
		if (!tok || tok->type != ISL_TOKEN_VALUE) {
			isl_stream_error(s, tok, "expecting coefficient");
			if (tok)
				isl_stream_push_token(s, tok);
			goto error;
		}
		isl_int_set(c[1 + nparam + j], tok->u.v);
		isl_token_free(tok);
	}
	for (j = 0; j < nparam; ++j) {
		tok = isl_stream_next_token(s);
		if (!tok || tok->type != ISL_TOKEN_VALUE) {
			isl_stream_error(s, tok, "expecting coefficient");
			if (tok)
				isl_stream_push_token(s, tok);
			goto error;
		}
		isl_int_set(c[1 + j], tok->u.v);
		isl_token_free(tok);
	}
	tok = isl_stream_next_token(s);
	if (!tok || tok->type != ISL_TOKEN_VALUE) {
		isl_stream_error(s, tok, "expecting coefficient");
		if (tok)
			isl_stream_push_token(s, tok);
		goto error;
	}
	isl_int_set(c[0], tok->u.v);
	isl_token_free(tok);

	return bmap;
error:
	isl_basic_map_free(bmap);
	return NULL;
}

static __isl_give isl_basic_map *basic_map_read_polylib(struct isl_stream *s,
	int nparam)
{
	int i;
	struct isl_token *tok;
	struct isl_token *tok2;
	int n_row, n_col;
	int on_new_line;
	unsigned dim;
	struct isl_basic_map *bmap = NULL;

	if (nparam < 0)
		nparam = 0;

	tok = isl_stream_next_token(s);
	if (!tok) {
		isl_stream_error(s, NULL, "unexpected EOF");
		return NULL;
	}
	tok2 = isl_stream_next_token(s);
	if (!tok2) {
		isl_token_free(tok);
		isl_stream_error(s, NULL, "unexpected EOF");
		return NULL;
	}
	n_row = isl_int_get_si(tok->u.v);
	n_col = isl_int_get_si(tok2->u.v);
	on_new_line = tok2->on_new_line;
	isl_token_free(tok2);
	isl_token_free(tok);
	isl_assert(s->ctx, !on_new_line, return NULL);
	isl_assert(s->ctx, n_row >= 0, return NULL);
	isl_assert(s->ctx, n_col >= 2 + nparam, return NULL);
	dim = n_col - 2 - nparam;
	bmap = isl_basic_map_alloc(s->ctx, nparam, 0, dim, 0, n_row, n_row);
	if (!bmap)
		return NULL;

	for (i = 0; i < n_row; ++i)
		bmap = basic_map_read_polylib_constraint(s, bmap);

	bmap = isl_basic_map_simplify(bmap);
	bmap = isl_basic_map_finalize(bmap);
	return bmap;
}

static struct isl_map *map_read_polylib(struct isl_stream *s, int nparam)
{
	struct isl_token *tok;
	struct isl_token *tok2;
	int i, n;
	struct isl_map *map;

	tok = isl_stream_next_token(s);
	if (!tok) {
		isl_stream_error(s, NULL, "unexpected EOF");
		return NULL;
	}
	tok2 = isl_stream_next_token(s);
	if (!tok2) {
		isl_token_free(tok);
		isl_stream_error(s, NULL, "unexpected EOF");
		return NULL;
	}
	if (!tok2->on_new_line) {
		isl_stream_push_token(s, tok2);
		isl_stream_push_token(s, tok);
		return isl_map_from_basic_map(basic_map_read_polylib(s, nparam));
	}
	isl_stream_push_token(s, tok2);
	n = isl_int_get_si(tok->u.v);
	isl_token_free(tok);

	isl_assert(s->ctx, n >= 1, return NULL);

	map = isl_map_from_basic_map(basic_map_read_polylib(s, nparam));

	for (i = 1; i < n; ++i)
		map = isl_map_union(map,
			isl_map_from_basic_map(basic_map_read_polylib(s, nparam)));

	return map;
}

static struct isl_dim *set_names(struct isl_dim *dim, struct vars *vars,
	enum isl_dim_type type, int offset, int n)
{
	int i;
	struct variable *v;

	for (i = 0, v = vars->v; i < offset; ++i, v = v->next)
		;
	for (i = n - 1; i >= 0; --i, v = v->next)
		dim = isl_dim_set_name(dim, type, i, v->name);

	return dim;
}

static struct isl_dim *dim_from_vars(struct vars *vars,
	int nparam, int n_in, int n_out)
{
	struct isl_dim *dim;

	dim = isl_dim_alloc(vars->ctx, nparam, n_in, n_out);
	dim = set_names(dim, vars, isl_dim_param, n_out + n_in, nparam);
	dim = set_names(dim, vars, isl_dim_in, n_out, n_in);
	dim = set_names(dim, vars, isl_dim_out, 0, n_out);

	return dim;
}

static struct isl_map *map_read(struct isl_stream *s, int nparam)
{
	struct isl_dim *dim;
	struct isl_basic_map *bmap = NULL;
	struct isl_token *tok;
	struct vars *v = NULL;
	int n1;
	int n2;

	tok = isl_stream_next_token(s);
	if (!tok) {
		isl_stream_error(s, NULL, "unexpected EOF");
		goto error;
	}
	if (tok->type == ISL_TOKEN_VALUE) {
		isl_stream_push_token(s, tok);
		return map_read_polylib(s, nparam);
	}
	v = vars_new(s->ctx);
	if (tok->type == '[') {
		isl_stream_push_token(s, tok);
		v = read_tuple(s, v);
		if (!v)
			return NULL;
		if (nparam >= 0)
			isl_assert(s->ctx, nparam == v->n, goto error);
		nparam = v->n;
		tok = isl_stream_next_token(s);
		if (!tok || tok->type != ISL_TOKEN_TO) {
			isl_stream_error(s, tok, "expecting '->'");
			if (tok)
				isl_stream_push_token(s, tok);
			goto error;
		}
		isl_token_free(tok);
		tok = isl_stream_next_token(s);
	}
	if (nparam < 0)
		nparam = 0;
	if (!tok || tok->type != '{') {
		isl_stream_error(s, tok, "expecting '{'");
		if (tok)
			isl_stream_push_token(s, tok);
		goto error;
	}
	isl_token_free(tok);
	v = read_tuple(s, v);
	if (!v)
		return NULL;
	n1 = v->n - nparam;
	tok = isl_stream_next_token(s);
	if (tok && tok->type == ISL_TOKEN_TO) {
		isl_token_free(tok);
		v = read_tuple(s, v);
		if (!v)
			return NULL;
		n2 = v->n - n1 - nparam;
	} else {
		if (tok)
			isl_stream_push_token(s, tok);
		n2 = n1;
		n1 = 0;
	}
	dim = dim_from_vars(v, nparam, n1, n2);
	bmap = isl_basic_map_alloc_dim(dim, 0, 0, 0);
	if (!bmap)
		goto error;
	tok = isl_stream_next_token(s);
	if (tok && tok->type == ':') {
		isl_token_free(tok);
		bmap = add_constraints(s, &v, bmap);
		tok = isl_stream_next_token(s);
	}
	if (tok && tok->type == '}') {
		isl_token_free(tok);
	} else {
		isl_stream_error(s, tok, "unexpected isl_token");
		if (tok)
			isl_token_free(tok);
		goto error;
	}
	vars_free(v);

	bmap = isl_basic_map_simplify(bmap);
	bmap = isl_basic_map_finalize(bmap);
	return isl_map_from_basic_map(bmap);
error:
	isl_basic_map_free(bmap);
	if (v)
		vars_free(v);
	return NULL;
}

static struct isl_basic_map *basic_map_read(struct isl_stream *s, int nparam)
{
	struct isl_map *map;
	struct isl_basic_map *bmap;

	map = map_read(s, nparam);
	if (!map)
		return NULL;

	isl_assert(map->ctx, map->n <= 1, goto error);

	if (map->n == 0)
		bmap = isl_basic_map_empty_like_map(map);
	else
		bmap = isl_basic_map_copy(map->p[0]);

	isl_map_free(map);

	return bmap;
error:
	isl_map_free(map);
	return NULL;
}

__isl_give isl_basic_map *isl_basic_map_read_from_file(isl_ctx *ctx,
		FILE *input, int nparam)
{
	struct isl_basic_map *bmap;
	struct isl_stream *s = isl_stream_new_file(ctx, input);
	if (!s)
		return NULL;
	bmap = basic_map_read(s, nparam);
	isl_stream_free(s);
	return bmap;
}

__isl_give isl_basic_set *isl_basic_set_read_from_file(isl_ctx *ctx,
		FILE *input, int nparam)
{
	struct isl_basic_map *bmap;
	bmap = isl_basic_map_read_from_file(ctx, input, nparam);
	if (!bmap)
		return NULL;
	isl_assert(ctx, isl_basic_map_n_in(bmap) == 0, goto error);
	return (struct isl_basic_set *)bmap;
error:
	isl_basic_map_free(bmap);
	return NULL;
}

struct isl_basic_map *isl_basic_map_read_from_str(struct isl_ctx *ctx,
		const char *str, int nparam)
{
	struct isl_basic_map *bmap;
	struct isl_stream *s = isl_stream_new_str(ctx, str);
	if (!s)
		return NULL;
	bmap = basic_map_read(s, nparam);
	isl_stream_free(s);
	return bmap;
}

struct isl_basic_set *isl_basic_set_read_from_str(struct isl_ctx *ctx,
		const char *str, int nparam)
{
	struct isl_basic_map *bmap;
	bmap = isl_basic_map_read_from_str(ctx, str, nparam);
	if (!bmap)
		return NULL;
	isl_assert(ctx, isl_basic_map_n_in(bmap) == 0, goto error);
	return (struct isl_basic_set *)bmap;
error:
	isl_basic_map_free(bmap);
	return NULL;
}

__isl_give isl_map *isl_map_read_from_file(struct isl_ctx *ctx,
		FILE *input, int nparam)
{
	struct isl_map *map;
	struct isl_stream *s = isl_stream_new_file(ctx, input);
	if (!s)
		return NULL;
	map = map_read(s, nparam);
	isl_stream_free(s);
	return map;
}

__isl_give isl_set *isl_set_read_from_file(struct isl_ctx *ctx,
		FILE *input, int nparam)
{
	struct isl_map *map;
	map = isl_map_read_from_file(ctx, input, nparam);
	if (!map)
		return NULL;
	isl_assert(ctx, isl_map_n_in(map) == 0, goto error);
	return (struct isl_set *)map;
error:
	isl_map_free(map);
	return NULL;
}

static char *next_line(FILE *input, char *line, unsigned len)
{
	char *p;

	do {
		if (!(p = fgets(line, len, input)))
			return NULL;
		while (isspace(*p) && *p != '\n')
			++p;
	} while (*p == '#' || *p == '\n');

	return p;
}

static struct isl_vec *isl_vec_read_from_file_polylib(struct isl_ctx *ctx,
		FILE *input)
{
	struct isl_vec *vec = NULL;
	char line[1024];
	char val[1024];
	char *p;
	unsigned size;
	int j;
	int n;
	int offset;

	isl_assert(ctx, next_line(input, line, sizeof(line)), return NULL);
	isl_assert(ctx, sscanf(line, "%u", &size) == 1, return NULL);

	vec = isl_vec_alloc(ctx, size);

	p = next_line(input, line, sizeof(line));
	isl_assert(ctx, p, goto error);

	for (j = 0; j < size; ++j) {
		n = sscanf(p, "%s%n", val, &offset);
		isl_assert(ctx, n != 0, goto error);
		isl_int_read(vec->el[j], val);
		p += offset;
	}

	return vec;
error:
	isl_vec_free(vec);
	return NULL;
}

struct isl_vec *isl_vec_read_from_file(struct isl_ctx *ctx,
		FILE *input, unsigned input_format)
{
	if (input_format == ISL_FORMAT_POLYLIB)
		return isl_vec_read_from_file_polylib(ctx, input);
	else
		isl_assert(ctx, 0, return NULL);
}
