#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

/* If we are compiling on Windows compile these functions */
#ifdef _WIN32
#include <string.h>

static char buffer[2048];

/* Fake readline function */
char* readline(char* prompt) {
    fputs(promt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer)+1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy)-1] = '\0';
    return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* Otherwise include the editline headers */
#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

#define LASSERT_TYPE(args, cond, fmt, ...) \
    if (!(args->type == cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

#define LASSERT_COUNT(args, cond, fmt, ...) \
    if (!(args->count == cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

/* Forward Declarations */

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Create Enumeration of Possible lval Types */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM,
       LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

char* ltype_name(int t) {
    switch(t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}

/* Declare a function pointer */
typedef lval*(*lbuiltin)(lenv*, lval*);

/* Declare New lval Struct */
struct lval {
    int type;

    /* Basic */
    double num;
    char* err;
    char* sym;

    /* Function */
    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    /* Expression */
    int count;
    struct lval** cell;
};

/* Define New lenv Struct */
struct lenv {
    int count;
    char** syms;
    lval** vals;
};

/* Create a new function */
lval* lval_fun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

/* Create a new number type lval */
lval* lval_num(double x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

/* Create a new error type lval */
lval* lval_err(char* fmt, ...) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    /* Create a va list and initialize it */
    va_list va;
    va_start(va, fmt);

    /* Allocate 512 bytes of space */
    v->err = malloc(512);

    /* printf the error string with a maximum of 511 characters */
    vsnprintf(v->err, 511, fmt, va);

    /* Reallocate to number of bytes actually used */
    v->err = realloc(v->err, strlen(v->err)+1);

    /* Cleanup our va list */
    va_end(va);

    return v;
}

/* Construct a pointer to a new Symbol lval */
lval* lval_sym(char* s) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

/* A pointer to a new empty Sexpr lval */
lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* A pointer to a new empty Qexpr lval */
lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

/* Destructor for lval */
void lval_del(lval* v) {
    switch (v->type) {
        /* Do nothing special for number type */
        case LVAL_NUM: break;

        /* For Err or Sym free the string data */
        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;

        /* If Qexpr or Sexpr then delete all elements inside */
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
               lval_del(v->cell[i]);
            }
            /* Also free the memory allocated to contain the pointers */
            free(v->cell);
            break;
        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
            break;
    }

    /* Free the memory allocated for the "lval" struct itself */
    free(v);
}

/* Make a copy of given lval */
lval* lval_copy(lval* v) {

    lval*x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {

        /* Copy Functions and Numbers Directly */
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
        case LVAL_NUM: x->num = v->num; break;

        /* Copy Strings using malloc and strcpy */
        case LVAL_ERR: x->err = malloc(strlen(v->err) + 1);
                       strcpy(x->err, v->err); break;

        case LVAL_SYM: x->sym = malloc(strlen(v->sym) + 1);
                       strcpy(x->sym, v->sym); break;

        /* Copy Lists by copying each sub-expression */
        case LVAL_SEXPR:
        case LVAL_QEXPR:
                       x->count = v->count;
                       x->cell = malloc(sizeof(lval*) * x->count);
                       for (int i = 0; i < x->count; i++) {
                           x->cell[i] = lval_copy(v->cell[i]);
                       }
                       break;
    }

    return x;
}

/* Define the pop function for lval */
lval* lval_pop(lval* v, int i) {
    /* Find the item at "i" */
    lval* x = v->cell[i];

    /* Shift memory after the item at "i" over the top */
    memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

    /* Decrease the count of items in the list */
    v->count--;

    /* Reallocate the memory used */
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    return x;
}

/* Single out an lval by it's index */
lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

/* Initialise lenv */
lenv* lenv_new(void) {
    lenv* e = malloc(sizeof(lenv));
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

/* Delete lenv */
void lenv_del(lenv* e) {
    for (int i = 0; i < e->count; i++) {
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

/* Get a value from the environment */
lval* lenv_get(lenv* e, lval* k) {

    /* Iterate over all items in environment */
    for (int i = 0; i < e->count; i++) {
        /* Check if the stored string matches the symbol string */
        /* If it does, return a copy of the value */
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }
    /* If no symbol found return error */
    return lval_err("Unbound symbol '%s'", k->sym);
}

/* Put a value into the environment */
void lenv_put(lenv* e, lval* k, lval* v) {

    /* Iterate over all items in environment */
    /* This is to see if variable already exists */
    for (int i = 0; i < e->count; i++) {

        /* If variable is found delete item at that position */
        /* And replace with variable supplied by user */
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    /* If no existing entry found allocate space for new entry */
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);
    e->syms = realloc(e->syms, sizeof(char*) * e->count);

    /* Copy contents of lval and symbol string into new location */
    e->vals[e->count-1] = lval_copy(v);
    e->syms[e->count-1] = malloc(strlen(k->sym)+1);
    strcpy(e->syms[e->count-1], k->sym);
}

/* Create a user defined function */
lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    /* Set Builtin to Null */
    v->builtin = NULL;

    /* Build new environment */
    v->env = lenv_new();

    /* Set Formals and Body */
    v->formals = formals;
    v->body = body;
    return v;
}

/* Convert number from string to double */
lval* lval_read_num(mpc_ast_t* t) {
    errno = 0;
    double x = strtod(t->contents, NULL);
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

/* Add another lval value to the cell field */
lval* lval_add(lval* v, lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

/* Convert parsed tree structure completely into an lval */
lval* lval_read(mpc_ast_t* t) {

    /* If Symbol or Number return conversion to that type */
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

    /* If root (>) or sexpr then create empty list */
    lval* x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
    if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

    /* Fill this list with any valid expression contained within */
    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
        if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
        x = lval_add(x, lval_read(t->children[i]));
    }

    return x;
}

/* Declaration for lval_expr_print */
void lval_expr_print(lval* v, char open, char close);

/* Print the lval as it is */
void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM: printf("%g", v->num); break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
        case LVAL_FUN:
            if (v->builtin) {
            printf("<builtin>");
            } else {
             printf("(\\ "); lval_print(v->formals);
             putchar(' '); lval_print(v->body); putchar(')');
            }
            break;
    }
}

/* Print expressions */
void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {

        /* Print Value contained within */
        lval_print(v->cell[i]);

        /* Don't print trailing space if last element */
        if (i != (v->count-1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

/* Print an "lval" followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

/* Do all the computations here */
lval* builtin_op(lenv* e, lval* a, char* op) {

    /* Ensure all arguments are numbers */
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    /* Pop the first element */
    lval* x = lval_pop(a, 0);

    /* If no arguments and sub then perform unary negation */
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    /* While there are still elements remaining */
    while (a->count > 0) {

        /* Pop the next element */
        lval* y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "^") == 0) { x->num = pow(x->num, y->num); }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x); lval_del(y);
                x = lval_err("Division By Zero!"); break;
            }
            x->num /= y->num;
        }
        if (strcmp(op, "%") == 0) {
            if (y->num == 0) {
                lval_del(x); lval_del(y);
                x = lval_err("Modulo by Zero!"); break;
            }
            if (rint(x->num) == x->num && rint(y->num) == y-> num) {
                x->num = (long)x->num % (long)y->num;
            } else {
                lval_del(x); lval_del(y);
                x = lval_err("Modulo without integer operands"); break;
            }
        }
        if (strcmp(op, "max") == 0) {
            x->num = (x->num > y->num) ? x->num : y->num;
        }
        if (strcmp(op, "min") == 0) {
            x->num = (x->num < y->num) ? x->num : y->num;
        }

        lval_del(y);
    }

    lval_del(a); return x;
}

/* The head function for Q-expressions */
lval* builtin_head(lenv* e, lval* a) {
    /* Check Error Conditions */
    LASSERT_COUNT(a, 1,
            "Function 'head' too many arguments! "
            "Got %i, Expected %i",
            a->count, 1);
    LASSERT_TYPE(a->cell[0], LVAL_QEXPR,
            "Function 'head' passed incorrect type for argument 0. "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    LASSERT(a->cell[0], a->cell[0]->count != 0,
            "Function 'head' passed {}!");

    /* Otherwise take first argument */
    lval* v = lval_take(a, 0);

    /* Delete all elements that are not head and return */
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

/* Builtin tail function for Q-expressions */
lval* builtin_tail(lenv* e, lval* a) {
    /* Check Error Conditions */
    LASSERT(a, a->count == 1,
            "Function 'tail' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'tail' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
            "Function 'tail' passed {}!");

    /* Take first argument */
    lval* v = lval_take(a, 0);

    /* Delete first element and return */
    lval_del(lval_pop(v, 0));
    return v;
}

/* Declare lval_eval_sexpr */
lval* lval_eval_sexpr(lenv* e, lval* v);

/* Evaluate expressions by transforming lval */
lval* lval_eval(lenv* e, lval* v) {
    if (v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
    return v;
}

/* Declare builtin function */
lval* builtin(lval* a, char* func);

/* Evaluate S-expressions */
lval* lval_eval_sexpr(lenv* e, lval* v) {

    /* Evaluate Children */
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }

    /* Error Checking */
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }

    /* Empty Expression */
    if (v->count == 0) { return v; }

    /* Single Expression */
    if (v->count == 1) { return lval_take(v, 0); }

    /* Ensure First Element is a functions after evaluation */
    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval_del(v); lval_del(f);
        return lval_err("first element is not a function");
    }

    /* If so call function to get result */
    lval* result = f->builtin(e, v);
    lval_del(f);
    return result;
}

/* Convert a S-expression to an Q-expression */
lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

/* Convert a Q-expression to an S-expression and evaluate it */
lval* builtin_eval(lenv* e, lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type!");

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

/* Function to join lval */
lval* lval_join(lval* x, lval* y) {

    /* For each cell in 'y' add it to 'x' */
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    /* Delete the empty 'y' and return 'x' */
    lval_del(y);
    return x;
}

/* Join Q-expressions together */
lval* builtin_join(lenv* e, lval* a) {

    for (int i = 0; i < a ->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type.");
    }

    lval* x = lval_pop(a, 0);

    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

/* Append given value to the front of the given Q-expression */
lval* builtin_cons(lenv* e, lval* a) {
    /* Check whether the last expression is a Q-expression */
    LASSERT(a, a->cell[a->count-1]->type == LVAL_QEXPR,
            "Function 'cons' didn't detect Q-expression at end.");
    /* Check if any passed values is an operator */
    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type != LVAL_SYM,
                "Cannot add a symbol to a Q-expression.");
    }

    lval* x = lval_pop(a, a->count-1);

    while (a->count) {
        x->count++;
        x->cell = realloc(x->cell, sizeof(lval*) * x->count);
        memmove(&x->cell[1], &x->cell[0], sizeof(lval*) *  (x->count-1));
        x->cell[0] = lval_pop(a, a->count-1);
        x->cell = realloc(x->cell, sizeof(lval*) * x->count);
    }

    lval_del(a);
    return x;
}

/* Define the len function to display length of Q-expression */
lval* builtin_len(lenv* e, lval* a) {
    /* Check whether the value passed is Q-expression */
    LASSERT(a, a->count == 1,
            "len passed too many arguments.");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "len was not given a Q-expression.");

    lval* x = lval_num(a->cell[0]->count);
    lval_del(a);
    return x;
}

/* Return Q-expression except for the last element */
lval* builtin_init(lenv* e, lval* a) {
    /* Check whether the value passed is Q-expression */
    LASSERT(a, a->count == 1,
            "init passed too many argument!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "init was not give a Q-expression!");

    lval* x = lval_take(a, 0);
    lval_del(lval_pop(x, x->count-1));
    return x;
}

/* Builtins */
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

lval* builtin_mod(lenv* e, lval* a) {
    return builtin_op(e, a, "%");
}

lval* builtin_pow(lenv* e, lval* a) {
    return builtin_op(e, a, "^");
}

lval* builtin_max(lenv* e, lval* a) {
    return builtin_op(e, a, "max");
}

lval* builtin_min(lenv* e, lval* a) {
    return builtin_op(e, a, "min");
}

lval* builtin_def(lenv* e, lval* a) {
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'def' passed incorrect type!");

    /* First argument is symbol list */
    lval* syms = a->cell[0];

    /* Ensure all elements of first list are symbols */
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM,
                "Function 'def' cannot define non-symbol");
    }

    /* Check correct number of symbols and values */
    LASSERT(a, syms->count == a->count-1,
            "Function 'def' cannot define incorrect "
            "number of values to symbols");

    /* Assign copies of values to symbols */
    for (int i = 0; i < syms->count; i++) {
        lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
    
    lval_del(a);
    return lval_sexpr();
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
    lval* k = lval_sym(name);
    lval* v = lval_fun(func);
    lenv_put(e, k, v);
    lval_del(k); lval_del(v);
}

void lenv_add_builtins(lenv* e) {
    /* List Functions */
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "cons", builtin_cons);
    lenv_add_builtin(e, "len", builtin_len);
    lenv_add_builtin(e, "init", builtin_init);

    /* Mathematical Functions */
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
    lenv_add_builtin(e, "%", builtin_mod);
    lenv_add_builtin(e, "^", builtin_pow);
    lenv_add_builtin(e, "max", builtin_max);
    lenv_add_builtin(e, "min", builtin_min);

    /* Variable Functions */
    lenv_add_builtin(e, "def", builtin_def);
}

int main(int argc, char** argv) {
    /* Create Some Parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Yuki = mpc_new("yuki");

    /* Define them with the following Language */
    mpca_lang(MPCA_LANG_DEFAULT,
            "\
            number   : /-?[0-9]+(\\.[0-9]+)?/ ;\
            symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&%]+/ ;\
            sexpr : '(' <expr>* ')' ; \
            qexpr : '{' <expr>* '}' ; \
            expr : <number> | <symbol> | <sexpr> | <qexpr> ; \
            yuki     : /^/ <expr>* /$/ ;\
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Yuki);

    /* Print Version and Exit Information */
    puts("YUKI Version 0.0.0.0.4");
    puts("Press Ctrl+c to Exit\n");

    lenv* e = lenv_new();
    lenv_add_builtins(e);

    /* In a never ending loop */
    while (1) {

        /* Output our prompt and get input */
        char* input = readline("YUKI. N>");

        /* Add input to history */
        add_history(input);

        /* Attempt to Parse the user Input */
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Yuki, &r)) {

            /* Evaluate and print the input */
            lval* x = lval_eval(e, lval_read(r.output));
            lval_println(x);
            lval_del(x);
            mpc_ast_delete(r.output);

        } else {
            /* Otherwise Print the Error */
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
        /* Free retrieved input */
        free(input);
    }

    lenv_del(e);

    /* Undefine and Delete our Parsers */
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Yuki);

    return 0;
}
