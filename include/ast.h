#ifndef FEMBOY_AST_H
#define FEMBOY_AST_H

#include <stdbool.h>
#include "lexer.h"

typedef enum {
    N_NUMBER, N_STRING, N_BOOL, N_NIL, N_VAR, N_ASSIGN,
    N_BINOP, N_UNOP, N_CALL,
    N_INCREMENT, N_DECREMENT, N_PRE_INCREMENT, N_PRE_DECREMENT,
    N_ARRAY_LIT, N_INDEX_GET, N_INDEX_SET, N_MAP_LIT,
    N_LET, N_PRINT, N_IF, N_WHILE, N_FOR, N_BREAK, N_CONTINUE,
    N_RETURN, N_BLOCK, N_EXPRSTMT,
    N_TRY, N_THROW,
    N_FUNCDEF, N_PROGRAM,
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType type;
    int line;

    double num;
    char *str;
    bool boolean;

    char *name;

    FemboyTokType op;
    Node *left, *right;

    Node *value;

    Node **args; int nargs;
    char **keys;

    Node *init;

    Node *cond, *thenb, *elseb;

    Node *body;
    Node *step;

    Node **stmts; int nstmts;

    char **params; int nparams;
    Node *fbody;
};

Node *ast_new_node(NodeType type);

void ast_free(Node *node);

#endif
