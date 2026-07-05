#ifndef FEMBOY_VALUE_H
#define FEMBOY_VALUE_H

#include <stdbool.h>
#include "object.h"

typedef enum { V_NUM, V_STR, V_BOOL, V_NIL, V_ARRAY, V_MAP } ValueType;

struct Value {
    ValueType type;
    union {
        double num;
        bool boolean;
        FemboyObj *obj;
    } as;
};

#define AS_STRING(v) ((ObjString *)(v).as.obj)
#define AS_ARRAY(v)  ((ObjArray *)(v).as.obj)
#define AS_MAP(v)    ((ObjMap *)(v).as.obj)
#define AS_CSTRING(v) (AS_STRING(v)->chars)

typedef struct {
    char *key;

    Value value;
} MapEntry;

struct ObjMap {
    FemboyObj obj;
    MapEntry *entries;
    int count;
    int cap;
};

struct VM;

Value value_num(double d);
Value value_bool(bool b);
Value value_nil(void);

Value value_str(struct VM *vm, const char *s);

Value value_str_n(struct VM *vm, const char *s, int length);

Value value_str_take(struct VM *vm, char *chars, int length);

Value value_array_new(struct VM *vm, int cap_hint);

void value_array_push(struct VM *vm, Value arr, Value elem);

int value_array_len(Value arr);

Value *value_array_at(Value arr, int index);

Value value_map_new(struct VM *vm, int cap_hint);

void value_map_set(struct VM *vm, Value map, const char *key, Value val);

Value *value_map_get(Value map, const char *key);

bool value_map_has(Value map, const char *key);

bool value_map_delete(Value map, const char *key);

int value_map_count(Value map);

bool value_truthy(Value v);

void value_print(Value v);

const char *value_to_temp_cstring(Value v, char *buf, size_t size);

char *value_to_owned_cstring(Value v);

const char *value_type_name(ValueType t);

bool value_equals(Value a, Value b);

#endif
