#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"
#include <editline/readline.h>
#include <editline/history.h>

#define LASSERT(args, cond, err) \
    if (!(cond)) { lval_del(args); return lval_err(err); }

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

typedef struct lval {
    int type;
    long num;
    char* err;
    char* sym;
    int count;
    struct lval** cell;
} lval;

lval* lval_eval(lval* v);

lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char * err) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = malloc(strlen(err) + 1);
    strcpy(v->err, err);
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
        default: break;
    }

    free(v);
}

lval* lval_read_num(mpc_ast_t* ast) {
    errno = 0;
    long x = strtol(ast->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
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

void lval_print(lval* v);

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
        default: printf("Unknown return type: %d", v->type); break;
    }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* eval_op(lval* x, char* op, lval* y) {
    if (x->type == LVAL_ERR) { return  x; }
    if (y->type == LVAL_ERR) { return  y; }

    if (strcmp(op, "+") == 0) { return lval_num(x->num + y->num); }
    if (strcmp(op, "-") == 0) { return lval_num(x->num - y->num); } 
    if (strcmp(op, "*") == 0) { return lval_num(x->num * y->num); } 
    if (strcmp(op, "/") == 0) {
        return y->num == 0 ? lval_err("LERR_DIV_ZERO") : lval_num(x->num / y->num); 
    } 
    return lval_err("LERR_BAD_OP");
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

lval* builtin_head(lval* a) {
    LASSERT(a, a->count == 1, "Function 'head' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorrect types!");
    LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

    lval* v = lval_take(a, 0);
    while(v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lval* a) {
    LASSERT(a, a->count == 1, "Function 'tail' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'tail' passed incorrect types!");
    LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");

    lval* v = lval_take(a, 0);
    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_list(lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lval* a) {
    LASSERT(a, a->count == 1, "Function 'eval' passed wrong number of arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type");

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(x);
}

lval* lval_join(lval* x, lval* y) {
    while(y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}

lval* builtin_join(lval* a) {
    for (int i = 0; i  < a->count; ++i) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR, "Function 'join' passed incorrect type.");
    }

    lval* x = lval_pop(a, 0);

    while(a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval* builtin_op(lval* a, char* op) {
    for (int i = 0; i < a->count; ++i) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
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


lval* builtin(lval* a, char* func) {
    if (strcmp("list", func) == 0) { return builtin_list(a); }
    if (strcmp("head", func) == 0) { return builtin_head(a); }
    if (strcmp("tail", func) == 0) { return builtin_tail(a); }
    if (strcmp("join", func) == 0) { return builtin_join(a); }
    if (strcmp("eval", func) == 0) { return builtin_eval(a); }
    if (strstr("+-/*", func)) { return builtin_op(a, func); }
    lval_del(a);
    return lval_err("Unknown function!");
}

lval* lval_eval_sexpr(lval* v) {
    for (int i = 0; i < v->count; ++i) {
        v->cell[i] = lval_eval(v->cell[i]);
    }

    for (int i = 0; i < v->count; ++i) {
        if (v->cell[i]->type == LVAL_ERR) {
            return lval_take(v, i);
        }
    }

    if (v->count == 0) { return v; }
    if (v->count == 1) { return lval_take(v, 0); }

    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression does not start with symbol!");
    }

    lval* result = builtin(v, f->sym);
    lval_del(f);
    return result;
}

lval* lval_eval(lval* v) {
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
    return v;
}

void rep(mpc_parser_t* parser) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t result;
    if (mpc_parse("<stdin>", input, parser, &result)) {
        lval* x = lval_eval(lval_read(result.output));
        lval_println(x);
        lval_del(x);
        mpc_ast_delete(result.output);
    } else {
        mpc_err_print(result.error);
        mpc_err_delete(result.error);
    }

    free(input);
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
         symbol   : \"list\" | \"head\" | \"tail\" \
                  | \"join\" | \"eval\" | '+' | '-' | '*' | '/' ; \
         sexpr    : '(' <expr>* ')' ; \
         qexpr    : '{' <expr>* '}' ; \
         expr     : <number> | <symbol> | <sexpr> | <qexpr> ; \
         lispy    : /^/ <expr>* /$/ ;",
        number, symbol, sexpr, qexpr, expr, lispy);

    puts("Lispy Version 0.0.0.0.0.1");
    puts("Press Ctrl+c to exit\n");

    while (1) {
        rep(lispy);
    }

    mpc_cleanup(6, number, symbol, sexpr, qexpr, expr, lispy);
    return 0;
}