#include "builtins.h"
#include "vm.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

typedef struct {
    const char *name;
    int arity;
} BuiltinEntry;

static const BuiltinEntry BUILTIN_TABLE[BUILTIN_COUNT] = {
    [BUILTIN_ABS]      = { "abs", 1 },
    [BUILTIN_SQRT]     = { "sqrt", 1 },
    [BUILTIN_POW]      = { "pow", 2 },
    [BUILTIN_FLOOR]    = { "floor", 1 },
    [BUILTIN_CEIL]     = { "ceil", 1 },
    [BUILTIN_ROUND]    = { "round", 1 },
    [BUILTIN_MIN]      = { "min", 2 },
    [BUILTIN_MAX]      = { "max", 2 },
    [BUILTIN_RANDOM]   = { "random", 0 },
    [BUILTIN_SUBSTR]     = { "substr", 3 },
    [BUILTIN_INDEX_OF]   = { "indexOf", 2 },
    [BUILTIN_TO_UPPER]   = { "toUpperCase", 1 },
    [BUILTIN_TO_LOWER]   = { "toLowerCase", 1 },
    [BUILTIN_TRIM]       = { "trim", 1 },
    [BUILTIN_SPLIT]      = { "split", 2 },
    [BUILTIN_CHAR_AT]    = { "charAt", 2 },
    [BUILTIN_REPLACE]    = { "replace", 3 },
};

bool builtin_lookup(const char *name, BuiltinId *id, int *arity) {
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (!strcmp(BUILTIN_TABLE[i].name, name)) {
            *id = (BuiltinId)i;
            *arity = BUILTIN_TABLE[i].arity;
            return true;
        }
    }
    return false;
}

const char *builtin_name(BuiltinId id) {
    if (id < 0 || id >= BUILTIN_COUNT) return "<unknown built-in function>";
    return BUILTIN_TABLE[id].name;
}

bool builtin_needs_stack_protection(BuiltinId id) {

    return id == BUILTIN_SPLIT;
}

static bool require_all_numbers(BuiltinId id, Value *args, int argc, FemboyError *err, int line) {
    for (int i = 0; i < argc; i++) {
        if (args[i].type != V_NUM) {
            femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                             "%s() expects a numeric argument #%d, got type %s",
                             builtin_name(id), i + 1, value_type_name(args[i].type));
            return false;
        }
    }
    return true;
}

static bool require_string_arg(BuiltinId id, Value *args, int idx, int argpos, FemboyError *err, int line) {
    if (args[idx].type != V_STR) {
        femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                         "%s() expects a string argument #%d, got type %s",
                         builtin_name(id), argpos, value_type_name(args[idx].type));
        return false;
    }
    return true;
}

