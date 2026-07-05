#include "vm.h"
#include "gc.h"
#include "builtins.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void vm_init(VM *vm) {
    vm->chunk = NULL;
    vm->sp = 0;
    vm->fp = 0;
    vm->nglobals = 0;
    vm->nhandlers = 0;
    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->gc_threshold = 64 * 1024;
}

void vm_free(VM *vm) {
    for (int i = 0; i < vm->nglobals; i++) free(vm->globals[i].name);
    vm->sp = 0;
    vm->nglobals = 0;
    gc_free_all(vm);
}

static FemboyStatus vm_push(VM *vm, Value v, FemboyError *err, int line) {
    if (vm->sp >= FEMBOY_STACK_MAX) {
        return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1, "virtual machine stack overflow");
    }
    vm->stack[vm->sp++] = v;
    return FEMBOY_OK;
}

static FemboyStatus vm_pop_checked(VM *vm, Value *out, FemboyError *err, int line) {
    if (vm->sp <= 0) {
        return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1,
                                "virtual machine stack underflow (corrupted bytecode?)");
    }
    *out = vm->stack[--vm->sp];
    return FEMBOY_OK;
}

static int find_global(VM *vm, const char *name) {
    for (int i = 0; i < vm->nglobals; i++)
        if (!strcmp(vm->globals[i].name, name)) return i;
    return -1;
}

static FemboyStatus set_global(VM *vm, const char *name, Value v, FemboyError *err, int line) {
    int idx = find_global(vm, name);
    if (idx >= 0) {
        vm->globals[idx].value = v;
        return FEMBOY_OK;
    }
    if (vm->nglobals >= FEMBOY_GLOBALS_MAX) {
        return femboy_error_set(err, FEMBOY_ERR_RUNTIME, line, -1, "too many global variables");
    }
    vm->globals[vm->nglobals].name = femboy_strdup(name);
    vm->globals[vm->nglobals].value = v;
    vm->nglobals++;
    return FEMBOY_OK;
}

#define POP_OR_FAIL(dst) \
    do { st = vm_pop_checked(vm, &(dst), err, cur_line); if (st != FEMBOY_OK) goto runtime_error; } while (0)

#define PUSH_OR_FAIL(val) \
    do { st = vm_push(vm, (val), err, cur_line); if (st != FEMBOY_OK) goto runtime_error; } while (0)

