#ifndef FEMBOY_LEXER_H
#define FEMBOY_LEXER_H

#include "common.h"

typedef enum {
    T_EOF, T_NUMBER, T_STRING, T_IDENT,
    T_INSERT_INTO,
    T_ROLEPLAY,
    T_HARDER_IF,
    T_OR_IM_GONNA_CUM,
    T_RIDE_UNTIL,
    T_FEMBOY_HOURS,
    T_STOP_RIDING,
    T_KEEP_RIDING,
    T_CUMING,
    T_MOAN,
    T_TRY_ME,
    T_CATCH_ME,
    T_THROW_ME,
    T_OH_YES,
    T_NOT_THERE,
    T_EMPTY_BALLS,
    T_PLUS, 
    T_MINUS, 
    T_STAR, 
    T_SLASH, 
    T_PERCENT,
    T_EQ, T_EQEQ, T_BANG, T_BANGEQ,
    T_LT, T_LE, T_GT, T_GE,
    T_AND, T_OR,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_SEMI, T_COLON,
} FemboyTokType;

typedef struct {
    FemboyTokType type;
    char *text;
    double num;
    int line;
} Token;

typedef struct {
    Token *items;
    int count;
} TokenArray;

FemboyStatus femboy_tokenize(const char *src, TokenArray *out, FemboyError *err);

void token_array_free(TokenArray *arr);

const char *token_type_name(FemboyTokType type);

#endif
