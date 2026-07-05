#ifndef FEMBOY_PARSER_H
#define FEMBOY_PARSER_H

#include "common.h"
#include "lexer.h"
#include "ast.h"

FemboyStatus femboy_parse(Token *tokens, int ntokens, Node **out_program, FemboyError *err);

#endif
