#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    Token *tokens;
    int ntokens;
    int pos;
    bool failed;
    FemboyError *err;
} ParserState;

static Token *cur(ParserState *ps) { return &ps->tokens[ps->pos]; }

static Token *advance(ParserState *ps) {
    Token *t = &ps->tokens[ps->pos];
    if (ps->pos < ps->ntokens - 1) ps->pos++;
    return t;
}

static bool check(ParserState *ps, FemboyTokType t) { return cur(ps)->type == t; }

static bool match(ParserState *ps, FemboyTokType t) {
    if (check(ps, t)) { advance(ps); return true; }
    return false;
}

static void *parse_fail(ParserState *ps, int line, const char *fmt, ...) {
    if (!ps->failed) {
        ps->failed = true;
        va_list args;
        va_start(args, fmt);
        ps->err->status = FEMBOY_ERR_PARSE;
        ps->err->line = line;
        ps->err->column = -1;
        vsnprintf(ps->err->message, FEMBOY_ERR_MSG_MAX, fmt, args);
        va_end(args);
    }
    return NULL;
}

static Token *expect(ParserState *ps, FemboyTokType t, const char *what) {
    if (ps->failed) return NULL;
    if (!check(ps, t)) {
        parse_fail(ps, cur(ps)->line, "expected %s, but found %s",
                    what, token_type_name(cur(ps)->type));
        return NULL;
    }
    return advance(ps);
}

static Node *parse_expr(ParserState *ps);
static Node *parse_block(ParserState *ps);
static Node *parse_stmt(ParserState *ps);

static Node *parse_call_args(ParserState *ps, Node *call_node) {
    int cap = 4;
    call_node->args = femboy_malloc(sizeof(Node *) * cap);
    call_node->nargs = 0;

    if (!check(ps, T_RPAREN)) {
        do {
            if (ps->failed) break;
            if (call_node->nargs >= cap) {
                cap *= 2;
                call_node->args = femboy_realloc(call_node->args, sizeof(Node *) * cap);
            }
            Node *arg = parse_expr(ps);
            if (ps->failed) break;
            call_node->args[call_node->nargs++] = arg;
        } while (match(ps, T_COMMA));
    }

    if (ps->failed) return call_node;
    if (!expect(ps, T_RPAREN, "')'")) return call_node;
    return call_node;
}

static Node *parse_primary(ParserState *ps) {
    if (ps->failed) return NULL;
    Token *t = cur(ps);

    if (match(ps, T_NUMBER)) {
        Node *n = ast_new_node(N_NUMBER);
        n->num = t->num; n->line = t->line;
        return n;
    }
    if (match(ps, T_STRING)) {
        Node *n = ast_new_node(N_STRING);
        n->str = femboy_strdup(t->text);
        n->line = t->line;
        return n;
    }
    if (match(ps, T_OH_YES)) { Node *n = ast_new_node(N_BOOL); n->boolean = true; n->line = t->line; return n; }
    if (match(ps, T_NOT_THERE)) { Node *n = ast_new_node(N_BOOL); n->boolean = false; n->line = t->line; return n; }
    if (match(ps, T_EMPTY_BALLS)) { Node *n = ast_new_node(N_NIL); n->line = t->line; return n; }

    if (match(ps, T_LBRACKET)) {
        Node *n = ast_new_node(N_ARRAY_LIT);
        n->line = t->line;
        int cap = 4;
        n->args = femboy_malloc(sizeof(Node *) * cap);
        n->nargs = 0;
        if (!check(ps, T_RBRACKET)) {
            do {
                if (ps->failed) break;
                if (n->nargs >= cap) { cap *= 2; n->args = femboy_realloc(n->args, sizeof(Node *) * cap); }
                Node *el = parse_expr(ps);
                if (ps->failed) { ast_free(el); break; }
                n->args[n->nargs++] = el;
            } while (match(ps, T_COMMA));
        }
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_RBRACKET, "']'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_LBRACE)) {

        Node *n = ast_new_node(N_MAP_LIT);
        n->line = t->line;
        int cap = 4;
        n->args = femboy_malloc(sizeof(Node *) * cap);
        n->keys = femboy_malloc(sizeof(char *) * cap);
        n->nargs = 0;
        if (!check(ps, T_RBRACE)) {
            do {
                if (ps->failed) break;
                if (n->nargs >= cap) {
                    cap *= 2;
                    n->args = femboy_realloc(n->args, sizeof(Node *) * cap);
                    n->keys = femboy_realloc(n->keys, sizeof(char *) * cap);
                }
                if (!check(ps, T_STRING)) {
                    parse_fail(ps, cur(ps)->line, "a map literal key must be a string, found %s",
                                token_type_name(cur(ps)->type));
                    break;
                }
                Token *keyTok = advance(ps);
                if (!expect(ps, T_COLON, "':'")) break;
                Node *val = parse_expr(ps);
                if (ps->failed) { ast_free(val); break; }
                n->keys[n->nargs] = femboy_strdup(keyTok->text);
                n->args[n->nargs] = val;
                n->nargs++;
            } while (match(ps, T_COMMA));
        }
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_RBRACE, "'}'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_LPAREN)) {
        Node *e = parse_expr(ps);
        if (ps->failed) { ast_free(e); return NULL; }
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(e); return NULL; }
        return e;
    }

    if (check(ps, T_IDENT)) {
        Token *id = advance(ps);
        if (match(ps, T_LPAREN)) {
            Node *n = ast_new_node(N_CALL);
            n->name = femboy_strdup(id->text);
            n->line = id->line;
            n = parse_call_args(ps, n);
            if (ps->failed) { ast_free(n); return NULL; }
            return n;
        }
        Node *n = ast_new_node(N_VAR);
        n->name = femboy_strdup(id->text);
        n->line = id->line;
        return n;
    }

    return parse_fail(ps, t->line, "expected an expression, but found %s", token_type_name(t->type));
}

