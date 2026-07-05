#include "lexer.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *src;
    int pos;
    int line;
    int col;
    int line_start;
} LexState;

static char peekc(LexState *lx) { return lx->src[lx->pos]; }
static char peekc2(LexState *lx) { return lx->src[lx->pos] ? lx->src[lx->pos + 1] : '\0'; }

static char advc(LexState *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') { lx->line++; lx->col = 1; }
    else lx->col++;
    return c;
}

static FemboyStatus skip_ws_comments(LexState *lx, FemboyError *err) {
    for (;;) {
        char c = peekc(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advc(lx); continue; }

        if (c == '/' && peekc2(lx) == '/') {
            while (peekc(lx) && peekc(lx) != '\n') advc(lx);
            continue;
        }

        if (c == '/' && peekc2(lx) == '*') {
            int start_line = lx->line;
            int start_col = lx->col;
            advc(lx); advc(lx);
            bool closed = false;
            while (peekc(lx)) {
                if (peekc(lx) == '*' && peekc2(lx) == '/') {
                    advc(lx); advc(lx);
                    closed = true;
                    break;
                }
                advc(lx);
            }
            if (!closed) {
                return femboy_error_set(err, FEMBOY_ERR_LEX, start_line, start_col,
                                        "unterminated block comment '/*'");
            }
            continue;
        }

        break;
    }
    return FEMBOY_OK;
}

