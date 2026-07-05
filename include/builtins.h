#ifndef FEMBOY_BUILTINS_H
#define FEMBOY_BUILTINS_H

#include <stdbool.h>
#include "common.h"
#include "value.h"

struct VM;

typedef enum {
    BUILTIN_ABS,
    BUILTIN_SQRT,
    BUILTIN_POW,
    BUILTIN_FLOOR,
    BUILTIN_CEIL,
    BUILTIN_ROUND,
    BUILTIN_MIN,
    BUILTIN_MAX,
    BUILTIN_RANDOM,
    BUILTIN_SUBSTR,
    BUILTIN_INDEX_OF,
    BUILTIN_TO_UPPER,
    BUILTIN_TO_LOWER,
    BUILTIN_TRIM,
    BUILTIN_SPLIT,
    BUILTIN_CHAR_AT,
    BUILTIN_REPLACE,
    BUILTIN_COUNT,
} BuiltinId;

bool builtin_lookup(const char *name, BuiltinId *id, int *arity);

const char *builtin_name(BuiltinId id);

FemboyStatus builtin_call(struct VM *vm, BuiltinId id, Value *args, int argc, Value *out, FemboyError *err, int line);

bool builtin_needs_stack_protection(BuiltinId id);

#endif