FemboyStatus femboy_vm_run(VM *vm, Chunk *chunk, FemboyError *err) {
    vm->chunk = chunk;
    vm->sp = 0;
    vm->fp = 0;
    uint8_t *code = chunk->code;
    int ip = 0;
    FemboyStatus st;
    int cur_line = -1;

    for (;;) {
        uint8_t op = code[ip++];
        switch (op) {
            case OP_PUSH_NUM: {
                int idx = chunk_read_u16(code, ip); ip += 2;
                PUSH_OR_FAIL(value_num(chunk->numbers[idx]));
                break;
            }
            case OP_PUSH_STR: {
                int idx = chunk_read_u16(code, ip); ip += 2;

                PUSH_OR_FAIL(value_str(vm, chunk->strings[idx]));
                break;
            }
            case OP_PUSH_BOOL: {
                bool b = code[ip++];
                PUSH_OR_FAIL(value_bool(b));
                break;
            }
            case OP_PUSH_NIL:
                PUSH_OR_FAIL(value_nil());
                break;
            case OP_POP: {
                Value v = value_nil(); 
                POP_OR_FAIL(v);
                break;
            }

            case OP_ADD: {
                Value b = value_nil(), a = value_nil(); 
                POP_OR_FAIL(b); 
                POP_OR_FAIL(a);
                if (a.type == V_NUM && b.type == V_NUM) {
                    PUSH_OR_FAIL(value_num(a.as.num + b.as.num));
                } else if (a.type == V_STR || b.type == V_STR) {
                    char bufa[64], bufb[64];
                    char *owned_a = NULL, *owned_b = NULL;
                    const char *sa, *sb;

                    if (a.type == V_STR) sa = AS_CSTRING(a);
                    else if (a.type == V_ARRAY || a.type == V_MAP) { owned_a = value_to_owned_cstring(a); sa = owned_a; }
                    else sa = value_to_temp_cstring(a, bufa, sizeof bufa);

                    if (b.type == V_STR) sb = AS_CSTRING(b);
                    else if (b.type == V_ARRAY || b.type == V_MAP) { owned_b = value_to_owned_cstring(b); sb = owned_b; }
                    else sb = value_to_temp_cstring(b, bufb, sizeof bufb);

                    int len = (int)(strlen(sa) + strlen(sb));
                    char *res = femboy_malloc((size_t)len + 1);
                    sprintf(res, "%s%s", sa, sb);
                    free(owned_a);
                    free(owned_b);
                    PUSH_OR_FAIL(value_str_take(vm, res, len));
                    break;
                } else {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "operator '+' is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                break;
            }

            case OP_SUB: {
                Value b = value_nil(), a = value_nil(); POP_OR_FAIL(b); POP_OR_FAIL(a);
                if (a.type != V_NUM || b.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "operator '-' is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_num(a.as.num - b.as.num));
                break;
            }
            case OP_MUL: {
                Value b = value_nil(), a = value_nil(); POP_OR_FAIL(b); POP_OR_FAIL(a);
                if (a.type != V_NUM || b.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "operator '*' is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_num(a.as.num * b.as.num));
                break;
            }
            case OP_DIV: {
                Value b = value_nil(), a = value_nil(); POP_OR_FAIL(b); POP_OR_FAIL(a);
                if (a.type != V_NUM || b.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "operator '/' is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                if (b.as.num == 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "division by zero");
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_num(a.as.num / b.as.num));
                break;
            }
            case OP_MOD: {
                Value b = value_nil(), a = value_nil(); POP_OR_FAIL(b); POP_OR_FAIL(a);
                if (a.type != V_NUM || b.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "operator '%%' is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                if (b.as.num == 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "division by zero (in operator '%%')");
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_num(fmod(a.as.num, b.as.num)));
                break;
            }
            case OP_NEG: {
                Value a = value_nil(); POP_OR_FAIL(a);
                if (a.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "unary operator '-' is not applicable to type %s", value_type_name(a.type));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_num(-a.as.num));
                break;
            }

            case OP_EQ: case OP_NEQ: {
                Value b = value_nil(), a = value_nil(); 
                POP_OR_FAIL(b); 
                POP_OR_FAIL(a);
                bool eq = value_equals(a, b);
                PUSH_OR_FAIL(value_bool(op == OP_EQ ? eq : !eq));
                break;
            }
            case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
                Value b = value_nil(), a = value_nil(); POP_OR_FAIL(b); POP_OR_FAIL(a);
                if (a.type != V_NUM || b.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "comparison operator is not applicable to types %s and %s",
                                     value_type_name(a.type), value_type_name(b.type));
                    goto runtime_error;
                }
                bool r;
                if (op == OP_LT) r = a.as.num < b.as.num;
                else if (op == OP_LE) r = a.as.num <= b.as.num;
                else if (op == OP_GT) r = a.as.num > b.as.num;
                else r = a.as.num >= b.as.num;
                PUSH_OR_FAIL(value_bool(r));
                break;
            }
            case OP_NOT: {
                Value a = value_nil(); POP_OR_FAIL(a);
                PUSH_OR_FAIL(value_bool(!value_truthy(a)));
                break;
            }

            case OP_GET_LOCAL: {
                int slot = chunk_read_u16(code, ip); ip += 2;
                int base = vm->fp > 0 ? vm->frames[vm->fp - 1].base_slot : 0;

                PUSH_OR_FAIL(vm->stack[base + slot]);
                break;
            }
            case OP_SET_LOCAL: {
                int slot = chunk_read_u16(code, ip); ip += 2;
                int base = vm->fp > 0 ? vm->frames[vm->fp - 1].base_slot : 0;
                vm->stack[base + slot] = vm->stack[vm->sp - 1];
                break;
            }
            case OP_DEFINE_LOCAL: {
                int slot = chunk_read_u16(code, ip); ip += 2;
                int base = vm->fp > 0 ? vm->frames[vm->fp - 1].base_slot : 0;
                Value v = value_nil(); 
                POP_OR_FAIL(v);
                while (base + slot >= vm->sp) PUSH_OR_FAIL(value_nil());
                vm->stack[base + slot] = v;
                break;
            }
            case OP_GET_GLOBAL: {
                int idx = chunk_read_u16(code, ip); ip += 2;
                const char *name = chunk->strings[idx];
                int g = find_global(vm, name);
                if (g < 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "undefined variable '%s'", name);
                    goto runtime_error;
                }
                PUSH_OR_FAIL(vm->globals[g].value);
                break;
            }
            case OP_SET_GLOBAL: {
                int idx = chunk_read_u16(code, ip); ip += 2;
                const char *name = chunk->strings[idx];
                int g = find_global(vm, name);
                if (g < 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "undefined variable '%s'", name);
                    goto runtime_error;
                }
                vm->globals[g].value = vm->stack[vm->sp - 1];
                break;
            }
            case OP_DEFINE_GLOBAL: {
                int idx = chunk_read_u16(code, ip); ip += 2;
                const char *name = chunk->strings[idx];
                Value v = value_nil(); 
                POP_OR_FAIL(v);
                st = set_global(vm, name, v, err, cur_line);
                if (st != FEMBOY_OK) goto runtime_error;
                break;
            }
            case OP_PRINT: {
                Value v = value_nil(); POP_OR_FAIL(v);
                value_print(v);
                printf("\n");
                break;
            }
            case OP_MAKE_ARRAY: {
                int n = chunk_read_u16(code, ip); ip += 2;
                if (vm->sp < n) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "corrupted bytecode: not enough values for MAKE_ARRAY");
                    goto runtime_error;
                }

                int base = vm->sp - n;
                Value arr = value_array_new(vm, n > 0 ? n : 4);
                for (int i = 0; i < n; i++) {
                    value_array_push(vm, arr, vm->stack[base + i]);
                }
                vm->sp = base;
                PUSH_OR_FAIL(arr);
                break;
            }
            case OP_MAKE_MAP: {
                int n = chunk_read_u16(code, ip); ip += 2;
                if (vm->sp < n) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "corrupted bytecode: not enough values for MAKE_MAP");
                    goto runtime_error;
                }

                int key_idx_pos = ip;
                ip += n * 2;

                int base = vm->sp - n;

                Value map = value_map_new(vm, n);
                for (int i = 0; i < n; i++) {
                    int kidx = chunk_read_u16(code, key_idx_pos + i * 2);
                    const char *key = chunk->strings[kidx];
                    value_map_set(vm, map, key, vm->stack[base + i]);
                }
                vm->sp = base;
                PUSH_OR_FAIL(map);
                break;
            }
            case OP_INDEX_GET: {
                Value idxv = value_nil(), objv = value_nil(); 
                POP_OR_FAIL(idxv); 
                POP_OR_FAIL(objv);

                if (objv.type == V_MAP) {
                    if (idxv.type != V_STR) {
                        femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                         "map key must be a string, got type %s",
                                         value_type_name(idxv.type));
                        goto runtime_error;
                    }
                    Value *val = value_map_get(objv, AS_CSTRING(idxv));

                    PUSH_OR_FAIL(val ? *val : value_nil());
                    break;
                }

                if (objv.type != V_ARRAY) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "'[]' indexing is only applicable to an array or map, got type %s",
                                     value_type_name(objv.type));
                    goto runtime_error;
                }
                if (idxv.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "array index must be a number, got type %s",
                                     value_type_name(idxv.type));
                    goto runtime_error;
                }
                int index = (int)idxv.as.num;
                Value *elem = value_array_at(objv, index);
                if (!elem) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "index %d is out of array bounds (length %d)", index, value_array_len(objv));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(*elem);
                break;
            }
            case OP_INDEX_SET_LOCAL: case OP_INDEX_SET_GLOBAL: {
                int operand = chunk_read_u16(code, ip); 
                ip += 2;

                Value newval = value_nil(), idxv = value_nil(); 
                POP_OR_FAIL(newval); 
                POP_OR_FAIL(idxv);

                Value *cell;
                if (op == OP_INDEX_SET_LOCAL) {
                    int base = vm->fp > 0 ? vm->frames[vm->fp - 1].base_slot : 0;
                    cell = &vm->stack[base + operand];
                } else {
                    const char *name = chunk->strings[operand];
                    int g = find_global(vm, name);
                    if (g < 0) {
                        femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "undefined variable '%s'", name);
                        goto runtime_error;
                    }
                    cell = &vm->globals[g].value;
                }

                if (cell->type == V_MAP) {
                    if (idxv.type != V_STR) {
                        femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                         "map key must be a string, got type %s",
                                         value_type_name(idxv.type));
                        goto runtime_error;
                    }

                    value_map_set(vm, *cell, AS_CSTRING(idxv), newval);
                    PUSH_OR_FAIL(newval);
                    break;
                }

                if (cell->type != V_ARRAY) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "'[]' indexing is only applicable to an array or map, got type %s",
                                     value_type_name(cell->type));
                    goto runtime_error;
                }

                if (idxv.type != V_NUM) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "array index must be a number, got type %s",
                                     value_type_name(idxv.type));
                    goto runtime_error;
                }

                int index = (int)idxv.as.num;
                Value *elem = value_array_at(*cell, index);
                if (!elem) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "index %d is out of array bounds (length %d)", index, value_array_len(*cell));
                    goto runtime_error;
                }

                *elem = newval;
                PUSH_OR_FAIL(newval);
                break;
            }
            case OP_ARRAY_LEN: {
                Value v = value_nil(); 
                POP_OR_FAIL(v);
                if (v.type == V_STR) {
                    ObjString *s = AS_STRING(v);
                    PUSH_OR_FAIL(value_num(s->length));
                    break;
                }
                if (v.type == V_MAP) {
                    PUSH_OR_FAIL(value_num(value_map_count(v)));
                    break;
                }
                if (v.type != V_ARRAY) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "len() is only applicable to an array or map, got type %s", value_type_name(v.type));
                    goto runtime_error;
                }
                int n = value_array_len(v);
                PUSH_OR_FAIL(value_num(n));
                break;
            }
            case OP_MAP_HAS: {
                Value keyv = value_nil(), mapv = value_nil(); POP_OR_FAIL(keyv); POP_OR_FAIL(mapv);
                if (mapv.type != V_MAP) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "has() is only applicable to a map, got type %s", value_type_name(mapv.type));
                    goto runtime_error;
                }
                if (keyv.type != V_STR) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "has(): key must be a string, got type %s", value_type_name(keyv.type));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_bool(value_map_has(mapv, AS_CSTRING(keyv))));
                break;
            }
            case OP_MAP_KEYS: {
                Value mapv = value_nil(); POP_OR_FAIL(mapv);
                if (mapv.type != V_MAP) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "keys() is only applicable to a map, got type %s", value_type_name(mapv.type));
                    goto runtime_error;
                }

                PUSH_OR_FAIL(mapv);
                ObjMap *m = AS_MAP(mapv);
                Value arr = value_array_new(vm, m->count > 0 ? m->count : 0);
                PUSH_OR_FAIL(arr);
                for (int i = 0; i < m->cap; i++) {
                    if (m->entries[i].key != NULL && !gc_map_key_is_tombstone(m->entries[i].key)) {
                        Value keyStr = value_str(vm, m->entries[i].key);
                        value_array_push(vm, arr, keyStr);
                    }
                }

                vm->sp -= 2;
                PUSH_OR_FAIL(arr);
                break;
            }
            case OP_MAP_DELETE: {
                Value keyv = value_nil(), mapv = value_nil(); POP_OR_FAIL(keyv); POP_OR_FAIL(mapv);
                if (mapv.type != V_MAP) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "delete() is only applicable to a map, got type %s", value_type_name(mapv.type));
                    goto runtime_error;
                }
                if (keyv.type != V_STR) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "delete(): key must be a string, got type %s", value_type_name(keyv.type));
                    goto runtime_error;
                }
                PUSH_OR_FAIL(value_bool(value_map_delete(mapv, AS_CSTRING(keyv))));
                break;
            }
            case OP_ARRAY_PUSH: {
                Value elem = value_nil(), arrv = value_nil(); POP_OR_FAIL(elem); POP_OR_FAIL(arrv);
                if (arrv.type != V_ARRAY) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "push() is only applicable to an array, got type %s", value_type_name(arrv.type));
                    goto runtime_error;
                }

                value_array_push(vm, arrv, elem);
                PUSH_OR_FAIL(arrv);
                break;
            }
            case OP_CALL_BUILTIN: {
                BuiltinId bid = (BuiltinId)code[ip++];
                int argc = code[ip++];
                if (vm->sp < argc) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "corrupted bytecode: not enough values for CALL_BUILTIN");
                    goto runtime_error;
                }

                Value *args = &vm->stack[vm->sp - argc];
                Value result = value_nil();
                st = builtin_call(vm, bid, args, argc, &result, err, cur_line);
                if (st != FEMBOY_OK) goto runtime_error;
                vm->sp -= argc;
                PUSH_OR_FAIL(result);
                break;
            }
            case OP_CALL_BUILTIN_MULTI: {
                BuiltinId bid = (BuiltinId)code[ip++];
                int argc = code[ip++];
                if (vm->sp < argc) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "corrupted bytecode: not enough values for CALL_BUILTIN_MULTI");
                    goto runtime_error;
                }

                if (bid != BUILTIN_SPLIT) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "internal error: CALL_BUILTIN_MULTI with unsupported id %d", (int)bid);
                    goto runtime_error;
                }
                if (argc != 2) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "corrupted bytecode: split() expects 2 arguments, got %d", argc);
                    goto runtime_error;
                }

                Value sepv = vm->stack[vm->sp - 1];
                Value strv = vm->stack[vm->sp - 2];
                if (strv.type != V_STR) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "split() expects a string argument #1, got type %s",
                                     value_type_name(strv.type));
                    goto runtime_error;
                }
                if (sepv.type != V_STR) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "split() expects a string argument #2 (separator), got type %s",
                                     value_type_name(sepv.type));
                    goto runtime_error;
                }

                ObjString *s = AS_STRING(strv);
                ObjString *sep = AS_STRING(sepv);

                Value arr = value_array_new(vm, 4);
                PUSH_OR_FAIL(arr);

                if (sep->length == 0) {

                    for (int i = 0; i < s->length; i++) {
                        Value piece = value_str_n(vm, s->chars + i, 1);
                        value_array_push(vm, arr, piece);
                    }
                } else {
                    int start = 0;
                    for (int i = 0; i <= s->length - sep->length; ) {
                        if (memcmp(s->chars + i, sep->chars, (size_t)sep->length) == 0) {
                            Value piece = value_str_n(vm, s->chars + start, i - start);
                            value_array_push(vm, arr, piece);
                            i += sep->length;
                            start = i;
                        } else {
                            i++;
                        }
                    }
                    Value lastPiece = value_str_n(vm, s->chars + start, s->length - start);
                    value_array_push(vm, arr, lastPiece);
                }

                vm->sp -= 3;
                PUSH_OR_FAIL(arr);
                break;
            }
            case OP_TRY_BEGIN: {
                int16_t offset = (int16_t)chunk_read_u16(code, ip); ip += 2;
                int catch_addr = ip + offset;
                if (vm->nhandlers >= FEMBOY_HANDLERS_MAX) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "try nesting too deep (maximum %d)", FEMBOY_HANDLERS_MAX);
                    goto runtime_error;
                }
                TryHandler *h = &vm->handlers[vm->nhandlers++];
                h->catch_addr = catch_addr;
                h->saved_sp = vm->sp;
                h->saved_fp = vm->fp;
                break;
            }
            case OP_TRY_END: {
                if (vm->nhandlers <= 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "OP_TRY_END without a matching handler (corrupted bytecode?)");
                    goto runtime_error;
                }
                vm->nhandlers--;
                break;
            }
            case OP_POP_HANDLERS: {

                int n = code[ip++];
                if (n > vm->nhandlers) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "OP_POP_HANDLERS: requested to pop %d handlers, only %d active "
                                     "(corrupted bytecode?)", n, vm->nhandlers);
                    goto runtime_error;
                }
                vm->nhandlers -= n;
                break;
            }
            case OP_THROW: {
                Value thrown = value_nil(); POP_OR_FAIL(thrown);
                if (vm->nhandlers <= 0) {
                    if (thrown.type == V_STR) {
                        femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                         "unhandled exception: %s", AS_CSTRING(thrown));
                    } else {
                        femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                         "unhandled exception: <value of type %s>",
                                         value_type_name(thrown.type));
                    }
                    goto runtime_error;
                }
                TryHandler h = vm->handlers[--vm->nhandlers];

                vm->sp = h.saved_sp;
                vm->fp = h.saved_fp;
                ip = h.catch_addr;
                PUSH_OR_FAIL(thrown);
                break;
            }
            case OP_JUMP: {
                int16_t offset = (int16_t)chunk_read_u16(code, ip); 
                ip += 2;
                ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                int16_t offset = (int16_t)chunk_read_u16(code, ip); 
                ip += 2;
                Value v = vm->stack[vm->sp - 1];
                if (!value_truthy(v)) ip += offset;
                break;
            }
            case OP_CALL: {
                int target = chunk_read_u16(code, ip); 
                ip += 2;
                int argc = code[ip++];
                if (vm->fp >= FEMBOY_FRAMES_MAX) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1, "recursion too deep");
                    goto runtime_error;
                }
                Frame *fr = &vm->frames[vm->fp++];
                fr->return_addr = ip;
                fr->base_slot = vm->sp - argc;
                fr->handler_depth = vm->nhandlers;
                ip = target;
                break;
            }
            case OP_RETURN: {
                Value result = value_nil(); POP_OR_FAIL(result);
                if (vm->fp <= 0) {
                    femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                     "return outside a function (corrupted bytecode?)");
                    goto runtime_error;
                }
                Frame *fr = &vm->frames[--vm->fp];
                vm->nhandlers = fr->handler_depth;
                vm->sp = fr->base_slot;
                ip = fr->return_addr;
                PUSH_OR_FAIL(result);
                break;
            }
            case OP_HALT:
                return FEMBOY_OK;
            default:
                femboy_error_set(err, FEMBOY_ERR_RUNTIME, cur_line, -1,
                                 "unknown bytecode instruction: %d", op);
                goto runtime_error;
        }
    }

runtime_error:

    vm->sp = 0;
    vm->nhandlers = 0;
    return FEMBOY_ERR_RUNTIME;
}
