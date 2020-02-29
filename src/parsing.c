#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"
#include <editline/readline.h>
#include <editline/history.h>

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) {  \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

#define LASSERT_TYPE(func, args, index, expected) \
    LASSERT(args, args->cell[index]->type == expected, \
        "Function '%s' passed incorrect type for argument %d!" \
        "Expected %s, got %s", func, index, ltype_name(expected),\
        ltype_name(args->cell[index]->type));

#define LASSERT_NUM_ARGS(func, args, expected) \
    LASSERT(args, args->count == expected, \
        "Function '%s' passed wrong number of arguments arguments!" \
        "Expected %d, got %d", func, expected, args->count);

#define LASSERT_NOT_EMPTY(func, args, index) \
    LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed {} for argument %d!", func, index);

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN };

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
    int type;
    long num;
    char* err;
    char* sym;
    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    int count;
    struct lval** cell;
};

struct lenv {
    lenv* par;
    int count;
    char **syms;
    lval** vals;
};

lval* lval_eval(lenv* e, lval* v);
void lval_print(lval* v);
lval* lval_copy(lval* v);
void lval_del(lval* v);
lval* lval_err(char* fmt, ...);

char* dupstr(char * str) {
    char * s = malloc(strlen(str) + 1);
    strcpy(s, str);
    return s;
}

lenv* lenv_new(void) {
    lenv* e = malloc(sizeof(lenv));
    e->par = NULL;
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

void lenv_del(lenv* e) {
    for (int i = 0; i < e->count; ++i) {
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

lval* lenv_get(lenv* e, lval* k) {
    for (int i = 0; i < e->count; ++i) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }

    if (e->par) {
        return lenv_get(e->par, k);
    }
    return lval_err("Unbound symbol: '%s'", k->sym);
}

lenv* lenv_copy(lenv* e) {
    lenv* n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char*) * n->count);
    n->vals = malloc(sizeof(lval*) * n->count);

    for (int i = 0; i < n->count; ++i) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }

    return n;
}

void lenv_put(lenv* e, lval* k, lval* v) {
    for (int i = 0; i < e->count; ++i) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    e->count++;
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);
    e->syms = realloc(e->syms, sizeof(char*) * e->count);

    e->vals[e->count-1] = lval_copy(v);
    e->syms[e->count-1] = dupstr(k->sym);
    
}

void lenv_def(lenv* e, lval* k, lval* v) {
    while (e->par) { e = e->par; }
    lenv_put(e, k, v);
}

lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* fmt, ...) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    va_list va;
    va_start(va, fmt);

    const int size = 512;
    v->err = malloc(size);
    vsnprintf(v->err, size -1, fmt, va);

    v->err = realloc(v->err, strlen(v->err) + 1);

    va_end(va);
    return v;
}

lval* lval_sym(char *sym) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(sym) + 1);
    strcpy(v->sym, sym);
    return v;
}

lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_builtin(lbuiltin builtin) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = builtin;
    return v;
}

lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    v->builtin = NULL;
    v->env = lenv_new();
    v->formals = formals;
    v->body = body;

    return v;
}

void lval_del(lval* v) {
    if (v == NULL) return;

    switch (v->type) {
        case LVAL_NUM: break;
        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;
        case LVAL_SEXPR: /* Fall through */
        case LVAL_QEXPR: 
            for (int i = 0; i < v->count; ++i) {
                lval_del(v->cell[i]);
            }

            free(v->cell);
            break;
        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
            break;
        default: break;
    }

    free(v);
}

lval* lval_read_num(mpc_ast_t* ast) {
    errno = 0;
    long x = strtol(ast->contents, NULL, 10);
    return errno != ERANGE ? 
        lval_num(x) : lval_err("Invalid number %s", ast->contents);
}