static char *dupstr_n(const char *s, int len) {
    char *r = femboy_malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

static FemboyTokType keyword_type(const char *s) {
    if (!strcmp(s, "insert_into")) return T_INSERT_INTO;        
    if (!strcmp(s, "roleplay")) return T_ROLEPLAY;            
    if (!strcmp(s, "harder_if")) return T_HARDER_IF;           
    if (!strcmp(s, "or_im_gonna_cum")) return T_OR_IM_GONNA_CUM;   
    if (!strcmp(s, "ride_until")) return T_RIDE_UNTIL;       
    if (!strcmp(s, "femboy_hours")) return T_FEMBOY_HOURS;       
    if (!strcmp(s, "stop_riding")) return T_STOP_RIDING;      
    if (!strcmp(s, "keep_riding")) return T_KEEP_RIDING;   
    if (!strcmp(s, "cuming")) return T_CUMING;          
    if (!strcmp(s, "moan")) return T_MOAN;             
    if (!strcmp(s, "try_me")) return T_TRY_ME;             
    if (!strcmp(s, "catch_me")) return T_CATCH_ME;         
    if (!strcmp(s, "throw_me")) return T_THROW_ME;         
    if (!strcmp(s, "oh_yes")) return T_OH_YES;            
    if (!strcmp(s, "not_there")) return T_NOT_THERE;        
    if (!strcmp(s, "empty_balls")) return T_EMPTY_BALLS;         
    return T_IDENT;
}

const char *token_type_name(FemboyTokType type) {
    switch (type) {
        case T_EOF: return "end of file";
        case T_NUMBER: return "number";
        case T_STRING: return "string";
        case T_IDENT: return "identifier";
        case T_INSERT_INTO: return "'insert_into'";
        case T_ROLEPLAY: return "'roleplay'";
        case T_HARDER_IF: return "'harder_if'";
        case T_OR_IM_GONNA_CUM: return "'or_im_gonna_cum'";
        case T_RIDE_UNTIL: return "'ride_until'";
        case T_FEMBOY_HOURS: return "'femboy_hours'";
        case T_STOP_RIDING: return "'stop_riding'";
        case T_KEEP_RIDING: return "'keep_riding'";
        case T_CUMING: return "'cuming'";
        case T_MOAN: return "'moan'";
        case T_TRY_ME: return "'try_me'";
        case T_CATCH_ME: return "'catch_me'";
        case T_THROW_ME: return "'throw_me'";
        case T_OH_YES: return "'oh_yes'";
        case T_NOT_THERE: return "'not_there'";
        case T_EMPTY_BALLS: return "'empty_balls'";
        case T_PLUS: return "'+'";
        case T_MINUS: return "'-'";
        case T_STAR: return "'*'";
        case T_SLASH: return "'/'";
        case T_PERCENT: return "'%'";
        case T_EQ: return "'='";
        case T_EQEQ: return "'=='";
        case T_BANG: return "'!'";
        case T_BANGEQ: return "'!='";
        case T_LT: return "'<'";
        case T_LE: return "'<='";
        case T_GT: return "'>'";
        case T_GE: return "'>='";
        case T_AND: return "'&&'";
        case T_OR: return "'||'";
        case T_LPAREN: return "'('";
        case T_RPAREN: return "')'";
        case T_LBRACE: return "'{'";
        case T_RBRACE: return "'}'";
        case T_LBRACKET: return "'['";
        case T_RBRACKET: return "']'";
        case T_COMMA: return "','";
        case T_SEMI: return "';'";
        case T_COLON: return "':'";
        default: return "?";
    }
}

static FemboyStatus next_token(LexState *lx, Token *out, FemboyError *err) {
    FemboyStatus ws_status = skip_ws_comments(lx, err);
    if (ws_status != FEMBOY_OK) return ws_status;
    out->text = NULL;
    out->num = 0;
    out->line = lx->line;
    int start_col = lx->col;
    char c = peekc(lx);

    if (c == '\0') { out->type = T_EOF; return FEMBOY_OK; }

    if (isdigit((unsigned char)c)) {
        int start = lx->pos;
        while (isdigit((unsigned char)peekc(lx))) advc(lx);
        if (peekc(lx) == '.' && isdigit((unsigned char)peekc2(lx))) {
            advc(lx);
            while (isdigit((unsigned char)peekc(lx))) advc(lx);
        }
        char *numstr = dupstr_n(lx->src + start, lx->pos - start);
        out->type = T_NUMBER;
        out->num = atof(numstr);
        free(numstr);
        return FEMBOY_OK;
    }

    if (c == '"') {
        advc(lx);
        int start_line = lx->line;
        int start = lx->pos;
        while (peekc(lx) && peekc(lx) != '"') {
            if (peekc(lx) == '\\' && peekc2(lx) != '\0') advc(lx);
            advc(lx);
        }
        if (peekc(lx) != '"') {
            return femboy_error_set(err, FEMBOY_ERR_LEX, out->line, start_col,
                                    "unterminated string literal");
        }
        char *raw = dupstr_n(lx->src + start, lx->pos - start);
        advc(lx);

        char *unescaped = femboy_malloc(strlen(raw) + 1);
        int j = 0;
        for (int i = 0; raw[i]; i++) {
            if (raw[i] == '\\') {
                if (!raw[i + 1]) break;
                i++;
                switch (raw[i]) {
                    case 'n': unescaped[j++] = '\n'; break;
                    case 't': unescaped[j++] = '\t'; break;
                    case 'r': unescaped[j++] = '\r'; break;
                    case '"': unescaped[j++] = '"'; break;
                    case '\\': unescaped[j++] = '\\'; break;
                    default: {
                        char bad = raw[i];
                        free(raw);
                        free(unescaped);
                        return femboy_error_set(err, FEMBOY_ERR_LEX, start_line, start_col,
                                                "unknown escape sequence '\\%c' in string literal",
                                                bad);
                    }
                }
            } else {
                unescaped[j++] = raw[i];
            }
        }
        unescaped[j] = '\0';
        free(raw);

        out->type = T_STRING;
        out->text = unescaped;
        return FEMBOY_OK;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        int start = lx->pos;
        while (isalnum((unsigned char)peekc(lx)) || peekc(lx) == '_') advc(lx);
        char *ident = dupstr_n(lx->src + start, lx->pos - start);
        FemboyTokType kw = keyword_type(ident);
        if (kw != T_IDENT) { out->type = kw; free(ident); }
        else { out->type = T_IDENT; out->text = ident; }
        return FEMBOY_OK;
    }

    advc(lx);
    switch (c) {
        case '+': out->type = T_PLUS; return FEMBOY_OK;
        case '-': out->type = T_MINUS; return FEMBOY_OK;
        case '*': out->type = T_STAR; return FEMBOY_OK;
        case '/': out->type = T_SLASH; return FEMBOY_OK;
        case '%': out->type = T_PERCENT; return FEMBOY_OK;
        case '(': out->type = T_LPAREN; return FEMBOY_OK;
        case ')': out->type = T_RPAREN; return FEMBOY_OK;
        case '{': out->type = T_LBRACE; return FEMBOY_OK;
        case '}': out->type = T_RBRACE; return FEMBOY_OK;
        case '[': out->type = T_LBRACKET; return FEMBOY_OK;
        case ']': out->type = T_RBRACKET; return FEMBOY_OK;
        case ',': out->type = T_COMMA; return FEMBOY_OK;
        case ';': out->type = T_SEMI; return FEMBOY_OK;
        case ':': out->type = T_COLON; return FEMBOY_OK;
        case '=':
            if (peekc(lx) == '=') { advc(lx); out->type = T_EQEQ; } else out->type = T_EQ;
            return FEMBOY_OK;
        case '!':
            if (peekc(lx) == '=') { advc(lx); out->type = T_BANGEQ; } else out->type = T_BANG;
            return FEMBOY_OK;
        case '<':
            if (peekc(lx) == '=') { advc(lx); out->type = T_LE; } else out->type = T_LT;
            return FEMBOY_OK;
        case '>':
            if (peekc(lx) == '=') { advc(lx); out->type = T_GE; } else out->type = T_GT;
            return FEMBOY_OK;
        case '&':
            if (peekc(lx) == '&') { advc(lx); out->type = T_AND; return FEMBOY_OK; }
            return femboy_error_set(err, FEMBOY_ERR_LEX, out->line, start_col,
                                    "unexpected character '&' (did you mean '&&'?)");
        case '|':
            if (peekc(lx) == '|') { advc(lx); out->type = T_OR; return FEMBOY_OK; }
            return femboy_error_set(err, FEMBOY_ERR_LEX, out->line, start_col,
                                    "unexpected character '|' (did you mean '||'?)");
        default:
            return femboy_error_set(err, FEMBOY_ERR_LEX, out->line, start_col,
                                    "unexpected character '%c'", c);
    }
}

FemboyStatus femboy_tokenize(const char *src, TokenArray *out, FemboyError *err) {
    LexState lx = { .src = src, .pos = 0, .line = 1, .col = 1 };

    int cap = 256, n = 0;
    Token *arr = femboy_malloc(sizeof(Token) * cap);

    for (;;) {
        Token t;
        FemboyStatus st = next_token(&lx, &t, err);
        if (st != FEMBOY_OK) {

            for (int i = 0; i < n; i++) free(arr[i].text);
            free(arr);
            out->items = NULL;
            out->count = 0;
            return st;
        }
        if (n >= cap) { cap *= 2; arr = femboy_realloc(arr, sizeof(Token) * cap); }
        arr[n++] = t;
        if (t.type == T_EOF) break;
    }

    out->items = arr;
    out->count = n;
    return FEMBOY_OK;
}

void token_array_free(TokenArray *arr) {
    if (!arr || !arr->items) return;
    for (int i = 0; i < arr->count; i++) free(arr->items[i].text);
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
}
