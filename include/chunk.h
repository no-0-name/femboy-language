#ifndef FEMBOY_CHUNK_H
#define FEMBOY_CHUNK_H

#include <stdint.h>
#include "common.h"

typedef enum {
    OP_PUSH_NUM, OP_PUSH_STR, OP_PUSH_BOOL, OP_PUSH_NIL,
    OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_EQ, OP_NEQ, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR, OP_NOT,
    OP_GET_LOCAL, OP_SET_LOCAL, OP_DEFINE_LOCAL,
    OP_GET_GLOBAL, OP_SET_GLOBAL, OP_DEFINE_GLOBAL,
    OP_PRINT,
    OP_MAKE_ARRAY, OP_INDEX_GET, OP_INDEX_SET_LOCAL, OP_INDEX_SET_GLOBAL,
    OP_ARRAY_LEN, OP_ARRAY_PUSH,
    OP_MAP_HAS, OP_MAP_KEYS, OP_MAP_DELETE,

    OP_MAKE_MAP,
    OP_CALL_BUILTIN,

    OP_CALL_BUILTIN_MULTI,
    OP_TRY_BEGIN, OP_TRY_END, OP_THROW, OP_POP_HANDLERS,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_LOOP,
    OP_CALL, OP_RETURN,
    OP_HALT,
} OpCode;

typedef struct {
    uint8_t *code;
    int count, cap;
    double *numbers; int nnum, capnum;
    char **strings; int nstr, capstr;
} Chunk;

void chunk_init(Chunk *c);
void chunk_free(Chunk *c);

void chunk_emit_byte(Chunk *c, uint8_t b);
void chunk_emit_u16(Chunk *c, uint16_t v);

int chunk_add_const_num(Chunk *c, double v);
int chunk_add_const_str(Chunk *c, const char *s);

uint16_t chunk_read_u16(const uint8_t *code, int pos);

#endif