lval* lval_add(lval* v, lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

lval* lval_read(mpc_ast_t* ast) {
    if (strstr(ast->tag, "number")) { return lval_read_num(ast); }
    if (strstr(ast->tag, "symbol")) { return lval_sym(ast->contents); }

    lval* x = NULL;
    if (strcmp(ast->tag, ">") == 0 || strstr(ast->tag, "sexpr")) { x = lval_sexpr(); }
    if (strstr(ast->tag, "qexpr")) { x = lval_qexpr(); }

    for (int i = 0; i < ast->children_num; ++i) {
        if(strcmp(ast->children[i]->contents, "(") == 0) { continue; }
        if(strcmp(ast->children[i]->contents, ")") == 0) { continue; }
        if(strcmp(ast->children[i]->contents, "{") == 0) { continue; }
        if(strcmp(ast->children[i]->contents, "}") == 0) { continue; }
        if(strcmp(ast->children[i]->tag, "regex") == 0) { continue; }
        x = lval_add(x, lval_read(ast->children[i]));
    }

    return x;
}

void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; ++i) {
        lval_print(v->cell[i]);

        if (i != (v->count -1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_print(lval* v) {
    switch(v->type) {
        case LVAL_NUM: printf("%ld", v->num); break;
        case LVAL_ERR: printf("Error %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
        case LVAL_FUN: 
            if (v->builtin) {
                printf("<function>");
            } else {
                printf("(\\");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
            break;
        default: printf("Unknown return type: %d", v->type); break;
    }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch(v->type) {
        case LVAL_NUM: x->num = v->num; break;
        case LVAL_ERR: x->err = dupstr(v->err); break;
        case LVAL_SYM: x->sym = dupstr(v->sym); break;
        case LVAL_SEXPR: /* Fall through */
        case LVAL_QEXPR: 
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count);
            for (int i = 0; i < x->count; ++i) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
            break;
        case LVAL_FUN: 
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
            break;
        default: printf("Unknown type '%d', copy might be incomplete", v->type); break;
    }
    return x;
}

lval* lval_pop(lval* v, int i) {
    lval* x = v->cell[i];
    memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count - i - 1));
    v->count--;
    v->cell = realloc(v->cell, sizeof(lval) * v->count);
    return x;
}

lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

char* ltype_name(int t) {
    switch (t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}


lval* builtin_head(lenv* e, lval* a) {
    LASSERT_NUM_ARGS("head", a, 1);
    LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("head", a, 0);

    lval* v = lval_take(a, 0);
    while(v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lenv* e, lval* a) {
    LASSERT_NUM_ARGS("tail", a, 1);
    LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("tail", a, 0);

    lval* v = lval_take(a, 0);
    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lenv* e, lval* a) {
    LASSERT_NUM_ARGS("eval", a, 1);
    LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval* builtin_lambda(lenv* e, lval* a) {
    LASSERT_NUM_ARGS("\\", a, 2);
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    for (int i = 0; i < a->cell[0]->count; ++i) {
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define non-symbol. Expected %s, got %s",
            ltype_name(LVAL_SYM), ltype_name(a->cell[0]->cell[i]->type));
    }

    lval* formals = lval_pop(a, 0);
    lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

lval* lval_join(lenv* e, lval* x, lval* y) {
    while(y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}

lval* builtin_join(lenv* e, lval* a) {
    for (int i = 0; i  < a->count; ++i) {
        LASSERT_TYPE("join", a, i, LVAL_QEXPR);
    }

    lval* x = lval_pop(a, 0);

    while(a->count) {
        x = lval_join(e, x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
    for (int i = 0; i < a->count; ++i) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number type: %s!",
                ltype_name(a->cell[i]->type));
        }
    }

    lval* x = lval_pop(a, 0);

    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    while(a->count > 0) {
        lval* y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; } 
        if (strcmp(op, "*") == 0) { x->num *= y->num; } 
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division by zero!");
                break;
            }
            x-> num /= y->num;
        } 

        lval_del(y);
    }

    lval_del(a);
    return x;
}

lval* builtin_add(lenv* e, lval* a) {
    return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
    return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
    return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
    return builtin_op(e, a, "/");
}

lval* builtin_var(lenv* e, lval* a, char* func) {
    LASSERT_TYPE("def", a, 0, LVAL_QEXPR);

    lval* syms = a->cell[0];

    for (int i = 0; i < syms->count; ++i) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM,
            "Function '%s' cannot define non-symbol type"
            "Expected %s, Got %s",
            func, ltype_name(LVAL_SYM), ltype_name(syms->cell[i]->type));
    }

    LASSERT(a, (syms->count == a->count -1),
        "Function '%s' cannot define incorrect number of values to symbols"
        "Expected %s, Got %s",
        func, a->count -1, syms->count);

    for (int i = 0; i < syms->count; ++i) {
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i+1]);
        }

        if (strcmp(func, "=") == 0)  {
            lenv_put(e, syms->cell[i], a->cell[i+1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval* builtin_def(lenv* e, lval* a) {
    return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
    return builtin_var(e, a, "=");
}

lval* lval_call(lenv* e, lval* f, lval* a) {
    if (f->builtin) { return f->builtin(e, a); }

    int given = a->count;
    int total = f->formals->count;

    while(a->count) {
        if (f->formals->count == 0) {
            lval_del(a);
            return lval_err(
                "Function passed to many arguments. "
                "Expected %d, got %d.", total, given);
        }

        lval* sym = lval_pop(f->formals, 0);
        if (strcmp(sym->sym, "&") == 0) {
            if (f->formals->count != 1) {
                lval_del(a);
                return lval_err("Function format invalid."
                    "Symbol '&' not followed by single symbol.");
            }

            lval* nsym = lval_pop(f->formals, 0);
            lenv_put(f->env, nsym, builtin_list(e, a));
            lval_del(sym);
            lval_del(nsym);
            break;
        }

        lval* val = lval_pop(a, 0);
        lenv_put(f->env, sym, val);

        lval_del(sym);
        lval_del(val);
    }

    lval_del(a);

    if (f->formals->count > 0 &&
         strcmp(f->formals->cell[0]->sym, "&") == 0) {
        if (f->formals->count != 2) {
            return lval_err("Function format invalid."
                    "Symbol '&' not followed by single symbol.");
        }

        lval_del(lval_pop(f->formals, 0));

        lval* sym = lval_pop(f->formals, 0);
        lval* val = lval_qexpr();

        lenv_put(f->env, sym, val);
        lval_del(sym);
        lval_del(val);
    }

    if (f->formals->count == 0) {
        f->env->par = e;
        return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    }
    return lval_copy(f);
}

lval* lval_eval_sexpr(lenv* e, lval* v) {
    for (int i = 0; i < v->count; ++i) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }

    for (int i = 0; i < v->count; ++i) {
        if (v->cell[i]->type == LVAL_ERR) {
            return lval_take(v, i);
        }
    }

    if (v->count == 0) { return v; }
    if (v->count == 1) { return lval_take(v, 0); }

    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression starts with incorrect type!"
            "Expected %s, got %s", ltype_name(LVAL_FUN), ltype_name(f->type));
    }

    lval* result = lval_call(e, f, v);
    lval_del(f);
    return result;
}

