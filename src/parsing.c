#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"
#include <editline/readline.h>
#include <editline/history.h>

enum { LVAL_NUM, LVAL_ERR };
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef struct {
    int type;
    long num;
    int err;
} lval;

lval lval_num(long x) {
    lval v;
    v.type = LVAL_NUM;
    v.num = x;
    return v;
}

lval lval_err(int x) {
    lval v;
    v.type = LVAL_ERR;
    v.err = x;
    return v;
}

void lval_print(lval v) {
    switch(v.type) {
        case LVAL_NUM:
            printf("%ld", v.num);
            break;
        case LVAL_ERR:
            switch(v.err) {
                case LERR_DIV_ZERO:
                    printf("Error: Division by zero!");
                    break;
                case LERR_BAD_OP:
                    printf("Error: Invalid Operator!");
                    break;
                case LERR_BAD_NUM:
                    printf("Error: Invalid Number!");
                    break;
                default:
                    printf("Unknown error: %d", v.err);
            }
            break;
        default:
            printf("Unknown return type: %d", v.type);
    }
}

void lval_println(lval v) {
    lval_print(v);
    putchar('\n');
}

lval eval_op(lval x, char* op, lval y) {
    if (x.type == LVAL_ERR) { return  x; }
    if (y.type == LVAL_ERR) { return  y; }

    if (strcmp(op, "+") == 0) { return lval_num(x.num + y.num); }
    if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); } 
    if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); } 
    if (strcmp(op, "/") == 0) {
        return y.num == 0 ? lval_err(LERR_DIV_ZERO) : lval_num(x.num / y.num); 
    } 
    return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* ast) {
    if (strstr(ast->tag, "number")) {
        errno = 0;
        long num =  strtol(ast->contents, NULL, 10);
        return errno != ERANGE ? lval_num(num) : lval_err(LERR_BAD_NUM);
    }

    char* op = ast->children[1]->contents;
    lval x = eval(ast->children[2]);

    int i = 3;
    while(strstr(ast->children[i]->tag, "expr")) {
        x = eval_op(x, op, eval(ast->children[i]));
        i++;
    }

    return x;
}

void rep(mpc_parser_t* parser) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t result;
    if (mpc_parse("<stdin>", input, parser, &result)) {
        lval eval_result = eval(result.output);
        lval_println(eval_result);
        mpc_ast_delete(result.output);
    } else {
        mpc_err_print(result.error);
        mpc_err_delete(result.error);
    }

    free(input);
}

int main(int argc, char **argv) {
    mpc_parser_t* number = mpc_new("number");
    mpc_parser_t* operator = mpc_new("operator");
    mpc_parser_t* expr = mpc_new("expr");
    mpc_parser_t* lispy = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
        "number   : /-?[0-9]+/ ; \
         operator : '+' | '-' | '*' | '/' ; \
         expr     : <number> | '(' <operator> <expr>+ ')' ; \
         lispy    : /^/ <operator> <expr>+ /$/ ;",
        number, operator, expr, lispy);

    puts("Lispy Version 0.0.0.0.0.1");
    puts("Press Ctrl+c to exit\n");

    while (1) {
        rep(lispy);
    }

    mpc_cleanup(4, number, operator, expr, lispy);
    return 0;
}