#include "value.h"
#include "gc.h"
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

Value value_num(double d) { Value v; v.type = V_NUM; v.as.num = d; return v; }
Value value_bool(bool b) { Value v; v.type = V_BOOL; v.as.boolean = b; return v; }
Value value_nil(void) { Value v; v.type = V_NIL; return v; }

Value value_str(struct VM *vm, const char *s) {
    Value v;
    v.type = V_STR;
    v.as.obj = (FemboyObj *)gc_new_string(vm, s, (int)strlen(s));
    return v;
}

Value value_str_n(struct VM *vm, const char *s, int length) {
    Value v;
    v.type = V_STR;
    v.as.obj = (FemboyObj *)gc_new_string(vm, s, length);
    return v;
}

Value value_str_take(struct VM *vm, char *chars, int length) {
    Value v;
    v.type = V_STR;
    v.as.obj = (FemboyObj *)gc_take_string(vm, chars, length);
    return v;
}

Value value_array_new(struct VM *vm, int cap_hint) {
    Value v;
    v.type = V_ARRAY;
    v.as.obj = (FemboyObj *)gc_new_array(vm, cap_hint);
    return v;
}

void value_array_push(struct VM *vm, Value arr, Value elem) {
    gc_array_push(vm, AS_ARRAY(arr), elem);
}

int value_array_len(Value arr) {
    return AS_ARRAY(arr)->count;
}

Value *value_array_at(Value arr, int index) {
    ObjArray *a = AS_ARRAY(arr);
    if (index < 0 || index >= a->count) return NULL;
    return &a->items[index];
}

Value value_map_new(struct VM *vm, int cap_hint) {
    Value v;
    v.type = V_MAP;
    v.as.obj = (FemboyObj *)gc_new_map(vm, cap_hint);
    return v;
}

void value_map_set(struct VM *vm, Value map, const char *key, Value val) {
    ObjMap *m = AS_MAP(map);
    size_t len = strlen(key);

    if (m->cap == 0 || (double)(m->count + 1) / (double)m->cap > 0.75) {
        gc_map_grow(vm, m);
    }

    MapEntry *slot = gc_map_find_slot(m->entries, m->cap, key, len);

    if (slot->key != NULL && !gc_map_key_is_tombstone(slot->key)) {
        slot->value = val;
        return;
    }

    char *owned_key = femboy_malloc(len + 1);
    memcpy(owned_key, key, len);
    owned_key[len] = '\0';

    slot->key = owned_key;
    slot->value = val;
    m->count++;
    gc_account_bytes(vm, (long)(len + 1));
}

Value *value_map_get(Value map, const char *key) {
    ObjMap *m = AS_MAP(map);
    if (m->cap == 0) return NULL;
    size_t len = strlen(key);
    MapEntry *slot = gc_map_find_slot(m->entries, m->cap, key, len);
    if (!slot || slot->key == NULL || gc_map_key_is_tombstone(slot->key)) return NULL;
    return &slot->value;
}

bool value_map_has(Value map, const char *key) {
    return value_map_get(map, key) != NULL;
}

bool value_map_delete(Value map, const char *key) {
    ObjMap *m = AS_MAP(map);
    if (m->cap == 0) return false;
    size_t len = strlen(key);
    MapEntry *slot = gc_map_find_slot(m->entries, m->cap, key, len);
    if (!slot || slot->key == NULL || gc_map_key_is_tombstone(slot->key)) return false;

    free(slot->key);
    slot->key = gc_map_tombstone();
    slot->value = value_nil();
    m->count--;
    return true;
}

int value_map_count(Value map) {
    return AS_MAP(map)->count;
}

bool value_truthy(Value v) {
    if (v.type == V_NIL) return false;
    if (v.type == V_BOOL) return v.as.boolean;
    if (v.type == V_NUM) return v.as.num != 0;
    return true;
}

typedef struct {
    char *buf;
    size_t len, cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    sb->cap = 64;
    sb->buf = femboy_malloc(sb->cap);
    sb->buf[0] = '\0';
    sb->len = 0;
}

