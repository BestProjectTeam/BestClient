// SPDX-License-Identifier: Zlib
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 * 
 * Maintained by SollyBunny at https://github.com/SollyBunny/tinyexpr
 * Created by codeplea at https://github.com/codeplea/tinyexpr
 */
/*
 * Copyright (c) 2015-2020 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* COMPILE TIME OPTIONS */

/* Exponentiation associativity:
For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

#ifdef __cplusplus
extern "C" {
#endif

#include "tinyexpr.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif

typedef double (*te_fun2)(double, double);

enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};

enum {
    TE_CONSTANT = 1
};

typedef struct state {
    const char *start;
    const char *next;
    int type;
    te_value v;
    void *context;

    const te_variable *lookup;
    int lookup_len;
} state;

#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )

static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const size_t arity = ARITY(type);
    const size_t psize = sizeof(void*) * arity;
    const size_t size = sizeof(te_expr) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = (te_expr*)malloc(size);
    if (!ret) return NULL;

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->v.bound = NULL;
    return ret;
}

static te_expr *new_expr_1p(const int type, const te_expr *p1) {
    const te_expr *parameters[] = {p1};
    return new_expr(type, parameters);
}

static te_expr *new_expr_2p(const int type, const te_expr *p1, const te_expr *p2) {
    const te_expr *parameters[] = {p1, p2};
    return new_expr(type, parameters);
}

static void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]); /* Falls through. */
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]); /* Falls through. */
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]); /* Falls through. */
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]); /* Falls through. */
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]); /* Falls through. */
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]); /* Falls through. */
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
        default: return;
    }
}

void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}

static double fn_pi(void) {
    return 3.14159265358979323846;
}
static double fn_e(void) {
    return 2.71828182845904523536;
}
static double fn_fac(double a) {
    if (a == 0.0)
        return 1.0;
    if (a == -1.0)
        return NAN;
    return tgamma(a + 1.0);
}
static double fn_ncr(double n, double r) {
    if (r < 0 || n < 0 || r > n) return NAN;
    if (r > n / 2) r = n - r; // symmetry for better numerical stability
    return round(exp(lgamma(n + 1.0) - lgamma(r + 1.0) - lgamma(n - r + 1.0)));
}
static double fn_npr(double n, double r) {
    return fn_ncr(n, r) * tgamma(r + 1.0);
}
static double fn_abs(double a) { return fabs(a); }
static double fn_acos(double a) { return acos(a); }
static double fn_asin(double a) { return asin(a); }
static double fn_atan(double a) { return atan(a); }
static double fn_atan2(double a, double b) { return atan2(a, b); }
static double fn_ceil(double a) { return ceil(a); }
static double fn_cos(double a) { return cos(a); }
static double fn_cosh(double a) { return cosh(a); }
static double fn_exp(double a) { return exp(a); }
static double fn_floor(double a) { return floor(a); }
static double fn_log(double a) { return log(a); }
static double fn_log10(double a) { return log10(a); }
static double fn_pow(double a, double b) { return pow(a, b); }
static double fn_sin(double a) { return sin(a); }
static double fn_sinh(double a) { return sinh(a); }
static double fn_sqrt(double a) { return sqrt(a); }
static double fn_tan(double a) { return tan(a); }
static double fn_tanh(double a) { return tanh(a); }

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", {(void*)fn_abs}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", {(void*)fn_acos}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", {(void*)fn_asin}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", {(void*)fn_atan}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", {(void*)fn_atan2}, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", {(void*)fn_ceil}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos", {(void*)fn_cos}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", {(void*)fn_cosh}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", {(void*)fn_e}, TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", {(void*)fn_exp}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", {(void*)fn_fac}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", {(void*)fn_floor}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ln", {(void*)fn_log}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", {(void*)fn_log}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", {(void*)fn_log10}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", {(void*)fn_log10}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ncr", {(void*)fn_ncr}, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", {(void*)fn_npr}, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", {(void*)fn_pi}, TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", {(void*)fn_pow}, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"sin", {(void*)fn_sin}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", {(void*)fn_sinh}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", {(void*)fn_sqrt}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", {(void*)fn_tan}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", {(void*)fn_tanh}, TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {0, {NULL}, 0, 0}
};