FemboyStatus builtin_call(struct VM *vm, BuiltinId id, Value *args, int argc, Value *out, FemboyError *err, int line) {
    if (id < 0 || id >= BUILTIN_COUNT) {
        return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                "unknown built-in function id %d (corrupted bytecode?)", (int)id);
    }

    switch (id) {
        case BUILTIN_ABS:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(fabs(args[0].as.num));
            return FEMBOY_OK;

        case BUILTIN_SQRT:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            if (args[0].as.num < 0) {
                return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                        "sqrt() is undefined for negative number %g", args[0].as.num);
            }
            *out = value_num(sqrt(args[0].as.num));
            return FEMBOY_OK;

        case BUILTIN_POW:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(pow(args[0].as.num, args[1].as.num));
            return FEMBOY_OK;

        case BUILTIN_FLOOR:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(floor(args[0].as.num));
            return FEMBOY_OK;

        case BUILTIN_CEIL:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(ceil(args[0].as.num));
            return FEMBOY_OK;

        case BUILTIN_ROUND:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;

            *out = value_num(round(args[0].as.num));
            return FEMBOY_OK;

        case BUILTIN_MIN:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(args[0].as.num < args[1].as.num ? args[0].as.num : args[1].as.num);
            return FEMBOY_OK;

        case BUILTIN_MAX:
            if (!require_all_numbers(id, args, argc, err, line)) return FEMBOY_ERR_RUNTIME;
            *out = value_num(args[0].as.num > args[1].as.num ? args[0].as.num : args[1].as.num);
            return FEMBOY_OK;

        case BUILTIN_RANDOM: {

            static bool seeded = false;
            if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
            *out = value_num((double)rand() / ((double)RAND_MAX + 1.0));
            return FEMBOY_OK;
        }

        case BUILTIN_SUBSTR: {

            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            if (args[1].type != V_NUM || args[2].type != V_NUM) {
                femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                 "substr() expects numeric arguments 'start' and 'len'");
                return FEMBOY_ERR_RUNTIME;
            }
            ObjString *s = AS_STRING(args[0]);
            int start = (int)args[1].as.num;
            int want_len = (int)args[2].as.num;
            if (start < 0) start = 0;
            if (start > s->length) start = s->length;
            if (want_len < 0) want_len = 0;
            if (start + want_len > s->length) want_len = s->length - start;
            *out = value_str_n(vm, s->chars + start, want_len);
            return FEMBOY_OK;
        }

        case BUILTIN_INDEX_OF: {

            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            if (!require_string_arg(id, args, 1, 2, err, line)) return FEMBOY_ERR_RUNTIME;
            ObjString *hay = AS_STRING(args[0]);
            ObjString *needle = AS_STRING(args[1]);
            if (needle->length == 0) { *out = value_num(0); return FEMBOY_OK; }
            if (needle->length > hay->length) { *out = value_num(-1); return FEMBOY_OK; }
            for (int i = 0; i <= hay->length - needle->length; i++) {
                if (memcmp(hay->chars + i, needle->chars, (size_t)needle->length) == 0) {
                    *out = value_num(i);
                    return FEMBOY_OK;
                }
            }
            *out = value_num(-1);
            return FEMBOY_OK;
        }

        case BUILTIN_TO_UPPER:
        case BUILTIN_TO_LOWER: {
            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            ObjString *s = AS_STRING(args[0]);
            char *buf = femboy_malloc((size_t)s->length + 1);
            for (int i = 0; i < s->length; i++) {
                unsigned char c = (unsigned char)s->chars[i];

                buf[i] = (char)(id == BUILTIN_TO_UPPER ? toupper(c) : tolower(c));
            }
            buf[s->length] = '\0';
            *out = value_str_take(vm, buf, s->length);
            return FEMBOY_OK;
        }

        case BUILTIN_TRIM: {
            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            ObjString *s = AS_STRING(args[0]);
            int start = 0, end = s->length;
            while (start < end && isspace((unsigned char)s->chars[start])) start++;
            while (end > start && isspace((unsigned char)s->chars[end - 1])) end--;
            *out = value_str_n(vm, s->chars + start, end - start);
            return FEMBOY_OK;
        }

        case BUILTIN_CHAR_AT: {

            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            if (args[1].type != V_NUM) {
                femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                 "charAt() expects a numeric index as the second argument");
                return FEMBOY_ERR_RUNTIME;
            }
            ObjString *s = AS_STRING(args[0]);
            int i = (int)args[1].as.num;
            if (i < 0 || i >= s->length) {
                *out = value_str(vm, "");
            } else {
                *out = value_str_n(vm, s->chars + i, 1);
            }
            return FEMBOY_OK;
        }

        case BUILTIN_REPLACE: {

            if (!require_string_arg(id, args, 0, 1, err, line)) return FEMBOY_ERR_RUNTIME;
            if (!require_string_arg(id, args, 1, 2, err, line)) return FEMBOY_ERR_RUNTIME;
            if (!require_string_arg(id, args, 2, 3, err, line)) return FEMBOY_ERR_RUNTIME;
            ObjString *s = AS_STRING(args[0]);
            ObjString *oldS = AS_STRING(args[1]);
            ObjString *newS = AS_STRING(args[2]);

            if (oldS->length == 0) {
                *out = value_str_n(vm, s->chars, s->length);
                return FEMBOY_OK;
            }

            int matches = 0;
            for (int i = 0; i <= s->length - oldS->length; ) {
                if (memcmp(s->chars + i, oldS->chars, (size_t)oldS->length) == 0) {
                    matches++;
                    i += oldS->length;
                } else {
                    i++;
                }
            }

            long new_total_len = (long)s->length + (long)matches * ((long)newS->length - (long)oldS->length);
            if (new_total_len < 0) new_total_len = 0;
            char *buf = femboy_malloc((size_t)new_total_len + 1);
            int w = 0;
            for (int i = 0; i < s->length; ) {
                if (i <= s->length - oldS->length && memcmp(s->chars + i, oldS->chars, (size_t)oldS->length) == 0) {
                    memcpy(buf + w, newS->chars, (size_t)newS->length);
                    w += newS->length;
                    i += oldS->length;
                } else {
                    buf[w++] = s->chars[i++];
                }
            }
            buf[w] = '\0';
            *out = value_str_take(vm, buf, w);
            return FEMBOY_OK;
        }

        case BUILTIN_SPLIT:

            return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                    "split() must be called through the protected path, not through builtin_call "
                                    "(internal compiler error)");

        case BUILTIN_COUNT:
            break;
    }

    return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                            "built-in function '%s' is not implemented (internal error)", builtin_name(id));
}