lval* lval_eval(lenv* e, lval* v) {
    if(v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
    return v;
}

void rep(mpc_parser_t* parser, lenv* e) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t result;
    if (mpc_parse("<stdin>", input, parser, &result)) {
        lval* x = lval_eval(e, lval_read(result.output));
        lval_println(x);
        lval_del(x);
        mpc_ast_delete(result.output);
    } else {
        mpc_err_print(result.error);
        mpc_err_delete(result.error);
    }

    free(input);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin builtin) {
    lval* k = lval_sym(name);
    lval* v = lval_builtin(builtin);
    lenv_put(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv* e) {
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);

    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "=", builtin_put);
    lenv_add_builtin(e, "\\", builtin_lambda);
    
}

int main(int argc, char **argv) {

    mpc_parser_t* number = mpc_new("number");
    mpc_parser_t* symbol = mpc_new("symbol");
    mpc_parser_t* sexpr = mpc_new("sexpr");
    mpc_parser_t* qexpr = mpc_new("qexpr");
    mpc_parser_t* expr = mpc_new("expr");
    mpc_parser_t* lispy = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
        "number   : /-?[0-9]+/ ; \
         symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ; \
         sexpr    : '(' <expr>* ')' ; \
         qexpr    : '{' <expr>* '}' ; \
         expr     : <number> | <symbol> | <sexpr> | <qexpr> ; \
         lispy    : /^/ <expr>* /$/ ;",
        number, symbol, sexpr, qexpr, expr, lispy);

    puts("Lispy Version 0.0.0.0.0.1");
    puts("Press Ctrl+c to exit\n");

    lenv* e = lenv_new();
    lenv_add_builtins(e);

    while (1) {
        rep(lispy, e);
    }

    lenv_del(e);

    mpc_cleanup(6, number, symbol, sexpr, qexpr, expr, lispy);
    return 0;
}