static const te_variable *find_builtin(const char *name, size_t len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, size_t len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0')
            return var;
    }
    return 0;
}

static double op_add(double a, double b) { return a + b; }
static double op_sub(double a, double b) { return a - b; }
static double op_mul(double a, double b) { return a * b; }
static double op_div(double a, double b) { return a / b; }
static double op_negate(double a) { return -a; }
static double op_comma(double a, double b) { (void)a; return b; }
static double op_pow(double a, double b) { return pow(a, b); }
static double op_mod(double a, double b) { return fmod(a, b); }

static void next_token(state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->v.value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (isalpha(s->next[0])) {
                const char *start;
                start = s->next;
                while (isalpha(s->next[0]) || isdigit(s->next[0]) || (s->next[0] == '_'))
                    s->next++;

                const te_variable *var = find_lookup(s, start, (size_t)(s->next - start));
                if (!var)
                    var = find_builtin(start, (size_t)(s->next - start));

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->v.bound = (const double *)var->address.any;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->v.f.any = var->address.any;
                            break;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->v.f.f2 = op_add; break;
                    case '-': s->type = TOK_INFIX; s->v.f.f2 = op_sub; break;
                    case '*': s->type = TOK_INFIX; s->v.f.f2 = op_mul; break;
                    case '/': s->type = TOK_INFIX; s->v.f.f2 = op_div; break;
                    case '^': s->type = TOK_INFIX; s->v.f.f2 = op_pow; break;
                    case '%': s->type = TOK_INFIX; s->v.f.f2 = op_mod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}

static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            if (!ret) return NULL;
            ret->v.value = s->v.value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            if (!ret) return NULL;
            ret->v.bound = s->v.bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            if (!ret) return NULL;
            ret->v.f.any = s->v.f.any;
            if (IS_CLOSURE(s->type)) ret->parameters[0] = (te_expr*)s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            if (!ret) return NULL;
            ret->v.f.any = s->v.f.any;
            if (IS_CLOSURE(s->type)) ret->parameters[1] = (te_expr*)s->context;
            next_token(s);
            ret->parameters[0] = power(s);
            if (!ret->parameters[0]) { te_free(ret); return NULL; }
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            if (!ret) return NULL;
            ret->v.f.any = s->v.f.any;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = (te_expr*)s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
                    if (!ret->parameters[i]) { te_free(ret); return NULL; }

                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            if (!ret) return NULL;

            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            if (!ret) return NULL;

            s->type = TOK_ERROR;
            ret->v.value = NAN;
            break;
    }

    return ret;
}

static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->v.f.f2 == op_add || s->v.f.f2 == op_sub)) {
        if (s->v.f.f2 == op_sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        te_expr *b = base(s);
        if (!b) return NULL;

        ret = new_expr_1p(TE_FUNCTION1 | TE_FLAG_PURE, b);
        if (!ret) { te_free(b); return NULL; }

        ret->v.f.f1 = op_negate;
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);
    if (!ret) return NULL;

    int neg = 0;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) && ret->v.f.f1 == op_negate) {
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
        neg = 1;
    }

    te_expr *insertion = NULL;

    while (s->type == TOK_INFIX && (s->v.f.f2 == op_pow)) {
        te_fun2 t = s->v.f.f2;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr *p = power(s);
            if (!p) { te_free(ret); return NULL; }

            te_expr *insert = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], p);
            if (!insert) { te_free(p), te_free(ret); return NULL; }

            insert->v.f.f2 = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        } else {
            te_expr *p = power(s);
            if (!p) { te_free(ret); return NULL; }

            te_expr *prev = ret;
            ret = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
            if (!ret) { te_free(p), te_free(prev); return NULL; }

            ret->v.f.f2 = t;
            insertion = ret;
        }
    }

    if (neg) {
        te_expr *prev = ret;
        ret = new_expr_1p(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        if (!ret) { te_free(prev); return NULL; }
        ret->v.f.f1 = op_negate;
    }

    return ret;
}
#else
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);
    if (!ret) return NULL;

    while (s->type == TOK_INFIX && (s->v.f.f2 == op_pow)) {
        te_fun2 t = s->v.f.f2;
        next_token(s);
        te_expr *p = power(s);
        if (!p) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
        if (!ret) { te_free(p), te_free(prev); return NULL; }

        ret->v.f.f2 = t;
    }

    return ret;
}
#endif