static void sb_append(StrBuf *sb, const char *data, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        while (sb->len + n + 1 > sb->cap) sb->cap *= 2;
        sb->buf = femboy_realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, data, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_append_cstr(StrBuf *sb, const char *s) {
    sb_append(sb, s, strlen(s));
}

static void append_value(StrBuf *sb, Value v, bool quote_strings) {
    switch (v.type) {
        case V_NUM: {
            char tmp[32];
            if (v.as.num == (long long)v.as.num) snprintf(tmp, sizeof tmp, "%lld", (long long)v.as.num);
            else snprintf(tmp, sizeof tmp, "%g", v.as.num);
            sb_append_cstr(sb, tmp);
            break;
        }
        case V_BOOL:
            sb_append_cstr(sb, v.as.boolean ? "true" : "false");
            break;
        case V_NIL:
            sb_append_cstr(sb, "nil");
            break;
        case V_STR:
            if (quote_strings) {
                sb_append(sb, "\"", 1);
                sb_append_cstr(sb, AS_CSTRING(v));
                sb_append(sb, "\"", 1);
            } else {
                sb_append_cstr(sb, AS_CSTRING(v));
            }
            break;
        case V_ARRAY: {
            ObjArray *a = AS_ARRAY(v);
            if (a->obj.marked) { sb_append_cstr(sb, "[...]"); break; }
            a->obj.marked = true;
            sb_append(sb, "[", 1);
            for (int i = 0; i < a->count; i++) {
                if (i > 0) sb_append_cstr(sb, ", ");
                append_value(sb, a->items[i], true);
            }
            sb_append(sb, "]", 1);
            a->obj.marked = false;
            break;
        }
        case V_MAP: {
            ObjMap *m = AS_MAP(v);
            if (m->obj.marked) { sb_append_cstr(sb, "{...}"); break; }
            m->obj.marked = true;
            sb_append(sb, "{", 1);
            bool first = true;
            for (int i = 0; i < m->cap; i++) {
                if (m->entries[i].key == NULL || gc_map_key_is_tombstone(m->entries[i].key)) continue;
                if (!first) sb_append_cstr(sb, ", ");
                first = false;
                sb_append(sb, "\"", 1);
                sb_append_cstr(sb, m->entries[i].key);
                sb_append_cstr(sb, "\": ");
                append_value(sb, m->entries[i].value, true);
            }
            sb_append(sb, "}", 1);
            m->obj.marked = false;
            break;
        }
    }
}

void value_print(Value v) {
    StrBuf sb;
    sb_init(&sb);
    append_value(&sb, v, false);
    printf("%s", sb.buf);
    free(sb.buf);
}

const char *value_to_temp_cstring(Value v, char *buf, size_t size) {
    switch (v.type) {
        case V_NUM:
        case V_BOOL:
        case V_NIL: {
            StrBuf sb;
            sb_init(&sb);
            append_value(&sb, v, false);
            snprintf(buf, size, "%s", sb.buf);
            free(sb.buf);
            return buf;
        }
        case V_STR:
        case V_ARRAY:
        case V_MAP:
        default:
            snprintf(buf, size, "<call value_to_owned_cstring for type %s>", value_type_name(v.type));
            return buf;
    }
}

char *value_to_owned_cstring(Value v) {
    StrBuf sb;
    sb_init(&sb);
    append_value(&sb, v, false);
    return sb.buf;
}

const char *value_type_name(ValueType t) {
    switch (t) {
        case V_NUM: return "number";
        case V_STR: return "string";
        case V_BOOL: return "boolean";
        case V_ARRAY: return "array";
        case V_MAP: return "map";
        default: return "nil";
    }
}

static bool array_equals(ObjArray *aa, ObjArray *ab) {
    if (aa == ab) return true;
    if (aa->obj.marked || ab->obj.marked) return true;
    if (aa->count != ab->count) return false;

    aa->obj.marked = true;
    ab->obj.marked = true;

    bool equal = true;
    for (int i = 0; i < aa->count; i++) {
        if (!value_equals(aa->items[i], ab->items[i])) { equal = false; break; }
    }

    aa->obj.marked = false;
    ab->obj.marked = false;
    return equal;
}

static bool map_equals(ObjMap *ma, ObjMap *mb) {
    if (ma == mb) return true;
    if (ma->obj.marked || mb->obj.marked) return true;
    if (ma->count != mb->count) return false;

    ma->obj.marked = true;
    mb->obj.marked = true;

    bool equal = true;
    for (int i = 0; i < ma->cap && equal; i++) {
        if (ma->entries[i].key == NULL || gc_map_key_is_tombstone(ma->entries[i].key)) continue;
        size_t len = strlen(ma->entries[i].key);
        MapEntry *other = gc_map_find_slot(mb->entries, mb->cap, ma->entries[i].key, len);
        if (!other || other->key == NULL || gc_map_key_is_tombstone(other->key) ||
            !value_equals(ma->entries[i].value, other->value)) {
            equal = false;
        }
    }

    ma->obj.marked = false;
    mb->obj.marked = false;
    return equal;
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case V_NUM: return a.as.num == b.as.num;
        case V_BOOL: return a.as.boolean == b.as.boolean;
        case V_NIL: return true;
        case V_STR: {
            ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
            return sa->length == sb->length && memcmp(sa->chars, sb->chars, (size_t)sa->length) == 0;
        }
        case V_ARRAY:
            return array_equals(AS_ARRAY(a), AS_ARRAY(b));
        case V_MAP:
            return map_equals(AS_MAP(a), AS_MAP(b));
        default: return false;
    }
}