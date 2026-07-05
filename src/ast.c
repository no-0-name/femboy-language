#include "ast.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

Node *ast_new_node(NodeType type) {
    Node *n = femboy_malloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->type = type;
    return n;
}

void ast_free(Node *node) {
    if (!node) return;

    free(node->str);
    free(node->name);

    ast_free(node->left);
    ast_free(node->right);

    for (int i = 0; i < node->nargs; i++) ast_free(node->args[i]);
    free(node->args);

    if (node->keys) {
        for (int i = 0; i < node->nargs; i++) free(node->keys[i]);
        free(node->keys);
    }

    ast_free(node->init);

    ast_free(node->value);

    ast_free(node->cond);
    ast_free(node->thenb);
    ast_free(node->elseb);

    ast_free(node->body);
    ast_free(node->step);

    for (int i = 0; i < node->nstmts; i++) ast_free(node->stmts[i]);
    free(node->stmts);

    for (int i = 0; i < node->nparams; i++) free(node->params[i]);
    free(node->params);

    ast_free(node->fbody);

    free(node);
}