static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);
    if (!ret) return NULL;

    while (s->type == TOK_INFIX && (s->v.f.f2 == op_mul || s->v.f.f2 == op_div || s->v.f.f2 == op_mod)) {
        te_fun2 t = s->v.f.f2;
        next_token(s);
        te_expr *f = factor(s);
        if (!f) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, ret, f);
        if (!ret) { te_free(f), te_free(prev); return NULL; }

        ret->v.f.f2 = t;
    }

    return ret;
}

static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);
    if (!ret) return NULL;

    while (s->type == TOK_INFIX && (s->v.f.f2 == op_add || s->v.f.f2 == op_sub)) {
        te_fun2 t = s->v.f.f2;
        next_token(s);
        te_expr *te = term(s);
        if (!te) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, ret, te);
        if (!ret) { te_free(te), te_free(prev); return NULL; }

        ret->v.f.f2 = t;
    }

    return ret;
}

static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);
    if (!ret) return NULL;

    while (s->type == TOK_SEP) {
        next_token(s);
        te_expr *e = expr(s);
        if (!e) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr_2p(TE_FUNCTION2 | TE_FLAG_PURE, ret, e);
        if (!ret) { te_free(e), te_free(prev); return NULL; }

        ret->v.f.f2 = op_comma;
    }

    return ret;
}

#define M(e) te_eval(n->parameters[e])

double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->v.value;
        case TE_VARIABLE: return *n->v.bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return n->v.f.f0();
                case 1: return n->v.f.f1(M(0));
                case 2: return n->v.f.f2(M(0), M(1));
                case 3: return n->v.f.f3(M(0), M(1), M(2));
                case 4: return n->v.f.f4(M(0), M(1), M(2), M(3));
                case 5: return n->v.f.f5(M(0), M(1), M(2), M(3), M(4));
                case 6: return n->v.f.f6(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return n->v.f.f7(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return n->v.f.cl0(n->parameters[0]);
                case 1: return n->v.f.cl1(n->parameters[1], M(0));
                case 2: return n->v.f.cl2(n->parameters[2], M(0), M(1));
                case 3: return n->v.f.cl3(n->parameters[3], M(0), M(1), M(2));
                case 4: return n->v.f.cl4(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return n->v.f.cl5(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return n->v.f.cl6(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return n->v.f.cl7(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef M

static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        for (int i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->v.value = value;
        }
    }
}

te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);
    if (root == NULL) {
        if (error) *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (int)(s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}

double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);

    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->v.value); break;
    case TE_VARIABLE: printf("bound %p\n", (void*)n->v.bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
        arity = ARITY(n->type);
        printf("f%d", arity);
        for(i = 0; i < arity; i++) {
            printf(" %p", (void*)n->parameters[i]);
        }
        printf("\n");
        for(i = 0; i < arity; i++) {
            pn(n->parameters[i], depth + 1);
        }
        break;
    }
}

void te_print(const te_expr *n) {
    pn(n, 0);
}

#ifdef __cplusplus
}
#endif
