#ifndef FEMBOY_COMPILER_H
#define FEMBOY_COMPILER_H

#include "common.h"
#include "ast.h"
#include "chunk.h"

FemboyStatus femboy_compile(Node *prog, Chunk *chunk, FemboyError *err);

#endif
