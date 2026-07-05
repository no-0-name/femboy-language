#include "debug.h"
#include "builtins.h"

#include <stdio.h>

const char *debug_op_name(uint8_t op) {
    switch (op) {
        case OP_PUSH_NUM: return "PUSH_NUM";
        case OP_PUSH_STR: return "PUSH_STR";
        case OP_PUSH_BOOL: return "PUSH_BOOL";
        case OP_PUSH_NIL: return "PUSH_NIL";
        case OP_POP: return "POP";
        case OP_ADD: return "ADD"; case OP_SUB: return "SUB";
        case OP_MUL: return "MUL"; case OP_DIV: return "DIV"; case OP_MOD: return "MOD";
        case OP_NEG: return "NEG";
        case OP_EQ: return "EQ"; case OP_NEQ: return "NEQ";
        case OP_LT: return "LT"; case OP_LE: return "LE"; case OP_GT: return "GT"; case OP_GE: return "GE";
        case OP_AND: return "AND"; case OP_OR: return "OR"; case OP_NOT: return "NOT";
        case OP_GET_LOCAL: return "GET_LOCAL"; case OP_SET_LOCAL: return "SET_LOCAL"; case OP_DEFINE_LOCAL: return "DEFINE_LOCAL";
        case OP_GET_GLOBAL: return "GET_GLOBAL"; case OP_SET_GLOBAL: return "SET_GLOBAL"; case OP_DEFINE_GLOBAL: return "DEFINE_GLOBAL";
        case OP_PRINT: return "PRINT";
        case OP_MAKE_ARRAY: return "MAKE_ARRAY";
        case OP_INDEX_GET: return "INDEX_GET";
        case OP_INDEX_SET_LOCAL: return "INDEX_SET_LOCAL"; case OP_INDEX_SET_GLOBAL: return "INDEX_SET_GLOBAL";
        case OP_ARRAY_LEN: return "ARRAY_LEN"; case OP_ARRAY_PUSH: return "ARRAY_PUSH";
        case OP_MAKE_MAP: return "MAKE_MAP";
        case OP_MAP_HAS: return "MAP_HAS"; case OP_MAP_KEYS: return "MAP_KEYS"; case OP_MAP_DELETE: return "MAP_DELETE";
        case OP_CALL_BUILTIN: return "CALL_BUILTIN";
        case OP_CALL_BUILTIN_MULTI: return "CALL_BUILTIN_MULTI";
        case OP_TRY_BEGIN: return "TRY_BEGIN"; case OP_TRY_END: return "TRY_END"; case OP_THROW: return "THROW";
        case OP_POP_HANDLERS: return "POP_HANDLERS";
        case OP_JUMP: return "JUMP"; case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE"; case OP_LOOP: return "LOOP";
        case OP_CALL: return "CALL"; case OP_RETURN: return "RETURN";
        case OP_HALT: return "HALT";
        default: return "???";
    }
}

void debug_dump_chunk(const Chunk *c) {
    printf("=== Bytecode (%d bytes) ===\n", c->count);
    int i = 0;
    while (i < c->count) {
        uint8_t op = c->code[i];
        printf("%04d  %-16s", i, debug_op_name(op));
        switch (op) {
            case OP_PUSH_NUM: {
                int idx = chunk_read_u16(c->code, i + 1);
                printf(" const#%d (%g)", idx, c->numbers[idx]);
                i += 3; break;
            }
            case OP_PUSH_STR: {
                int idx = chunk_read_u16(c->code, i + 1);
                printf(" const#%d (\"%s\")", idx, c->strings[idx]);
                i += 3; break;
            }
            case OP_PUSH_BOOL:
                printf(" %s", c->code[i + 1] ? "true" : "false");
                i += 2; break;
            case OP_POP_HANDLERS:
                printf(" n=%d", c->code[i + 1]);
                i += 2; break;
            case OP_CALL_BUILTIN: case OP_CALL_BUILTIN_MULTI:
                printf(" %s argc=%d", builtin_name((BuiltinId)c->code[i + 1]), c->code[i + 2]);
                i += 3; break;
            case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_DEFINE_LOCAL: case OP_INDEX_SET_LOCAL: {
                printf(" slot#%d", chunk_read_u16(c->code, i + 1));
                i += 3; break;
            }
            case OP_MAKE_ARRAY: {
                printf(" n=%d", chunk_read_u16(c->code, i + 1));
                i += 3; break;
            }
            case OP_MAKE_MAP: {
                int n = chunk_read_u16(c->code, i + 1);
                printf(" n=%d keys=[", n);
                for (int k = 0; k < n; k++) {
                    int kidx = chunk_read_u16(c->code, i + 3 + k * 2);
                    if (k > 0) printf(", ");
                    printf("\"%s\"", c->strings[kidx]);
                }
                printf("]");
                i += 3 + n * 2; break;
            }
            case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_DEFINE_GLOBAL: case OP_INDEX_SET_GLOBAL: {
                int idx = chunk_read_u16(c->code, i + 1);
                printf(" \"%s\"", c->strings[idx]);
                i += 3; break;
            }
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_LOOP: case OP_TRY_BEGIN: {
                int16_t off = (int16_t)chunk_read_u16(c->code, i + 1);
                printf(" -> %04d", i + 3 + off);
                i += 3; break;
            }
            case OP_CALL: {
                int target = chunk_read_u16(c->code, i + 1);
                int argc = c->code[i + 3];
                printf(" addr=%04d argc=%d", target, argc);
                i += 4; break;
            }
            default:
                i += 1; break;
        }
        printf("\n");
    }
}