static Node *parse_postfix(ParserState *ps) {
    Node *expr = parse_primary(ps);
    if (ps->failed) { ast_free(expr); return NULL; }

    while (true) {
        if (check(ps, T_LBRACKET)) {
            Token *br = advance(ps);
            Node *idx_node = parse_expr(ps);
            if (ps->failed) { ast_free(expr); ast_free(idx_node); return NULL; }
            if (!expect(ps, T_RBRACKET, "']'")) { ast_free(expr); ast_free(idx_node); return NULL; }

            Node *n = ast_new_node(N_INDEX_GET);
            n->line = br->line;
            n->left = expr;
            n->right = idx_node;
            expr = n;
        } else if (check(ps, T_INCREMENT) || check(ps, T_DECREMENT)) {
            Token *op = advance(ps);
            Node *n = ast_new_node(op->type == T_INCREMENT ? N_INCREMENT : N_DECREMENT);  // постфиксные
            n->line = op->line;
            n->left = expr;
            expr = n;
        } else {
            break;
        }
    }
    return expr;
}

static Node *parse_unary(ParserState *ps) {
    if (ps->failed) return NULL;
    
    if (check(ps, T_MINUS) || check(ps, T_BANG) || 
        check(ps, T_INCREMENT) || check(ps, T_DECREMENT)) {
        Token *op = advance(ps);
        Node *n;
        
        if (op->type == T_INCREMENT) {
            n = ast_new_node(N_PRE_INCREMENT);
        } else if (op->type == T_DECREMENT) {
            n = ast_new_node(N_PRE_DECREMENT);
        } else {
            n = ast_new_node(N_UNOP);
            n->op = op->type;
        }
        n->line = op->line;
        n->left = parse_unary(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }
    return parse_postfix(ps);
}

#define LEVEL_OR    0
#define LEVEL_AND   1
#define LEVEL_EQ    2
#define LEVEL_CMP   3
#define LEVEL_ADD   4
#define LEVEL_MUL   5
#define LEVEL_MAX   5

static int prec_of(FemboyTokType t) {
    switch (t) {
        case T_STAR: case T_SLASH: case T_PERCENT: return LEVEL_MUL;
        case T_PLUS: case T_MINUS: return LEVEL_ADD;
        case T_LT: case T_LE: case T_GT: case T_GE: return LEVEL_CMP;
        case T_EQEQ: case T_BANGEQ: return LEVEL_EQ;
        case T_AND: return LEVEL_AND;
        case T_OR: return LEVEL_OR;
        default: return -1;
    }
}

static Node *parse_at_level(ParserState *ps, int level) {
    if (ps->failed) return NULL;
    Node *left = (level == LEVEL_MAX) ? parse_unary(ps) : parse_at_level(ps, level + 1);
    if (ps->failed) { ast_free(left); return NULL; }

    while (prec_of(cur(ps)->type) == level) {
        Token *op = advance(ps);
        Node *right = (level == LEVEL_MAX) ? parse_unary(ps) : parse_at_level(ps, level + 1);
        if (ps->failed) { ast_free(left); ast_free(right); return NULL; }

        Node *n = ast_new_node(N_BINOP);
        n->op = op->type;
        n->left = left; n->right = right;
        n->line = op->line;
        left = n;
    }
    return left;
}

static Node *parse_assign(ParserState *ps) {
    if (ps->failed) return NULL;

    if (check(ps, T_IDENT) && ps->tokens[ps->pos + 1].type == T_EQ) {
        Token *id = advance(ps);
        advance(ps);
        Node *n = ast_new_node(N_ASSIGN);
        n->name = femboy_strdup(id->text);
        n->line = id->line;
        n->init = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    Node *expr = parse_at_level(ps, LEVEL_OR);
    if (ps->failed) { ast_free(expr); return NULL; }

    if (expr && expr->type == N_INDEX_GET && check(ps, T_EQ)) {
        Token *eq = advance(ps);
        Node *n = ast_new_node(N_INDEX_SET);
        n->line = eq->line;
        n->left = expr->left;
        n->right = expr->right;
        expr->left = NULL; expr->right = NULL;
        ast_free(expr);
        n->value = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    return expr;
}

static Node *parse_expr(ParserState *ps) {
    return parse_assign(ps);
}

static Node *parse_block(ParserState *ps) {
    if (ps->failed) return NULL;
    if (!expect(ps, T_LBRACE, "'{'")) return NULL;

    Node *blk = ast_new_node(N_BLOCK);
    int cap = 8;
    blk->stmts = femboy_malloc(sizeof(Node *) * cap);
    blk->nstmts = 0;

    while (!check(ps, T_RBRACE) && !check(ps, T_EOF) && !ps->failed) {
        if (blk->nstmts >= cap) { cap *= 2; blk->stmts = femboy_realloc(blk->stmts, sizeof(Node *) * cap); }
        blk->stmts[blk->nstmts++] = parse_stmt(ps);
    }

    if (ps->failed) { ast_free(blk); return NULL; }

    if (!expect(ps, T_RBRACE, "'}'")) { ast_free(blk); return NULL; }
    return blk;
}

static Node *parse_stmt(ParserState *ps) {
    if (ps->failed) return NULL;
    Token *t = cur(ps);

    if (match(ps, T_INSERT_INTO)) {
        Token *id = expect(ps, T_IDENT, "variable name");
        if (ps->failed) return NULL;
        Node *n = ast_new_node(N_LET);
        n->name = femboy_strdup(id->text);
        n->line = t->line;
        if (match(ps, T_EQ)) n->init = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_MOAN)) {
        if (!expect(ps, T_LPAREN, "'('")) return NULL;
        Node *n = ast_new_node(N_PRINT);
        n->line = t->line;
        n->init = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_HARDER_IF)) {
        if (!expect(ps, T_LPAREN, "'('")) return NULL;
        Node *n = ast_new_node(N_IF);
        n->line = t->line;
        n->cond = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }
        n->thenb = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (match(ps, T_OR_IM_GONNA_CUM)) {
            if (check(ps, T_HARDER_IF)) n->elseb = parse_stmt(ps);
            else n->elseb = parse_block(ps);
            if (ps->failed) { ast_free(n); return NULL; }
        }
        return n;
    }

    if (match(ps, T_RIDE_UNTIL)) {
        if (!expect(ps, T_LPAREN, "'('")) return NULL;
        Node *n = ast_new_node(N_WHILE);
        n->line = t->line;
        n->cond = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }
        n->body = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_FEMBOY_HOURS)) {

        if (!expect(ps, T_LPAREN, "'('")) return NULL;
        Node *n = ast_new_node(N_FOR);
        n->line = t->line;

        if (!check(ps, T_SEMI)) {
            n->init = parse_stmt(ps);
            if (ps->failed) { ast_free(n); return NULL; }
        } else {
            if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        }

        if (!check(ps, T_SEMI)) {
            n->cond = parse_expr(ps);
            if (ps->failed) { ast_free(n); return NULL; }
        }
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }

        if (!check(ps, T_RPAREN)) {
            n->step = parse_expr(ps);
            if (ps->failed) { ast_free(n); return NULL; }
        }
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }

        n->body = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_STOP_RIDING)) {
        Node *n = ast_new_node(N_BREAK);
        n->line = t->line;
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_KEEP_RIDING)) {
        Node *n = ast_new_node(N_CONTINUE);
        n->line = t->line;
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_TRY_ME)) {
        Node *n = ast_new_node(N_TRY);
        n->line = t->line;
        n->thenb = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }

        if (!expect(ps, T_CATCH_ME, "'catch'")) { ast_free(n); return NULL; }
        if (!expect(ps, T_LPAREN, "'('")) { ast_free(n); return NULL; }
        Token *id = expect(ps, T_IDENT, "exception variable name");
        if (ps->failed) { ast_free(n); return NULL; }
        n->name = femboy_strdup(id->text);
        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }

        n->elseb = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_THROW_ME)) {
        Node *n = ast_new_node(N_THROW);
        n->line = t->line;
        n->init = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_CUMING)) {
        Node *n = ast_new_node(N_RETURN);
        n->line = t->line;
        if (!check(ps, T_SEMI)) n->init = parse_expr(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
        return n;
    }

    if (match(ps, T_ROLEPLAY)) {
        Token *id = expect(ps, T_IDENT, "function name");
        if (ps->failed) return NULL;
        Node *n = ast_new_node(N_FUNCDEF);
        n->name = femboy_strdup(id->text);
        n->line = t->line;

        if (!expect(ps, T_LPAREN, "'('")) { ast_free(n); return NULL; }

        int cap = 4;
        n->params = femboy_malloc(sizeof(char *) * cap);
        n->nparams = 0;

        if (!check(ps, T_RPAREN)) {
            do {
                if (ps->failed) break;
                Token *p = expect(ps, T_IDENT, "parameter name");
                if (ps->failed) break;
                if (n->nparams >= cap) { cap *= 2; n->params = femboy_realloc(n->params, sizeof(char *) * cap); }
                n->params[n->nparams++] = femboy_strdup(p->text);
            } while (match(ps, T_COMMA));
        }
        if (ps->failed) { ast_free(n); return NULL; }

        if (!expect(ps, T_RPAREN, "')'")) { ast_free(n); return NULL; }

        n->fbody = parse_block(ps);
        if (ps->failed) { ast_free(n); return NULL; }
        return n;
    }

    if (check(ps, T_LBRACE)) return parse_block(ps);

    Node *n = ast_new_node(N_EXPRSTMT);
    n->line = t->line;
    n->init = parse_expr(ps);
    if (ps->failed) { ast_free(n); return NULL; }
    if (!expect(ps, T_SEMI, "';'")) { ast_free(n); return NULL; }
    return n;
}

static Node *parse_program(ParserState *ps) {
    Node *prog = ast_new_node(N_PROGRAM);
    int cap = 16;
    prog->stmts = femboy_malloc(sizeof(Node *) * cap);
    prog->nstmts = 0;

    while (!check(ps, T_EOF) && !ps->failed) {
        if (prog->nstmts >= cap) { cap *= 2; prog->stmts = femboy_realloc(prog->stmts, sizeof(Node *) * cap); }
        prog->stmts[prog->nstmts++] = parse_stmt(ps);
    }

    if (ps->failed) { ast_free(prog); return NULL; }
    return prog;
}

FemboyStatus femboy_parse(Token *tokens, int ntokens, Node **out_program, FemboyError *err) {
    ParserState ps = { .tokens = tokens, .ntokens = ntokens, .pos = 0, .failed = false, .err = err };

    Node *prog = parse_program(&ps);

    if (ps.failed) {
        *out_program = NULL;
        return FEMBOY_ERR_PARSE;
    }

    *out_program = prog;
    return FEMBOY_OK;
}